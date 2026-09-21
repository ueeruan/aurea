import java.util.Properties

// =============================================================================
//  Módulo :app — o app Aurea.
//
//  A parte que mais importa aqui é a ASSINATURA. O Aurea oficial é assinado com
//  a chave de DEBUG da máquina do dono (auditado: o SHA-256 do certificado do
//  aurea-release.apk bate exatamente com a de ~/.android/debug.keystore). Para
//  que o APK novo ATUALIZE por cima do instalado, ele tem que sair com a MESMA
//  chave. `android/key.properties` aponta para ela; sem o arquivo, o build cai
//  no debug padrão do Gradle, que é a mesma chave.
//
//  Ver _identity/signing/IDENTIDADE.md para a auditoria completa.
// =============================================================================

plugins {
    // Só o plugin do Android. A partir do AGP 9 o suporte a Kotlin é EMBUTIDO —
    // aplicar `org.jetbrains.kotlin.android` além dele faz o build falhar com
    // "plugin no longer required". O compilador de Compose também vem junto:
    // basta `buildFeatures.compose = true`.
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

val keystoreProperties = Properties()
val keystorePropertiesFile = rootProject.file("key.properties")
val temChaveDeRelease = keystorePropertiesFile.exists()
if (temChaveDeRelease) {
    keystorePropertiesFile.inputStream().use { keystoreProperties.load(it) }
}

// Uma ABI por build. `--split-per-abi` produz um APK por arquitetura com um
// nome que não bate com o que a loja espera; aqui o filtro é explícito e o APK
// sai inteiro, com os assets certos.
val aureaAbi = providers.gradleProperty("aureaAbi").orNull
val aureasAbisPermitidas = setOf("armeabi-v7a", "arm64-v8a")
require(aureaAbi == null || aureaAbi in aureasAbisPermitidas) {
    "aureaAbi invalida: $aureaAbi (esperado: armeabi-v7a ou arm64-v8a)"
}

android {
    namespace = "com.aurea.aurea"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "com.aurea.aurea"

        // 26 (Android 8.0) é o piso: abaixo disso não há AHardwareBuffer nem
        // Vulkan de forma confiável, e o pipeline zero-copy do motor não roda.
        // O Aurea antigo aceitava mais versões porque não usava nenhuma das
        // duas — trocar de piso é consequência da arquitetura nova.
        minSdk = 26
        targetSdk = 36

        // versionCode 2102: o Aurea oficial instalado é 2101. Um número maior é
        // o que faz o Android aceitar a atualização por cima.
        versionCode = 2102
        versionName = "2.0.0-beta1"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        if (aureaAbi != null) {
            ndk { abiFilters += aureaAbi }
        } else {
            ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a") }
        }

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++23", "-fno-exceptions", "-fno-rtti")
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DCMAKE_BUILD_TYPE=RelWithDebInfo",
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            // O motor é COMPARTILHADO com o iOS. O caminho aponta para fora do
            // projeto Android de propósito: é a mesma árvore que o Xcode usa.
            path = file("../../engine/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    signingConfigs {
        if (temChaveDeRelease) {
            create("release") {
                keyAlias = keystoreProperties["keyAlias"] as String
                keyPassword = keystoreProperties["keyPassword"] as String
                storeFile = file(keystoreProperties["storeFile"] as String)
                storePassword = keystoreProperties["storePassword"] as String
                enableV1Signing = true
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false      // o motor é C++; ofuscar só a UI não ajuda
            isShrinkResources = false    // os assets de marca são referenciados por nome
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
            signingConfig = if (temChaveDeRelease) {
                signingConfigs.getByName("release")
            } else {
                signingConfigs.getByName("debug")
            }
        }
        debug {
            applicationIdSuffix = ".debug"
            versionNameSuffix = "-debug"
            isDebuggable = true
            // Sem isso, o depurador nativo não enxerga os símbolos do motor
            // quando o crash acontece dentro do C++.
            ndk { debugSymbolLevel = "FULL" }
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        compose = true
        prefab = false
    }

    packaging {
        jniLibs {
            // A .so do motor é carregada uma vez no processo e não pode ser
            // extraída para um diretório temporário: extrair duplica em disco e
            // o System.loadLibrary falha quando o /data está cheio.
            useLegacyPackaging = false
        }
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }

    androidResources {
        // A identidade da marca é referenciada por nome de recurso em vários
        // pontos; o encolhimento por nome de arquivo quebraria a splash.
        noCompress += listOf("aurea")
    }

    lint {
        abortOnError = false
        checkReleaseBuilds = false
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)

    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)

    implementation(libs.kotlinx.coroutines.android)

    debugImplementation(libs.androidx.compose.ui.tooling)
}
