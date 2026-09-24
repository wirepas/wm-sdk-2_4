package com.sensorv26.companion.nfc.se050

import org.bouncycastle.crypto.engines.AESEngine
import org.bouncycastle.crypto.macs.CMac
import org.bouncycastle.crypto.params.KeyParameter
import javax.crypto.Cipher
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec

/**
 * AES-CBC (no padding) and AES-CMAC (RFC 4493), the two primitives the
 * GlobalPlatform SCP03 handshake is built from. AES-CBC uses the platform's
 * own javax.crypto.Cipher (available since API 1). AES-CMAC uses BouncyCastle
 * rather than javax.crypto.Mac("AESCMAC") - that algorithm name is only
 * guaranteed registered from API 28, and this app's minSdk is 26 - or a
 * hand-rolled implementation, which is exactly the kind of subtle-bug-prone
 * code this project has already been bitten by once this week (KCV/TLV
 * length bugs in src/se050.c's PUT KEY encoder).
 */
object Scp03Crypto {

    /** AES-128-CBC encrypt, no padding. [data] must be a multiple of 16 bytes. */
    fun aesCbcEncrypt(key: ByteArray, iv: ByteArray, data: ByteArray): ByteArray {
        val cipher = Cipher.getInstance("AES/CBC/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "AES"), IvParameterSpec(iv))
        return cipher.doFinal(data)
    }

    /** AES-128-CBC decrypt, no padding. [data] must be a multiple of 16 bytes. */
    fun aesCbcDecrypt(key: ByteArray, iv: ByteArray, data: ByteArray): ByteArray {
        val cipher = Cipher.getInstance("AES/CBC/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "AES"), IvParameterSpec(iv))
        return cipher.doFinal(data)
    }

    /** AES-128-CMAC (RFC 4493) over the concatenation of [parts]. Full 16-byte tag. */
    fun aesCmac(key: ByteArray, vararg parts: ByteArray): ByteArray {
        val mac = CMac(AESEngine.newInstance(), 128)
        mac.init(KeyParameter(key))
        for (part in parts) {
            mac.update(part, 0, part.size)
        }
        val out = ByteArray(mac.macSize)
        mac.doFinal(out, 0)
        return out
    }

    /** ISO 7816-4 padding: 0x80 then zeros up to the next 16-byte boundary. */
    fun iso7816Pad(data: ByteArray): ByteArray {
        val padded = data.size + 1
        val total = padded + ((16 - padded % 16) % 16)
        val out = ByteArray(total)
        System.arraycopy(data, 0, out, 0, data.size)
        out[data.size] = 0x80.toByte()
        return out
    }

    /** Strip ISO 7816-4 padding (trailing zeros then one 0x80 byte). */
    fun iso7816Unpad(data: ByteArray): ByteArray {
        var i = data.size - 1
        while (i >= 0 && data[i] == 0x00.toByte()) i--
        require(i >= 0 && data[i] == 0x80.toByte()) { "bad ISO 7816-4 padding" }
        return data.copyOfRange(0, i)
    }
}
