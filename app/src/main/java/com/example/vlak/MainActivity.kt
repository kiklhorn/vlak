package com.example.vlak

import android.Manifest
import android.annotation.SuppressLint
import android.bluetooth.BluetoothDevice
import android.os.Build
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.core.app.ActivityCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.vlak.ui.theme.VLAKTheme

// @SuppressLint("MissingPermission") potlačuje varování, protože oprávnění
// pro Bluetooth si žádáme explicitně hned při startu aktivity.
@SuppressLint("MissingPermission")
// MainActivity je hlavní a jedinou aktivitou v této aplikaci.
// Slouží jako vstupní bod pro celé uživatelské rozhraní.
class MainActivity : ComponentActivity() {

    // Metoda onCreate se volá při vytvoření aktivity.
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Tento blok kódu řeší vyžádání potřebných oprávnění pro práci s Bluetooth.
        // Oprávnění se liší v závislosti na verzi Androidu.
        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            // Pro Android 12 (API 31) a vyšší jsou potřeba nová, specifičtější oprávnění.
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.ACCESS_FINE_LOCATION)
        } else {
            // Pro starší verze Androidu stačí obecnější oprávnění.
            arrayOf(Manifest.permission.BLUETOOTH, Manifest.permission.BLUETOOTH_ADMIN, Manifest.permission.ACCESS_FINE_LOCATION)
        }
        // Zobrazí uživateli systémový dialog pro udělení oprávnění.
        ActivityCompat.requestPermissions(this, permissions, 1)

        // setContent je vstupní bod pro definici UI pomocí Jetpack Compose.
        setContent {
            // Aplikuje globální téma (barvy, typografie) definované v ui.theme.
            VLAKTheme {
                // Získá instanci VlakViewModel. `viewModel()` se postará o správný životní cyklus,
                // takže ViewModel přežije i změny konfigurace (např. otočení obrazovky).
                val viewModel: VlakViewModel = viewModel()
                // Sleduje stavové proměnné z ViewModelu. `collectAsState` převede Flow na State,
                // který Compose automaticky sleduje. Při změně hodnoty se UI automaticky překreslí.
                val connected by viewModel.connected.collectAsState()
                val speed by viewModel.speed.collectAsState()
                val isScanning by viewModel.isScanning.collectAsState()
                val discoveredDevices by viewModel.discoveredDevices.collectAsState()
                val trainState by viewModel.trainState.collectAsState()

                // Lokální stavy pro ovládání prvků UI, které nesouvisí s business logikou ve ViewModelu.
                var showDeviceDialog by remember { mutableStateOf(false) } // Řídí viditelnost dialogu pro výběr zařízení.
                var microstepsExpanded by remember { mutableStateOf(false) } // Řídí viditelnost dropdown menu pro microsteps.
                val microstepsOptions = listOf(1, 2, 4, 8, 16, 32) // Seznam voleb pro microsteps.
                var selectedMicrosteps by remember { mutableIntStateOf(4) } // Uchovává aktuálně vybranou hodnotu microsteps.

                // Pokud je `showDeviceDialog` true, zobrazí se dialogové okno.
                if (showDeviceDialog) {
                    DeviceSelectionDialog(
                        devices = discoveredDevices, // Seznam nalezených zařízení z ViewModelu.
                        isScanning = isScanning, // Informace, zda právě probíhá skenování.
                        onDismiss = { // Co se stane, když uživatel dialog zavře.
                            viewModel.stopScan() // Zastaví skenování na pozadí.
                            showDeviceDialog = false // Skryje dialog.
                        },
                        onDeviceSelected = { // Co se stane, když uživatel vybere zařízení ze seznamu.
                            viewModel.connectToDevice(it) // Zavolá funkci pro připojení ve ViewModelu.
                            showDeviceDialog = false // Skryje dialog.
                        }
                    )
                }

                // Scaffold je základní layout komponenta, která poskytuje strukturu pro Material Design.
                Scaffold { paddingValues -> // `paddingValues` obsahují odsazení pro systémové lišty (např. stavový řádek).
                    // Column uspořádává své podřízené prvky vertikálně pod sebe.
                    Column(
                        modifier = Modifier
                            .fillMaxSize() // Vyplní celou dostupnou velikost obrazovky.
                            .padding(paddingValues) // Aplikuje odsazení od Scaffold, aby se obsah nepřekrýval s lištami.
                            .padding(8.dp),// Přidá další vnitřní odsazení pro celý obsah.
                        verticalArrangement = Arrangement.Top // Zarovná prvky v sloupci nahoru.
                    ) {
                        // Box slouží jako jednoduchý vizuální indikátor stavu připojení.
                        Box(
                            Modifier
                                .fillMaxWidth() // Roztáhne se na celou šířku.
                                .height(16.dp)
                                .background(if (connected) Color(0xFF00FF00) else Color.Red) // Zelená pro připojeno, červená pro odpojeno.
                        )
                        Spacer(Modifier.height(16.dp)) // Vertikální mezera.

                        // Row uspořádává prvky horizontálně vedle sebe.
                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.End, // Zarovná prvky na konec (doprava).
                            verticalAlignment = Alignment.CenterVertically // Vertikálně centruje prvky v řádku.
                        ) {
                            // Tlačítko Microsteps se zobrazí pouze po připojení.
                            if (connected) {
                                Box { // Box je zde použit, aby DropdownMenu bylo správně pozicováno vůči tlačítku.
                                    Button(onClick = { microstepsExpanded = true }) { // Po kliknutí zobrazí menu.
                                        Text("Microsteps: $selectedMicrosteps")
                                    }
                                    DropdownMenu(expanded = microstepsExpanded, onDismissRequest = { microstepsExpanded = false }) {
                                        microstepsOptions.forEach { selectionOption ->
                                            DropdownMenuItem(
                                                text = { Text(selectionOption.toString()) },
                                                onClick = {
                                                    selectedMicrosteps = selectionOption // Aktualizuje lokální stav.
                                                    viewModel.sendMicrosteps(selectionOption) // Odešle hodnotu do ViewModelu.
                                                    microstepsExpanded = false // Zavře menu.
                                                }
                                            )
                                        }
                                    }
                                }
                                Spacer(Modifier.width(12.dp)) // Mezera mezi tlačítky.
                            }
                            // Hlavní tlačítko pro připojení/odpojení.
                            Button(
                                onClick = {
                                    if (connected) {
                                        viewModel.disconnectBLE()
                                    } else {
                                        viewModel.startScan()
                                        showDeviceDialog = true // Zobrazí dialog pro výběr.
                                    }
                                },
                                colors = ButtonDefaults.buttonColors(containerColor = if (connected) Color.Red else Color.Green)
                            ) {
                                Text(if (connected) "BLE odpojit" else "BLE připojit", color = Color.White)
                            }
                        }

                        Spacer(Modifier.height(24.dp))
                        // Celá sekce pro ovládání vlaku se zobrazí pouze po úspěšném připojení.
                        if (connected) {
                            Spacer(Modifier.height(8.dp))
                            Text("Rychlost:")
                            Slider(
                                value = speed,
                                onValueChange = { viewModel.setSpeed(it) },
                                valueRange = 0f..100f,
                                modifier = Modifier.fillMaxWidth()
                            )
                            Spacer(Modifier.height(24.dp))
                            Row(
                                Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceEvenly // Rozmístí tlačítka rovnoměrně.
                            ) {
                                // Tlačítka pro ovládání směru, která mění barvu podle `trainState` z ViewModelu.
                                Button(
                                    onClick = { viewModel.sendBackward() },
                                    colors = ButtonDefaults.buttonColors(
                                        containerColor = if (trainState == TrainState.BACKWARD) Color.LightGray else MaterialTheme.colorScheme.primary
                                    )
                                ) { Text("Vzad") }
                                Button(
                                    onClick = { viewModel.sendStop() },
                                    colors = ButtonDefaults.buttonColors(
                                        containerColor = if (trainState == TrainState.STOPPED) Color.LightGray else MaterialTheme.colorScheme.primary
                                    )
                                ) { Text("STOP") }
                                Button(
                                    onClick = { viewModel.sendForward() },
                                    colors = ButtonDefaults.buttonColors(
                                        containerColor = if (trainState == TrainState.FORWARD) Color.LightGray else MaterialTheme.colorScheme.primary
                                    )
                                ) { Text("Vpřed") }
                            }
                        }
                    }
                }
            }
        }
    }
}

