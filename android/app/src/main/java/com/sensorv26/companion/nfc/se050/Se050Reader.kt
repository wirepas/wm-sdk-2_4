package com.sensorv26.companion.nfc.se050

import android.nfc.Tag
import android.nfc.tech.IsoDep

/** Outcome of reading a factory-fresh (never PUT-KEY-rotated) SE050's identity over NFC. */
sealed class Se050ReadResult {
    data class Success(
        val keySetUsed: String,
        val uidHex: String,
        /** Raw uncompressed EC point 04||X||Y (65 bytes) - NOT SubjectPublicKeyInfo DER,
         *  despite the gateway firmware's C function being named accordingly. See
         *  src/se050.c:se050_get_device_pubkey()'s own comment and
         *  docs/se050-pki-architecture.md. */
        val pubkeyRawHex: String,
        val certDer: ByteArray,
    ) : Se050ReadResult()

    data class Error(val message: String) : Se050ReadResult()
}

/**
 * Reads UID / identity pubkey / device certificate from an SE050C over NFC
 * IsoDep. Tries, in order: an operator-supplied key (scanned from a QR code
 * generated server-side for a specific already-rotated device - see
 * Se050QrKey - never hardcoded, never persisted), then the two PUBLIC
 * factory-default key sets (a200/a201, for never-rotated chips).
 */
object Se050Reader {

    private val APPLET_AID = byteArrayOf(
        0xA0.toByte(), 0x00, 0x00, 0x03, 0x96.toByte(), 0x54, 0x53, 0x00,
        0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00,
    )

    private const val OBJ_UID = 0x7FFF0206
    private const val OBJ_DEVICE_KEY = 0xBE000001.toInt()
    private const val OBJ_DEVICE_CERT = 0xBE000002.toInt()
    private const val UID_LEN = 18
    private const val CERT_CHUNK = 128
    private const val CERT_MAX = 1024

    fun read(tag: Tag, manualKey: Se050QrKey.Parsed? = null): Se050ReadResult {
        val isoDep = IsoDep.get(tag)
            ?: return Se050ReadResult.Error("Pas un tag ISO-DEP (SE050 attendu)")

        return try {
            isoDep.connect()
            isoDep.timeout = 5000

            val selectApdu = byteArrayOf(0x00, 0xA4.toByte(), 0x04, 0x00, APPLET_AID.size.toByte()) +
                    APPLET_AID + byteArrayOf(0x00)
            val selectResp = isoDep.transceive(selectApdu)
            val selectSw = swOf(selectResp)
            if (selectSw != 0x9000) {
                return Se050ReadResult.Error("SELECT applet a échoué, SW=${hexSw(selectSw)}")
            }

            val candidates = listOfNotNull(manualKey?.keySet) + Se050Keys.ALL
            var session: Se050Scp03Session? = null
            var usedKeySet: String? = null
            for (keys in candidates) {
                session = Se050Scp03Session.open(isoDep, keys)
                if (session != null) {
                    usedKeySet = keys.name
                    break
                }
            }
            if (session == null || usedKeySet == null) {
                val triedManual = if (manualKey != null) "la clé QR, " else ""
                return Se050ReadResult.Error(
                    "SCP03 a échoué avec ${triedManual}a200 et a201 - puce déjà rotée sur une " +
                            "clé différente de celle fournie, ou puce défectueuse"
                )
            }

            val uid = readObject(isoDep, session, OBJ_UID, offset = 0, length = UID_LEN)
            val uidHex = uid.toHex()

            val expectedUid = manualKey?.expectedUidHex
            if (expectedUid != null && !expectedUid.equals(uidHex, ignoreCase = true)) {
                return Se050ReadResult.Error(
                    "UID lu ($uidHex) différent de l'UID attendu par le QR ($expectedUid) - " +
                            "mauvais QR pour cette puce, clé non utilisable en confiance"
                )
            }

            val pubkey = readObject(isoDep, session, OBJ_DEVICE_KEY, offset = 0, length = 0)
            val cert = readCert(isoDep, session)

            Se050ReadResult.Success(
                keySetUsed = usedKeySet,
                uidHex = uidHex,
                pubkeyRawHex = pubkey.toHex(),
                certDer = cert,
            )
        } catch (e: Exception) {
            Se050ReadResult.Error(e.message ?: "Erreur NFC/SE050 inconnue")
        } finally {
            try { isoDep.close() } catch (_: Exception) {}
        }
    }

    /** One ReadObject round-trip; returns the object bytes (tag 0x41 payload) already unwrapped. */
    private fun readObject(
        isoDep: IsoDep, session: Se050Scp03Session, objectId: Int, offset: Int, length: Int,
    ): ByteArray {
        val data = ArrayList<Byte>(16)
        data.addTlv(0x41, u32be(objectId))
        if (offset != 0) data.addTlv(0x42, u16be(offset))
        if (length != 0) data.addTlv(0x43, u16be(length))

        val plainResp = session.transceiveWrapped(
            isoDep, cla = 0x80, ins = 0x02, p1 = 0x00, p2 = 0x00, data = data.toByteArray(),
        )
        return parseTag41(plainResp)
    }

