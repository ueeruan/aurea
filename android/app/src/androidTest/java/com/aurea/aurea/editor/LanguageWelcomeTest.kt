package com.aurea.aurea.editor

import android.content.Context
import androidx.compose.ui.test.*
import androidx.compose.ui.test.junit4.createEmptyComposeRule
import androidx.test.core.app.ActivityScenario
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.MainActivity
import com.aurea.aurea.ui.i18n.AppLanguage
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test

class LanguageWelcomeTest {
    @get:Rule val compose = createEmptyComposeRule()

    @Test fun firstOpeningAsksAndTheChoiceSurvivesActivityRecreationAndRelaunch() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        assertTrue(context.packageName.endsWith(".uitest"))
        val prefs = context.getSharedPreferences("aurea.settings", Context.MODE_PRIVATE)
        prefs.edit().remove("language_chosen").remove("idioma").commit()
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            compose.onNodeWithText("Choose your language · Escolha seu idioma").assertIsDisplayed()
            compose.onNodeWithText("English").performClick()
            compose.waitUntil(5000) { !AppLanguage.needsChoice(context) }
            assertEquals(AppLanguage.EN, AppLanguage.current(context))
            scenario.onActivity { assertEquals("en", it.resources.configuration.locales[0].language) }
        }
        ActivityScenario.launch(MainActivity::class.java).use {
            compose.onNodeWithText("Choose your language · Escolha seu idioma").assertDoesNotExist()
        }
        // Choosing system is also an explicit choice; it must not ask on every opening.
        AppLanguage.select(context, AppLanguage.SYSTEM)
        assertFalse(AppLanguage.needsChoice(context))
        assertEquals(AppLanguage.SYSTEM, AppLanguage.current(context))
    }
}
