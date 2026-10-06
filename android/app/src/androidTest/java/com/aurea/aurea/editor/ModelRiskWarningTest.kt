package com.aurea.aurea.editor

import android.app.Application
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.test.platform.app.InstrumentationRegistry
import com.aurea.aurea.R
import com.aurea.aurea.engine.ModelPlan
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ModelOptimizeDialog
import com.aurea.aurea.ui.i18n.AppText
import com.aurea.aurea.ui.theme.AureaTheme
import org.junit.Rule
import org.junit.Test

class ModelRiskWarningTest {
    @get:Rule val compose = createComposeRule()

    @Test fun measuredHeavyModelDialogIncludesRiskBeforeImport() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        check(context.packageName.endsWith(".uitest"))
        val report = longArrayOf(1, 1, 1, 0, 1, 10_000_000, 0, 4, 4096, 128_000_000,
            0, 1, 1, 250_000_000, 90_000_000, 40_000_000, 10_000_000, 100_000, 40_000, 4096, 2048, 1024)
        compose.setContent {
            val store: EditorStore = viewModel(factory = ViewModelProvider.AndroidViewModelFactory.getInstance(context.applicationContext as Application))
            AureaTheme { ModelOptimizeDialog(store, EditorStore.ModelOptimizeRequest("", "Heavy model", ModelPlan(report), null)) }
        }
        val warning = AppText.get(context, R.string.model3d_risk_warning)
        compose.onNodeWithText(warning, substring = true).assertExists()
        stabilityScreenshot("heavy-model-warning.png")
    }
}
