package com.aurea.aurea.diagnostics

import android.app.ActivityManager
import android.content.Context
import android.os.Build
import com.aurea.aurea.BuildConfig
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Quarentena de projeto: o app morreu com o motor lendo um projeto.
 *
 * Galaxy A32: um projeto que derrubava o export derrubou também o import do
 * `.aureaproj` e, depois, toda abertura do app. Um projeto que já matou o
 * processo não pode ser tocado de novo sem a pessoa pedir.
 *
 * Antes de cada etapa perigosa (abrir, importar, exportar vídeo, gravar o
 * arquivo do projeto) grava-se um marcador com o caminho; ao fim, ele sai. Na
 * abertura seguinte, marcador que sobrou + o Android dizendo que o processo
 * anterior terminou em CRASH/CRASH_NATIVE/ANR depois do marcador = o projeto
 * entra em quarentena: a Home não decodifica a capa dele nem o abre com um
 * toque — pergunta "Recuperar" (abre de novo, com o marcador de volta) ou
 * "Apagar". Morte por outro motivo (usuário fechou, falta de memória no fundo)
 * não é culpa do projeto: o marcador só é apagado. No Android 8–10 não há
 * histórico de encerramentos: o marcador que sobrou basta.
 *
 * Tudo em `files/crash/` (sobrevive a "limpar cache", como o problema).
 */
object ProjectGuard {
    enum class Stage(val phase: ExitDiagnostics.Phase) {
        OPEN(ExitDiagnostics.Phase.PROJECT_OPEN),
        IMPORT(ExitDiagnostics.Phase.PROJECT_IMPORT),
        EXPORT_VIDEO(ExitDiagnostics.Phase.PROJECT_EXPORT_VIDEO),
        EXPORT_FILE(ExitDiagnostics.Phase.PROJECT_EXPORT_FILE),
    }

    data class Mark(val stage: Stage, val path: String, val atMs: Long, val build: Int)

    /** Um projeto em quarentena (o que a Home mostra). */
    data class Quarantined(val path: String, val stage: Stage, val atMs: Long)

    /** Um encerramento do histórico do Android: motivo + quando (relógio de parede). */
    data class Exit(val reason: Int, val timestampMs: Long)

    private const val REASON_CRASH = 4          // ApplicationExitInfo.REASON_CRASH
    private const val REASON_CRASH_NATIVE = 5   // ApplicationExitInfo.REASON_CRASH_NATIVE
    private const val REASON_ANR = 6            // ApplicationExitInfo.REASON_ANR
    private const val MAX_QUARANTINE = 32

    // --- Parte pura (testada na JVM) -------------------------------------------------

    fun encode(m: Mark): String = JSONObject()
        .put("stage", m.stage.name).put("path", m.path).put("at", m.atMs).put("build", m.build).toString()

    fun decode(text: String?): Mark? = runCatching {
        val o = JSONObject(text ?: return null)
        val stage = Stage.valueOf(o.getString("stage"))
        val path = o.getString("path")
        if (path.isBlank()) return null
        Mark(stage, path, o.getLong("at"), o.optInt("build"))
    }.getOrNull()

    /**
     * O processo que deixou o marcador morreu DURANTE a etapa? `exits` = o
     * histórico do Android (vazio + sdk < 30 = sem histórico). 2 s de folga:
     * o relógio do marcador e o do sistema não são o mesmo carimbo.
     */
    fun diedDuring(mark: Mark, exits: List<Exit>, sdk: Int): Boolean {
        if (sdk < 30) return true
        return exits.any { e ->
            (e.reason == REASON_CRASH || e.reason == REASON_CRASH_NATIVE || e.reason == REASON_ANR) &&
                e.timestampMs >= mark.atMs - 2_000
        }
    }

    fun encodeList(list: List<Quarantined>): String = JSONArray().apply {
        list.forEach { put(JSONObject().put("path", it.path).put("stage", it.stage.name).put("at", it.atMs)) }
    }.toString()

    fun decodeList(text: String?): List<Quarantined> = runCatching {
        val a = JSONArray(text ?: return emptyList())
        (0 until a.length()).mapNotNull { i ->
            runCatching {
                val o = a.getJSONObject(i)
                Quarantined(o.getString("path"), Stage.valueOf(o.getString("stage")), o.optLong("at"))
            }.getOrNull()
        }
    }.getOrDefault(emptyList())

