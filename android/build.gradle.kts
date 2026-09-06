allprojects {
    repositories {
        google()
        mavenCentral()
    }
}

val newBuildDir: Directory =
    rootProject.layout.buildDirectory
        .dir("../../build")
        .get()
rootProject.layout.buildDirectory.value(newBuildDir)

subprojects {
    val newSubprojectBuildDir: Directory = newBuildDir.dir(project.name)
    project.layout.buildDirectory.value(newSubprojectBuildDir)
}
subprojects {
    project.evaluationDependsOn(":app")
}

// Plugins que pinam outro NDK (ex.: whisper_flutter_new -> 27.x) passam a
// usar o NDK 28 ja instalado, evitando novo download de ~600 MB.
subprojects {
    fun applyNdkOverride(p: Project) {
        p.extensions.findByType(com.android.build.gradle.BaseExtension::class.java)
            ?.ndkVersion = "28.2.13676358"
    }
    if (state.executed) {
        applyNdkOverride(this)
    } else {
        afterEvaluate { applyNdkOverride(this) }
    }
}

tasks.register<Delete>("clean") {
    delete(rootProject.layout.buildDirectory)
}
