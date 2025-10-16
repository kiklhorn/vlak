package com.example.vlak

import android.Manifest
import android.annotation.SuppressLint
import android.app.Application
import android.bluetooth.*
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import android.os.ParcelUuid
import androidx.core.content.ContextCompat
import androidx.lifecycle.AndroidViewModel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import java.util.*

// Enum (výčtový typ) pro jednoduché a bezpečné uchování stavu pohybu vlaku.
// Použití enumu zabraňuje chybám z překlepů, které by mohly nastat při použití obyčejných stringů.
enum class TrainState {
    FORWARD,
    BACKWARD,
    STOPPED
}

// @SuppressLint("MissingPermission") je zde použit, protože oprávnění jsou kontrolována a vyžadována
// v MainActivity dříve, než jsou volány metody tohoto ViewModelu.
@SuppressLint("MissingPermission")
// VlakViewModel dědí z AndroidViewModel, protože potřebujeme přístup k aplikačnímu kontextu
// pro získání systémových služeb jako je BluetoothManager.
class VlakViewModel(application: Application) : AndroidViewModel(application) {

    // --- Stavové proměnné pro UI --- //
    // _connected je privátní MutableStateFlow, který uchovává interní stav připojení (true/false).
    private val _connected = MutableStateFlow(false)
    val connected = _connected.asStateFlow() // Veřejná, neměnitelná verze pro UI.

    private val _speed = MutableStateFlow(50f)
    val speed = _speed.asStateFlow()

    // Stav informující UI, zda právě probíhá skenování zařízení.
    private val _isScanning = MutableStateFlow(false)
    val isScanning = _isScanning.asStateFlow()

    // Seznam nalezených BLE zařízení, který se zobrazí v dialogu.
    private val _discoveredDevices = MutableStateFlow<List<BluetoothDevice>>(emptyList())
    val discoveredDevices = _discoveredDevices.asStateFlow()

    // Uchovává aktuální stav pohybu vlaku (STOPPED, FORWARD, BACKWARD).
    private val _trainState = MutableStateFlow(TrainState.STOPPED)
    val trainState = _trainState.asStateFlow()

    // --- Bluetooth proměnné a konstanty --- //
    private val serviceUuid: UUID = UUID.fromString("d1f61c9f-6eef-4911-8b49-98e13dd94938")
    private val characteristicUuid: UUID = UUID.fromString("abd41953-43f6-426c-b9d3-ff151d71b489")
    private var bluetoothGatt: BluetoothGatt? = null

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        (getApplication<Application>().getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    }

    // --- Veřejné funkce volané z UI --- //
    fun setSpeed(value: Float) {
        _speed.value = value
        sendSpeed(value)
    }

    // --- Logika skenování a připojování --- //

    // Spustí skenování BLE zařízení.
    fun startScan() {
        if (!hasRequiredBluetoothPermissions()) return

        _discoveredDevices.value = emptyList() // Vyčistí seznam od předchozího skenování.
        _isScanning.value = true // Informuje UI, že skenování začalo.

        // Vytvoříme filtr, který bude hledat pouze zařízení inzerující naši specifickou službu (serviceUuid).
        val scanFilter = ScanFilter.Builder()
            .setServiceUuid(ParcelUuid(serviceUuid))
            .build()
        // Nastavení skenování pro nízkou latenci (rychlejší odezva).
        val scanSettings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        bluetoothAdapter?.bluetoothLeScanner?.startScan(listOf(scanFilter), scanSettings, leScanCallback)
    }

    // Zastaví probíhající skenování.
    fun stopScan() {
        _isScanning.value = false // Informuje UI, že skenování skončilo.
        bluetoothAdapter?.bluetoothLeScanner?.stopScan(leScanCallback)
    }

    // Připojí se k zařízení, které si uživatel vybral v dialogu.
    fun connectToDevice(device: BluetoothDevice) {
        stopScan() // Vždy zastavíme skenování před pokusem o připojení.
        device.connectGatt(getApplication(), false, gattCallback)
    }

    private val leScanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult?) {
            result?.device?.let { device ->
                // Zabrání duplicitám v seznamu. Přidá zařízení, pouze pokud v seznamu ještě není (podle MAC adresy).
                if (device.name != null && _discoveredDevices.value.none { it.address == device.address }) {
                    _discoveredDevices.value = _discoveredDevices.value + device
                }
            }
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(gatt: BluetoothGatt?, status: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                bluetoothGatt = gatt
                _connected.value = true
                _discoveredDevices.value = emptyList() // Po připojení vyčistíme seznam, už ho nepotřebujeme.
                gatt?.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                disconnectBLE()
            }
        }
        override fun onServicesDiscovered(gatt: BluetoothGatt?, status: Int) {}
    }

    fun disconnectBLE() {
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        _connected.value = false
        _trainState.value = TrainState.STOPPED // Po odpojení vždy resetujeme stav vlaku na "zastavený".
    }

    // --- Odesílací funkce --- //
    // Každá z těchto funkcí nejprve odešle příkaz přes BLE a poté aktualizuje interní stav `_trainState`.
    fun sendForward() {
        writeStringToCharacteristic("FORWARD")
        _trainState.value = TrainState.FORWARD
    }

    fun sendBackward() {
        writeStringToCharacteristic("BACKWARD")
        _trainState.value = TrainState.BACKWARD
    }

    fun sendStop() {
        writeStringToCharacteristic("STOP")
        _trainState.value = TrainState.STOPPED
    }

    fun sendMicrosteps(steps: Int) = writeStringToCharacteristic("MICROSTEPS:$steps")
    private fun sendSpeed(speed: Float) = writeStringToCharacteristic("SPEED:${speed.toInt()}")

    private fun writeStringToCharacteristic(value: String) {
        val service = bluetoothGatt?.getService(serviceUuid)
        val charac = service?.getCharacteristic(characteristicUuid)
        if (charac != null) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                bluetoothGatt?.writeCharacteristic(charac, value.toByteArray(), BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
            } else {
                @Suppress("DEPRECATION")
                charac.value = value.toByteArray()
                @Suppress("DEPRECATION")
                bluetoothGatt?.writeCharacteristic(charac)
            }
        }
    }

    override fun onCleared() {
        super.onCleared()
        stopScan()
        disconnectBLE()
    }

    private fun hasRequiredBluetoothPermissions(): Boolean {
        val context = getApplication<Application>()
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
        } else {
            ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH) == PackageManager.PERMISSION_GRANTED &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.BLUETOOTH_ADMIN) == PackageManager.PERMISSION_GRANTED &&
            ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED
        }
    }
}
