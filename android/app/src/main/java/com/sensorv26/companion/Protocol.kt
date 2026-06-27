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
 * BLE beacon (card -> phone), Manufacturer Specific Data:
 *   company 0xFFFF, then  ver(1) addr(4 LE) vbat(2 LE, mV) temp(1) flags(1)
 *   temp  = (T_celsius + 40) * 2          -> T = raw/2 - 40
 *   flags = bit0 charging, bit1 valid
 *
 * The card's RX filter accepts an advert iff it contains 16-bit UUID 0x1234
 * AND manufacturer data company 0xFFFF with version byte 0x01 — so the phone
 * emits the very same layout to be recognised.
 */
object Protocol {

    const val SERVICE_UUID_16 = 0x1234

    /** 0x1234 expanded onto the Bluetooth SIG base UUID. */
    val SERVICE_UUID: ParcelUuid =
        ParcelUuid(UUID.fromString("00001234-0000-1000-8000-00805F9B34FB"))

    /** 0xFFFF — "no company / test" manufacturer ID. */
    const val COMPANY_ID = 0xFFFF
    const val VERSION: Byte = 0x01

    const val MFR_PAYLOAD_LEN = 9 // ver(1)+addr(4)+vbat(2)+temp(1)+flags(1)

    // ---- Beacon decode (card -> phone) -------------------------------------

    data class SensorBeacon(
        val nodeAddr: Long,
        val vbatMv: Int,
        val tempC: Double,
        val charging: Boolean,
        val valid: Boolean,
    )

    /**
     * Decode the manufacturer-specific payload (already stripped of the 2-byte
     * company id by [android.bluetooth.le.ScanRecord.getManufacturerSpecificData]).
     * Returns null if it is not one of our sensor beacons.
     */
    fun decodeManufacturerData(data: ByteArray?): SensorBeacon? {
        if (data == null || data.size < MFR_PAYLOAD_LEN) return null
        if (data[0] != VERSION) return null
        val addr = (data[1].u() ) or (data[2].u() shl 8) or
                (data[3].u() shl 16) or (data[4].u() shl 24)
        val vbat = data[5].u() or (data[6].u() shl 8)
        val tempRaw = data[7].toInt() and 0xFF
        val tempC = tempRaw / 2.0 - 40.0
        val flags = data[8].toInt() and 0xFF
        return SensorBeacon(
            nodeAddr = addr.toLong() and 0xFFFFFFFFL,
            vbatMv = vbat,
            tempC = tempC,
            charging = flags and 0x01 != 0,
            valid = flags and 0x02 != 0,
        )
    }

    // ---- Beacon encode (phone -> card) -------------------------------------

    /** Build the manufacturer payload (without the company id) for emission. */
    fun encodeManufacturerData(
        nodeAddr: Long,
        vbatMv: Int,
        tempC: Double,
        charging: Boolean,
        valid: Boolean,
    ): ByteArray {
        val tempRaw = ((tempC + 40.0) * 2.0).toInt().coerceIn(0, 255)
        val flags = (if (charging) 0x01 else 0) or (if (valid) 0x02 else 0)
        val a = nodeAddr.toInt()
        return byteArrayOf(
            VERSION,
            (a ushr 0).toByte(), (a ushr 8).toByte(),
            (a ushr 16).toByte(), (a ushr 24).toByte(),
            (vbatMv and 0xFF).toByte(), ((vbatMv ushr 8) and 0xFF).toByte(),
            tempRaw.toByte(),
            flags.toByte(),
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
