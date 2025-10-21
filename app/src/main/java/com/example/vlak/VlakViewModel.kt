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
import java.util.concurrent.ConcurrentLinkedQueue
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.text.Charsets.UTF_8

enum class TrainState {
    FORWARD,
    BACKWARD,
    STOPPED
}

@SuppressLint("MissingPermission")
class VlakViewModel(application: Application) : AndroidViewModel(application) {

    // --- Stavové proměnné pro UI ---
    private val _connected = MutableStateFlow(false)
    val connected = _connected.asStateFlow()

    private val _isSynced = MutableStateFlow(false)
    val isSynced = _isSynced.asStateFlow()

    private val _speed = MutableStateFlow(50f)
    val speed = _speed.asStateFlow()

    private val _isScanning = MutableStateFlow(false)
    val isScanning = _isScanning.asStateFlow()

    private val _discoveredDevices = MutableStateFlow<List<BluetoothDevice>>(emptyList())
    val discoveredDevices = _discoveredDevices.asStateFlow()

    private val _trainState = MutableStateFlow(TrainState.STOPPED)
    val trainState = _trainState.asStateFlow()

    private val _microsteps = MutableStateFlow(4)
    val microsteps = _microsteps.asStateFlow()

    private val _speedCms = MutableStateFlow(0f)
    val speedCms = _speedCms.asStateFlow()

    private val _rpm = MutableStateFlow(0f)
    val rpm = _rpm.asStateFlow()

    // --- Bluetooth proměnné a konstanty ---
    private val serviceUuid: UUID = UUID.fromString("d1f61c9f-6eef-4911-8b49-98e13dd94938")
    private val commandCharUuid: UUID = UUID.fromString("abd41953-43f6-426c-b9d3-ff151d71b489")
    private val stateCharUuid: UUID = UUID.fromString("c3413c66-4001-42e3-a553-064950de6833")
    private val CCCD_UUID = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb")

    private var bluetoothGatt: BluetoothGatt? = null

    // --- Architektura: Příkazová fronta ---
    private val commandQueue = ConcurrentLinkedQueue<ByteArray>()
    private val isWriting = AtomicBoolean(false)

    private val bluetoothAdapter: BluetoothAdapter? by lazy {
        (getApplication<Application>().getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
    }

    // --- Logika skenování a připojování --- //
    fun startScan() {
        if (!hasRequiredBluetoothPermissions()) return
        _discoveredDevices.value = emptyList()
        _isScanning.value = true
        val scanFilter = ScanFilter.Builder().setServiceUuid(ParcelUuid(serviceUuid)).build()
        val scanSettings = ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build()
        bluetoothAdapter?.bluetoothLeScanner?.startScan(listOf(scanFilter), scanSettings, leScanCallback)
    }

    fun stopScan() {
        _isScanning.value = false
        bluetoothAdapter?.bluetoothLeScanner?.stopScan(leScanCallback)
    }

    fun connectToDevice(device: BluetoothDevice) {
        stopScan()
        device.connectGatt(getApplication(), false, gattCallback)
    }

    private val leScanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult?) {
            result?.device?.let { device ->
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
                _discoveredDevices.value = emptyList()
                commandQueue.clear()
                isWriting.set(false)
                gatt?.requestMtu(67)
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                disconnectBLE()
            }
        }

        override fun onMtuChanged(gatt: BluetoothGatt?, mtu: Int, status: Int) {
            gatt?.discoverServices()
        }

        override fun onServicesDiscovered(gatt: BluetoothGatt?, status: Int) {
            val service = gatt?.getService(serviceUuid)
            val stateChar = service?.getCharacteristic(stateCharUuid)
            gatt?.setCharacteristicNotification(stateChar, true)

            val descriptor = stateChar?.getDescriptor(CCCD_UUID)
            descriptor?.let { desc ->
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    gatt?.writeDescriptor(desc, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
                } else {
                    @Suppress("DEPRECATION")
                    desc.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
                    @Suppress("DEPRECATION")
                    gatt?.writeDescriptor(desc)
                }
            }
        }
        
