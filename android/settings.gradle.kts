// =============================================================================
//  Aurea Editor — projeto Android nativo (Kotlin + Compose).
//
//  Sem o plugin do Flutter. O motor C++ entra por `externalNativeBuild`, e a
//  UI não processa frame nenhum: ela desenha superfície nativa e envia
//  comandos.
// =============================================================================
pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.PREFER_SETTINGS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "Aurea"
include(":app")
