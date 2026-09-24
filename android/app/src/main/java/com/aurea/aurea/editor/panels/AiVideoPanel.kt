package com.aurea.aurea.editor.panels

import android.app.Application
import android.media.MediaPlayer
import android.net.Uri
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import com.aurea.aurea.R
import com.aurea.aurea.ai.AureaAiEstado
import com.aurea.aurea.ai.SessaoStatus
import com.aurea.aurea.ai.Pedido
import com.aurea.aurea.ai.explicarFalhaDeVideo
import com.aurea.aurea.ai.formarDuracao
import com.aurea.aurea.ai.lerImagemParaEnvio
import com.aurea.aurea.ai.podeGerar
import com.aurea.aurea.ai.recursoDoModo
import com.aurea.aurea.ai.rotulo
import com.aurea.aurea.ui.theme.AureaColors
import com.aurea.aurea.ui.theme.AureaType
import com.aurea.aurea.ui.theme.tocavel
import kotlinx.coroutines.launch

/**
 * AI VIDEO — geração remota (MiniMax H3 numa A100, via Colab).
 *
 * A tela não mostra endereço, IP, porta, túnel, ComfyUI nem Colab. O que ela
 * mostra é o estado ("● Aurea AI / Conectado"), o que o servidor sabe fazer e
 * o progresso. Quando o vídeo fica pronto, ele entra na timeline como qualquer
 * outro vídeo importado.
 */
