package com.sensorv26.companion.nfc.se050

/**
 * SE050C2 Platform SCP03 factory-default key sets, as PUBLISHED by NXP in
 * AN12436 Table 6. Not secret - readable in the clear over I2C or NFC by
 * anyone until the die is rotated via GP PUT KEY (see the gateway firmware,
 * src/se050_scp03_defaults.c, the source of truth these are copied from).
 *
 * AN12436 Rev 2.4 contradicts itself between Table 2 (variant->OEF mapping)
 * and Table 6 (actual key bytes), so both candidate sets are kept, tried in
 * this order (a200 first: empirically the correct one on this project's real
 * SE050C2 chips, docs/se050-pki-architecture.md P0b, 2026-09-15).
 *
 * DEK is intentionally omitted: this app only ever reads (UID/pubkey/cert),
 * never PUT KEY, so the encryption key for key rotation is never needed.
 */
data class Se050KeySet(val name: String, val enc: ByteArray, val mac: ByteArray)

object Se050Keys {

    private fun hex(s: String): ByteArray =
        ByteArray(s.length / 2) { i -> s.substring(i * 2, i * 2 + 2).toInt(16).toByte() }

    val A200 = Se050KeySet(
        name = "a200",
        enc = hex("BD1DE20A81EAB2BF3B709A9D69A31254"),
        mac = hex("9A761B8DBA6BEDF22741E45D8D4236F5"),
    )

    val A201 = Se050KeySet(
        name = "a201",
        enc = hex("852B5962E9CCE5D0BE746B833BCC6287"),
        mac = hex("DB0AA319A4D8696C8E107AB4E3C26B47"),
    )

    /** Tried in this order by [com.sensorv26.companion.nfc.se050.Se050Reader]. */
    val ALL = listOf(A200, A201)
}
