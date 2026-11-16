@file:Suppress("unused", "UNUSED_PARAMETER")

package android.security.keystore

import java.security.spec.AlgorithmParameterSpec

class KeyGenParameterSpec private constructor(val keystoreAlias: String, val purposes: Int) : AlgorithmParameterSpec {
    class Builder(private val alias: String, private val purposes: Int) {
        fun setBlockModes(vararg m: String) = this
        fun setEncryptionPaddings(vararg p: String) = this
        fun setKeySize(bits: Int) = this
        fun setUserAuthenticationRequired(v: Boolean) = this
        fun setRandomizedEncryptionRequired(v: Boolean) = this
        fun build() = KeyGenParameterSpec(alias, purposes)
    }
}

object KeyProperties {
    const val KEY_ALGORITHM_AES = "AES"
    const val PURPOSE_ENCRYPT = 1
    const val PURPOSE_DECRYPT = 2
    const val BLOCK_MODE_GCM = "GCM"
    const val BLOCK_MODE_CBC = "CBC"
    const val ENCRYPTION_PADDING_NONE = "NoPadding"
    const val ENCRYPTION_PADDING_PKCS7 = "PKCS7Padding"
}
