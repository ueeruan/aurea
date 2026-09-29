package com.aurea.aurea.diagnostics

import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class ProjectGuardTest {
    @get:Rule val tmp = TemporaryFolder()

    private val crash = 4
    private val native = 5
    private val anr = 6
    private val userStopped = 10
    private val lowMemory = 3

    @Test fun markRoundTripsAndGarbageIsIgnored() {
        val m = ProjectGuard.Mark(ProjectGuard.Stage.EXPORT_VIDEO, "/p/Clipe.aurea", 1_000L, 2126)
        assertEquals(m, ProjectGuard.decode(ProjectGuard.encode(m)))
        for (junk in listOf(null, "", "{}", "lixo", """{"stage":"NOPE","path":"/a","at":1}""", """{"stage":"OPEN","path":"","at":1}"""))
            assertNull(junk, ProjectGuard.decode(junk))
    }

    @Test fun onlyACrashAfterTheMarkQuarantines() {
        val m = ProjectGuard.Mark(ProjectGuard.Stage.IMPORT, "/p/a.aurea", 100_000L, 1)
        assertTrue(ProjectGuard.diedDuring(m, listOf(ProjectGuard.Exit(native, 150_000L)), 33))
        assertTrue(ProjectGuard.diedDuring(m, listOf(ProjectGuard.Exit(crash, 99_000L)), 33))   // folga de 2 s
        assertTrue(ProjectGuard.diedDuring(m, listOf(ProjectGuard.Exit(anr, 120_000L)), 33))
        // Morte que não é culpa do projeto, ou crash ANTES do marcador: não.
        assertFalse(ProjectGuard.diedDuring(m, listOf(ProjectGuard.Exit(userStopped, 150_000L)), 33))
        assertFalse(ProjectGuard.diedDuring(m, listOf(ProjectGuard.Exit(lowMemory, 150_000L)), 33))
        assertFalse(ProjectGuard.diedDuring(m, listOf(ProjectGuard.Exit(native, 10_000L)), 33))
        assertFalse(ProjectGuard.diedDuring(m, emptyList(), 33))
        // Android 8–10 não tem histórico: o marcador que sobrou basta.
        assertTrue(ProjectGuard.diedDuring(m, emptyList(), 29))
    }

    @Test fun storeQuarantinesACrashedImportOnceAndReleases() {
        val store = ProjectGuard.Store(tmp.newFolder("crash"))
        val path = "/p/Importado.aurea"
        store.begin(ProjectGuard.Stage.IMPORT, path, 1_000L, 7)
        // Próxima abertura: o processo anterior morreu em crash nativo.
        val list = store.onLaunch(listOf(ProjectGuard.Exit(native, 2_000L)), 33) { true }
        assertEquals(1, list.size)
        assertEquals(path, list[0].path)
        assertEquals(ProjectGuard.Stage.IMPORT, list[0].stage)
        // O marcador foi consumido: a abertura seguinte não duplica.
        assertEquals(list, store.onLaunch(listOf(ProjectGuard.Exit(native, 2_000L)), 33) { true })
        // Projeto apagado por fora: sai da lista.
        assertTrue(store.onLaunch(emptyList(), 33) { false }.isEmpty())
    }

    @Test fun cleanEndOrNonCrashExitLeavesNoQuarantine() {
        val store = ProjectGuard.Store(tmp.newFolder("crash2"))
        store.begin(ProjectGuard.Stage.OPEN, "/p/a.aurea", 1_000L, 7)
        store.end(ProjectGuard.Stage.OPEN)
        assertTrue(store.onLaunch(listOf(ProjectGuard.Exit(native, 5_000L)), 33) { true }.isEmpty())
        store.begin(ProjectGuard.Stage.EXPORT_VIDEO, "/p/b.aurea", 1_000L, 7)
        assertTrue(store.onLaunch(listOf(ProjectGuard.Exit(userStopped, 5_000L)), 33) { true }.isEmpty())
    }

    @Test fun releaseTakesTheProjectOutAndCorruptListIsEmpty() {
        val dir = tmp.newFolder("crash3")
        val store = ProjectGuard.Store(dir)
        store.begin(ProjectGuard.Stage.OPEN, "/p/a.aurea", 1_000L, 7)
        store.begin(ProjectGuard.Stage.EXPORT_FILE, "/p/b.aurea", 1_000L, 7)
        assertEquals(2, store.onLaunch(listOf(ProjectGuard.Exit(crash, 3_000L)), 33) { true }.size)
        assertEquals(listOf("/p/b.aurea"), store.release("/p/a.aurea").map { it.path })
        java.io.File(dir, "quarentena.json").writeText("{{{ lixo")
        assertTrue(store.read().isEmpty())
    }
}
