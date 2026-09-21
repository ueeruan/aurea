// =============================================================================
//  Raiz do build Android.
// =============================================================================

plugins {
    // Ver o comentário em app/build.gradle.kts: com o AGP 9 o Kotlin é
    // embutido, e aplicar o plugin separado QUEBRA o build.
    alias(libs.plugins.android.application) apply false
    alias(libs.plugins.kotlin.compose) apply false
}

// O diretório de build fica FORA da árvore do projeto. Um `build/` dentro de
// `android/` apareceria para o git, para o backup e para qualquer busca no
// código — e são gigabytes de artefato que ninguém quer versionar por engano.
val aureaBuildDir: Directory = rootProject.layout.buildDirectory.dir("../../build/android").get()
rootProject.layout.buildDirectory.value(aureaBuildDir)

subprojects {
    layout.buildDirectory.value(aureaBuildDir.dir(project.name))
}

tasks.register<Delete>("clean") {
    delete(rootProject.layout.buildDirectory)
}
