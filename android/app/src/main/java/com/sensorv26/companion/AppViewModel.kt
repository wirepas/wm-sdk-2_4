package com.sensorv26.companion

import android.app.Application
import android.content.Context
import android.nfc.Tag
import androidx.lifecycle.AndroidViewModel
import com.sensorv26.companion.ble.BeaconAdvertiser
import com.sensorv26.companion.ble.BeaconScanner
import com.sensorv26.companion.nfc.NfcNdef
import com.sensorv26.companion.nfc.se050.Se050QrKey
import com.sensorv26.companion.nfc.se050.Se050ReadResult
import com.sensorv26.companion.nfc.se050.Se050Reader
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/** What to do the next time the phone is tapped on the card. */
enum class NfcMode { READ, WRITE }

data class NfcUiState(
    val mode: NfcMode = NfcMode.READ,
    val lastRead: String? = null,
    val lastWrite: String? = null,
    val message: String? = null,
)

/** Which tab is active, so a single global tag-discovery callback knows where to route a tap.
 *  MESH_TAG is the existing nRF54L15 NDEF/mesh flow; SE050 is a different device entirely
 *  (the nRF9160 gateway's secure element, read over IsoDep) - kept as a separate mode rather
 *  than folded into NfcMode.READ/WRITE since it's not the same target hardware. */
enum class Screen { MESH_TAG, SE050 }

data class Se050UiState(
    val busy: Boolean = false,
    val result: Se050ReadResult? = null,
    /** Non-null once a QR key has been scanned this session. In-memory only - never
     *  written to SharedPreferences/disk, never survives process death, by design:
     *  this is a real per-device secret, not the public a200/a201 defaults. */
    val manualKey: Se050QrKey.Parsed? = null,
    val manualKeyError: String? = null,
)

class AppViewModel(app: Application) : AndroidViewModel(app) {

    val scanner = BeaconScanner(app)
    val advertiser = BeaconAdvertiser(app)

    private val _nfc = MutableStateFlow(NfcUiState())
    val nfc: StateFlow<NfcUiState> = _nfc

    private val _screen = MutableStateFlow(Screen.MESH_TAG)
    val screen: StateFlow<Screen> = _screen

    private val _se050 = MutableStateFlow(Se050UiState())
    val se050: StateFlow<Se050UiState> = _se050

    fun setScreen(s: Screen) {
        _screen.value = s
    }

    /** Called with the raw text decoded from a scanned QR. Returns true on success. */
    fun setManualKeyFromQr(text: String): Boolean {
        val parsed = Se050QrKey.parse(text)
        _se050.value = if (parsed != null) {
            _se050.value.copy(manualKey = parsed, manualKeyError = null)
        } else {
            _se050.value.copy(manualKeyError = "QR illisible (format attendu : se050scp03;enc=...;mac=...)")
        }
        return parsed != null
    }

    fun clearManualKey() {
        _se050.value = _se050.value.copy(manualKey = null, manualKeyError = null)
    }

    /** Persisted history of addresses read via NFC (most-recent first). */
    private val prefs = app.getSharedPreferences("sensorv26_companion", Context.MODE_PRIVATE)
    private val _nfcHistory = MutableStateFlow(loadHistory())
    val nfcHistory: StateFlow<List<String>> = _nfcHistory

    /** Commissioning text staged for the next WRITE tap. */
    @Volatile private var pendingWriteText: String? = null

    private fun loadHistory(): List<String> =
        prefs.getString(KEY_HISTORY, null)
            ?.split(',')?.map { it.trim() }?.filter { it.isNotEmpty() }
            ?: emptyList()

    /** Add a canonical 8-hex-digit address to the front, dedup, cap at 20. */
    private fun rememberAddress(canonHex: String) {
        val list = (listOf(canonHex) + _nfcHistory.value.filter { !it.equals(canonHex, true) })
            .take(MAX_HISTORY)
        _nfcHistory.value = list
        prefs.edit().putString(KEY_HISTORY, list.joinToString(",")).apply()
    }

    fun clearHistory() {
        _nfcHistory.value = emptyList()
        prefs.edit().remove(KEY_HISTORY).apply()
    }

    fun setNfcMode(mode: NfcMode) {
        _nfc.value = _nfc.value.copy(mode = mode, message = null)
    }

    fun stageWrite(netHex: String, channel: Int, addrHex: String?) {
        val text = Protocol.buildCommissioningText(netHex, channel, addrHex)
        pendingWriteText = text
        _nfc.value = _nfc.value.copy(
            mode = NfcMode.WRITE,
            message = "Approchez le téléphone de la carte pour écrire : $text",
        )
    }

    /** Called from MainActivity when a tag is in range. */
    fun onTagDiscovered(tag: Tag) {
        if (_screen.value == Screen.SE050) {
            val manualKey = _se050.value.manualKey
            _se050.value = _se050.value.copy(busy = true)
            _se050.value = _se050.value.copy(
                busy = false,
                result = Se050Reader.read(tag, manualKey),
            )
            return
        }
        when (_nfc.value.mode) {
            NfcMode.READ -> {
                _nfc.value = when (val r = NfcNdef.read(tag)) {
                    is com.sensorv26.companion.nfc.ReadResult.Success -> {
                        // Normalise to 8 hex digits and store in history.
                        r.text.trim().removePrefix("0x").removePrefix("0X")
                            .toLongOrNull(16)?.let { rememberAddress("%08X".format(it)) }
                        _nfc.value.copy(lastRead = r.text, message = "Lecture OK")
                    }
                    is com.sensorv26.companion.nfc.ReadResult.NoText ->
                        _nfc.value.copy(message = "Aucun texte NDEF (techs: ${r.techs.joinToString()})")
                    is com.sensorv26.companion.nfc.ReadResult.Error ->
                        _nfc.value.copy(message = "Erreur: ${r.msg} (techs: ${r.techs.joinToString()})")
                }
            }
            NfcMode.WRITE -> {
                val toWrite = pendingWriteText
                if (toWrite == null) {
                    _nfc.value = _nfc.value.copy(message = "Aucun credential préparé")
                    return
                }
                val err = NfcNdef.writeText(tag, toWrite)
                _nfc.value = if (err == null) {
                    _nfc.value.copy(
                        lastWrite = toWrite,
                        message = "Écriture OK — la carte va redémarrer",
                    )
                } else {
                    _nfc.value.copy(message = "Échec écriture : $err")
                }
            }
        }
    }

    override fun onCleared() {
        scanner.stop()
        advertiser.stop()
    }

    private companion object {
        const val KEY_HISTORY = "nfc_history"
        const val MAX_HISTORY = 20
    }
}
