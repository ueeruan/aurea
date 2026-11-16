@file:Suppress("unused", "UNUSED_PARAMETER")

package androidx.compose.ui.res

import android.content.res.Resources
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.painter.BitmapPainter
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalConfiguration

@Composable
@ReadOnlyComposable
internal fun currentResources(): Resources = Resources.forConfiguration(LocalConfiguration.current)

@Composable
@ReadOnlyComposable
fun stringResource(id: Int): String = currentResources().getString(id)

@Composable
@ReadOnlyComposable
fun stringResource(id: Int, vararg formatArgs: Any): String = currentResources().getString(id, *formatArgs)

@Composable
@ReadOnlyComposable
fun pluralStringResource(id: Int, count: Int): String = currentResources().getQuantityString(id, count)

@Composable
@ReadOnlyComposable
fun pluralStringResource(id: Int, count: Int, vararg formatArgs: Any): String = currentResources().getQuantityString(id, count, *formatArgs)

@Composable
@ReadOnlyComposable
fun stringArrayResource(id: Int): Array<String> = currentResources().getStringArray(id)

@Composable
@ReadOnlyComposable
fun colorResource(id: Int): Color = Color(currentResources().getColor(id))

@Composable
@ReadOnlyComposable
fun dimensionResource(id: Int): androidx.compose.ui.unit.Dp = androidx.compose.ui.unit.Dp(0f)

/** Imagens de `res/drawable*` (PNG/JPG) e vetores do Android (XML). */
@Composable
fun painterResource(id: Int): Painter =
    if (com.aurea.pc.PcDrawables.isVector(id)) androidx.compose.ui.graphics.vector.rememberVectorPainter(com.aurea.pc.PcDrawables.vector(id))
    else androidx.compose.runtime.remember(id) { com.aurea.pc.PcDrawables.painter(id) }

@Composable
fun vectorResource(id: Int): ImageVector = com.aurea.pc.PcDrawables.vector(id)

fun ImageVector.Companion.vectorResource(res: Resources, id: Int): ImageVector = com.aurea.pc.PcDrawables.vector(id)

@Composable
fun imageResource(id: Int): androidx.compose.ui.graphics.ImageBitmap = com.aurea.pc.PcDrawables.bitmap(id)
