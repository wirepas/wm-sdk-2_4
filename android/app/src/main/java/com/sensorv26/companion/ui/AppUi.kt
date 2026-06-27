package com.sensorv26.companion.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Nfc
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material.icons.filled.WifiTethering
import androidx.compose.material3.Card
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.sensorv26.companion.AppViewModel
import com.sensorv26.companion.NfcMode
import com.sensorv26.companion.ble.ScannedBeacon

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppRoot(vm: AppViewModel, nfcAvailable: Boolean) {
    var tab by remember { mutableIntStateOf(0) }
    val titles = listOf("NFC", "Scan", "Émission")

    Scaffold(
        topBar = { CenterAlignedTopAppBar(title = { Text("SensorV26 Companion") }) },
        bottomBar = {
            NavigationBar {
                NavigationBarItem(
                    selected = tab == 0, onClick = { tab = 0 },
                    icon = { Icon(Icons.Filled.Nfc, null) }, label = { Text(titles[0]) })
                NavigationBarItem(
                    selected = tab == 1, onClick = { tab = 1 },
                    icon = { Icon(Icons.Filled.Sensors, null) }, label = { Text(titles[1]) })
                NavigationBarItem(
                    selected = tab == 2, onClick = { tab = 2 },
                    icon = { Icon(Icons.Filled.WifiTethering, null) }, label = { Text(titles[2]) })
            }
        }
    ) { pad ->
        Column(Modifier.fillMaxSize().padding(pad).padding(16.dp)) {
            when (tab) {
                0 -> NfcTab(vm, nfcAvailable)
                1 -> ScanTab(vm)
                2 -> EmitTab(vm)
            }
        }
    }
}

// ---------------------------------------------------------------- NFC tab

@Composable
private fun NfcTab(vm: AppViewModel, nfcAvailable: Boolean) {
    val state by vm.nfc.collectAsStateWithLifecycle()
    var net by remember { mutableStateOf("") }
    var ch by remember { mutableStateOf("") }
    var addr by remember { mutableStateOf("") }

    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        if (!nfcAvailable) {
            SectionCard {
                Text("NFC désactivé ou indisponible.", color = MaterialTheme.colorScheme.error)
            }
        }

        SectionCard {
            Text("Mode", fontWeight = FontWeight.Bold)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilterChip(
                    selected = state.mode == NfcMode.READ,
                    onClick = { vm.setNfcMode(NfcMode.READ) },
                    label = { Text("Lire l'adresse") })
                FilterChip(
                    selected = state.mode == NfcMode.WRITE,
                    onClick = { vm.setNfcMode(NfcMode.WRITE) },
                    label = { Text("Écrire credentials") })
            }
            Text(
                "Approchez le téléphone de l'antenne NFC de la carte.",
                style = MaterialTheme.typography.bodySmall,
            )
        }

        if (state.mode == NfcMode.READ) {
            SectionCard {
                Text("Adresse du nœud lue", fontWeight = FontWeight.Bold)
                Text(state.lastRead ?: "—", fontFamily = FontFamily.Monospace)
                val dec = state.lastRead
                    ?.trim()?.removePrefix("0x")?.removePrefix("0X")?.toLongOrNull(16)
                if (dec != null) {
                    Text("décimal : $dec", fontFamily = FontFamily.Monospace,
                        style = MaterialTheme.typography.bodySmall)
                }
            }
        } else {
            SectionCard {
                Text("Credentials de commissioning", fontWeight = FontWeight.Bold)
                OutlinedTextField(
                    value = net, onValueChange = { net = it.filter { c -> c.isLetterOrDigit() } },
                    label = { Text("net (hex 24 bits, ex: A1B2C3)") },
                    singleLine = true, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(
                    value = ch, onValueChange = { ch = it.filter { c -> c.isDigit() } },
                    label = { Text("ch (canal 1-11)") },
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                    singleLine = true, modifier = Modifier.fillMaxWidth())
                OutlinedTextField(
                    value = addr, onValueChange = { addr = it.filter { c -> c.isLetterOrDigit() } },
                    label = { Text("addr (hex 32 bits, optionnel)") },
                    singleLine = true, modifier = Modifier.fillMaxWidth())
                OutlinedButton(
                    onClick = { vm.stageWrite(net, ch.toIntOrNull() ?: 0, addr.ifBlank { null }) },
                    enabled = net.isNotBlank() && (ch.toIntOrNull() ?: 0) in 1..11,
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Préparer l'écriture") }
                state.lastWrite?.let {
                    Text("Dernier envoi : $it", fontFamily = FontFamily.Monospace,
                        style = MaterialTheme.typography.bodySmall)
                }
            }
        }

        state.message?.let {
            SectionCard { Text(it) }
        }
    }
}

