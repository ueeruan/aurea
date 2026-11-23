package com.aurea.aurea.home

import android.content.res.AssetManager
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.aurea.aurea.R
import com.aurea.aurea.ui.theme.AureaColors
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray

private data class LibraryModel(val id: String, val name: String, val provider: String, val category: String)

private fun readModels(assets: AssetManager, language: String): List<LibraryModel> {
    val entries = assets.open("ModelLibrary/catalog.json").bufferedReader().use { JSONArray(it.readText()) }
    return List(entries.length()) { index ->
        val entry = entries.getJSONObject(index)
        val names = entry.getJSONObject("names")
        LibraryModel(entry.getString("id"), names.optString(language, names.getString("en")),
            entry.getString("provider"), entry.getString("category"))
    }
}

@Composable
internal fun ModelLibrary(
    actionLabel: String,
    onUse: (id: String, name: String) -> Unit,
    onClose: (() -> Unit)? = null,
) {
    val assets = LocalContext.current.assets
    val language = LocalConfiguration.current.locales[0].language
    val models by produceState<List<LibraryModel>?>(null, assets, language) {
        value = withContext(Dispatchers.IO) { runCatching { readModels(assets, language) }.getOrDefault(emptyList()) }
    }
    var category by rememberSaveable { mutableStateOf("") }
    val list = models
    Column(Modifier.fillMaxSize().background(AureaColors.LightBackground).testTag("models.library")) {
        Row(
            Modifier.fillMaxWidth().padding(start = 16.dp, end = 8.dp, top = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(stringResource(R.string.model_shop_title), Modifier.weight(1f),
                color = AureaColors.LightText, fontSize = 24.sp, fontWeight = FontWeight.SemiBold)
            if (onClose != null) TextButton(onClick = onClose) { Text(stringResource(R.string.editor_fechar)) }
        }
        Row(Modifier.horizontalScroll(rememberScrollState()).padding(horizontal = 16.dp),
            horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            for ((id, label) in listOf("" to R.string.model_shop_all, "low_poly" to R.string.model_shop_low_poly,
                "realistic" to R.string.model_shop_realistic)) {
                FilterChip(selected = category == id, onClick = { category = id },
                    label = { Text(stringResource(label)) }, modifier = Modifier.testTag("models.filter.${id.ifEmpty { "all" }}"))
            }
        }
        when {
            list == null -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                CircularProgressIndicator(color = AureaColors.Accent)
            }
            list.isEmpty() -> Text(stringResource(R.string.msg_nao_consegui_ler_esse_arquivo),
                Modifier.padding(16.dp), color = AureaColors.LightTextMuted)
            else -> LazyVerticalGrid(
                columns = GridCells.Adaptive(144.dp),
                contentPadding = PaddingValues(16.dp),
                verticalArrangement = Arrangement.spacedBy(12.dp),
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                items(list.filter { category.isEmpty() || it.category == category }, key = { it.id }) { model ->
                    val thumbnail by produceState<ImageBitmap?>(null, model.id, assets) {
                        value = withContext(Dispatchers.IO) {
                            runCatching {
                                assets.open("ModelLibrary/${model.id}.jpg").use { BitmapFactory.decodeStream(it)?.asImageBitmap() }
                            }.getOrNull()
                        }
                    }
                    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp))
                        .background(AureaColors.LightSurface).testTag("models.card.${model.id}")) {
                        Box(Modifier.fillMaxWidth().aspectRatio(1.16f).background(AureaColors.LightBackground),
                            contentAlignment = Alignment.Center) {
                            thumbnail?.let { Image(it, contentDescription = null,
                                modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Fit) }
                        }
                        Column(Modifier.padding(start = 10.dp, end = 10.dp, top = 8.dp)) {
                            Text(model.name, color = AureaColors.LightText, fontSize = 15.sp, fontWeight = FontWeight.Medium)
                            Text("${model.provider} · CC0", color = AureaColors.LightTextMuted, fontSize = 12.sp)
                        }
                        TextButton(onClick = { onUse(model.id, model.name) }) { Text(actionLabel, color = AureaColors.Accent) }
                    }
                }
            }
        }
    }
}

@Composable
internal fun ModelLibraryDialog(onUse: (String, String) -> Unit, onDismiss: () -> Unit) {
    Dialog(onDismissRequest = onDismiss, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        Box(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing)) {
            ModelLibrary(stringResource(R.string.model_shop_insert), onUse, onDismiss)
        }
    }
}
