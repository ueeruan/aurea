package com.aurea.aurea.home

import android.content.Context
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.os.Build
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.core.content.pm.PackageInfoCompat
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon

/** Versão real do APK (a A.01 mostrava uma constante desatualizada, §8.37). */
private class AppVersion(val name: String, val build: Long)

private fun readVersion(context: Context): AppVersion = try {
    val pm = context.packageManager
    val info: PackageInfo = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
        pm.getPackageInfo(context.packageName, PackageManager.PackageInfoFlags.of(0))
    } else {
        @Suppress("DEPRECATION")
        pm.getPackageInfo(context.packageName, 0)
    }
    AppVersion(info.versionName ?: "?", PackageInfoCompat.getLongVersionCode(info))
} catch (_: Exception) {
    AppVersion("?", 0)
}

private val DescriptionStyle = AureaType.Base.merge(TextStyle(color = AureaColors.Muted))

/**
 * Aba Sobre (`about_tab.dart@762dbfe`, spec §6.2): identidade, versão e
 * créditos. Sete toques na versão ligam as ferramentas de desenvolvedor.
 *
 * "Tecnologia: Flutter + FFmpeg" e "Feito por … com Flutter" ficariam falsos
 * no app nativo (§8.36): dizem o que o Aurea novo é.
 */
@Composable
internal fun AboutTab(store: EditorStore, vm: HomeViewModel, listState: LazyListState, bottomBar: Dp) {
    val context = LocalContext.current
    val version = remember { readVersion(context) }
    var taps by remember { mutableIntStateOf(0) }
    val soon: (String) -> Unit = { store.comingSoon(it) }
    LazyColumn(
        state = listState,
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(start = 20.dp, top = 12.dp, end = 20.dp, bottom = HomeDims.ListEndSpace + bottomBar - HomeDims.TabBarHeight),
    ) {
        item(key = "identidade") {
            Column(Modifier.fillMaxWidth()) {
                Text("Sobre", style = AureaType.HeadlineLarge)
                Spacer(Modifier.height(32.dp))
                Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
                    AureaLogo(96.dp)
                    Spacer(Modifier.height(18.dp))
                    Text("Aurea", style = HomeType.AboutName)
                    Spacer(Modifier.height(4.dp))
                    Text("Editor de video e composicao", style = AureaType.BodySmall)
                    Spacer(Modifier.height(10.dp))
                    Box(
                        Modifier
                            .clip(RoundedCornerShape(20.dp))
                            .background(AureaColors.Accent.copy(alpha = 0.12f))
                            .clickable(interactionSource = null, indication = null) {
                                taps++
                                if (taps >= 7) {
                                    taps = 0
                                    val on = vm.toggleDevTools()
                                    store.showToast(if (on) "Ferramentas de desenvolvedor ligadas" else "Ferramentas de desenvolvedor desligadas")
                                }
                            }
                            .padding(horizontal = 12.dp, vertical = 5.dp),
                    ) {
                        Text("Versao ${version.name}", style = HomeType.VersionPill)
                    }
                }
                Spacer(Modifier.height(24.dp))
            }
        }
        if (vm.devTools) {
            item(key = "dev") {
                Group {
                    TileRow(
                        leading = { CupertinoIcon(CupertinoGlyph.Wrench, 21.dp, AureaColors.Accent) },
                        title = "Ferramentas de desenvolvedor",
                        subtitle = "Versao ${version.name} · build ${version.build}",
                    )
                    GroupDivider()
                    TileRow(
                        leading = { CupertinoIcon(CupertinoGlyph.ArrowCounterclockwise, 21.dp, AureaColors.Accent) },
                        title = "Rever avisos e dicas",
                        subtitle = "Novidades da versao e dicas de primeiro uso voltam",
                        onClick = { soon("Avisos e dicas") },
                    )
                }
                Spacer(Modifier.height(18.dp))
            }
        }
        item(key = "beta") {
            BetaBanner { soon("Reportar") }
            Spacer(Modifier.height(18.dp))
            Text(
                "Aurea e um editor de video e composicao para celular: timeline multi-trilha, preview em tempo real e exportacao direto do aparelho, sem depender de nuvem.",
                style = DescriptionStyle,
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(HomeDims.GroupRadius))
                    .background(AureaColors.Surface)
                    .padding(16.dp),
            )
            Spacer(Modifier.height(22.dp))
        }
        item(key = "links") {
            Group {
                TileRow(
                    leading = { CupertinoIcon(CupertinoGlyph.Book, 24.dp, AureaColors.Accent) },
                    title = "Como usar o AUREA",
                    subtitle = "Guia rápido e ajuda dos efeitos · offline",
                    subtitleStyle = HomeType.TileSubtitleMedium,
                    trailing = { TileChevron() },
                    onClick = { soon("Guia rápido") },
                )
                TileRow(
                    leading = { CupertinoIcon(CupertinoGlyph.Bolt, 21.dp, AureaColors.Accent) },
                    title = "Tecnologia",
                    subtitle = "Jetpack Compose + motor C++ / Vulkan",
                )
                GroupDivider()
                TileRow(
                    leading = { CupertinoIcon(CupertinoGlyph.ExclamationmarkBubble, 21.dp, AureaColors.Accent) },
                    title = "Reportar erro ou sugerir",
                    subtitle = "Vai direto para o criador",
                    trailing = { TileChevron() },
                    onClick = { soon("Reportar") },
                )
                GroupDivider()
                TileRow(
                    leading = { CupertinoIcon(CupertinoGlyph.PersonCropCircle, 21.dp, AureaColors.Accent) },
                    title = "Criador",
                    subtitle = "Ruanzitwo  ·  @ofruanzitwo  ·  TikTok @ruanzitwo",
                    trailing = { TileChevron() },
                    onClick = { soon("Reportar") },
                )
                GroupDivider()
                TileRow(
                    leading = { CupertinoIcon(CupertinoGlyph.DocText, 21.dp, AureaColors.Accent) },
                    title = "Licencas de codigo aberto",
                    trailing = { TileChevron() },
                    onClick = { soon("Licenças") },
                )
            }
            Spacer(Modifier.height(26.dp))
            Text("Feito por Ruanzitwo", style = HomeType.Footer, textAlign = TextAlign.Center, modifier = Modifier.fillMaxWidth())
        }
    }
}

/** `BetaBanner`: âmbar, "Versao beta para testes" + a explicação e a seta. */
@Composable
private fun BetaBanner(onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clip(RoundedCornerShape(14.dp))
            .background(HomeColors.BetaFill)
            .border(1.dp, HomeColors.BetaBorder, RoundedCornerShape(14.dp))
            .clickable(interactionSource = null, indication = null, onClick = onClick)
            .padding(14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.ExclamationmarkTriangle, 18.dp, HomeColors.Beta)
        Spacer(Modifier.width(10.dp))
        Column(Modifier.weight(1f)) {
            Text("Versao beta para testes", style = HomeType.BetaTitle)
            Spacer(Modifier.height(3.dp))
            Text(
                "Pode ter erros, travar ou perder alteracoes nao salvas. Achou um problema ou quer sugerir algo? Toque aqui.",
                style = HomeType.BetaBody,
            )
        }
        Spacer(Modifier.width(8.dp))
        CupertinoIcon(CupertinoGlyph.ChevronRight, 14.dp, HomeColors.Beta)
    }
}
