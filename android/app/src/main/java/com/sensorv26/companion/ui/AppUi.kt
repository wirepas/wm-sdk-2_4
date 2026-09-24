package com.sensorv26.companion.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Fingerprint
import androidx.compose.material.icons.filled.Nfc
import androidx.compose.material.icons.filled.Sensors
import androidx.compose.material.icons.filled.WifiTethering
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
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
import androidx.compose.material3.TextButton
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.scale
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.sensorv26.companion.AppViewModel
import com.sensorv26.companion.NfcMode
import com.sensorv26.companion.Protocol
import com.sensorv26.companion.Screen
import com.sensorv26.companion.ble.ScannedBeacon
import com.sensorv26.companion.nfc.se050.Se050ReadResult
import com.journeyapps.barcodescanner.ScanContract
import com.journeyapps.barcodescanner.ScanOptions
import java.security.cert.CertificateFactory
import java.text.SimpleDateFormat
import java.util.Locale

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppRoot(vm: AppViewModel, nfcAvailable: Boolean) {
    var tab by remember { mutableIntStateOf(0) }
    val titles = listOf("NFC", "Scan", "Émission", "SE050")

    fun selectTab(i: Int) {
        tab = i
        // Screen 3 (SE050) reads a different device over a different NFC tech
        // (IsoDep) than tabs 0-2 (NDEF mesh tag) - the ViewModel needs to know
        // which one the single global onTagDiscovered callback should route to.
        vm.setScreen(if (i == 3) Screen.SE050 else Screen.MESH_TAG)
    }

    Scaffold(
        topBar = { CenterAlignedTopAppBar(title = { Text("SensorV26 Companion") }) },
        bottomBar = {
            NavigationBar {
                NavigationBarItem(
                    selected = tab == 0, onClick = { selectTab(0) },
                    icon = { Icon(Icons.Filled.Nfc, null) }, label = { Text(titles[0]) })
                NavigationBarItem(
                    selected = tab == 1, onClick = { selectTab(1) },
                    icon = { Icon(Icons.Filled.Sensors, null) }, label = { Text(titles[1]) })
                NavigationBarItem(
                    selected = tab == 2, onClick = { selectTab(2) },
                    icon = { Icon(Icons.Filled.WifiTethering, null) }, label = { Text(titles[2]) })
                NavigationBarItem(
                    selected = tab == 3, onClick = { selectTab(3) },
                    icon = { Icon(Icons.Filled.Fingerprint, null) }, label = { Text(titles[3]) })
            }
        }
    ) { pad ->
        Column(Modifier.fillMaxSize().padding(pad).padding(16.dp)) {
            when (tab) {
                0 -> NfcTab(vm, nfcAvailable)
                1 -> ScanTab(vm)
                2 -> EmitTab(vm)
                3 -> Se050Tab(vm, nfcAvailable)
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

    Column(
        Modifier.verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
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
        val n = b.net
        if (n != null) {
            HorizontalDivider(Modifier.padding(vertical = 4.dp))
            Text("node = 0x%08X (%d)".format(n.nodeAddr, n.nodeAddr),
                fontFamily = FontFamily.Monospace)
            if (n.hasRoute)
                Text("sink = 0x%08X   cost = %d".format(n.sinkAddr, n.cost),
                    fontFamily = FontFamily.Monospace)
            else
                Text("sink = — (pas de route)", fontFamily = FontFamily.Monospace)
            Text("voisins = %d   rssi = %d".format(n.neighbourCount, n.rssi),
                fontFamily = FontFamily.Monospace)
        } else {
            Text(b.address, style = MaterialTheme.typography.bodySmall)
        }
    }
}

// ---------------------------------------------------------------- Emit tab

@OptIn(ExperimentalMaterial3Api::class, ExperimentalLayoutApi::class)
@Composable
private fun EmitTab(vm: AppViewModel) {
    val advertising by vm.advertiser.advertising.collectAsStateWithLifecycle()
    val status by vm.advertiser.status.collectAsStateWithLifecycle()
    val states by vm.advertiser.sentStates.collectAsStateWithLifecycle()
    val nfc by vm.nfc.collectAsStateWithLifecycle()
    val history by vm.nfcHistory.collectAsStateWithLifecycle()

    val haptic = LocalHapticFeedback.current
    fun isActive(cmd: Int, param: Long) = states[cmd] == param

    // Address auto-filled from the last NFC read; re-seeded whenever it changes.
    val nfcAddr = nfc.lastRead
        ?.trim()?.removePrefix("0x")?.removePrefix("0X")?.takeIf { it.toLongOrNull(16) != null }
    var addr by remember(nfcAddr) { mutableStateOf(nfcAddr ?: "0000002A") }
    var cmd by remember { mutableStateOf("16") }
    var param by remember { mutableStateOf("0") }

    fun target(): Long = addr.toLongOrNull(16) ?: 0L
    val canEmit = vm.advertiser.isSupported

    Column(
        Modifier.verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        SectionCard {
            Text("Carte cible", fontWeight = FontWeight.Bold)
            OutlinedTextField(
                value = addr, onValueChange = { addr = it.filter { c -> c.isLetterOrDigit() } },
                label = { Text("node addr (hex 32 bits)") },
                singleLine = true, modifier = Modifier.fillMaxWidth())
            if (nfcAddr != null)
                Text("auto-renseignée depuis NFC : 0x$nfcAddr",
                    style = MaterialTheme.typography.bodySmall)
            else
                Text("Lis la carte dans l'onglet NFC, ou choisis ci-dessous.",
                    style = MaterialTheme.typography.bodySmall)

            if (history.isNotEmpty()) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Historique NFC", Modifier.weight(1f),
                        fontWeight = FontWeight.Bold,
                        style = MaterialTheme.typography.bodySmall)
                    TextButton(onClick = { vm.clearHistory() }) { Text("Effacer") }
                }
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    history.forEach { h ->
                        FilterChip(
                            selected = addr.equals(h, ignoreCase = true),
                            onClick = {
                                haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                                addr = h
                            },
                            label = { Text("0x$h") })
                    }
                }
            }
        }

        SectionCard {
            Text("Commandes LED", fontWeight = FontWeight.Bold)
            Row(verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("LED rouge", Modifier.weight(1f))
                CmdButton("ON", isActive(Protocol.CMD_LED_RED, 1), canEmit) {
                    vm.advertiser.send(target(), Protocol.CMD_LED_RED, 1) }
                CmdButton("OFF", isActive(Protocol.CMD_LED_RED, 0), canEmit) {
                    vm.advertiser.send(target(), Protocol.CMD_LED_RED, 0) }
            }
            Row(verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("LED verte", Modifier.weight(1f))
                CmdButton("ON", isActive(Protocol.CMD_LED_GREEN, 1), canEmit) {
                    vm.advertiser.send(target(), Protocol.CMD_LED_GREEN, 1) }
                CmdButton("OFF", isActive(Protocol.CMD_LED_GREEN, 0), canEmit) {
                    vm.advertiser.send(target(), Protocol.CMD_LED_GREEN, 0) }
            }
        }

        SectionCard {
            Text("Beacon de la carte", fontWeight = FontWeight.Bold)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                CmdButton("START", isActive(Protocol.CMD_BEACON_TX, 1), canEmit,
                    Modifier.weight(1f)) {
                    vm.advertiser.send(target(), Protocol.CMD_BEACON_TX, 1) }
                CmdButton("STOP", isActive(Protocol.CMD_BEACON_TX, 0), canEmit,
                    Modifier.weight(1f)) {
                    vm.advertiser.send(target(), Protocol.CMD_BEACON_TX, 0) }
            }
        }

        SectionCard {
            Text("Commande générique", fontWeight = FontWeight.Bold)
            OutlinedTextField(
                value = cmd, onValueChange = { cmd = it.filter { c -> c.isDigit() } },
                label = { Text("CMD (0-255)") },
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                singleLine = true, modifier = Modifier.fillMaxWidth())
            OutlinedTextField(
                value = param, onValueChange = { param = it.filter { c -> c.isLetterOrDigit() } },
                label = { Text("PARAM (déc, ou 0x… hex)") },
                singleLine = true, modifier = Modifier.fillMaxWidth())
            CmdButton("Envoyer", false, canEmit, Modifier.fillMaxWidth()) {
                vm.advertiser.send(target(), cmd.toIntOrNull() ?: 0, parseNum(param))
            }
        }

        SectionCard {
            OutlinedButton(
                onClick = {
                    haptic.performHapticFeedback(HapticFeedbackType.LongPress)
                    vm.advertiser.stop()
                },
                enabled = canEmit && advertising,
                modifier = Modifier.fillMaxWidth(),
            ) { Text("Arrêter l'émission") }
            status?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
            if (!canEmit)
                Text("Cet appareil ne supporte pas l'émission BLE.",
                    color = MaterialTheme.colorScheme.error)
        }
    }
}

