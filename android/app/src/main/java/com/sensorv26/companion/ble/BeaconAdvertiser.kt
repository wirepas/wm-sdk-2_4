package com.sensorv26.companion.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.bluetooth.le.AdvertiseCallback
import android.bluetooth.le.AdvertiseData
import android.bluetooth.le.AdvertiseSettings
import android.bluetooth.le.BluetoothLeAdvertiser
import android.content.Context
import com.sensorv26.companion.Protocol
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * Emits a beacon the card recognises: 16-bit service UUID 0x1234 plus
 * manufacturer data (company 0xFFFF, version 0x01, sensor payload).
 */
class BeaconAdvertiser(context: Context) {

    private val advertiser: BluetoothLeAdvertiser? =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager)
            .adapter?.bluetoothLeAdvertiser

    private val _advertising = MutableStateFlow(false)
    val advertising: StateFlow<Boolean> = _advertising

    private val _status = MutableStateFlow<String?>(null)
    val status: StateFlow<String?> = _status

    val isSupported: Boolean get() = advertiser != null

    private val callback = object : AdvertiseCallback() {
        override fun onStartSuccess(settingsInEffect: AdvertiseSettings?) {
            _advertising.value = true
            _status.value = "Émission en cours"
        }

        override fun onStartFailure(errorCode: Int) {
            _advertising.value = false
            _status.value = "Échec (code $errorCode): " + when (errorCode) {
                ADVERTISE_FAILED_DATA_TOO_LARGE -> "données trop grandes"
                ADVERTISE_FAILED_TOO_MANY_ADVERTISERS -> "trop d'annonceurs"
                ADVERTISE_FAILED_ALREADY_STARTED -> "déjà démarré"
                ADVERTISE_FAILED_INTERNAL_ERROR -> "erreur interne"
                ADVERTISE_FAILED_FEATURE_UNSUPPORTED -> "non supporté par l'appareil"
                else -> "inconnu"
            }
        }
    }

    @SuppressLint("MissingPermission")
    fun start(nodeAddr: Long, vbatMv: Int, tempC: Double, charging: Boolean) {
        val adv = advertiser
        if (adv == null) {
            _status.value = "BLE advertising non supporté"
            return
        }
        if (_advertising.value) return

        val settings = AdvertiseSettings.Builder()
            .setAdvertiseMode(AdvertiseSettings.ADVERTISE_MODE_LOW_LATENCY)
            .setTxPowerLevel(AdvertiseSettings.ADVERTISE_TX_POWER_HIGH)
            .setConnectable(false)
            .build()

        val payload = Protocol.encodeManufacturerData(
            nodeAddr = nodeAddr, vbatMv = vbatMv, tempC = tempC,
            charging = charging, valid = true,
        )

        val data = AdvertiseData.Builder()
            .setIncludeDeviceName(false)
            .addServiceUuid(Protocol.SERVICE_UUID)
            .addManufacturerData(Protocol.COMPANY_ID, payload)
            .build()

        adv.startAdvertising(settings, data, callback)
    }

    @SuppressLint("MissingPermission")
    fun stop() {
        if (!_advertising.value) return
        advertiser?.stopAdvertising(callback)
        _advertising.value = false
        _status.value = "Arrêté"
    }
}