    /** Os marcadores e a lista numa pasta (a do app; nos testes, uma temporária). */
    class Store(private val dir: File) {
        private fun markFile(stage: Stage) = File(dir, "em_uso_${stage.name.lowercase()}.json")
        private val listFile get() = File(dir, "quarentena.json")

        fun begin(stage: Stage, path: String, nowMs: Long, build: Int) {
            runCatching {
                dir.mkdirs()
                writeAtomic(markFile(stage), encode(Mark(stage, path, nowMs, build)))
            }
        }

        fun end(stage: Stage) { runCatching { markFile(stage).delete() } }

        /**
         * Abertura do app: consome os marcadores que sobraram e devolve a
         * quarentena atual (só projetos que ainda existem).
         */
        fun onLaunch(exits: List<Exit>, sdk: Int, exists: (String) -> Boolean = { File(it).exists() }): List<Quarantined> {
            var list = read()
            var changed = false
            for (stage in Stage.entries) {
                val f = markFile(stage)
                if (!f.exists()) continue
                val mark = decode(runCatching { f.readText() }.getOrNull())
                runCatching { f.delete() }
                if (mark != null && diedDuring(mark, exits, sdk) && list.none { it.path == mark.path }) {
                    list = (list + Quarantined(mark.path, mark.stage, mark.atMs)).takeLast(MAX_QUARANTINE)
                    changed = true
                }
            }
            val alive = list.filter { exists(it.path) }
            if (changed || alive.size != list.size) save(alive)
            return alive
        }

        fun read(): List<Quarantined> = decodeList(runCatching { listFile.readText() }.getOrNull())

        fun release(path: String): List<Quarantined> {
            val left = read().filterNot { it.path == path }
            save(left)
            return left
        }

        private fun save(list: List<Quarantined>) {
            runCatching {
                dir.mkdirs()
                if (list.isEmpty()) listFile.delete() else writeAtomic(listFile, encodeList(list))
            }
        }

        private fun writeAtomic(target: File, text: String) {
            val tmp = File(target.path + ".tmp")
            tmp.writeText(text)
            if (!tmp.renameTo(target)) {
                target.delete()
                if (!tmp.renameTo(target)) tmp.delete()
            }
        }
    }

    // --- Android -------------------------------------------------------------------

    private fun store(context: Context) = Store(File(context.applicationContext.filesDir, "crash"))

    /** Etapas abertas neste processo (o marcador do SO mostra a mais recente). */
    private val active = LinkedHashSet<Stage>()

    /** Antes da etapa: marcador no disco + etapa no marcador do SO (crash por e-mail). */
    fun begin(context: Context, stage: Stage, path: String) {
        store(context).begin(stage, path, System.currentTimeMillis(), BuildConfig.VERSION_CODE)
        synchronized(active) { active.remove(stage); active.add(stage) }
        ExitDiagnostics.mark(context, stage.phase)
    }

    fun end(context: Context, stage: Stage) {
        store(context).end(stage)
        val still = synchronized(active) { active.remove(stage); active.lastOrNull() }
        ExitDiagnostics.mark(context, still?.phase ?: ExitDiagnostics.Phase.ENGINE_READY)
    }

    /** Na abertura, em IO. Nunca lança: um erro aqui não pode virar outro crash de abertura. */
    fun onLaunch(context: Context): List<Quarantined> = runCatching {
        store(context).onLaunch(exits(context), Build.VERSION.SDK_INT)
    }.getOrDefault(emptyList())

    fun release(context: Context, path: String): List<Quarantined> =
        runCatching { store(context).release(path) }.getOrDefault(emptyList())

    private fun exits(context: Context): List<Exit> {
        if (Build.VERSION.SDK_INT < 30) return emptyList()
        return runCatching {
            context.getSystemService(ActivityManager::class.java)
                ?.getHistoricalProcessExitReasons(context.packageName, 0, 4).orEmpty()
                .map { Exit(it.reason, it.timestamp) }
        }.getOrDefault(emptyList())
    }

    /** Linha do diagnóstico de fechamentos (sem caminho nem nome de projeto). */
    fun summary(context: Context): String = runCatching {
        val list = store(context).read()
        if (list.isEmpty()) "projetos em quarentena: nenhum"
        else "projetos em quarentena: ${list.size} (" + list.joinToString { "${it.stage.name}@${it.atMs}" } + ")"
    }.getOrDefault("projetos em quarentena: indisponível")
}
