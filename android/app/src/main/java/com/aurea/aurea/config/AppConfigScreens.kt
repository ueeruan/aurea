package com.aurea.aurea.config

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
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
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.editor.ActivityIndicator
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.state.ProjectFile
import com.aurea.aurea.ui.ds.AureaAlert
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.CupertinoGlyph
import com.aurea.aurea.ui.theme.CupertinoIcon
import com.aurea.aurea.ui.theme.tocavel

// =============================================================================
//  A tela de ATUALIZAÇÃO OBRIGATÓRIA e o aviso da Home (config/AppConfig.kt).
//  Tags: atualizacao.obrigatoria · atualizacao.abrir · aviso.config · aviso.config.fechar
//  O par do iOS: AppConfigViews.swift (mesmos accessibilityIdentifier).
// =============================================================================

/** Cobre o app inteiro: Home e editor não aparecem por trás. Os projetos ficam no aparelho. */
@Composable
fun ForcedUpdateScreen(config: AppConfig, store: EditorStore) {
    val context = LocalContext.current
    val url = config.updateUrl.takeIf { it.startsWith("https://") } ?: AppConfigKeys.PLAY_LISTING
    val language = LocalConfiguration.current.locales[0].language
    val action = stringResource(R.string.app_update_button)
    val backupTarget = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { uri ->
        if (uri != null) store.exportProjectBackup(uri)
    }
    Column(
        Modifier.fillMaxSize().background(AureaColors.Background).safeDrawingPadding()
            .verticalScroll(rememberScrollState()).padding(horizontal = 24.dp).testTag("atualizacao.obrigatoria"),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        Spacer(Modifier.height(48.dp))
        Box(Modifier.size(64.dp).clip(RoundedCornerShape(18.dp)).background(AureaColors.Accent.copy(alpha = 0.18f)),
            contentAlignment = Alignment.Center) { CupertinoIcon(CupertinoGlyph.ArrowDownCircleFill, 32.dp, AureaColors.Accent) }
        Spacer(Modifier.height(20.dp))
        Text(stringResource(R.string.app_update_title), style = AureaType.TitleLarge, textAlign = TextAlign.Center,
            modifier = Modifier.semantics { heading() })
        Spacer(Modifier.height(12.dp))
        Text(stringResource(R.string.app_update_body), style = AureaType.of(15f), textAlign = TextAlign.Center)
        config.messageFor(language)?.let {
            Spacer(Modifier.height(12.dp))
            Text(it, style = AureaType.of(14f, color = AureaColors.Muted), textAlign = TextAlign.Center)
        }
        Spacer(Modifier.height(10.dp))
        Text(stringResource(R.string.app_update_projects_safe), style = AureaType.of(13f, color = AureaColors.Muted),
            textAlign = TextAlign.Center)
        Spacer(Modifier.height(28.dp))
        Box(
            Modifier.fillMaxWidth().heightIn(min = 52.dp).clip(RoundedCornerShape(12.dp)).background(AureaColors.Accent)
                .testTag("atualizacao.abrir").semantics { role = Role.Button; contentDescription = action }
                .tocavel(haptic = true) {
                    runCatching {
                        context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                    }
                },
            contentAlignment = Alignment.Center,
        ) { Text(action, style = AureaType.of(16f, FontWeight.W600, color = AureaColors.OnAccent)) }
        Spacer(Modifier.height(8.dp))
        TextButton(
            onClick = { backupTarget.launch("Aurea.${ProjectFile.BACKUP_EXTENSION}") },
            enabled = store.engineReady && store.busyMessage == null,
            modifier = Modifier.fillMaxWidth().heightIn(min = 52.dp).testTag("atualizacao.backup"),
        ) { Text(stringResource(R.string.project_backup_save), style = AureaType.of(15f, FontWeight.SemiBold)) }
        store.busyMessage?.let { status ->
            ActivityIndicator()
            Spacer(Modifier.height(8.dp))
            Text(status, style = AureaType.of(13f), textAlign = TextAlign.Center)
        } ?: store.toast?.let { status ->
            Text(status, style = AureaType.of(13f), textAlign = TextAlign.Center)
        }
        Spacer(Modifier.height(48.dp))
    }
    store.errorMessage?.let { message ->
        AureaAlert(
            title = stringResource(R.string.project_backup_save),
            message = message,
            confirmLabel = stringResource(R.string.common_ok),
            showCancel = false,
            onConfirm = {},
            onDismiss = store::dismissError,
        )
    }
}

/** O aviso opcional da config na Home; fechar vale até o texto mudar. */
@Composable
fun AppConfigBanner() {
    val config = AppConfigStore.config ?: return
    val language = LocalConfiguration.current.locales[0].language
    val text = config.messageFor(language) ?: return
    if (AppConfigStore.dismissedMessage == text) return
    val close = stringResource(R.string.app_notice_dismiss)
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 6.dp).clip(RoundedCornerShape(12.dp))
            .background(AureaColors.Surface).padding(start = 14.dp, top = 10.dp, bottom = 10.dp).testTag("aviso.config"),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        CupertinoIcon(CupertinoGlyph.InfoCircleFill, 18.dp, AureaColors.Accent)
        Spacer(Modifier.width(10.dp))
        Text(text, style = AureaType.of(14f), modifier = Modifier.weight(1f))
        Box(Modifier.size(44.dp).testTag("aviso.config.fechar").semantics { role = Role.Button; contentDescription = close }
            .tocavel { AppConfigStore.dismissMessage(text) }, contentAlignment = Alignment.Center) {
            CupertinoIcon(CupertinoGlyph.Xmark, 16.dp, AureaColors.Muted)
        }
    }
}
