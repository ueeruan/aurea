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

class HomeNavigationTest {
    @get:Rule val compose = createComposeRule()

    @Test fun appWithoutSessionOpensProjectsAndOptionalLoginCanBeClosed() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        com.aurea.aurea.captions.KeyVault(context).apply {
            remove("conta_token")
            remove("conta_email")
        }
        context.getSharedPreferences("aurea.releaseNotes", android.content.Context.MODE_PRIVATE).edit()
            .putString("read", "${com.aurea.aurea.BuildConfig.VERSION_NAME}:${com.aurea.aurea.BuildConfig.VERSION_CODE}").commit()
        AureaDonations.launchPromptPending = false
        compose.setContent {
            val store: EditorStore = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            com.aurea.aurea.ui.AureaApp(store)
        }
        compose.onNodeWithTag("home.projects").assertIsSelected()
        compose.onNodeWithTag("conta.email").assertDoesNotExist()
        compose.onNodeWithTag("home.menu").performClick()
        compose.onNodeWithText(context.getString(com.aurea.aurea.R.string.conta_sair)).assertDoesNotExist()
        compose.onNodeWithText(context.getString(com.aurea.aurea.R.string.conta_entrar)).performClick()
        compose.onNodeWithTag("conta.email").assertIsDisplayed()
        compose.onNodeWithTag("conta.fechar").performClick()
        compose.onNodeWithTag("conta.email").assertDoesNotExist()
        compose.onNodeWithTag("home.projects").assertIsSelected()
        compose.onNodeWithTag("home.create").performClick()
        compose.onNodeWithText(context.getString(com.aurea.aurea.R.string.new_project_title)).assertIsDisplayed()
    }

    @Test fun narrowDockKeepsCreateCenteredAndTargetsSeparateWithLargeText() {
        val calls = IntArray(5)
        compose.setContent {
            val density = LocalDensity.current
            CompositionLocalProvider(LocalDensity provides Density(density.density, 1.5f)) {
                AureaTheme {
                    Box(Modifier.width(320.dp).testTag("dockBounds")) {
                        HomeDock(HomeViewModel.PROJECTS_TAB, { calls[1]++ }, { calls[3]++ },
                            { calls[2]++ }, { calls[0]++ }, {}, { calls[4]++ })
                    }
                }
            }
        }
        val tags = listOf("home.menu", "home.projects", "home.create", "home.community", "home.profile")
        val bounds = tags.map {
            compose.onNodeWithTag(it).assertHeightIsAtLeast(48.dp).assertWidthIsAtLeast(48.dp)
            compose.onNodeWithTag(it).fetchSemanticsNode().boundsInRoot
        }
        val parent = compose.onNodeWithTag("dockBounds").fetchSemanticsNode().boundsInRoot
        assertEquals(parent.center.x, bounds[2].center.x, 1f)
        bounds.zipWithNext().forEach { (a,b) -> assertTrue(a.right <= b.left + 1f) }
        tags.forEach { compose.onNodeWithTag(it).performClick() }
        compose.runOnIdle { assertEquals(listOf(1,1,1,1,1), calls.toList()) }
    }

    @Test fun homeStartsWithProjectsAndCreateOpensTheExistingProjectSheet() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        context.getSharedPreferences("aurea.releaseNotes", android.content.Context.MODE_PRIVATE).edit()
            .putString("read", "${com.aurea.aurea.BuildConfig.VERSION_NAME}:${com.aurea.aurea.BuildConfig.VERSION_CODE}").commit()
        AureaDonations.launchPromptPending = false
        compose.setContent {
            val store: EditorStore = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { HomeScreen(store) }
        }
        compose.onNodeWithTag("home.projects").assertIsSelected()
        val file = File(context.getExternalFilesDir(null), "home-redesign.png")
        compose.waitForIdle()
        val screenshot = InstrumentationRegistry.getInstrumentation().uiAutomation.takeScreenshot()
        try { file.outputStream().use { assertTrue(screenshot.compress(Bitmap.CompressFormat.PNG, 100, it)) } }
        finally { screenshot.recycle() }
        InstrumentationRegistry.getInstrumentation().uiAutomation.executeShellCommand(
            "cp '${file.absolutePath}' /sdcard/Download/aurea-home-redesign.png").close()
        compose.onNodeWithTag("home.create").performClick()
        compose.onNodeWithText(context.getString(com.aurea.aurea.R.string.new_project_title)).assertIsDisplayed()
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
        compose.onNodeWithText(context.getString(com.aurea.aurea.R.string.editor_fechar)).performClick()
        compose.runOnIdle {
            assertEquals("${com.aurea.aurea.BuildConfig.VERSION_NAME}:${com.aurea.aurea.BuildConfig.VERSION_CODE}", preferences.getString("read", null))
        }
        compose.onNodeWithText(version, substring = true).assertDoesNotExist()
        compose.onNodeWithText(context.getString(com.aurea.aurea.R.string.release_notes_title)).performClick()
        compose.onNodeWithText(version, substring = true).assertIsDisplayed()
    }
}
