import java.util.Properties

plugins {
    id("com.android.application")
    // The Flutter Gradle Plugin must be applied after the Android and Kotlin Gradle plugins.
    id("dev.flutter.flutter-gradle-plugin")
}

// A CHAVE DE ASSINATURA, quando existe.
//
// "Por que tenho de apagar e reinstalar a versao antiga toda vez?" —
// porque o release era assinado com a chave de DEBUG, que e diferente em
// cada maquina: um APK vindo do CI e outro vindo do PC do dono tinham
// assinaturas diferentes, e o Android recusa atualizar um app por cima
// de outro com assinatura diferente. Com `android/key.properties`
// apontando para um keystore proprio, toda build sai com a MESMA chave e
// o telefone atualiza no lugar. Sem o arquivo, cai no debug de sempre
// para `flutter run` continuar funcionando.
val keystoreProperties = Properties()
val keystorePropertiesFile = rootProject.file("key.properties")
val temChaveDeRelease = keystorePropertiesFile.exists()
if (temChaveDeRelease) {
    keystorePropertiesFile.inputStream().use { keystoreProperties.load(it) }
}

android {
    namespace = "com.aurea.aurea"
    compileSdk = flutter.compileSdkVersion
    ndkVersion = flutter.ndkVersion

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    defaultConfig {
        // TODO: Specify your own unique Application ID (https://developer.android.com/studio/build/application-id.html).
        applicationId = "com.aurea.aurea"
        // You can update the following values to match your application needs.
        // For more information, see: https://flutter.dev/to/review-gradle-config.
        minSdk = flutter.minSdkVersion
        targetSdk = flutter.targetSdkVersion
        // Uses the version code from pubspec.yaml. When using split APKs, 1000 * ABI_VERSION
        // is added automatically by Flutter. (https://developer.android.com/studio/build/configure-apk-splits#configure-APK-versions)
        // You can force using the value of versionCode by specifying the `-P force-version-code-ignoring-abi=true`
        // flag during build.
        versionCode = flutter.versionCode
        versionName = flutter.versionName

        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++20"
                arguments += "-DANDROID_STL=c++_shared"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../native/CMakeLists.txt")
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
            }
        }
    }

    buildTypes {
        release {
            signingConfig = if (temChaveDeRelease) {
                signingConfigs.getByName("release")
            } else {
                signingConfigs.getByName("debug")
            }
        }
    }
}

kotlin {
    compilerOptions {
        jvmTarget = org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17
    }
}

flutter {
    source = "../.."
}

// ============================================================================
// O REGISTRANTE NAO PODE CITAR O `integration_test`.
// ============================================================================
//
// O `flutter pub get` gera `GeneratedPluginRegistrant.java` com TODOS os
// plugins, inclusive os de desenvolvimento — e o `integration_test` e um
// deles. No build de RELEASE o plugin nao entra no classpath (a Flutter
// Gradle plugin so inclui os de producao), entao a linha
//
//   new dev.flutter.plugins.integration_test.IntegrationTestPlugin()
//
// nao compila: `package dev.flutter.plugins.integration_test does not
// exist`. Era o unico erro do build de release.
//
// APAGAR O ARQUIVO NAO RESOLVE, e foi o que se tentou: sem ele o app fica
// SEM REGISTRAR PLUGIN NENHUM, o `SharedPreferences` estoura na abertura
// e a tela fica presa na logo. O arquivo e gerado, entao a correcao nao
// pode ser feita nele — ela e feita AQUI, antes de compilar, e vale para
// todo build daqui para a frente.
tasks.configureEach {
    if (name == "preBuild") {
        doFirst {
            val f = file(
                "src/main/java/io/flutter/plugins/GeneratedPluginRegistrant.java",
            )
            if (f.exists()) {
                val texto = f.readText()
                if (texto.contains("integration_test")) {
                    f.writeText(
                        texto.lines()
                            .filterNot { it.contains("integration_test") }
                            .joinToString("\n"),
                    )
                    logger.lifecycle(
                        "Aurea: integration_test fora do registrante " +
                            "(so existe em build de teste).",
                    )
                }
            }
        }
    }
}