// Samostatná Composable funkce pro dialogové okno.
// Je dobrým zvykem oddělovat komplexnější části UI do vlastních funkcí.
@SuppressLint("MissingPermission")
@Composable
private fun DeviceSelectionDialog(
    devices: List<BluetoothDevice>,
    isScanning: Boolean,
    onDismiss: () -> Unit,
    onDeviceSelected: (BluetoothDevice) -> Unit
) {
    // Komponenta pro standardní vyskakovací dialog.
    AlertDialog(
        onDismissRequest = onDismiss, // Co se stane při kliknutí mimo dialog nebo na tlačítko Zpět.
        title = { Text("Vyber si svůj vlak") },
        text = {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                // Zobrazí se, pokud právě probíhá skenování.
                if (isScanning) {
                    CircularProgressIndicator() // Rotující kolečko.
                    Spacer(Modifier.height(8.dp))
                    Text("Hledám vlaky...")
                }
                // Zobrazí se, pokud skenování skončilo a seznam zařízení je prázdný.
                if (devices.isEmpty() && !isScanning) {
                    Text("Žádný vlak nebyl nalezen. Zkus to znovu.")
                }
                // `LazyColumn` je efektivní způsob, jak zobrazit dlouhé seznamy.
                // Vykreslí pouze ty položky, které jsou aktuálně viditelné na obrazovce.
                LazyColumn {
                    items(devices) { device ->
                        Text(
                            text = device.name ?: "Neznámý vlak", // Zobrazí název zařízení, nebo "Neznámý vlak", pokud je název null.
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { onDeviceSelected(device) } // Umožní kliknutí na položku.
                                .padding(vertical = 12.dp)
                        )
                    }
                }
            }
        },
        // Tlačítko pro potvrzení/zavření dialogu.
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text("Zrušit")
            }
        }
    )
}
