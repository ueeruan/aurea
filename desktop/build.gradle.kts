import org.jetbrains.compose.desktop.application.dsl.TargetFormat
import java.util.Properties
import java.util.TreeSet

// =============================================================================
//  Aurea PC — módulo único (JVM).
//
//  FONTES: o código Kotlin do app Android entra INTEIRO e sem cópia
//  (`../android/app/src/main/java`). Só ficam de fora os arquivos presos a SDKs
//  que não existem no PC (anúncios, Play Billing, login do Credential Manager,
//  ARCore, ML Kit, serviço de primeiro plano). No lugar deles, e no lugar das
//  classes `android.*` que a UI usa, entra `src/main/kotlin` (a camada de
//  compatibilidade, ver desktop/README.md).
//
//  RECURSOS: o `R` é gerado aqui a partir de `res/values*/` do Android, e os XML
//  vão como estão para o classpath: os mesmos textos, nos mesmos idiomas.
// =============================================================================

plugins {
    kotlin("jvm") version "2.4.0"
    id("org.jetbrains.kotlin.plugin.compose") version "2.4.0"
    id("org.jetbrains.compose") version "1.12.2"
}

val androidApp = file("../android/app/src/main")
val androidRes = androidApp.resolve("res")
val generatedR = layout.buildDirectory.dir("generated/aureaR")
val generatedRes = layout.buildDirectory.dir("generated/aureaRes")

/** Arquivos do app Android que não compilam no PC: substituídos em src/main/kotlin. */
val androidOnly = listOf(
    "com/aurea/aurea/MainActivity.kt",
    "com/aurea/aurea/AureaApplication.kt",
    "com/aurea/aurea/ads/GoogleAdsBackend.kt",
    "com/aurea/aurea/ads/LevelPlayAdsBackend.kt",
    "com/aurea/aurea/ads/UmpConsent.kt",
    "com/aurea/aurea/conta/GoogleLogin.kt",
    "com/aurea/aurea/editor/RealCameraArTracker.kt",
    "com/aurea/aurea/editor/RealCameraMotion.kt",
    "com/aurea/aurea/mocap/MocapDetector.kt",
    "com/aurea/aurea/state/ExportForegroundService.kt",
) + (findProperty("aureaExtraExcludes") as String? ?: "").split(',').map { it.trim() }.filter { it.isNotEmpty() } +
    file("android-excludes.txt").takeIf { it.exists() }?.readLines()
        ?.map { it.substringBefore('#').trim() }?.filter { it.isNotEmpty() }.orEmpty()

kotlin {
    jvmToolchain(21)
    sourceSets.main {
        kotlin.srcDir("src/main/kotlin")
        kotlin.srcDir(androidApp.resolve("java"))
        kotlin.srcDir(generatedR)
        kotlin.exclude(androidOnly)
        resources.srcDir(generatedRes)
    }
    compilerOptions {
        freeCompilerArgs.addAll(
            "-Xexpect-actual-classes",
            "-opt-in=androidx.compose.ui.ExperimentalComposeUiApi",
        )
        // O código do Android tem avisos aos milhares; erros são o que importa aqui.
        suppressWarnings.set(true)
    }
}

dependencies {
    implementation(compose.desktop.currentOs)
    implementation("org.jetbrains.compose.material3:material3:1.9.0")
    implementation("org.jetbrains.compose.material:material-icons-extended:1.7.3")
    implementation("org.jetbrains.androidx.lifecycle:lifecycle-viewmodel-compose:2.11.0")
    implementation("org.jetbrains.androidx.lifecycle:lifecycle-runtime-compose:2.11.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-swing:1.10.2")
    implementation("androidx.annotation:annotation:1.9.1")
    implementation("org.json:json:20250517")
}

// -----------------------------------------------------------------------------
//  R.kt e os textos
// -----------------------------------------------------------------------------

