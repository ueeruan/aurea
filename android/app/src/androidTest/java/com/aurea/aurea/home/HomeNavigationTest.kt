package com.aurea.aurea.home

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import java.io.File

/**
 * Omega 0.3.2 (área 01): o dock Início · Perfil · [+] · Projetos · Presets, a
 * primeira abertura em Início (D5) e a última aba lembrada, a conta pelo
 * avatar, a gaveta de ajustes e a folha "Novo projeto".
 */
class HomeNavigationTest {
    @get:Rule val compose = createComposeRule()

    private fun freshHome() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        context.getSharedPreferences("aurea.releaseNotes", android.content.Context.MODE_PRIVATE).edit()
            .putString("read", "${com.aurea.aurea.BuildConfig.VERSION_NAME}:${com.aurea.aurea.BuildConfig.VERSION_CODE}").commit()
        // D5: sem aba gravada = primeira abertura.
        context.getSharedPreferences("aurea.home", android.content.Context.MODE_PRIVATE).edit()
            .remove(HomeViewModel.KEY_TAB).commit()
        AureaDonations.launchPromptPending = false
    }

    @Test fun appWithoutSessionOpensStartAndOptionalLoginCanBeClosed() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        freshHome()
        com.aurea.aurea.captions.KeyVault(context).apply {
            remove("conta_token")
            remove("conta_email")
        }
        compose.setContent {
            val store: EditorStore = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            com.aurea.aurea.ui.AureaApp(store)
        }
        compose.onNodeWithTag("home.community").assertIsSelected()
        compose.onNodeWithTag("conta.email").assertDoesNotExist()
        // Sem login, o avatar abre a tela de entrar (P0-4).
        compose.onNodeWithTag("home.account").performClick()
        compose.onNodeWithTag("conta.email").assertIsDisplayed()
        compose.onNodeWithTag("conta.fechar").performClick()
        compose.onNodeWithTag("conta.email").assertDoesNotExist()
        compose.onNodeWithTag("home.projects").performClick()
        compose.onNodeWithTag("home.projects").assertIsSelected()
        compose.onNodeWithTag("home.create").performClick()
        compose.onNodeWithTag("newProject.sheet").assertIsDisplayed()
    }

    @Test fun narrowDockKeepsCreateCenteredAndTargetsSeparateWithLargeText() {
        val picked = mutableListOf<Int>()
        var created = 0
        compose.setContent {
            val density = LocalDensity.current
            CompositionLocalProvider(LocalDensity provides Density(density.density, 1.5f)) {
                AureaTheme {
                    Box(Modifier.width(320.dp).testTag("dockBounds")) {
                        HomeDock(HomeViewModel.PROJECTS_TAB, { picked += it }, { created++ })
                    }
                }
            }
        }
        val tags = listOf("home.community", "home.profile", "home.create", "home.projects", "home.presets")
        val bounds = tags.map {
            compose.onNodeWithTag(it).assertHeightIsAtLeast(48.dp).assertWidthIsAtLeast(48.dp)
            compose.onNodeWithTag(it).fetchSemanticsNode().boundsInRoot
        }
        val parent = compose.onNodeWithTag("dockBounds").fetchSemanticsNode().boundsInRoot
        assertEquals(parent.center.x, bounds[2].center.x, 1f)
        bounds.zipWithNext().forEach { (a, b) -> assertTrue(a.right <= b.left + 1f) }
        compose.onNodeWithTag("home.projects").assertIsSelected()
        tags.forEach { compose.onNodeWithTag(it).performClick() }
        compose.runOnIdle {
            assertEquals(listOf(HomeViewModel.HOME_TAB, HomeViewModel.PROFILE_TAB, HomeViewModel.PROJECTS_TAB, HomeViewModel.PRESETS_TAB), picked)
            assertEquals(1, created)
        }
    }

    @Test fun homeStartsInStartRemembersTheLastTabAndCreateOpensTheSheet() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        freshHome()
        compose.setContent {
            val store: EditorStore = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { HomeScreen(store) }
        }
        compose.onNodeWithTag("home.community").assertIsSelected()
        compose.onNodeWithTag("home.projects").performClick()
        compose.onNodeWithTag("home.projects").assertIsSelected()
        compose.runOnIdle {
            val saved = context.getSharedPreferences("aurea.home", android.content.Context.MODE_PRIVATE).getInt(HomeViewModel.KEY_TAB, -1)
            assertEquals(HomeViewModel.PROJECTS_TAB, saved)
        }
        compose.onNodeWithTag("home.projects.subtab.projects").assertIsSelected()
        val file = File(context.getExternalFilesDir(null), "home-omega.png")
        compose.waitForIdle()
        val screenshot = InstrumentationRegistry.getInstrumentation().uiAutomation.takeScreenshot()
        try { file.outputStream().use { assertTrue(screenshot.compress(Bitmap.CompressFormat.PNG, 100, it)) } }
        finally { screenshot.recycle() }
        InstrumentationRegistry.getInstrumentation().uiAutomation.executeShellCommand(
            "cp '${file.absolutePath}' /sdcard/Download/aurea-home-omega.png").close()
        compose.onNodeWithTag("home.create").performClick()
        compose.onNodeWithTag("newProject.sheet").assertIsDisplayed()
        compose.onNodeWithTag("newProject.uiMode.composition").assertIsSelected()
        compose.onNodeWithTag("newProject.cancel").performClick()
        compose.waitUntil(3_000) { compose.onAllNodesWithTag("newProject.sheet").fetchSemanticsNodes().isEmpty() }
    }

    @Test fun menuOpensTheSettingsDrawerWithTheCreateCard() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        freshHome()
        compose.setContent {
            val store: EditorStore = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { HomeScreen(store) }
        }
        compose.onNodeWithTag("home.drawer.title").assertIsNotDisplayed()
        compose.onNodeWithTag("home.menu").performClick()
        compose.waitUntil(3_000) { runCatching { compose.onNodeWithTag("home.drawer.title").assertIsDisplayed() }.isSuccess }
        compose.onNodeWithTag("home.drawer.importMedia").assertIsDisplayed()
        compose.onNodeWithTag("home.drawer.importProject").assertIsDisplayed()
        compose.onNodeWithTag("home.drawer.hyper").assertIsDisplayed()
    }

    @Test fun oldReadMarkerShowsInstalledReleaseNotesAndDismissalStoresCurrentEdition() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        check(context.packageName.endsWith(".uitest"))
        val preferences = context.getSharedPreferences("aurea.releaseNotes", android.content.Context.MODE_PRIVATE)
        preferences.edit().putString("read", "0.0.5:2123-1").commit()
        AureaDonations.launchPromptPending = false
        compose.setContent { AureaTheme { ReleaseNotesEntry() } }
        val version = "Aurea Beta ${com.aurea.aurea.BuildConfig.VERSION_NAME.removeSuffix("-uitest")} (${com.aurea.aurea.BuildConfig.VERSION_CODE})"
        compose.onNodeWithText(version, substring = true).assertIsDisplayed()
        val screenshot = instrumentation.uiAutomation.takeScreenshot()
        try {
            File(context.getExternalFilesDir(null), "beta008-release-notes.png").outputStream().use {
                assertTrue(screenshot.compress(Bitmap.CompressFormat.PNG, 100, it))
            }
        } finally { screenshot.recycle() }
        compose.onNodeWithTag("releaseNotes.close").performClick()
        compose.runOnIdle {
            assertEquals("${com.aurea.aurea.BuildConfig.VERSION_NAME}:${com.aurea.aurea.BuildConfig.VERSION_CODE}", preferences.getString("read", null))
        }
        compose.onNodeWithText(version, substring = true).assertDoesNotExist()
        compose.onNodeWithTag("home.releaseNotes").performClick()
        compose.onNodeWithText(version, substring = true).assertIsDisplayed()
    }
}
