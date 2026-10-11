package com.aurea.aurea.editor

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.size
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.test.assertHeightIsAtLeast
import androidx.compose.ui.test.click
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipe
import androidx.compose.ui.unit.dp
import com.aurea.aurea.ui.ds.ValueBox
import com.aurea.aurea.ui.ds.valueDrag
import com.aurea.aurea.ui.theme.AureaTheme
import com.aurea.aurea.ui.theme.tocavel
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test

class TouchTargetsTest {
    @get:Rule val compose = createComposeRule()

    @Test fun numericFieldHasFingerSizedTargetIncludingSpaceBelowPill() {
        var taps = 0
        compose.setContent {
            AureaTheme { ValueBox("120px", modifier = Modifier.testTag("value"), onTap = { taps++ }) }
        }
        compose.onNodeWithTag("value").assertHeightIsAtLeast(48.dp)
        compose.onNodeWithTag("value").performTouchInput { click(Offset(width / 2f, height - 2f)) }
        compose.runOnIdle { assertEquals(1, taps) }
    }

    @Test fun pressedButtonKeepsItsOriginalEdgeTarget() {
        var taps = 0
        compose.setContent {
            AureaTheme { Box(Modifier.size(100.dp, 48.dp).testTag("button").tocavel(shrink = 0.80f) { taps++ }) }
        }
        compose.onNodeWithTag("button").performTouchInput {
            down(Offset(1f, height / 2f))
            advanceEventTime(200)
            moveTo(Offset(2f, height / 2f))
            up()
        }
        compose.runOnIdle { assertEquals(1, taps) }
    }

    @Test fun switchingPropertyDoesNotKeepPreviousDragCallback() {
        val selected = mutableStateOf(0)
        val values = floatArrayOf(10f, 200f)
        val begins = intArrayOf(0, 0)
        val ends = intArrayOf(0, 0)
        compose.setContent {
            val target = selected.value
            Column {
                Box(Modifier.size(240.dp, 64.dp).testTag("ruler").valueDrag(
                    true, start = { values[target] }, unitsPerDp = { 1f },
                    min = 0f, max = 1000f,
                    onStart = { begins[target]++ }, onValue = { values[target] = it }, onEnd = { ends[target]++ },
                ))
            }
        }
        fun drag() = compose.onNodeWithTag("ruler").performTouchInput {
            swipe(Offset(width * .8f, height / 2f), Offset(width * .2f, height / 2f), 250)
        }
        drag()
        var first = 0f
        compose.runOnIdle { first = values[0]; selected.value = 1 }
        compose.waitForIdle()
        drag()
        compose.runOnIdle {
            assertTrue(first > 10f)
            assertEquals(first, values[0], .001f)
            assertTrue(values[1] > 200f)
            assertEquals(listOf(1, 1), begins.toList())
            assertEquals(listOf(1, 1), ends.toList())
        }
    }
}