/**
 * Gera `com.aurea.aurea.R` com os nomes de `res/values/` (string, plurals,
 * color), `res/drawable*`, `res/font` e `res/mipmap*`. Os números são só chaves:
 * em execução o texto vem do XML do idioma ativo (AndroidResources.kt).
 */
abstract class GenerateAureaR : DefaultTask() {
    @get:InputDirectory abstract val resDir: DirectoryProperty
    /** Os textos só do PC (desktop/res/values*). */
    @get:InputDirectory abstract val pcResDir: DirectoryProperty
    @get:OutputDirectory abstract val outDir: DirectoryProperty
    @get:OutputFile abstract val namesFile: RegularFileProperty

    @TaskAction
    fun run() {
        val res = resDir.get().asFile
        val kinds = sortedMapOf<String, TreeSet<String>>()
        fun add(kind: String, name: String) { kinds.getOrPut(kind) { TreeSet() }.add(name) }
        val tag = Regex("""<(string|plurals|color|string-array|integer|bool|dimen)\s+name="([A-Za-z0-9_.]+)"""")
        (res.resolve("values").listFiles { f -> f.extension == "xml" }!! +
            pcResDir.get().asFile.resolve("values").listFiles { f -> f.extension == "xml" }.orEmpty()).forEach { f ->
            tag.findAll(f.readText()).forEach { m ->
                val kind = when (m.groupValues[1]) { "string-array" -> "array"; else -> m.groupValues[1] }
                add(kind, m.groupValues[2].replace('.', '_'))
            }
        }
        res.listFiles { f -> f.isDirectory }!!.forEach { dir ->
            val kind = dir.name.substringBefore('-')
            if (kind in setOf("drawable", "font", "mipmap", "raw", "xml")) {
                dir.listFiles()!!.forEach { add(kind, it.nameWithoutExtension.substringBefore('.')) }
            }
        }
        val sb = StringBuilder("// Gerado por desktop/build.gradle.kts (GenerateAureaR). Não edite.\n")
        sb.append("package com.aurea.aurea\n\n@Suppress(\"ClassName\", \"unused\")\nobject R {\n")
        var id = 0x7f000000
        for ((kind, names) in kinds) {
            sb.append("    object $kind {\n")
            for (n in names) sb.append("        const val $n: Int = ${id++}\n")
            sb.append("    }\n")
        }
        // A tabela id → nome vai num arquivo (8 mil entradas não cabem num método da JVM).
        sb.append("}\n\n/** Nome de cada id, para achar o texto no XML (aurea-rnames.txt, gerado junto). */\ninternal object RNames {\n")
        sb.append("    val byId: Map<Int, Pair<String, String>> by lazy {\n")
        sb.append("        val m = HashMap<Int, Pair<String, String>>(${id - 0x7f000000} * 2)\n")
        sb.append("        RNames::class.java.getResourceAsStream(\"/aurea-rnames.txt\")!!.bufferedReader().forEachLine { l ->\n")
        sb.append("            val p = l.split(' '); if (p.size == 3) m[p[0].toInt()] = p[1] to p[2]\n")
        sb.append("        }\n        m\n    }\n}\n")
        val names = StringBuilder()
        id = 0x7f000000
        for ((kind, ns) in kinds) for (n in ns) names.append("${id++} $kind $n\n")
        namesFile.get().asFile.apply { parentFile.mkdirs() }.writeText(names.toString())
        val out = outDir.get().asFile.resolve("com/aurea/aurea")
        out.mkdirs()
        out.resolve("R.kt").writeText(sb.toString())
        // BuildConfig do PC: release (sem os atalhos de depuração do emulador).
        val fields = buildConfig.get()
        out.resolve("BuildConfig.kt").writeText(
            "// Gerado. Não edite.\npackage com.aurea.aurea\n\nobject BuildConfig {\n" +
                "    const val DEBUG: Boolean = false\n" +
                "    const val APPLICATION_ID: String = \"com.aurea.pc\"\n" +
                "    const val BUILD_TYPE: String = \"release\"\n" +
                "    const val PLAY_STORE: Boolean = false\n" +
                "    const val VERSION_CODE: Int = ${fields["VERSION_CODE"]}\n" +
                fields.filterKeys { it != "VERSION_CODE" }.entries.joinToString("") { (k, v) ->
                    "    const val $k: String = \"${v.replace("\\", "\\\\").replace("\"", "\\\"")}\"\n"
                } +
                "}\n",
        )
    }

    @get:Input abstract val buildConfig: MapProperty<String, String>
}

