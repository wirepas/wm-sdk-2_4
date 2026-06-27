package com.sensorv26.companion.nfc

import android.nfc.NdefMessage
import android.nfc.NdefRecord
import android.nfc.Tag
import android.nfc.tech.Ndef
import android.nfc.tech.NfcA
import java.nio.charset.Charset

/** Outcome of a read attempt, with enough context to diagnose failures. */
sealed class ReadResult {
    data class Success(val text: String) : ReadResult()
    data class NoText(val techs: List<String>) : ReadResult()
    data class Error(val msg: String, val techs: List<String>) : ReadResult()
}

/** NDEF read/write helpers for the card's Type-2 tag emulation. */
object NfcNdef {

    /** Read the first NDEF Text record, trying the Ndef tech then a raw T2 fallback. */
    fun read(tag: Tag): ReadResult {
        val techs = tag.techList.map { it.substringAfterLast('.') }

        // 1) Standard NDEF path.
        Ndef.get(tag)?.let { ndef ->
            try {
                ndef.connect()
                val msg = ndef.ndefMessage ?: ndef.cachedNdefMessage
                val text = msg?.records?.firstNotNullOfOrNull { decodeTextRecord(it) }
                if (text != null) return ReadResult.Success(text)
            } catch (e: Exception) {
                // fall through to the raw fallback
            } finally {
                try { ndef.close() } catch (_: Exception) {}
            }
        }

        // 2) Raw Type-2 fallback: read pages and parse the NDEF TLV ourselves.
        readType2Text(tag)?.let { return ReadResult.Success(it) }

        return ReadResult.NoText(techs)
    }

    /** Write [text] as a single NDEF Text record (lang "en"). Returns error or null. */
    fun writeText(tag: Tag, text: String): String? {
        val ndef = Ndef.get(tag) ?: return "Tag non NDEF (techs: ${tag.techList.joinToString()})"
        return try {
            ndef.connect()
            if (!ndef.isWritable) return "Tag en lecture seule"
            val msg = NdefMessage(arrayOf(createTextRecord("en", text)))
            if (msg.byteArrayLength > ndef.maxSize)
                return "Message trop grand (${msg.byteArrayLength} > ${ndef.maxSize})"
            ndef.writeNdefMessage(msg)
            null
        } catch (e: Exception) {
            e.message ?: "Erreur d'écriture NFC"
        } finally {
            try { ndef.close() } catch (_: Exception) {}
        }
    }

    // ---- raw Type-2 (NfcA) fallback ----------------------------------------

    private fun readType2Text(tag: Tag): String? {
        val nfca = NfcA.get(tag) ?: return null
        return try {
            nfca.connect()
            // User memory starts at page 4. Read in 16-byte bursts (4 pages).
            val mem = ArrayList<Byte>(256)
            var page = 4
            while (page < 4 + 60) {
                val resp = nfca.transceive(byteArrayOf(0x30, page.toByte())) // READ
                if (resp == null || resp.size < 16) break
                resp.forEach { mem.add(it) }
                if (mem.size >= 256) break
                page += 4
            }
            parseNdefTlvText(mem.toByteArray())
        } catch (e: Exception) {
            null
        } finally {
            try { nfca.close() } catch (_: Exception) {}
        }
    }

    /** Find the NDEF-message TLV (type 0x03) and decode its first Text record. */
    private fun parseNdefTlvText(mem: ByteArray): String? {
        var i = 0
        while (i < mem.size) {
            val type = mem[i].toInt() and 0xFF
            when (type) {
                0x00 -> { i++; continue }       // NULL TLV
                0xFE -> return null              // Terminator
                0x03 -> {                        // NDEF message TLV
                    if (i + 1 >= mem.size) return null
                    var len = mem[i + 1].toInt() and 0xFF
                    var valStart = i + 2
                    if (len == 0xFF) {           // 3-byte length
                        if (i + 3 >= mem.size) return null
                        len = ((mem[i + 2].toInt() and 0xFF) shl 8) or (mem[i + 3].toInt() and 0xFF)
                        valStart = i + 4
                    }
                    if (valStart + len > mem.size) return null
                    val ndefBytes = mem.copyOfRange(valStart, valStart + len)
                    return try {
                        NdefMessage(ndefBytes).records.firstNotNullOfOrNull { decodeTextRecord(it) }
                    } catch (e: Exception) { null }
                }
                else -> {                        // skip other TLVs (length-prefixed)
                    if (i + 1 >= mem.size) return null
                    val len = mem[i + 1].toInt() and 0xFF
                    i += 2 + len
                }
            }
        }
        return null
    }

    // ---- record codec ------------------------------------------------------

    private fun decodeTextRecord(record: NdefRecord): String? {
        if (record.tnf != NdefRecord.TNF_WELL_KNOWN) return null
        if (!record.type.contentEquals(NdefRecord.RTD_TEXT)) return null
        val payload = record.payload
        if (payload.isEmpty()) return null
        val status = payload[0].toInt()
        val langLen = status and 0x3F
        val charset = if (status and 0x80 == 0) Charsets.UTF_8 else Charset.forName("UTF-16")
        return String(payload, 1 + langLen, payload.size - 1 - langLen, charset)
    }

    private fun createTextRecord(language: String, text: String): NdefRecord {
        val lang = language.toByteArray(Charsets.US_ASCII)
        val txt = text.toByteArray(Charsets.UTF_8)
        val payload = ByteArray(1 + lang.size + txt.size)
        payload[0] = lang.size.toByte() // UTF-8, lang length
        System.arraycopy(lang, 0, payload, 1, lang.size)
        System.arraycopy(txt, 0, payload, 1 + lang.size, txt.size)
        return NdefRecord(NdefRecord.TNF_WELL_KNOWN, NdefRecord.RTD_TEXT, ByteArray(0), payload)
    }
}