/** Parse a uint32 as decimal, or hex when prefixed with 0x. */
private fun parseNum(s: String): Long {
    val t = s.trim()
    return if (t.startsWith("0x") || t.startsWith("0X"))
        t.removePrefix("0x").removePrefix("0X").toLongOrNull(16) ?: 0L
    else t.toLongOrNull() ?: 0L
}

// ---------------------------------------------------------------- shared

/** Button with haptic feedback, a press scale-down effect, and an active
 * (filled) vs idle (muted) colour state. */
@Composable
private fun CmdButton(
    label: String,
    active: Boolean,
    enabled: Boolean,
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    val haptic = LocalHapticFeedback.current
    val interaction = remember { MutableInteractionSource() }
    val pressed by interaction.collectIsPressedAsState()
    val scale by animateFloatAsState(if (pressed) 0.92f else 1f, label = "press")
    val colors = if (active)
        ButtonDefaults.buttonColors(
            containerColor = MaterialTheme.colorScheme.primary,
            contentColor = MaterialTheme.colorScheme.onPrimary)
    else
        ButtonDefaults.buttonColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant,
            contentColor = MaterialTheme.colorScheme.onSurfaceVariant)
    Button(
        onClick = {
            haptic.performHapticFeedback(HapticFeedbackType.LongPress)
            onClick()
        },
        enabled = enabled,
        interactionSource = interaction,
        colors = colors,
        modifier = modifier.scale(scale),
    ) { Text(label) }
}