@Composable
internal fun AiVideoPanel(env: PanelEnv) {
    val store = env.store
    val ai = store.aureaAi
    val app = LocalContext.current.applicationContext as Application
    val escopo = rememberCoroutineScope()

    // A capacidade vem do servidor: nada de oferecer duração que ele recusa.
    val caps = ai.capacidades
    var modo by remember { mutableStateOf("text_to_video") }
    var prompt by remember { mutableStateOf("") }
    var negativo by remember { mutableStateOf("") }
    var avancado by remember { mutableStateOf(false) }
    var duracao by remember { mutableStateOf(0) }
    var aspecto by remember { mutableStateOf(0) }
    var resolucao by remember { mutableStateOf(1) }
    var audio by remember { mutableStateOf(true) }
    var turbo by remember { mutableStateOf(true) }
    var assetId by remember { mutableStateOf<String?>(null) }
    var nomeImagem by remember { mutableStateOf("") }
    var enviandoImagem by remember { mutableStateOf(false) }

    // Abriu o painel, o app já sai atrás do servidor. Não há nada a confirmar
    // antes: nem endereço, nem token, nem botão de conectar.
    LaunchedEffect(Unit) {
        ai.conectar()
        // O Rewarded carrega já ao entrar: no toque em "Gerar" ele está pronto.
        ai.prepararAnuncio()
    }

    // Ajusta a escolha quando as capacidades chegam (ou mudam de servidor).
    LaunchedEffect(caps) {
        if (caps.duracoes.isNotEmpty() && duracao >= caps.duracoes.size) duracao = 0
        if (caps.aspectos.isNotEmpty() && aspecto >= caps.aspectos.size) aspecto = 0
        if (caps.resolucoes.isNotEmpty() && resolucao >= caps.resolucoes.size) resolucao = 1
        if (caps.duracoes.isNotEmpty()) duracao = duracao.coerceIn(0, caps.duracoes.size - 1)
        if (caps.aspectos.isNotEmpty()) aspecto = aspecto.coerceIn(0, caps.aspectos.size - 1)
        if (caps.resolucoes.isNotEmpty()) resolucao = resolucao.coerceIn(0, caps.resolucoes.size - 1)
        if (!caps.temImagemParaVideo()) modo = "text_to_video"
    }

    val escolherImagem = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenDocument(),
    ) { uri: Uri? ->
        if (uri == null) return@rememberLauncherForActivityResult
        escopo.launch {
            enviandoImagem = true
            val par = lerImagemParaEnvio(app, uri)
            if (par == null) {
                enviandoImagem = false
                return@launch
            }
            // Sobe já: o servidor devolve o UUID que o pedido vai citar.
            val id = runCatching { ai.enviarImagem(par.first, par.second) }.getOrNull()
            assetId = id
            nomeImagem = if (id != null) uri.lastPathSegment?.takeLast(28) ?: "imagem" else ""
            enviandoImagem = false
        }
    }

    LazyColumn(
        Modifier.fillMaxSize(),
        contentPadding = PaddingValues(horizontal = 18.dp, vertical = 10.dp),
    ) {
        item(key = "estado") {
            Column {
                Etiqueta(ai.estado, ai.modelo, ai.gpu)
                if (ai.estado.podeGerar()) Nota(stringResource(R.string.ai_aviso_online), AureaColors.Accent)
                ai.erro.takeIf { it.isNotBlank() }?.let { Nota(it, AureaColors.Danger) }
                ai.mensagem.takeIf { it.isNotBlank() && ai.estado.podeGerar() }?.let { Nota(it, AureaColors.Muted) }
            }
        }

        // Offline: nada de formulário. Um prompt que não tem para onde ir só
        // faz o usuário escrever à toa.
        if (!ai.estado.podeGerar()) {
            item(key = "offline") {
                Column {
                    Spacer(Modifier.height(8.dp))
                    Nota(stringResource(R.string.ai_offline_corpo), AureaColors.Muted)
                    Spacer(Modifier.height(10.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Botao(stringResource(R.string.ai_procurar_de_novo)) { ai.conectar() }
                    }
                }
            }
            return@LazyColumn
        }

        // --- formulário -----------------------------------------------------
        item(key = "modo") {
            Column {
                Rotulo(stringResource(R.string.ai_modo))
                val ctx = LocalContext.current
                val rotulos = caps.modos.map { m ->
                    val r = recursoDoModo(m)
                    if (r != 0) ctx.getString(r) else m
                }
                Faixa(rotulos, caps.modos.indexOf(modo),
                    habilitado = ai.job?.rodando != true) { modo = caps.modos[it] }
            }
        }

        if (modo == "image_to_video") {
            item(key = "imagem") {
                Column {
                    Rotulo(stringResource(R.string.ai_imagem_de_partida))
                    Row(
                        Modifier.fillMaxWidth().height(44.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        Botao(
                            if (assetId == null) stringResource(R.string.ai_escolher_imagem)
                            else stringResource(R.string.ai_trocar_imagem),
                            ativo = !enviandoImagem && ai.job?.rodando != true,
                        ) { escolherImagem.launch(arrayOf("image/png", "image/jpeg", "image/webp")) }
                        Text(
                            when {
                                enviandoImagem -> stringResource(R.string.ai_enviando)
                                assetId != null -> nomeImagem
                                else -> stringResource(R.string.ai_nenhuma_imagem)
                            },
                            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Subtle)),
                        )
                    }
                }
            }
        }

        item(key = "prompt") {
            Column {
                Rotulo(stringResource(R.string.ai_prompt))
                Campo(prompt, stringResource(R.string.ai_prompt_dica), altura = 88.dp) { prompt = it.take(ai.promptMax) }
                Spacer(Modifier.height(4.dp))
                if (avancado) {
                    Rotulo(stringResource(R.string.ai_prompt_negativo))
                    Campo(negativo, stringResource(R.string.ai_prompt_negativo_dica), altura = 56.dp) { negativo = it }
                }
            }
        }

        item(key = "opcoes") {
            Column {
                if (caps.duracoes.isNotEmpty()) {
                    Rotulo(stringResource(R.string.ai_duracao))
                    Faixa(caps.duracoes.map { "${it}s" },
                        duracao, habilitado = ai.job?.rodando != true) { duracao = it }
                }
                if (caps.aspectos.isNotEmpty()) {
                    Rotulo(stringResource(R.string.ai_proporcao))
                    Faixa(caps.aspectos, aspecto, habilitado = ai.job?.rodando != true) { aspecto = it }
                }
                if (caps.resolucoes.isNotEmpty()) {
                    Rotulo(stringResource(R.string.ai_qualidade))
                    Faixa(caps.resolucoes.map { nomeDaResolucao(it) }, resolucao,
                        habilitado = ai.job?.rodando != true) { resolucao = it }
                }
                if (caps.audio) {
                    Interruptor(stringResource(R.string.ai_com_audio), audio,
                        habilitado = ai.job?.rodando != true) { audio = it }
                }
                Interruptor(stringResource(R.string.ai_avancado), avancado) { avancado = it }
            }
        }

        item(key = "gerar") {
            val rodando = ai.job?.rodando == true
            val pode = !rodando && !ai.anunciando && !ai.sessaoOcupada && prompt.isNotBlank() &&
                (modo != "image_to_video" || assetId != null)
            Column {
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Botao(
                        stringResource(R.string.ai_assistir_e_gerar),
                        primario = true, ativo = pode,
                    ) {
                        ai.gerarComRecompensa(
                            Pedido(
                                modo = modo,
                                prompt = prompt.trim(),
                                promptNegativo = negativo.trim(),
                                duracao = caps.duracoes.getOrElse(duracao) { 5 },
                                aspecto = caps.aspectos.getOrElse(aspecto) { "16:9" },
                                resolucao = caps.resolucoes.getOrElse(resolucao) { caps.resolucoes.firstOrNull() ?: "480p" },
                                fps = caps.fps.firstOrNull() ?: 24,
                                audio = audio,
                                turbo = turbo,
                                assetId = if (modo == "image_to_video") assetId else null,
                            ),
                        )
                    }
                    if (rodando) Botao(stringResource(R.string.ai_cancelar), secundario = true) { ai.cancelar() }
                }
                if (ai.anunciando) {
                    Spacer(Modifier.height(6.dp))
                    Nota(stringResource(R.string.ai_anuncio_em_curso), AureaColors.Muted)
                }
            }
        }

        // --- direito ao vídeo (Rewarded) -------------------------------------
        ai.sessao?.let { s ->
            item(key = "recompensa") {
                Column {
                    when (s.status) {
                        SessaoStatus.Preparando ->
                            Nota(stringResource(R.string.ai_preparando_geracao), AureaColors.Muted)
                        SessaoStatus.AnuncioIndisponivel -> {
                            Nota(stringResource(R.string.ai_anuncio_indisponivel), AureaColors.Danger)
                            Botao(stringResource(R.string.ai_procurar_de_novo)) { ai.tentarGerarDeNovo() }
                        }
                        SessaoStatus.AnuncioNaTela ->
                            Nota(stringResource(R.string.ai_assista_para_gerar), AureaColors.Muted)
                        SessaoStatus.SemRecompensa -> {
                            // Nada foi gerado: o pedido espera um anúncio completo.
                            Nota(stringResource(R.string.ai_assista_para_gerar), AureaColors.Muted)
                            Botao(stringResource(R.string.ai_assistir_e_gerar_de_novo), primario = true,
                                ativo = !ai.anunciando) { ai.liberarComAnuncio() }
                        }
                        SessaoStatus.Gerando ->
                            Nota(stringResource(R.string.ai_gerando_seu_video), AureaColors.Muted)
                        SessaoStatus.Falhou -> {
                            Spacer(Modifier.height(10.dp))
                            Text(stringResource(R.string.ai_falhou_titulo), style = AureaType.Base.merge(
                                TextStyle(fontSize = 15.sp, fontWeight = FontWeight.W600, color = AureaColors.Danger)))
                            Nota(explicarFalhaDeVideo(s.erro), AureaColors.Muted)
                            Spacer(Modifier.height(6.dp))
                            // Falha técnica depois da recompensa: tenta de novo SEM outro anúncio.
                            Botao(
                                stringResource(if (s.podeRepetirSemAnuncio) R.string.ai_tentar_sem_anuncio else R.string.ai_gerar_de_novo),
                                primario = true, ativo = !ai.anunciando && !ai.sessaoOcupada,
                            ) { ai.tentarDeNovoAposFalha() }
                        }
                        SessaoStatus.Liberado -> Unit
                    }
                }
            }
        }

        // --- progresso ------------------------------------------------------
        ai.job?.takeIf { ai.sessao?.status != SessaoStatus.Falhou }?.let { j ->
            item(key = "progresso") {
                Column {
                    Spacer(Modifier.height(10.dp))
                    val linha = when {
                        j.status == "queued" && j.posicaoNaFila > 0 ->
                            stringResource(R.string.ai_na_fila, j.posicaoNaFila)
                        else -> j.etapa.ifBlank { j.status }
                    }
                    Text(linha, style = AureaType.Base.merge(
                        TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = AureaColors.Accent)))
                    // O ComfyUI não dá porcentagem pelo /history: a barra só aparece
                    // quando há progresso real; senão, a etapa e o tempo decorrido.
                    if (j.progresso > 0.0) {
                        Spacer(Modifier.height(6.dp))
                        Barra(j.progresso)
                    }
                    Spacer(Modifier.height(4.dp))
                    Text(
                        (if (j.progresso > 0.0) "${(j.progresso * 100).toInt()}%  ·  " else "") + formarDuracao(j.segundos),
                        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Subtle)),
                    )
                }
            }
        }

        // --- resultado ------------------------------------------------------
        if (ai.ultimoArquivo != null) {
            item(key = "resultado") {
                Column {
                    Spacer(Modifier.height(12.dp))
                    ai.job?.resultado?.let { r ->
                        Text(
                            "${r.largura}×${r.altura} · ${formarDuracao(r.duracaoSegundos)} · ${r.fps} fps" +
                                if (r.comAudio) " · " + stringResource(R.string.ai_com_audio_curto) else "",
                            style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Muted)),
                        )
                    }
                    Spacer(Modifier.height(6.dp))
                    // Player do aparelho, sem biblioteca nova.
                    Player(ai.ultimoArquivo!!.absolutePath)
                    Spacer(Modifier.height(8.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        Botao(stringResource(R.string.ai_adicionar_timeline), primario = true) {
                            ai.adicionarNaTimeline()
                            env.onClose()
                        }
                        Botao(stringResource(R.string.ai_salvar_galeria), secundario = true) { ai.salvarNaGaleria() }
                    }
                    Nota(stringResource(R.string.ai_ficou_no_aparelho), AureaColors.Subtle)
                }
            }
        } else if (ai.baixando) {
            item(key = "baixando") {
                Nota(stringResource(R.string.ai_baixando), AureaColors.Muted)
            }
        }

        // --- histórico ------------------------------------------------------
        if (ai.historico.isNotEmpty() && ai.job == null) {
            item(key = "historico_titulo") { Rotulo(stringResource(R.string.ai_historico)) }
            items(ai.historico.size) { i ->
                val h = ai.historico[i]
                Column(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
                    Row(Modifier.fillMaxWidth().height(40.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            h.etapa.ifBlank { h.status },
                            modifier = Modifier.weight(1f),
                            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp)),
                        )
                        Botao(
                            if (h.rodando) stringResource(R.string.ai_acompanhar)
                            else stringResource(R.string.ai_abrir),
                            secundario = true,
                        ) { ai.tocar(h) }
                    }
                }
            }
        }

        item(key = "rodape") {
            Column {
                Spacer(Modifier.height(14.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Botao(stringResource(R.string.ai_atualizar)) { ai.atualizarHistorico() }
                }
                if (caps.fila > 0) {
                    Nota(stringResource(R.string.ai_fila_do_servidor, caps.fila), AureaColors.Subtle)
                }
                Spacer(Modifier.height(24.dp))
            }
        }
    }
}

