package com.aurea.aurea.ai

import android.app.Application
import android.content.Context
import android.util.Log
import org.json.JSONObject
import java.io.File

/**
 * A geração da IA gravada no aparelho.
 *
 * Existe por causa do Rewarded: abrir um anúncio traz a Activity para trás e,
 * num aparelho de verdade com pouca memória, o sistema chega a matar o
 * processo. Sem isto, o job ia embora com o processo e a geração terminava
 * sozinha no servidor — o usuário ficava em "Gerando..." para sempre.
 *
 * O que é gravado é o mínimo para RETOMAR a MESMA geração: o id, o ticket, o
 * job, e o que já foi conquistado (recompensa, geração concluída). Nada de
 * segredo: ticket e job só valem com o id deste aparelho.
 *
 * Regra que não muda: retomar NUNCA pede outra geração. Com job, só se
 * ACOMPANHA; sem job, o ticket (que é idempotente no servidor) devolve o
 * mesmo job — gerar duas vezes seria pagar duas vezes pelo mesmo vídeo.
 */
class GuardaDaSessao(app: Application) {

    private val prefs = app.getSharedPreferences("aurea_ai_sessao", Context.MODE_PRIVATE)

    private val chave = "sessao"

    fun gravar(s: AiGenerationSession) {
        val o = JSONObject().apply {
            put("generationId", s.generationId)
            put("ticket", s.ticket)
            put("jobId", s.jobId)
            put("podeRepetirSemAnuncio", s.podeRepetirSemAnuncio)
            put("generationCompleted", s.generationCompleted)
            put("rewardEarned", s.rewardEarned)
            put("generationStarted", s.generationStarted)
            put("adClosedEarly", s.adClosedEarly)
            put("arquivo", s.result?.absolutePath)
            put("erro", s.erro)
            put("adError", s.adError)
            put("status", s.status.name)
            put("pedido", JSONObject().apply {
                put("modo", s.pedido.modo)
                put("prompt", s.pedido.prompt)
                put("promptNegativo", s.pedido.promptNegativo)
                put("duracao", s.pedido.duracao)
                put("aspecto", s.pedido.aspecto)
                put("resolucao", s.pedido.resolucao)
                put("fps", s.pedido.fps)
                put("audio", s.pedido.audio)
                put("semente", s.pedido.semente)
                put("turbo", s.pedido.turbo)
                put("assetId", s.pedido.assetId)
            })
        }
        prefs.edit().putString(chave, o.toString()).apply()
    }

    fun limpar() {
        prefs.edit().remove(chave).apply()
    }

    /** A sessão gravada, ou nulo quando não há nenhuma (ou a gravada está ilegível). */
    fun ler(): AiGenerationSession? {
        val texto = prefs.getString(chave, null) ?: return null
        return try {
            val o = JSONObject(texto)
            val p = o.getJSONObject("pedido")
            val caminho = o.optString("arquivo").ifBlank { null }
            val arquivo = caminho?.let { File(it) }
            val status = runCatching { SessaoStatus.valueOf(o.getString("status")) }
                .getOrDefault(SessaoStatus.Gerando)
            val repetivel = o.optBoolean("podeRepetirSemAnuncio")
            if (status == SessaoStatus.Liberado || (status == SessaoStatus.Falhou && !repetivel)) {
                // Já terminou da última vez: não há o que retomar.
                limpar()
                return null
            }
            AiGenerationSession(
                generationId = o.getString("generationId"),
                pedido = Pedido(
                    modo = p.getString("modo"),
                    prompt = p.getString("prompt"),
                    promptNegativo = p.optString("promptNegativo"),
                    duracao = p.optInt("duracao", 5),
                    aspecto = p.optString("aspecto", "16:9"),
                    resolucao = p.optString("resolucao", "standard"),
                    fps = p.optInt("fps", 24),
                    audio = p.optBoolean("audio", true),
                    semente = p.optInt("semente", -1),
                    turbo = p.optBoolean("turbo", true),
                    assetId = p.optString("assetId").ifBlank { null },
                ),
                ticket = o.optString("ticket").ifBlank { null },
                jobId = o.optString("jobId").ifBlank { null },
                podeRepetirSemAnuncio = repetivel,
                generationCompleted = o.optBoolean("generationCompleted"),
                rewardEarned = o.optBoolean("rewardEarned"),
                generationStarted = o.optBoolean("generationStarted"),
                adClosedEarly = o.optBoolean("adClosedEarly"),
                result = if (arquivo?.isFile == true) arquivo else null,
                erro = o.optString("erro").ifBlank { null },
                adError = o.optString("adError").ifBlank { null },
                status = status,
            )
        } catch (e: Exception) {
            Log.w("AureaAI", "[AUREA AI] ERROR = sessão gravada ilegível: $e")
            limpar()
            null
        }
    }
}
