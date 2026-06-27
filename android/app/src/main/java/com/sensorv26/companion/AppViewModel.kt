package com.sensorv26.companion

import android.app.Application
import android.nfc.Tag
import androidx.lifecycle.AndroidViewModel
import com.sensorv26.companion.ble.BeaconAdvertiser
import com.sensorv26.companion.ble.BeaconScanner
import com.sensorv26.companion.nfc.NfcNdef
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

class AppViewModel(app: Application) : AndroidViewModel(app) {

    val scanner = BeaconScanner(app)
    val advertiser = BeaconAdvertiser(app)

    private val _nfc = MutableStateFlow(NfcUiState())
    val nfc: StateFlow<NfcUiState> = _nfc

    /** Commissioning text staged for the next WRITE tap. */
    @Volatile private var pendingWriteText: String? = null

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
        when (_nfc.value.mode) {
            NfcMode.READ -> {
                _nfc.value = when (val r = NfcNdef.read(tag)) {
                    is com.sensorv26.companion.nfc.ReadResult.Success ->
                        _nfc.value.copy(lastRead = r.text, message = "Lecture OK")
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
}