val androidVersion = file("../android/app/build.gradle.kts").readText().let { src ->
    val code = Regex("""versionCode\s*=\s*(\d+)""").find(src)?.groupValues?.get(1) ?: "0"
    val name = Regex("""versionName\s*=\s*"([^"]+)"""").find(src)?.groupValues?.get(1) ?: "0.0.0"
    code to name
}
/** Os mesmos campos públicos do BuildConfig do Android (android/firebase.properties). */
val firebaseProps = Properties().apply {
    file("../android/firebase.properties").takeIf { it.isFile }?.inputStream()?.use { load(it) }
}

val generateAureaR by tasks.registering(GenerateAureaR::class) {
    resDir.set(androidRes)
    pcResDir.set(file("res"))
    outDir.set(generatedR)
    namesFile.set(generatedRes.map { it.file("aurea-rnames.txt") })
    buildConfig.set(mapOf(
        "VERSION_CODE" to androidVersion.first,
        "VERSION_NAME" to androidVersion.second,
        "AUREA_DISCOVERY_REPO" to (findProperty("aureaDiscoveryRepo") as String? ?: "ueeruan/aurea"),
        "AUREA_DISCOVERY_BRANCH" to (findProperty("aureaDiscoveryBranch") as String? ?: "main"),
        "FIREBASE_PROJECT_ID" to firebaseProps.getProperty("projectId", "").trim(),
        "FIREBASE_APP_ID" to firebaseProps.getProperty("appId", "").trim(),
        "FIREBASE_API_KEY" to firebaseProps.getProperty("apiKey", "").trim(),
        "FIREBASE_WEB_CLIENT_ID" to firebaseProps.getProperty("webClientId", "").trim(),
    ))
}

/** Os XML de texto (todos os idiomas), a fonte de ícones e as imagens: no classpath em aurea-res/. */
val copyAureaRes by tasks.registering(Sync::class) {
    from(file("res")) { include("values*/*.xml") }
    from(androidRes) {
        include("values*/*.xml", "font/**", "drawable*/**", "mipmap*/**", "raw/**")
    }
    into(generatedRes.map { it.dir("aurea-res") })
    // O classpath de um jar não lista pastas: o índice diz que XML existem.
    doLast {
        val root = destinationDir
        val lines = root.walkTopDown().filter { it.isFile && it.name != "index.txt" }
            .map { it.relativeTo(root).invariantSeparatorsPath }.sorted().toList()
        root.resolve("index.txt").writeText(lines.joinToString("\n"))
    }
}

/**
 * Os `assets/` do APK (android/app/src/main/assets + engine/assets/fonts na raiz
 * + licenças) e as DLLs nativas (aurea.dll e as do FFmpeg), no
 * formato de `appResourcesRootDir`: common/ para todos, windows/ para o Windows.
 */
/** A aurea.dll (toolsuild_aurea_pc_jni.cmd) e as DLLs/executáveis do FFmpeg que vão junto. */
val nativeDir = file(findProperty("aureaNativeDir") as String? ?: "../engine/build/pc-jni/bin")
val ffmpegBin = file(findProperty("aureaFFmpegBin") as String? ?: "../build/astra-ffmpeg/host-dev/ffmpeg-8.1.2-full_build-shared/bin")
val appResources = layout.buildDirectory.dir("appres")
val stageAppResources by tasks.registering(Sync::class) {
    into(appResources)
    from(androidApp.resolve("assets")) { into("common/assets") }
    from(file("../engine/assets/fonts")) { into("common/assets") }
    from(file("../engine/assets/licenses")) { into("common/assets/licenses/oss") }
    // Motion Capture do PC (BlazePose, Apache-2.0): os avisos vão para "Licenças"; os
    // modelos seguem em engine-assets/mocap (o motor lê de lá, PcPaths.engineAssets).
    from(file("../engine/assets/mocap")) { include("LICENSE*", "NOTICE*"); into("common/assets/licenses/oss/mocap") }
    from(file("../engine/assets")) { into("common/engine-assets") }
    // ffmpeg.exe/ffprobe.exe (miniaturas, metadados, Hyper) também pedem avfilter e avdevice.
    from(nativeDir) { include("aurea.dll", "avcodec-*.dll", "avformat-*.dll", "avutil-*.dll", "swresample-*.dll", "swscale-*.dll", "avfilter-*.dll", "avdevice-*.dll"); into("windows") }
    from(ffmpegBin) { include("ffmpeg.exe", "ffprobe.exe"); into("windows") }
}

tasks.named("compileKotlin") { dependsOn(generateAureaR) }
tasks.named("processResources") { dependsOn(copyAureaRes, generateAureaR) }

// -----------------------------------------------------------------------------
//  O app
// -----------------------------------------------------------------------------

compose.desktop {
    application {
        mainClass = "com.aurea.pc.MainKt"
        jvmArgs += listOf(
            "-Xms512m", "-Xmx4g",
            "-XX:+UseZGC", "-XX:+ZGenerational",
            // GPU: Skia em Direct3D 12 na placa dedicada, com vsync.
            "-Dskiko.renderApi=DIRECT3D",
            "-Dskiko.vsync.enabled=true",
            "-Dskiko.gpu.priority=discrete",
            "-Dsun.java2d.d3d=true",
            "-Dfile.encoding=UTF-8",
            // A aurea.dll mora nos recursos do app instalado.
            "-Djava.library.path=\$APPDIR/resources",
        )
        nativeDistributions {
            targetFormats(TargetFormat.Msi, TargetFormat.Exe)
            packageName = "Aurea PC"
            packageVersion = androidVersion.second.substringBefore('-').let { v ->
                // MSI exige MAJOR > 0.
                val parts = v.split('.').mapNotNull { it.toIntOrNull() }
                if (parts.isNotEmpty() && parts[0] == 0) "1.${parts.getOrElse(1) { 0 }}.${parts.getOrElse(2) { 0 }}" else v
            }
            description = "Aurea — editor de vídeo e motion"
            vendor = "Aurea"
            appResourcesRootDir.set(appResources)
            modules("java.instrument", "java.management", "java.naming", "java.net.http", "java.prefs", "jdk.unsupported", "java.sql")
            windows {
                menu = true
                shortcut = true
                menuGroup = "Aurea"
                dirChooser = true
                perUserInstall = true
                upgradeUuid = "6f1d2b5e-6a3c-4b0e-9a62-1f0c2d7a4e11"
                iconFile.set(project.file("icon/aurea.ico"))
            }
        }
    }
}

// `run` e o instalador montam os recursos a partir da pasta preparada acima.
tasks.matching { it.name == "prepareAppResources" }.configureEach { dependsOn(stageAppResources) }
tasks.withType<JavaExec>().configureEach {
    dependsOn(stageAppResources)
    // O `$APPDIR` do instalador não existe no `run`: aponta para as DLLs preparadas.
    val libs = appResources.get().dir("windows").asFile.absolutePath
    doFirst { jvmArgs = jvmArgs.orEmpty().filterNot { it.startsWith("-Djava.library.path=") } + "-Djava.library.path=$libs" }
}
