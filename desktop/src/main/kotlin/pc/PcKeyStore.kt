package com.aurea.pc

import java.io.File
import java.security.Key
import java.security.KeyStoreSpi
import java.security.Provider
import java.security.SecureRandom
import java.security.cert.Certificate
import java.util.Collections
import java.util.Date
import java.util.Enumeration
import javax.crypto.KeyGeneratorSpi
import javax.crypto.SecretKey
import javax.crypto.spec.SecretKeySpec

/**
 * O "AndroidKeyStore" do Aurea PC (o cofre de chaves de API das legendas usa).
 * As chaves AES ficam em no_backup/keystore, na pasta do usuário do Windows —
 * só ele lê. Não é um chip seguro como no celular.
 */
object PcKeyStore {
    private val dir = File(PcPaths.noBackup, "keystore").apply { mkdirs() }

    fun install() {
        if (java.security.Security.getProvider("AndroidKeyStore") != null) return
        java.security.Security.addProvider(object : Provider("AndroidKeyStore", "1.0", "Aurea PC") {
            init {
                put("KeyStore.AndroidKeyStore", Store::class.java.name)
                put("KeyGenerator.AES", Generator::class.java.name)
            }
        })
    }

    private fun file(alias: String) = File(dir, alias.replace(Regex("[^A-Za-z0-9_.-]"), "_") + ".key")

    class Store : KeyStoreSpi() {
        override fun engineGetKey(alias: String, password: CharArray?): Key? =
            file(alias).takeIf { it.isFile }?.let { SecretKeySpec(it.readBytes(), "AES") }
        /** Como no AndroidKeyStore: a chave sai sem senha (`getEntry(alias, null)`). */
        override fun engineGetEntry(alias: String, protParam: java.security.KeyStore.ProtectionParameter?): java.security.KeyStore.Entry? =
            (engineGetKey(alias, null) as? SecretKey)?.let { java.security.KeyStore.SecretKeyEntry(it) }
        override fun engineSetEntry(alias: String, entry: java.security.KeyStore.Entry, protParam: java.security.KeyStore.ProtectionParameter?) {
            (entry as? java.security.KeyStore.SecretKeyEntry)?.let { file(alias).writeBytes(it.secretKey.encoded) }
        }
        override fun engineEntryInstanceOf(alias: String, entryClass: Class<out java.security.KeyStore.Entry>): Boolean =
            entryClass == java.security.KeyStore.SecretKeyEntry::class.java && engineContainsAlias(alias)
        override fun engineGetCertificateChain(alias: String?): Array<Certificate>? = null
        override fun engineGetCertificate(alias: String?): Certificate? = null
        override fun engineGetCreationDate(alias: String): Date? = file(alias).takeIf { it.isFile }?.let { Date(it.lastModified()) }
        override fun engineSetKeyEntry(alias: String, key: Key, password: CharArray?, chain: Array<out Certificate>?) { file(alias).writeBytes(key.encoded) }
        override fun engineSetKeyEntry(alias: String?, key: ByteArray?, chain: Array<out Certificate>?) {}
        override fun engineSetCertificateEntry(alias: String?, cert: Certificate?) {}
        override fun engineDeleteEntry(alias: String) { file(alias).delete() }
        override fun engineAliases(): Enumeration<String> = Collections.enumeration(dir.list()?.map { it.removeSuffix(".key") }.orEmpty())
        override fun engineContainsAlias(alias: String) = file(alias).isFile
        override fun engineSize() = dir.list()?.size ?: 0
        override fun engineIsKeyEntry(alias: String) = engineContainsAlias(alias)
        override fun engineIsCertificateEntry(alias: String?) = false
        override fun engineGetCertificateAlias(cert: Certificate?): String? = null
        override fun engineStore(stream: java.io.OutputStream?, password: CharArray?) {}
        override fun engineLoad(stream: java.io.InputStream?, password: CharArray?) {}
    }

    class Generator : KeyGeneratorSpi() {
        private var alias: String? = null
        private var random = SecureRandom()
        override fun engineInit(random: SecureRandom?) { if (random != null) this.random = random }
        override fun engineInit(params: java.security.spec.AlgorithmParameterSpec?, random: SecureRandom?) {
            alias = (params as? android.security.keystore.KeyGenParameterSpec)?.keystoreAlias
            if (random != null) this.random = random
        }
        override fun engineInit(keysize: Int, random: SecureRandom?) = engineInit(random)
        override fun engineGenerateKey(): SecretKey {
            val bytes = ByteArray(32).also { random.nextBytes(it) }
            alias?.let { file(it).writeBytes(bytes) }
            return SecretKeySpec(bytes, "AES")
        }
    }
}