// ---------------------------------------------------------------- SE050 tab

@Composable
private fun Se050Tab(vm: AppViewModel, nfcAvailable: Boolean) {
    val state by vm.se050.collectAsStateWithLifecycle()

    Column(
        Modifier.verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        if (!nfcAvailable) {
            SectionCard {
                Text("NFC désactivé ou indisponible.", color = MaterialTheme.colorScheme.error)
            }
        }

        SectionCard {
            Text("Identité SE050 (gateway nRF9160)", fontWeight = FontWeight.Bold)
            Text(
                "Approchez le téléphone de l'antenne NFC du SE050. Puce " +
                        "vierge : clés d'usine a200/a201 essayées automatiquement. " +
                        "Puce déjà rotée : scanner d'abord le QR de sa clé dérivée " +
                        "(généré côté serveur pour ce device précis, jamais codé " +
                        "en dur ni sauvegardé sur le téléphone).",
                style = MaterialTheme.typography.bodySmall,
            )
        }

        SectionCard {
            Text("Clé pour puce déjà rotée", fontWeight = FontWeight.Bold)
            val scanLauncher = rememberLauncherForActivityResult(ScanContract()) { result ->
                result.contents?.let { vm.setManualKeyFromQr(it) }
            }
            val mk = state.manualKey
            if (mk != null) {
                Text("QR chargé — UID attendu : ${mk.expectedUidHex ?: "(non vérifié)"}",
                    fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
                TextButton(onClick = { vm.clearManualKey() }) { Text("Oublier cette clé") }
            } else {
                OutlinedButton(
                    onClick = {
                        scanLauncher.launch(
                            ScanOptions()
                                .setDesiredBarcodeFormats(ScanOptions.QR_CODE)
                                .setPrompt("Scanner le QR de la clé SCP03")
                                .setBeepEnabled(false)
                        )
                    },
                    modifier = Modifier.fillMaxWidth(),
                ) { Text("Scanner un QR de clé") }
            }
            state.manualKeyError?.let {
                Text(it, color = MaterialTheme.colorScheme.error,
                    style = MaterialTheme.typography.bodySmall)
            }
        }

        when (val r = state.result) {
            null -> Unit
            is Se050ReadResult.Error -> SectionCard {
                Text("Erreur", fontWeight = FontWeight.Bold)
                Text(r.message, color = MaterialTheme.colorScheme.error)
            }
            is Se050ReadResult.Success -> {
                SectionCard {
                    Text("Jeu de clés SCP03", fontWeight = FontWeight.Bold)
                    Text(r.keySetUsed, fontFamily = FontFamily.Monospace)
                }
                SectionCard {
                    Text("UID (18 octets)", fontWeight = FontWeight.Bold)
                    Text(r.uidHex, fontFamily = FontFamily.Monospace)
                }
                SectionCard {
                    Text("Clé publique (point EC brut 04‖X‖Y, 65 octets)", fontWeight = FontWeight.Bold)
                    Text(r.pubkeyRawHex, fontFamily = FontFamily.Monospace)
                }
                SectionCard {
                    Text("Certificat (${r.certDer.size} octets DER)", fontWeight = FontWeight.Bold)
                    val parsed = remember(r.certDer) { runCatching { parseCert(r.certDer) } }.getOrNull()
                    if (parsed != null) {
                        Text("Sujet : ${parsed.subject}", fontFamily = FontFamily.Monospace,
                            style = MaterialTheme.typography.bodySmall)
                        Text("Valide du ${parsed.notBefore} au ${parsed.notAfter}",
                            fontFamily = FontFamily.Monospace, style = MaterialTheme.typography.bodySmall)
                    } else {
                        Text("Certificat lu mais non parseable en X.509 standard",
                            color = MaterialTheme.colorScheme.error,
                            style = MaterialTheme.typography.bodySmall)
                    }
                }
            }
        }
    }
}

private data class ParsedCert(val subject: String, val notBefore: String, val notAfter: String)

private fun parseCert(der: ByteArray): ParsedCert {
    val cert = CertificateFactory.getInstance("X.509")
        .generateCertificate(der.inputStream()) as java.security.cert.X509Certificate
    val fmt = SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.US)
    return ParsedCert(
        subject = cert.subjectX500Principal.name,
        notBefore = fmt.format(cert.notBefore),
        notAfter = fmt.format(cert.notAfter),
    )
}

@Composable
private fun SectionCard(content: @Composable ColumnScope.() -> Unit) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            content()
        }
    }
}
