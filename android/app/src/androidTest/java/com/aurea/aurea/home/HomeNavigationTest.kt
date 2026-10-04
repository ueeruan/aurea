package com.aurea.aurea.home

import android.app.Application
import android.graphics.Bitmap
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asAndroidBitmap
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
            .putString("read", "${com.aurea.aurea.BuildConfig.VERSION_CODE}:2123-1").commit()
        AureaDonations.launchPromptPending = false
        compose.setContent {
            val store: EditorStore = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { HomeScreen(store) }
        }
        compose.onNodeWithTag("home.projects").assertIsSelected()
        val file = File(context.getExternalFilesDir(null), "home-redesign.png")
        file.outputStream().use { compose.onRoot().captureToImage().asAndroidBitmap().compress(Bitmap.CompressFormat.PNG, 100, it) }
        InstrumentationRegistry.getInstrumentation().uiAutomation.executeShellCommand(
            "cp '${file.absolutePath}' /sdcard/Download/aurea-home-redesign.png").close()
        compose.onNodeWithTag("home.create").performClick()
        compose.onNodeWithText(context.getString(com.aurea.aurea.R.string.new_project_title)).assertIsDisplayed()
    }
}
