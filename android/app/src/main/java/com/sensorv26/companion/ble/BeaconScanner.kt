package com.sensorv26.companion.ble

import android.annotation.SuppressLint
import android.bluetooth.BluetoothManager
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import com.sensorv26.companion.Protocol
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.update

/** A scanned advertisement, decoded when it matches our sensor format. */
data class ScannedBeacon(
    val address: String,
    val name: String?,
    val rssi: Int,
    val sensor: Protocol.SensorBeacon?,
    val lastSeenMs: Long,
)

/**
 * Scans for the card's BLE beacons. When [filterOurs] is true the OS-level
 * scan filter only surfaces adverts carrying manufacturer company 0xFFFF, so
 * unrelated devices never reach the app.
 */
class BeaconScanner(context: Context) {

    private val scanner: BluetoothLeScanner? =
        (context.getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager)
            .adapter?.bluetoothLeScanner

    private val _beacons = MutableStateFlow<List<ScannedBeacon>>(emptyList())
    val beacons: StateFlow<List<ScannedBeacon>> = _beacons

    private val _scanning = MutableStateFlow(false)
    val scanning: StateFlow<Boolean> = _scanning

    val isSupported: Boolean get() = scanner != null

    private val callback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) = handle(result)
        override fun onBatchScanResults(results: MutableList<ScanResult>) =
            results.forEach { handle(it) }
    }

    @SuppressLint("MissingPermission")
    private fun handle(result: ScanResult) {
        val record = result.scanRecord
        val mfr = record?.getManufacturerSpecificData(Protocol.COMPANY_ID)
        val sensor = Protocol.decodeManufacturerData(mfr)
        val entry = ScannedBeacon(
            address = result.device.address,
            name = record?.deviceName,
            rssi = result.rssi,
            sensor = sensor,
            lastSeenMs = System.currentTimeMillis(),
        )
        _beacons.update { list ->
            (list.filterNot { it.address == entry.address } + entry)
                .sortedByDescending { it.rssi }
        }
    }

    @SuppressLint("MissingPermission")
    fun start(filterOurs: Boolean) {
        val s = scanner ?: return
        if (_scanning.value) return
        _beacons.value = emptyList()

        val filters = if (filterOurs) {
            listOf(
                ScanFilter.Builder()
                    .setManufacturerData(Protocol.COMPANY_ID, byteArrayOf(Protocol.VERSION))
                    .build()
            )
        } else emptyList()

        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .build()

        s.startScan(filters, settings, callback)
        _scanning.value = true
    }

    @SuppressLint("MissingPermission")
    fun stop() {
        if (!_scanning.value) return
        scanner?.stopScan(callback)
        _scanning.value = false
    }
}