// ---------------------------------------------------------------- Scan tab

@Composable
private fun ScanTab(vm: AppViewModel) {
    val scanning by vm.scanner.scanning.collectAsStateWithLifecycle()
    val beacons by vm.scanner.beacons.collectAsStateWithLifecycle()
    var filterOurs by remember { mutableStateOf(true) }

    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        SectionCard {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Filtrer la carte (0xFFFF/0x1234)", Modifier.weight(1f))
                Switch(checked = filterOurs, onCheckedChange = {
                    filterOurs = it
                    if (scanning) { vm.scanner.stop(); vm.scanner.start(filterOurs) }
                })
            }
            OutlinedButton(
                onClick = { if (scanning) vm.scanner.stop() else vm.scanner.start(filterOurs) },
                enabled = vm.scanner.isSupported,
                modifier = Modifier.fillMaxWidth(),
            ) { Text(if (scanning) "Arrêter le scan" else "Démarrer le scan") }
        }

        if (beacons.isEmpty()) {
            Text("Aucun beacon pour l'instant…", style = MaterialTheme.typography.bodyMedium)
        } else {
            LazyColumn(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                items(beacons, key = { it.address }) { BeaconRow(it) }
            }
        }
    }
}

@Composable
private fun BeaconRow(b: ScannedBeacon) {
    SectionCard {
        Row {
            Text(b.name ?: b.address, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
            Text("${b.rssi} dBm", fontFamily = FontFamily.Monospace)
        }
        val s = b.sensor
        if (s != null) {
            HorizontalDivider(Modifier.padding(vertical = 4.dp))
            Text("node = 0x%08X (%d)".format(s.nodeAddr, s.nodeAddr),
                fontFamily = FontFamily.Monospace)
            Text("vbat = %d mV   T = %.1f °C".format(s.vbatMv, s.tempC),
                fontFamily = FontFamily.Monospace)
            Text("charge=${if (s.charging) 1 else 0}  valid=${if (s.valid) 1 else 0}",
                fontFamily = FontFamily.Monospace)
        } else {
            Text(b.address, style = MaterialTheme.typography.bodySmall)
        }
    }
}

// ---------------------------------------------------------------- Emit tab

@Composable
private fun EmitTab(vm: AppViewModel) {
    val advertising by vm.advertiser.advertising.collectAsStateWithLifecycle()
    val status by vm.advertiser.status.collectAsStateWithLifecycle()

    var addr by remember { mutableStateOf("0000002A") }
    var vbat by remember { mutableStateOf("3300") }
    var temp by remember { mutableStateOf("23") }
    var charging by remember { mutableStateOf(false) }

    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        SectionCard {
            Text("Beacon à émettre (lu par la carte)", fontWeight = FontWeight.Bold)
            OutlinedTextField(
                value = addr, onValueChange = { addr = it.filter { c -> c.isLetterOrDigit() } },
                label = { Text("node addr (hex 32 bits)") },
                singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(
                value = vbat, onValueChange = { vbat = it.filter { c -> c.isDigit() } },
                label = { Text("vbat (mV)") },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(
                value = temp, onValueChange = { temp = it.filter { c -> c.isDigit() || c == '-' || c == '.' } },
                label = { Text("température (°C)") },
                singleLine = true, modifier = Modifier.fillMaxWidth())
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("en charge", Modifier.weight(1f))
                Switch(checked = charging, onCheckedChange = { charging = it })
            }
        }

        SectionCard {
            OutlinedButton(
                onClick = {
                    if (advertising) vm.advertiser.stop()
                    else vm.advertiser.start(
                        nodeAddr = addr.toLongOrNull(16) ?: 0L,
                        vbatMv = vbat.toIntOrNull() ?: 0,
                        tempC = temp.toDoubleOrNull() ?: 0.0,
                        charging = charging,
                    )
                },
                enabled = vm.advertiser.isSupported,
                modifier = Modifier.fillMaxWidth(),
            ) { Text(if (advertising) "Arrêter l'émission" else "Émettre le beacon") }
            status?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
            if (!vm.advertiser.isSupported)
                Text("Cet appareil ne supporte pas l'émission BLE.",
                    color = MaterialTheme.colorScheme.error)
        }
    }
}

// ---------------------------------------------------------------- shared

@Composable
private fun SectionCard(content: @Composable ColumnScope.() -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            content()
        }
    }
}
