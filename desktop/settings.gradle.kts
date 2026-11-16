// =============================================================================
//  Aurea PC — o app nativo de Windows.
//
//  A UI é a MESMA do Android (android/app/src/main/java), compilada com Compose
//  Multiplatform para a JVM e desenhada pelo Skia em Direct3D 12. O motor C++
//  entra como aurea.dll (engine/platform/desktop), pela mesma ponte JNI.
//  Ver desktop/README.md.
// =============================================================================
pluginManagement {
    repositories {
        mavenCentral()
        gradlePluginPortal()
        google()
    }
}

dependencyResolutionManagement {
    repositories {
        mavenCentral()
        google()
    }
}

rootProject.name = "AureaPC"
