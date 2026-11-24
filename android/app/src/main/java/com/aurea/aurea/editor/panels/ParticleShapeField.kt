package com.aurea.aurea.editor.panels

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.imageResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel
import kotlin.math.roundToInt

// Shape IDs belong to particular::Shape. Legacy presets keep their IDs.
private val particleShapes = listOf(
    R.string.particle_shape_sphere,
    R.string.particle_shape_star,
    R.string.particle_shape_heart,
    R.string.particle_shape_leaf,
    R.string.particle_shape_petal,
    R.string.particle_shape_butterfly,
    R.string.particle_shape_confetti,
    R.string.particle_shape_sparkle,
    R.string.particle_shape_snowflake,
    R.string.particle_shape_line,
    R.string.particle_shape_shaded,
    R.string.particle_shape_faded,
    R.string.particle_shape_dark_faded,
    R.string.particle_shape_bubble,
    R.string.particle_shape_motion_polygon,
    R.string.particle_shape_motion_square,
    R.string.particle_shape_triangle,
    R.string.particle_shape_quad,
    R.string.particle_shape_textured_triangle,
    R.string.particle_shape_textured_quad,
    R.string.particle_shape_tetrahedron,
    R.string.particle_shape_cube,
    R.string.particle_shape_lens_convex,
    R.string.particle_shape_lens_concave,
    R.string.particle_shape_lens_fade,
    R.string.particle_shape_lens_dark_fade,
    R.string.particle_shape_lens_bubble,
    R.string.particle_shape_textured_square,
    R.string.particle_shape_textured_disc,
    R.string.particle_shape_textured_faded_disc,
    R.string.particle_shape_glow_sphere,
    R.string.particle_shape_glow_star,
    R.string.particle_shape_cloudlet,
    R.string.particle_shape_streaklet,
    R.string.particle_shape_sprite,
)
private val shapeSections = listOf(
    R.string.particle_basic to listOf(0, 1, 9, 14, 15, 16, 17, 20, 21),
    R.string.particle_natural to listOf(2, 3, 4, 5, 6, 8, 32),
    R.string.particle_optical to listOf(7, 10, 11, 12, 13, 22, 23, 24, 25, 26, 30, 31, 33),
    R.string.particle_textures to listOf(18, 19, 27, 28, 29, 34),
)

@Composable
internal fun ParticleShapeField(current: Int, enabled: Boolean, tag: String, onPick: (Int) -> Unit) {
    var show by remember(tag) { mutableStateOf(false) }
    val name = stringResource(particleShapes.getOrElse(current) { particleShapes.first() })
    val label = stringResource(R.string.particle_gallery)
    Row(
        Modifier.fillMaxWidth().padding(6.dp).heightIn(min = 52.dp).clip(RoundedCornerShape(12.dp))
            .background(AureaColors.Surface).testTag(tag)
            .semantics { contentDescription = "$label · $name"; role = Role.Button }
            .tocavel(enabled = enabled) { show = true }
            .padding(horizontal = 12.dp, vertical = 6.dp),
        horizontalArrangement = Arrangement.spacedBy(10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.Sparkles, 18.dp, AureaColors.Accent)
        Column(Modifier.weight(1f)) {
            Text(stringResource(R.string.particular_shape), style = AureaType.of(12f, color = AureaColors.Muted))
            Text(name, maxLines = 1, overflow = TextOverflow.Ellipsis,
                style = AureaType.of(14f, FontWeight.SemiBold, color = AureaColors.Text))
        }
        CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, AureaColors.Accent)
    }
    if (show) ParticleShapeGallery(current, onClose = { show = false }) { id ->
        show = false
        onPick(id)
    }
}

@Composable
private fun ParticleShapeGallery(current: Int, onClose: () -> Unit, onPick: (Int) -> Unit) {
    val atlas = ImageBitmap.imageResource(R.drawable.particle_shape_atlas)
    val close = stringResource(R.string.common_close)
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Column(
            Modifier.fillMaxSize().background(AureaColors.Background)
                .windowInsetsPadding(WindowInsets.safeDrawing).testTag("particles.gallery"),
        ) {
            Row(
                Modifier.fillMaxWidth().heightIn(min = 54.dp).background(AureaColors.Surface).padding(end = 16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(
                    Modifier.size(48.dp).semantics { contentDescription = close; role = Role.Button }
                        .testTag("particles.gallery.close").tocavel(onClick = onClose),
                    contentAlignment = Alignment.Center,
                ) { CupertinoIcon(CupertinoGlyph.Xmark, 18.dp, AureaColors.Text) }
                Text(stringResource(R.string.particle_gallery), Modifier.weight(1f).semantics { heading() },
                    style = AureaType.of(19f, FontWeight.Bold, color = AureaColors.Text))
            }
            LazyVerticalGrid(
                columns = GridCells.Adaptive(112.dp),
                contentPadding = PaddingValues(14.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
                modifier = Modifier.fillMaxSize().testTag("particles.gallery.grid"),
            ) {
                shapeSections.forEach { (title, shapes) ->
                    item(key = "section:$title", span = { GridItemSpan(maxLineSpan) }) {
                        Text(stringResource(title), Modifier.padding(top = 7.dp).semantics { heading() },
                            style = AureaType.of(14f, FontWeight.SemiBold, color = AureaColors.Text))
                    }
                    items(shapes, key = { it }) { id ->
                        val name = stringResource(particleShapes[id])
                        val active = current == id
                        val shape = RoundedCornerShape(12.dp)
                        Column(
                            Modifier.fillMaxWidth().heightIn(min = 122.dp).clip(shape)
                                .background(if (active) AureaColors.AccentDim else AureaColors.Surface)
                                .border(1.dp, if (active) AureaColors.Accent else AureaColors.Border, shape)
                                .semantics { contentDescription = name; selected = active; role = Role.Button }
                                .testTag("particles.gallery.shape.$id").tocavel { onPick(id) }
                                .padding(horizontal = 6.dp, vertical = 8.dp),
                            verticalArrangement = Arrangement.Center,
                            horizontalAlignment = Alignment.CenterHorizontally,
                        ) {
                            Canvas(Modifier.size(70.dp)) {
                                drawImage(
                                    atlas,
                                    srcOffset = IntOffset(id % 7 * 80, id / 7 * 80), srcSize = IntSize(80, 80),
                                    dstSize = IntSize(size.width.roundToInt(), size.height.roundToInt()),
                                )
                            }
                            Text(name, minLines = 2, maxLines = 3, textAlign = TextAlign.Center,
                                style = AureaType.of(11.5f, FontWeight.Medium, color = AureaColors.Text))
                        }
                    }
                }
            }
        }
    }
}
