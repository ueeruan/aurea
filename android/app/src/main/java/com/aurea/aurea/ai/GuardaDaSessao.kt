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
 * processo. Sem isto, o `prompt_id` ia embora com o processo e o H3 terminava
 * sozinho do outro lado — o usuário ficava em "Gerando..." para sempre.
 *
 * O que é gravado é o mínimo para RETOMAR a MESMA geração: o id, o prompt_id, o
 * endereço que a aceitou, e o que já foi conquistado (geração concluída,
 * recompensa). Nada de segredo: `prompt_id` e endereço não abrem nada sozinhos.
 *
 * Regra que não muda: retomar NUNCA manda outro `POST /prompt`. Ou a geração é
 * acompanhada pelo prompt_id que já existe, ou não é acompanhada — repetir o
 * POST geraria um segundo job e cobraria a A100 duas vezes pelo mesmo vídeo.
 */
class GuardaDaSessao(app: Application) {

    private val prefs = app.getSharedPreferences("aurea_ai_sessao", Context.MODE_PRIVATE)

    private val chave = "sessao"

    fun gravar(s: AiGenerationSession) {
        val o = JSONObject().apply {
            put("generationId", s.generationId)
            put("promptId", s.promptId)
            put("endpointUsado", s.endpointUsado)
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
            if (status == SessaoStatus.Liberado || status == SessaoStatus.Falhou) {
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
                promptId = o.optString("promptId").ifBlank { null },
                endpointUsado = o.optString("endpointUsado").ifBlank { null },
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
