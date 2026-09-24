package com.aurea.aurea.diagnostics

import android.app.Application
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.aurea.aurea.R
import com.aurea.aurea.state.EditorStore
import com.aurea.aurea.ui.ds.AureaModalSheet
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaDims
import com.aurea.aurea.ui.theme.AureaShape
import com.aurea.aurea.ui.theme.AureaType
import kotlinx.coroutines.launch

/**
 * A folha do teste de estresse: um botão para rodar, o passo atual, e o relatório
 * em texto que o tester copia e devolve.
 *
 * O texto é o produto. É de propósito que ele saia como texto puro e não como
 * tela bonita: quem recebe precisa colar num chat e comparar número com número.
 *
 * O relatório também vai gravando em arquivo enquanto roda. Se o app travar ou
 * fechar no meio — que é justamente o defeito que estamos caçando — o arquivo já
 * tem tudo até o passo anterior, e é o passo anterior que diz onde travou.
 */
@Composable
fun StressSheet(
    store: EditorStore,
    version: Pair<String, Long>,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var rodando by remember { mutableStateOf(false) }
    var passo by remember { mutableStateOf("") }
    var relatorio by remember { mutableStateOf("") }

    // A fase pesada quer vídeo de verdade. Em vez de exigir que o tester prepare
    // um projeto, o arquivo é pedido na hora.
    val escolherVideo = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) {
            runCatching {
                context.contentResolver.takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION
                )
            }
            rodando = true
            relatorio = ""
            passo = ""
            scope.launch {
                relatorio = executar(context, store, version, uri) { passo = it }
                rodando = false
            }
        }
    }

    AureaModalSheet(onDismiss = onDismiss) {
        Column(
            Modifier
                .fillMaxWidth()
                .padding(horizontal = AureaDims.S4)
                .padding(bottom = AureaDims.S4),
        ) {
            Text(stringResource(R.string.stress_titulo), style = AureaType.HeadlineLarge)
            Spacer(Modifier.height(6.dp))
            Text(stringResource(R.string.stress_corpo), style = AureaType.Body)

            if (rodando) {
                Spacer(Modifier.height(AureaDims.S3))
                Text(passo.ifBlank { stringResource(R.string.stress_preparando) }, style = AureaType.Body)
            }

            if (relatorio.isNotEmpty()) {
                Spacer(Modifier.height(AureaDims.S3))
                Text(
                    relatorio,
                    style = AureaType.BodySmall.copy(fontFamily = FontFamily.Monospace),
                    modifier = Modifier
                        .fillMaxWidth()
                        .heightIn(max = 360.dp)
                        .clip(AureaShape.Sm)
                        .background(AureaColors.SurfaceHigh)
                        .border(1.dp, AureaColors.Border, AureaShape.Sm)
                        .verticalScroll(rememberScrollState())
                        .padding(10.dp),
                )
                Spacer(Modifier.height(AureaDims.S3))
                Row(horizontalArrangement = Arrangement.spacedBy(AureaDims.S2)) {
                    Botao(stringResource(R.string.stress_copiar), modifier = Modifier.weight(1f)) {
                        val cb = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                        cb.setPrimaryClip(ClipData.newPlainText("Aurea", relatorio))
                        store.showToast(context.getString(R.string.stress_copiado))
                    }
                    Botao(stringResource(R.string.stress_compartilhar), modifier = Modifier.weight(1f)) {
                        val i = Intent(Intent.ACTION_SEND).apply {
                            type = "text/plain"
                            putExtra(Intent.EXTRA_SUBJECT, "Aurea — teste de estresse")
                            putExtra(Intent.EXTRA_TEXT, relatorio)
                        }
                        context.startActivity(Intent.createChooser(i, null))
                    }
                }
            }

            Spacer(Modifier.height(AureaDims.S3))
            Botao(
                if (rodando) stringResource(R.string.stress_rodando) else stringResource(R.string.stress_rodar_pesado),
                enabled = !rodando,
                modifier = Modifier.fillMaxWidth(),
            ) {
                rodando = true
                relatorio = ""
                passo = ""
                escolherVideo.launch(arrayOf("video/*"))
            }
            Spacer(Modifier.height(AureaDims.S2))
            Botao(
                stringResource(R.string.stress_rodar),
                enabled = !rodando,
                modifier = Modifier.fillMaxWidth(),
                secundario = true,
            ) {
                rodando = true
                relatorio = ""
                passo = ""
                scope.launch {
                    relatorio = executar(context, store, version, null, { passo = it })
                    rodando = false
                }
            }
        }
    }
}

/// Roda a bateria e devolve o relatório; `null` nunca — em falha, devolve o erro em texto.
private suspend fun executar(
    context: Context,
    store: EditorStore,
    version: Pair<String, Long>,
    video: Uri?,
    onProgress: (String) -> Unit,
): String = try {
    StressBattery(
        app = context.applicationContext as Application,
        store = store,
        engine = store.engineForStress,
        version = version,
        videoDoUsuario = video,
        onProgress = onProgress,
    ).run()
} catch (t: Throwable) {
    // Sem isto, qualquer falha deixava a folha presa em "Rodando…" e o tester
    // ficava sem nada para mandar.
    "O TESTE NÃO TERMINOU\n\n${t::class.java.name}: ${t.message}\n\n${t.stackTraceToString()}"
}

@Composable
private fun Botao(
    texto: String,
    enabled: Boolean = true,
    modifier: Modifier = Modifier,
    secundario: Boolean = false,
    onClick: () -> Unit,
) {
    Row(
        modifier
            .clip(AureaShape.Md)
            .background(
                when {
                    !enabled -> AureaColors.SurfaceHigh
                    secundario -> AureaColors.Chip
                    else -> AureaColors.Accent
                }
            )
            .clickable(enabled = enabled) { onClick() }
            .padding(vertical = 12.dp),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            texto,
            style = AureaType.Button,
            color = when {
                !enabled -> AureaColors.Muted
                secundario -> AureaColors.Text
                else -> AureaColors.OnAccent
            },
        )
    }
}