    /**
     * P2_SIZE (0x07) variant of READ - same CLA/INS as readObject, just a
     * different P2 and a TAG_1-only payload. Returns the object's real
     * stored size. A ReadObject whose offset+length overruns that size is
     * REJECTED outright (SW 6985), not short-read, so this must be known
     * up front rather than discovered by over-reading fixed-size chunks -
     * see se050.c's se050_read_device_cert(), fixed for the same reason.
     */
    private fun readSize(isoDep: IsoDep, session: Se050Scp03Session, objectId: Int): Int {
        val data = ArrayList<Byte>(4)
        data.addTlv(0x41, u32be(objectId))
        val plainResp = session.transceiveWrapped(
            isoDep, cla = 0x80, ins = 0x02, p1 = 0x00, p2 = 0x07, data = data.toByteArray(),
        )
        val sizeBytes = parseTag41(plainResp)
        require(sizeBytes.size == 2) { "réponse ReadSize inattendue (${sizeBytes.size} octets)" }
        return ((sizeBytes[0].toInt() and 0xFF) shl 8) or (sizeBytes[1].toInt() and 0xFF)
    }

    /**
     * Reads the certificate in chunks, each sized to never cross the
     * object's real size (from readSize) - the leading SEQUENCE TLV then
     * gives the exact DER length within that.
     */
    private fun readCert(isoDep: IsoDep, session: Se050Scp03Session): ByteArray {
        val objSize = readSize(isoDep, session, OBJ_DEVICE_CERT)
        require(objSize > 0) { "certificat: objet absent (taille 0)" }
        val cap = minOf(objSize, CERT_MAX)

        val buf = ArrayList<Byte>(cap)
        var offset = 0
        while (offset < cap) {
            val chunk = readObject(
                isoDep, session, OBJ_DEVICE_CERT, offset = offset,
                length = minOf(CERT_CHUNK, cap - offset),
            )
            if (chunk.isEmpty()) break
            buf.addAll(chunk.toList())
            offset += chunk.size
        }
        val raw = buf.toByteArray()
        require(raw.size >= 4 && raw[0] == 0x30.toByte()) { "certificat: pas une SEQUENCE DER en tête" }
        val (derLen, headerLen) = berLength(raw, 1)
        val total = 1 + headerLen + derLen // +1 for the SEQUENCE tag byte itself
        require(total <= raw.size) { "certificat: longueur DER ($total) > données lues (${raw.size})" }
        return raw.copyOfRange(0, total)
    }

    // ---- TLV / BER helpers --------------------------------------------------

    private fun ArrayList<Byte>.addTlv(tag: Int, value: ByteArray) {
        add(tag.toByte())
        add(value.size.toByte())
        addAll(value.toList())
    }

    private fun u32be(v: Int) = byteArrayOf(
        (v ushr 24).toByte(), (v ushr 16).toByte(), (v ushr 8).toByte(), v.toByte(),
    )

    private fun u16be(v: Int) = byteArrayOf((v ushr 8).toByte(), v.toByte())

    /** Parses `41 <BER-length> <payload>` and returns the payload. */
    private fun parseTag41(data: ByteArray): ByteArray {
        require(data.isNotEmpty() && data[0] == 0x41.toByte()) { "réponse SE050: tag 0x41 attendu" }
        val (len, headerLen) = berLength(data, 1)
        val start = 1 + headerLen
        require(start + len <= data.size) { "réponse SE050: TLV tronqué" }
        return data.copyOfRange(start, start + len)
    }

    /** BER length starting at [offset] (the byte AFTER the tag). Returns (length, bytesConsumedForLength). */
    private fun berLength(data: ByteArray, offset: Int): Pair<Int, Int> {
        val first = data[offset].toInt() and 0xFF
        return when {
            first <= 0x7F -> first to 1
            first == 0x81 -> (data[offset + 1].toInt() and 0xFF) to 2
            first == 0x82 -> (((data[offset + 1].toInt() and 0xFF) shl 8) or (data[offset + 2].toInt() and 0xFF)) to 3
            else -> error("longueur BER non supportée (0x${"%02X".format(first)})")
        }
    }

    private fun swOf(resp: ByteArray): Int {
        require(resp.size >= 2) { "réponse trop courte" }
        return ((resp[resp.size - 2].toInt() and 0xFF) shl 8) or (resp[resp.size - 1].toInt() and 0xFF)
    }

    private fun hexSw(sw: Int) = "%04X".format(sw)

    private fun ByteArray.toHex(): String = joinToString("") { "%02X".format(it) }
}
