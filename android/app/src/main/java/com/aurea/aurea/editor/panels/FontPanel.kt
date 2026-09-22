package com.aurea.aurea.editor.panels

import android.graphics.Typeface
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import java.io.File

/**
 * Prévia de cada família na PRÓPRIA fonte: o Android abre o arquivo uma vez
 * (Typeface) e a lista preguiçosa só desenha as linhas visíveis — abrir o
 * painel não rasteriza 200 fontes.
 */
private val previewCache = HashMap<String, FontFamily?>()

private fun previewFamily(path: String): FontFamily? = previewCache.getOrPut(path) {
    try {
        if (File(path).exists()) FontFamily(Typeface.createFromFile(path)) else null
    } catch (e: Exception) {
        null
    }
}

/**
 * FONTE do texto: famílias do aparelho e importadas (cada uma escrita na
 * própria fonte), busca, peso e itálico da família escolhida, e importar
 * TTF/OTF (vai para o projeto e abre em outro aparelho).
 */
@Composable
internal fun FontPanel(env: PanelEnv) {
    val store = env.store
    LaunchedEffect(Unit) { store.loadFonts() }
    val fonts by remember(store) { derivedStateOf { store.fonts } }
    val current by remember(store) { derivedStateOf { store.textFont } }
    var query by remember { mutableStateOf("") }
    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) store.importFont(uri)
    }
    val families = remember(fonts, query) {
        fonts.groupBy { it.family }
            .filterKeys { query.isBlank() || it.contains(query, ignoreCase = true) }
            .toSortedMap(String.CASE_INSENSITIVE_ORDER)
            .map { (fam, list) -> fam to list }
    }
    Column(Modifier.fillMaxSize().padding(start = 14.dp, top = 8.dp, end = 14.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(
                Modifier.weight(1f).heightIn(min = 40.dp).clip(RoundedCornerShape(10.dp)).background(AureaColors.Chip)
                    .padding(horizontal = 12.dp, vertical = 10.dp),
            ) {
                BasicTextField(
                    value = query,
                    onValueChange = { query = it },
                    singleLine = true,
                    textStyle = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Text)),
                    cursorBrush = SolidColor(AureaColors.Accent),
                    modifier = Modifier.fillMaxWidth(),
                )
                if (query.isEmpty()) Text("Buscar fonte", style = AureaType.Base.merge(TextStyle(fontSize = 14.sp, color = AureaColors.Muted)))
            }
            Spacer(Modifier.padding(4.dp))
            Box(
                Modifier.clip(RoundedCornerShape(10.dp)).background(AureaColors.AccentDim)
                    .tocavel(onClick = { pick.launch(arrayOf("font/ttf", "font/otf", "application/x-font-ttf", "application/octet-stream", "*/*")) })
                    .padding(horizontal = 12.dp, vertical = 10.dp),
            ) {
                Text("Importar", style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Accent)))
            }
        }
        // Peso e itálico da família escolhida.
        val cur = current
        val curFamily = cur?.family.orEmpty()
        val styles = remember(fonts, curFamily) { fonts.filter { it.family == curFamily } }
        if (styles.size > 1) {
            Spacer(Modifier.height(8.dp))
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                styles.forEach { st ->
                    val on = cur != null && st.weight == cur.weight && st.italic == cur.italic
                    Box(
                        Modifier.clip(RoundedCornerShape(8.dp)).background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                            .tocavel(onClick = { store.applyTextFont(st) }).padding(horizontal = 10.dp, vertical = 6.dp),
                    ) {
                        Text(
                            st.style.ifEmpty { "${st.weight}" },
                            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = if (on) AureaColors.Accent else AureaColors.Text,
                                fontFamily = previewFamily(st.path))),
                        )
                    }
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        LazyColumn(Modifier.fillMaxWidth().weight(1f)) {
            item {
                FontRow("Padrão do aparelho", null, curFamily.isEmpty()) { store.applyTextFont(null) }
            }
            items(families, key = { it.first }) { (family, list) ->
                // A regular da família (peso mais perto de 400, sem itálico).
                val regular = list.minByOrNull { kotlin.math.abs(it.weight - 400) + if (it.italic) 1000 else 0 } ?: list.first()
                FontRow(family, regular.path, family == curFamily, imported = list.any { it.imported }) { store.applyTextFont(regular) }
            }
        }
    }
}

@Composable
private fun FontRow(name: String, path: String?, on: Boolean, imported: Boolean = false, onClick: () -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(46.dp).clip(RoundedCornerShape(10.dp))
            .background(if (on) AureaColors.AccentDim else androidx.compose.ui.graphics.Color.Transparent)
            .tocavel(onClick = onClick).padding(horizontal = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            name,
            modifier = Modifier.weight(1f),
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            style = AureaType.Base.merge(TextStyle(fontSize = 17.sp, color = if (on) AureaColors.Accent else AureaColors.Text,
                fontFamily = path?.let { previewFamily(it) } ?: FontFamily.Default, fontWeight = FontWeight.Normal)),
        )
        if (imported) Text("importada", style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Muted)))
    }
}
