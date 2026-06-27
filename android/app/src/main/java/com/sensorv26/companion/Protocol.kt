package com.sensorv26.companion

import android.os.ParcelUuid
import java.util.UUID

/**
 * Wire protocol shared with the nRF54L15 firmware (rs485_bridge app).
 *
 * NFC  : Type-2 tag NDEF Text records.
 *   - read  : node address as "0x%08X"
 *   - write : "net=AABBCC;ch=7;addr=0000002A"  (addr optional)
 *
 * BLE beacon (card -> phone), Manufacturer Specific Data — Wirepas net info:
 *   company 0xFFFF, then type(0x01) node(4 LE) sink(4 LE) cost(1)
 *                        nbor_count(1) best_rssi(1, signed)
 *   sink = 0 and cost = 0xFF when the node has no route to a sink.
 *
 * The card's RX filter accepts an advert iff it contains 16-bit UUID 0x1234
 * AND manufacturer data company 0xFFFF.
 *
 * Control command (phone -> card), Manufacturer Specific Data:
 *   company 0xFFFF, then  type=0x02 target(4 LE) cmd(1) param(4 LE)
 *   target — node address the command is aimed at (from the NFC read)
 *   cmd    — command id (see CMD_*)
 *   param  — 32-bit parameter (e.g. LED state 0/1)
 */
object Protocol {

    const val SERVICE_UUID_16 = 0x1234

    /** 0x1234 expanded onto the Bluetooth SIG base UUID. */
    val SERVICE_UUID: ParcelUuid =
        ParcelUuid(UUID.fromString("00001234-0000-1000-8000-00805F9B34FB"))

    /** 0xFFFF — "no company / test" manufacturer ID. */
    const val COMPANY_ID = 0xFFFF

    /** Manufacturer payload type byte (first byte after the company id). */
    const val TYPE_SENSOR: Byte = 0x01   // card -> phone telemetry
    const val TYPE_COMMAND: Byte = 0x02  // phone -> card control command

    const val VERSION: Byte = TYPE_SENSOR

    const val MFR_PAYLOAD_LEN = 12 // net: type(1)+node(4)+sink(4)+cost(1)+nbors(1)+rssi(1)

    /** Route cost meaning "no route to a sink" (APP_LIB_STATE_INVALID_ROUTE_COST). */
    const val NO_ROUTE_COST = 0xFF

    // ---- Control command ids (phone -> card) -------------------------------
    const val CMD_LED_RED = 0x01     // param = 0/1 (off/on)
    const val CMD_LED_GREEN = 0x02   // param = 0/1 (off/on)
    const val CMD_BEACON_TX = 0x03   // param = 0/1 (card stops/starts its own beacon)

    // ---- Beacon decode (card -> phone) -------------------------------------

    data class NetworkBeacon(
        val nodeAddr: Long,
        val sinkAddr: Long,
        val cost: Int,
        val neighbourCount: Int,
        val rssi: Int,
    ) {
        val hasRoute: Boolean get() = cost != NO_ROUTE_COST && sinkAddr != 0L
    }

    /**
     * Decode the manufacturer-specific payload (already stripped of the 2-byte
     * company id by [android.bluetooth.le.ScanRecord.getManufacturerSpecificData]).
     * Returns null if it is not one of our Wirepas network beacons.
     */
    fun decodeManufacturerData(data: ByteArray?): NetworkBeacon? {
        if (data == null || data.size < MFR_PAYLOAD_LEN) return null
        if (data[0] != TYPE_SENSOR) return null
        val node = data[1].u() or (data[2].u() shl 8) or
                (data[3].u() shl 16) or (data[4].u() shl 24)
        val sink = data[5].u() or (data[6].u() shl 8) or
                (data[7].u() shl 16) or (data[8].u() shl 24)
        return NetworkBeacon(
            nodeAddr = node.toLong() and 0xFFFFFFFFL,
            sinkAddr = sink.toLong() and 0xFFFFFFFFL,
            cost = data[9].toInt() and 0xFF,
            neighbourCount = data[10].toInt() and 0xFF,
            rssi = data[11].toInt(),  // signed
        )
    }

    // ---- Command encode (phone -> card) ------------------------------------

    /**
     * Build the command manufacturer payload (without the company id):
     * type(1) target(4 LE) cmd(1) param(4 LE) = 10 bytes.
     */
    fun encodeCommand(targetAddr: Long, cmd: Int, param: Long): ByteArray {
        val a = targetAddr.toInt()
        val p = param.toInt()
        return byteArrayOf(
            TYPE_COMMAND,
            (a ushr 0).toByte(), (a ushr 8).toByte(),
            (a ushr 16).toByte(), (a ushr 24).toByte(),
            (cmd and 0xFF).toByte(),
            (p ushr 0).toByte(), (p ushr 8).toByte(),
            (p ushr 16).toByte(), (p ushr 24).toByte(),
        )
    }

    // ---- NFC commissioning string ------------------------------------------

    /** "net=AABBCC;ch=7[;addr=0000002A]" expected by the firmware. */
    fun buildCommissioningText(netHex: String, channel: Int, addrHex: String?): String {
        val sb = StringBuilder()
        sb.append("net=").append(netHex.trim().removePrefix("0x").uppercase())
        sb.append(";ch=").append(channel)
        val a = addrHex?.trim()?.removePrefix("0x")?.uppercase()
        if (!a.isNullOrEmpty()) sb.append(";addr=").append(a)
        return sb.toString()
    }

    private fun Byte.u(): Int = this.toInt() and 0xFF
}