private fun nomeDaResolucao(r: String) = when (r) {
    "preview" -> "Rascunho"
    "standard" -> "Padrão"
    "high" -> "Alta"
    else -> r
}

// ---------------------------------------------------------------------------
// Peças da tela
// ---------------------------------------------------------------------------

@Composable
private fun Etiqueta(estado: AureaAiEstado, modelo: String, gpu: String) {
    val cor = when (estado) {
        AureaAiEstado.Connected, AureaAiEstado.Generating -> AureaColors.Success
        AureaAiEstado.Checking, AureaAiEstado.Reconnecting -> AureaColors.Warning
        AureaAiEstado.Error -> AureaColors.Danger
        AureaAiEstado.Disconnected -> AureaColors.Subtle
    }
    // Círculo cheio quando está no ar, vazado quando não está: dá para ler o
    // estado de longe, sem ler a palavra.
    val bola = if (estado.podeGerar()) "●" else "○"
    Column {
        Row(Modifier.fillMaxWidth().height(28.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(bola, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = cor)))
            Text(
                "  Aurea AI • ${estado.rotulo()}",
                style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = cor)),
            )
        }
        // "MiniMax H3 • A100": o que o discovery anunciou, só quando online.
        val detalhe = listOf(modelo, gpu).filter { it.isNotBlank() }.joinToString(" • ")
        if (estado.podeGerar() && detalhe.isNotEmpty()) {
            Text(
                detalhe,
                style = AureaType.Base.merge(TextStyle(fontSize = 11.sp, color = AureaColors.Subtle)),
            )
        }
    }
}

