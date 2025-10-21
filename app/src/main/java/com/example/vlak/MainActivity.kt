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
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.GenericShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.app.ActivityCompat
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.vlak.ui.theme.VLAKTheme
import java.util.Locale

@SuppressLint("MissingPermission")
class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val permissions = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            arrayOf(Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT, Manifest.permission.ACCESS_FINE_LOCATION)
        } else {
            arrayOf(Manifest.permission.BLUETOOTH, Manifest.permission.BLUETOOTH_ADMIN, Manifest.permission.ACCESS_FINE_LOCATION)
        }
        ActivityCompat.requestPermissions(this, permissions, 1)

        setContent {
            VLAKTheme {
                val viewModel: VlakViewModel = viewModel()
                val connected by viewModel.connected.collectAsState()
                val isSynced by viewModel.isSynced.collectAsState()
                val discoveredDevices by viewModel.discoveredDevices.collectAsState()
                val isScanning by viewModel.isScanning.collectAsState()

                var showDeviceDialog by remember { mutableStateOf(false) }

                if (showDeviceDialog) {
                    DeviceSelectionDialog(
                        devices = discoveredDevices,
                        isScanning = isScanning,
                        onDismiss = {
                            viewModel.stopScan()
                            showDeviceDialog = false
                        },
                        onDeviceSelected = {
                            viewModel.connectToDevice(it)
                            showDeviceDialog = false
                        }
                    )
                }

                Scaffold { paddingValues ->
                    Box(modifier = Modifier.fillMaxSize().padding(paddingValues)) {
                        // Hlavní obsah stránky
                        TrainControlContent(viewModel) { showDeviceDialog = true }

                        // Nouzová brzda umístěná dole uprostřed
                        if (connected && isSynced) {
                            Box(
                                modifier = Modifier
                                    .align(Alignment.BottomCenter)
                                    .padding(bottom = 32.dp)
                            ) {
                                EmergencyStopButton { viewModel.sendEmergencyStop() }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun TrainControlContent(viewModel: VlakViewModel, onShowDeviceDialog: () -> Unit) {
    val connected by viewModel.connected.collectAsState()
    val isSynced by viewModel.isSynced.collectAsState()
    val speed by viewModel.speed.collectAsState()
    val trainState by viewModel.trainState.collectAsState()
    val microsteps by viewModel.microsteps.collectAsState()
    val speedCms by viewModel.speedCms.collectAsState()
    val rpm by viewModel.rpm.collectAsState()

    var microstepsExpanded by remember { mutableStateOf(false) }
    val microstepsOptions = listOf(1, 2, 4, 8, 16, 32)

    // Celý obsah je nyní v jednom sloupci, který lze rolovat
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState()) // Toto umožňuje rolování
            .padding(8.dp)
            // Přidáme spodní padding, aby obsah nezajížděl pod E-STOP tlačítko
            .padding(bottom = 120.dp),
        verticalArrangement = Arrangement.Top
    ) {
        Box(
            Modifier
                .fillMaxWidth()
                .height(16.dp)
                .background(if (connected) Color(0xFF00FF00) else Color.Red)
        )
        Spacer(Modifier.height(16.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween, // Zarovnání na opačné konce
            verticalAlignment = Alignment.CenterVertically
        ) {
            // Tlačítko Microsteps vlevo (pokud je připojeno a synchronizováno)
            if (connected && isSynced) {
                Box {
                    Button(onClick = { microstepsExpanded = true }) {
                        Text("Microsteps: $microsteps")
                    }
                    DropdownMenu(expanded = microstepsExpanded, onDismissRequest = { microstepsExpanded = false }) {
                        microstepsOptions.forEach { selectionOption ->
                            DropdownMenuItem(
                                text = { Text(selectionOption.toString()) },
                                onClick = {
                                    viewModel.sendMicrosteps(selectionOption)
                                    microstepsExpanded = false
                                }
                            )
                        }
                    }
                }
            } else {
                // Prázdné místo, aby tlačítko pro připojení zůstalo vpravo
                Spacer(Modifier.width(0.dp))
            }

            // Tlačítko Připojit/Odpojit vpravo
            Button(
                onClick = {
                    if (connected) {
                        viewModel.disconnectBLE()
                    } else {
                        viewModel.startScan()
                        onShowDeviceDialog()
                    }
                },
                colors = ButtonDefaults.buttonColors(containerColor = if (connected) Color.Red else Color.Green)
            ) {
                Text(if (connected) "BLE odpojit" else "BLE připojit", color = Color.White)
            }
        }

        Spacer(Modifier.height(24.dp))
        
        if (connected && isSynced) {
            // Tento řádek byl odstraněn, protože tlačítko Microsteps je nyní nahoře

            Spacer(Modifier.height(8.dp))

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text("Rychlost:")
                Text(
                    text = String.format(Locale.US, "%.2f cm/s | %.2f RPM", speedCms, rpm),
                    style = MaterialTheme.typography.bodyMedium
                )
            }
            Slider(
                value = speed,
                onValueChange = { viewModel.setSpeed(it) },
                valueRange = 0f..100f,
                modifier = Modifier.fillMaxWidth()
            )
            Spacer(Modifier.height(24.dp))
            Row(
                Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Button(
                    onClick = { viewModel.sendBackward() },
                    colors = ButtonDefaults.buttonColors(containerColor = if (trainState == TrainState.BACKWARD) Color.LightGray else MaterialTheme.colorScheme.primary)
                ) { Text("Vzad") }
                Button(
                    onClick = { viewModel.sendStop() },
                    colors = ButtonDefaults.buttonColors(containerColor = if (trainState == TrainState.STOPPED) Color.LightGray else MaterialTheme.colorScheme.primary)
                ) { Text("STOP") }
                Button(
                    onClick = { viewModel.sendForward() },
                    colors = ButtonDefaults.buttonColors(containerColor = if (trainState == TrainState.FORWARD) Color.LightGray else MaterialTheme.colorScheme.primary)
                ) { Text("Vpřed") }
            }
            
            Spacer(Modifier.height(24.dp))
            
            Button(
                onClick = { /* Logiku řeší pointerInput */ },
                modifier = Modifier
                    .fillMaxWidth()
                    .pointerInput(Unit) {
                        awaitPointerEventScope {
                            while (true) {
                                awaitFirstDown(requireUnconsumed = false)
                                viewModel.sendHornOn()
                                waitForUpOrCancellation()
                                viewModel.sendHornOff()
                            }
                        }
                    }
            ) {
                Text("Houkačka")
            }

        } else if (connected) {
            Column(modifier = Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
                CircularProgressIndicator()
                Spacer(Modifier.height(8.dp))
                Text("Synchronizace s vlakem...")
            }
        }
    }
}

// Tvar osmiúhelníku pro značku STOP
val OctagonShape = GenericShape { size, _ ->
    val width = size.width
    val height = size.height
    val a = 0.2929f 

    moveTo(width * a, 0f)
    lineTo(width * (1 - a), 0f)
    lineTo(width, height * a)
    lineTo(width, height * (1 - a))
    lineTo(width * (1 - a), height)
    lineTo(width * a, height)
    lineTo(0f, height * (1 - a))
    lineTo(0f, height * a)
    close()
}

@Composable
fun EmergencyStopButton(onClick: () -> Unit) {
    Box(
        modifier = Modifier
            .size(100.dp)
            .clip(OctagonShape)
            .background(Color(0xFFB00020))
            .clickable(onClick = onClick),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = "STOP",
            color = Color.White,
            fontSize = 24.sp,
            fontWeight = FontWeight.Bold
        )
    }
}

@SuppressLint("MissingPermission")
@Composable
private fun DeviceSelectionDialog(
    devices: List<BluetoothDevice>,
    isScanning: Boolean,
    onDismiss: () -> Unit,
    onDeviceSelected: (BluetoothDevice) -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Vyber si svůj vlak") },
        text = {
            Column(horizontalAlignment = Alignment.CenterHorizontally) {
                if (isScanning) {
                    CircularProgressIndicator()
                    Spacer(Modifier.height(8.dp))
                    Text("Hledám vlaky...")
                }
                if (devices.isEmpty() && !isScanning) {
                    Text("Žádný vlak nebyl nalezen. Zkus to znovu.")
                }
                LazyColumn {
                    items(devices) { device ->
                        Text(
                            text = device.name ?: "Neznámý vlak",
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable { onDeviceSelected(device) }
                                .padding(vertical = 12.dp)
                        )
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text("Zrušit")
            }
        }
    )
}
