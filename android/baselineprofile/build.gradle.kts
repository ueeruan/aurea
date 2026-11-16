// =============================================================================
//  Aurea — gerador do perfil de base (Baseline Profile) do app. 0.5.3.
//
//  ESTE MÓDULO AINDA NÃO ESTÁ LIGADO AO BUILD: `settings.gradle.kts` só inclui
//  `:app`, então nada aqui é configurado nem compilado. Foi preparado sem aparelho
//  e sem gerar APK; NUNCA foi compilado nem executado. Trate como rascunho.
//
//  Por que existe: o AAB de 11/11 leva um `baseline.prof` de 12,5 KB, só das
//  bibliotecas (Compose etc.). Os ~22 MB de dex do próprio app rodam
//  interpretados/JIT nas primeiras aberturas depois de instalar ou atualizar.
//
//  Para ligar (precisa de um emulador ou aparelho com API 28+; em API < 33 o
//  aparelho tem de ser rooteado ou userdebug):
//    1. settings.gradle.kts:  include(":baselineprofile")
//    2. app/build.gradle.kts: plugins { id("androidx.baselineprofile") version "<versão>" }
//                             dependencies {
//                                 implementation("androidx.profileinstaller:profileinstaller:<versão>")
//                                 "baselineProfile"(project(":baselineprofile"))
//                             }
//    3. Conferir as versões abaixo contra o AGP do projeto (libs.versions.toml,
//       agp = 9.1.0 em 12/11/2026). Não foram testadas com ele.
//    4. .\gradlew.bat :app:generateBaselineProfile -PaureaAbi=x86_64 (emulador) com
//       as flags nativas de sempre (aureaGlesCompiler, aureaFFmpegRoot).
//       O resultado vai para app/src/release/generated/baselineProfiles/ e entra
//       sozinho no próximo AAB.
//    5. Medir: `adb shell am start -W` com e sem o perfil compilado
//       (`adb shell cmd package compile -m speed-profile -f com.aurea.aurea`).
//
//  Limite conhecido: o build de release exige conta. Sem login o percurso só
//  cobre a abertura fria até a primeira tela, que é a parte que mais importa.
// =============================================================================
plugins {
    id("com.android.test")
    id("androidx.baselineprofile") version "1.4.1"
}

android {
    namespace = "com.aurea.aurea.baselineprofile"
    compileSdk = 36
    defaultConfig {
        minSdk = 28
        targetSdk = 36
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }
    targetProjectPath = ":app"
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

baselineProfile {
    useConnectedDevices = true
}

dependencies {
    implementation("androidx.test.ext:junit:1.2.1")
    implementation("androidx.test.uiautomator:uiautomator:2.3.0")
    implementation("androidx.benchmark:benchmark-macro-junit4:1.4.1")
}