@Composable
private fun Nota(texto: String, cor: Color) {
    Text(texto, modifier = Modifier.padding(vertical = 5.dp),
        style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = cor)))
}

@Composable
private fun Rotulo(texto: String) {
    Spacer(Modifier.height(8.dp))
    Text(texto, style = AureaType.Base.merge(
        TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W700, color = AureaColors.Muted)))
}

@Composable
private fun Faixa(opcoes: List<String>, escolhido: Int, habilitado: Boolean = true, aoEscolher: (Int) -> Unit) {
    Row(
        Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).height(44.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        opcoes.forEachIndexed { i, o ->
            val on = i == escolhido
            Box(
                Modifier.clip(RoundedCornerShape(8.dp))
                    .background(if (on) AureaColors.AccentDim else AureaColors.Chip)
                    .tocavel(onClick = { if (habilitado) aoEscolher(i) })
                    .padding(horizontal = 10.dp, vertical = 6.dp),
            ) {
                Text(o, style = AureaType.Base.merge(TextStyle(fontSize = 12.sp,
                    color = if (on) AureaColors.Accent else if (habilitado) AureaColors.Text else AureaColors.Muted)))
            }
        }
    }
}

@Composable
private fun Botao(texto: String, primario: Boolean = false, secundario: Boolean = false, ativo: Boolean = true, aoTocar: () -> Unit) {
    val fundo = when {
        !ativo -> AureaColors.Chip.copy(alpha = 0.4f)
        primario -> AureaColors.AccentDim
        secundario -> AureaColors.ChipHigh
        else -> AureaColors.Chip
    }
    val cor = when {
        !ativo -> AureaColors.Muted
        primario -> AureaColors.Accent
        else -> AureaColors.Text
    }
    Box(
        Modifier.clip(RoundedCornerShape(10.dp)).background(fundo)
            .tocavel(onClick = { if (ativo) aoTocar() }).padding(horizontal = 14.dp, vertical = 10.dp),
    ) {
        Text(texto, style = AureaType.Base.merge(
            TextStyle(fontSize = 13.sp, fontWeight = FontWeight.W600, color = cor)))
    }
}