        override fun onCharacteristicWrite(gatt: BluetoothGatt?, characteristic: BluetoothGattCharacteristic?, status: Int) {
            if (status == BluetoothGatt.GATT_SUCCESS) {
                isWriting.set(false)
                processCommandQueue()
            }
        }

        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU && characteristic.uuid == stateCharUuid) {
                @Suppress("DEPRECATION")
                parseAndApplyState(characteristic.getStringValue(0))
            }
        }
        
        override fun onCharacteristicChanged(gatt: BluetoothGatt, characteristic: BluetoothGattCharacteristic, value: ByteArray) {
            if (characteristic.uuid == stateCharUuid) {
                parseAndApplyState(value.toString(UTF_8))
            }
        }
    }

    fun disconnectBLE() {
        commandQueue.clear()
        isWriting.set(false)
        bluetoothGatt?.disconnect()
        bluetoothGatt?.close()
        bluetoothGatt = null
        _connected.value = false
        _isSynced.value = false 
        _trainState.value = TrainState.STOPPED
    }

    // --- Logika odesílání přes frontu ---
    private fun sendCommand(command: String) {
        commandQueue.offer(command.toByteArray(UTF_8))
        processCommandQueue()
    }

    private fun processCommandQueue() {
        if (isWriting.get() || commandQueue.isEmpty()) {
            return
        }

        val data = commandQueue.poll()
        if (data != null) {
            isWriting.set(true)
            val service = bluetoothGatt?.getService(serviceUuid)
            val charac = service?.getCharacteristic(commandCharUuid)
            if (charac != null) {
                 if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    bluetoothGatt?.writeCharacteristic(charac, data, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
                } else {
                    @Suppress("DEPRECATION")
                    charac.value = data
                    @Suppress("DEPRECATION")
                    bluetoothGatt?.writeCharacteristic(charac)
                }
            } else {
                isWriting.set(false)
            }
        }
    }

    // --- Veřejné funkce pro ovládání ---
    fun sendForward() = sendCommand("FORWARD")
    fun sendBackward() = sendCommand("BACKWARD")
    fun sendStop() = sendCommand("STOP")
    fun sendMicrosteps(steps: Int) = sendCommand("MICROSTEPS:$steps")
    fun setSpeed(value: Float) {
        _speed.value = value
        sendCommand("SPEED:${value.toInt()}")
    }
    fun sendHornOn() = sendCommand("HORN_ON")
    fun sendHornOff() = sendCommand("HORN_OFF")
    
    // Prioritní příkaz nouzové brzdy
    fun sendEmergencyStop() {
        commandQueue.clear() // Vymaže všechny čekající příkazy
        sendCommand("E_STOP") // Pošle nouzový příkaz
    }

    private fun parseAndApplyState(stateString: String?) {
        if (stateString == null) return

        val parts = stateString.trim().split(";")
        parts.forEach { part ->
            if (part.contains(":")) {
                val key = part.substringBefore(":")
                val value = part.substringAfter(":")
                when (key) {
                    "S" -> {
                        when (value) {
                            "F" -> _trainState.value = TrainState.FORWARD
                            "B" -> _trainState.value = TrainState.BACKWARD
                            "S" -> _trainState.value = TrainState.STOPPED
                        }
                    }
                    "SP" -> {
                        value.toIntOrNull()?.let { _speed.value = it.toFloat() }
                    }
                    "MS" -> {
                        value.toIntOrNull()?.let { _microsteps.value = it }
                    }
                    "SC" -> {
                        value.toFloatOrNull()?.let { _speedCms.value = it }
                    }
                    "RPM" -> {
                        value.toFloatOrNull()?.let { _rpm.value = it }
                    }
                }
            }
        }
        
        if (!_isSynced.value) {
            _isSynced.value = true
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
