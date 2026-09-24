package com.sensorv26.companion.nfc.se050

import android.nfc.tech.IsoDep
import java.security.SecureRandom

/**
 * GlobalPlatform SCP03 secure channel over an already-SELECTed SE05x applet,
 * ported byte-for-byte from the gateway firmware's validated C
 * implementation (ext/nxp-plug-and-trust-nano/lib/apdu/scp03/se05x_scp03.c
 * and se05x_auth_utils.c) - not re-derived from the GlobalPlatform spec
 * directly, so it matches what this project's real hardware has already
 * proven to work with, KCV-bug-class mistakes included as lessons already
 * paid for once.
 *
 * Security level is unconditionally SECLVL_CDEC_RENC_CMAC_RMAC (0x33, full:
 * command decryption + command MAC + response encryption + response MAC) -
 * the vendored SDK never negotiates anything weaker, so neither does this.
 */
class Se050Scp03Session private constructor(
    private val sEnc: ByteArray,
    private val sMac: ByteArray,
    private val sRmac: ByteArray,
    private var mcv: ByteArray,
) {
    private var counter: Long = 1L

    companion object {
        private const val INS_INITIALIZE_UPDATE = 0x50
        private const val INS_EXTERNAL_AUTHENTICATE = 0x82
        private const val SECLVL_FULL = 0x33
        private val ZERO16 = ByteArray(16)

        /**
         * Attempts the full SCP03 mutual-authentication handshake with
         * [keys]. Returns null (not an exception) on a card-cryptogram
         * mismatch, since a wrong key set is an expected, non-fatal outcome
         * the caller tries the next key set for - only real transport/APDU
         * failures throw.
         */
        fun open(isoDep: IsoDep, keys: Se050KeySet): Se050Scp03Session? {
            val hostChallenge = ByteArray(8).also { SecureRandom().nextBytes(it) }

            val initUpdateApdu = byteArrayOf(
                0x80.toByte(), INS_INITIALIZE_UPDATE.toByte(), 0x0B, 0x00,
                0x08, *hostChallenge, 0x00,
            )
            val resp = isoDep.transceive(initUpdateApdu)
            check(resp.size >= 31) { "INITIALIZE UPDATE: short response (${resp.size} bytes)" }
            val sw = ((resp[resp.size - 2].toInt() and 0xFF) shl 8) or (resp[resp.size - 1].toInt() and 0xFF)
            check(sw == 0x9000) { "INITIALIZE UPDATE failed, SW=${"%04X".format(sw)}" }

            // keyDivData(10) keyInfo(3) cardChallenge(8) cardCryptogram(8) [seqCounter(3)] SW(2)
            val cardChallenge = resp.copyOfRange(13, 21)
            val cardCryptogram = resp.copyOfRange(21, 29)

            val sEnc = deriveKey(keys.enc, 0x04, 0x0080, hostChallenge, cardChallenge)
            val sMac = deriveKey(keys.mac, 0x06, 0x0080, hostChallenge, cardChallenge)
            val sRmac = deriveKey(keys.mac, 0x07, 0x0080, hostChallenge, cardChallenge)

            val expectedCardCryptogram = deriveKey(sMac, 0x00, 0x0040, hostChallenge, cardChallenge)
            if (!expectedCardCryptogram.contentEquals(cardCryptogram)) {
                return null // wrong key set - not this device's current SCP03 keys
            }

            val hostCryptogram = deriveKey(sMac, 0x01, 0x0040, hostChallenge, cardChallenge)

            val extAuthHeader = byteArrayOf(0x84.toByte(), INS_EXTERNAL_AUTHENTICATE.toByte(), SECLVL_FULL.toByte(), 0x00, 0x10)
            val fullMac = Scp03Crypto.aesCmac(sMac, ZERO16, extAuthHeader, hostCryptogram)
            val extAuthApdu = extAuthHeader + hostCryptogram + fullMac.copyOfRange(0, 8)

            val authResp = isoDep.transceive(extAuthApdu)
            check(authResp.size >= 2) { "EXTERNAL AUTHENTICATE: short response" }
            val authSw = ((authResp[authResp.size - 2].toInt() and 0xFF) shl 8) or
                    (authResp[authResp.size - 1].toInt() and 0xFF)
            check(authSw == 0x9000) { "EXTERNAL AUTHENTICATE failed, SW=${"%04X".format(authSw)}" }

            return Se050Scp03Session(sEnc, sMac, sRmac, mcv = fullMac)
        }

        /**
         * SP 800-108 counter-mode KDF, GP Amendment D §4.1.5 - 32-byte
         * derivation block: 11 zero bytes, label, separator(0x00),
         * L big-endian(2), counter(0x01), hostChallenge(8), cardChallenge(8).
         * Output = AES-CMAC(key, block), truncated to L/8 bytes.
         */
        private fun deriveKey(
            key: ByteArray, label: Int, lBits: Int,
            hostChallenge: ByteArray, cardChallenge: ByteArray,
        ): ByteArray {
            val block = ByteArray(32)
            block[11] = label.toByte()
            block[12] = 0x00
            block[13] = ((lBits shr 8) and 0xFF).toByte()
            block[14] = (lBits and 0xFF).toByte()
            block[15] = 0x01
            System.arraycopy(hostChallenge, 0, block, 16, 8)
            System.arraycopy(cardChallenge, 0, block, 24, 8)
            val full = Scp03Crypto.aesCmac(key, block)
            return full.copyOfRange(0, lBits / 8)
        }
    }

    /** Big-endian 16-byte counter block, as the wire protocol expects it. */
    private fun counterBlock(): ByteArray {
        val block = ByteArray(16)
        var c = counter
        for (i in 15 downTo 0) {
            block[i] = (c and 0xFF).toByte()
            c = c ushr 8
        }
        return block
    }

    /**
     * Wraps [data] (a plain command APDU header + data field, CLA
     * unsecured e.g. 0x80) under this session, sends it, unwraps and
     * verifies the response, and returns the plain response data field
     * (SW and MAC already stripped/verified). Throws on any MAC/SW/
     * transport failure - a caller never receives unauthenticated bytes.
     */
    fun transceiveWrapped(isoDep: IsoDep, cla: Int, ins: Int, p1: Int, p2: Int, data: ByteArray): ByteArray {
        fun hex(b: ByteArray) = b.joinToString("") { "%02X".format(it) }

        val cmdCounter = counterBlock()
        val mcvBefore = mcv.copyOf()

        val padded = Scp03Crypto.iso7816Pad(data)
        val icv = Scp03Crypto.aesCbcEncrypt(sEnc, ZERO16, cmdCounter)
        val encData = Scp03Crypto.aesCbcEncrypt(sEnc, icv, padded)

        val lc = encData.size + 8
        // Se05x_API_ReadObject always calls DoAPDUTxRx with hasle=1 (extended
        // length), which forces the 3-byte Lc encoding (00 LcHi LcLo) + a
        // trailing 2-byte Le=0000 REGARDLESS of whether lc would fit in one
        // byte - se05x_scp03.c's se05xCmdLCW picks the short 1-byte form only
        // when length_extended is false, which it never is here. Using the
        // short form (as an earlier version of this function did) desyncs
        // the wire framing from what the applet expects and made the card
        // reject the very first wrapped command with a bare 2-byte SW.
        val header = byteArrayOf(
            (cla or 0x04).toByte(), ins.toByte(), p1.toByte(), p2.toByte(),
            0x00, ((lc shr 8) and 0xFF).toByte(), (lc and 0xFF).toByte(),
        )

        val toMac = header + encData
        val fullMac = Scp03Crypto.aesCmac(sMac, mcvBefore, toMac)
        mcv = fullMac
        val apdu = toMac + fullMac.copyOfRange(0, 8) + byteArrayOf(0x00, 0x00)

        val resp = isoDep.transceive(apdu)
        check(resp.size >= 10) {
            "resp too short (${resp.size}B): ${hex(resp)} " +
                    "| counter=${hex(cmdCounter)} sEnc=${hex(sEnc)} sMac=${hex(sMac)} mcvBefore=${hex(mcvBefore)} " +
                    "| plain=${hex(data)} padded=${hex(padded)} icv=${hex(icv)} enc=${hex(encData)} " +
                    "| header=${hex(header)} mac16=${hex(fullMac)} sent=${hex(apdu)}"
        }

        val rmacExpected = Scp03Crypto.aesCmac(
            sRmac, mcv,
            resp.copyOfRange(0, resp.size - 10),
            resp.copyOfRange(resp.size - 2, resp.size),
        ).copyOfRange(0, 8)
        val rmacActual = resp.copyOfRange(resp.size - 10, resp.size - 2)
        check(rmacExpected.contentEquals(rmacActual)) { "response R-MAC did not verify" }

        val sw = ((resp[resp.size - 2].toInt() and 0xFF) shl 8) or (resp[resp.size - 1].toInt() and 0xFF)

        val plainData = if (resp.size > 10) {
            val respIcvCounter = cmdCounter.copyOf().also { it[0] = 0x80.toByte() }
            val respIcv = Scp03Crypto.aesCbcEncrypt(sEnc, ZERO16, respIcvCounter)
            val encRespData = resp.copyOfRange(0, resp.size - 10)
            Scp03Crypto.iso7816Unpad(Scp03Crypto.aesCbcDecrypt(sEnc, respIcv, encRespData))
        } else {
            ByteArray(0)
        }

        counter += 1
        check(sw == 0x9000) { "SW=${"%04X".format(sw)}" }
        return plainData
    }
}