@Composable
private fun Interruptor(texto: String, ligado: Boolean, habilitado: Boolean = true, aoMudar: (Boolean) -> Unit) {
    Row(
        Modifier.fillMaxWidth().height(44.dp).tocavel { if (habilitado) aoMudar(!ligado) },
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(texto, modifier = Modifier.weight(1f),
            style = AureaType.Base.merge(TextStyle(fontSize = 13.sp,
                color = if (habilitado) AureaColors.Text else AureaColors.Muted)))
        Box(
            Modifier.clip(RoundedCornerShape(999.dp))
                .background(if (ligado && habilitado) AureaColors.AccentDim else AureaColors.Chip)
                .padding(horizontal = 12.dp, vertical = 5.dp),
        ) {
            Text(if (ligado) stringResource(R.string.ai_sim) else stringResource(R.string.ai_nao),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp,
                    color = if (ligado && habilitado) AureaColors.Accent else AureaColors.Muted)))
        }
    }
}

/** Campo de texto com a mesma moldura dos outros painéis. */
@Composable
private fun Campo(valor: String, dica: String, altura: androidx.compose.ui.unit.Dp = 44.dp, aoMudar: (String) -> Unit) {
    Box(
        Modifier.fillMaxWidth().height(altura).clip(RoundedCornerShape(10.dp))
            .background(AureaColors.Chip).padding(horizontal = 10.dp, vertical = 8.dp),
    ) {
        if (valor.isEmpty()) {
            Text(dica, style = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Subtle)))
        }
        BasicTextField(
            value = valor,
            onValueChange = aoMudar,
            textStyle = AureaType.Base.merge(TextStyle(fontSize = 13.sp, color = AureaColors.Text)),
            cursorBrush = SolidColor(AureaColors.Accent),
            modifier = Modifier.fillMaxWidth(),
        )
    }
}

