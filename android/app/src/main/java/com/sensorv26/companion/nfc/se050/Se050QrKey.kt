package com.sensorv26.companion.nfc.se050

/**
 * Parses the QR payload produced by the server-side key-derivation script
 * (see docs/ or the session that added this - a script under
 * server/renewal/ that derives K_ENC/K_MAC via scp03_kdf.py for a rotated
 * device and renders a QR locally with `qrencode`, never over the network).
 *
 * Format, deliberately text/key=value like Protocol.kt's NFC commissioning
 * string, for the same reason: human-readable, greppable, easy to generate
 * from a one-line shell script:
 *
 *   se050scp03;enc=<32 hex>;mac=<32 hex>[;uid=<36 hex>]
 *
 * `uid` is optional but recommended: when present, Se050Reader cross-checks
 * it against the UID actually read off the tapped chip before trusting the
 * key, so a stale/wrong QR fails loudly instead of producing a confusing
 * SCP03 authentication failure.
 */
object Se050QrKey {

    data class Parsed(val keySet: Se050KeySet, val expectedUidHex: String?)

    fun parse(text: String): Parsed? {
        // "se050scp03" itself is a bare tag, not a key=value pair - skip any
        // segment without '=' rather than rejecting the whole payload on it.
        val parts = text.trim().split(';').mapNotNull { field ->
            val i = field.indexOf('=')
            if (i < 0) null else field.substring(0, i).trim() to field.substring(i + 1).trim()
        }.toMap()
        if (parts.isEmpty()) return null
        val enc = parts["enc"]?.takeIf { it.length == 32 } ?: return null
        val mac = parts["mac"]?.takeIf { it.length == 32 } ?: return null
        val uid = parts["uid"]?.takeIf { it.length == 36 }

        return try {
            Parsed(
                keySet = Se050KeySet(name = "QR (device-derived)", enc = enc.hexToBytes(), mac = mac.hexToBytes()),
                expectedUidHex = uid?.uppercase(),
            )
        } catch (e: NumberFormatException) {
            null
        }
    }

    private fun String.hexToBytes(): ByteArray =
        ByteArray(length / 2) { i -> substring(i * 2, i * 2 + 2).toInt(16).toByte() }
}