@Composable
private fun Barra(fracao: Double) {
    Box(Modifier.fillMaxWidth().height(6.dp).clip(RoundedCornerShape(999.dp)).background(AureaColors.Chip)) {
        Box(
            Modifier.fillMaxWidth(fracao.coerceIn(0.0, 1.0).toFloat())
                .height(6.dp).clip(RoundedCornerShape(999.dp)).background(AureaColors.Accent),
        )
    }
}

/**
 * Player do vídeo gerado, com `MediaPlayer` e `SurfaceView` do próprio Android.
 *
 * Não entra ExoPlayer no APK por causa de uma prévia: o arquivo é um MP4
 * local, que é exatamente o que o `MediaPlayer` toca desde a API 1.
 */
@Composable
private fun Player(caminho: String) {
    var pronto by remember { mutableStateOf(false) }
    val media = remember { MediaPlayer() }

    DisposableEffect(caminho) {
        onDispose {
            runCatching { if (media.isPlaying) media.stop() }
            runCatching { media.release() }
        }
    }

    Box(
        Modifier.fillMaxWidth().height(180.dp).clip(RoundedCornerShape(10.dp))
            .background(AureaColors.Stage).tocavel {
                if (!pronto) return@tocavel
                if (media.isPlaying) media.pause() else media.start()
            },
    ) {
        AndroidView(
            factory = { ctx ->
                SurfaceView(ctx).also { vista ->
                    vista.holder.addCallback(object : SurfaceHolder.Callback {
                        override fun surfaceCreated(holder: SurfaceHolder) {
                            runCatching {
                                media.setDataSource(caminho)
                                media.setDisplay(holder)
                                media.setOnPreparedListener {
                                    pronto = true
                                    it.isLooping = true
                                    it.start()
                                }
                                media.prepareAsync()
                            }
                        }

                        override fun surfaceChanged(h: SurfaceHolder, f: Int, w: Int, hh: Int) = Unit
                        override fun surfaceDestroyed(h: SurfaceHolder) {
                            runCatching { media.reset() }
                            pronto = false
                        }
                    })
                }
            },
            modifier = Modifier.fillMaxSize(),
        )
        if (!pronto) {
            Text(
                stringResource(R.string.ai_preparando_previa),
                modifier = Modifier.align(Alignment.Center),
                style = AureaType.Base.merge(TextStyle(fontSize = 12.sp, color = AureaColors.Subtle)),
            )
        }
    }
}
