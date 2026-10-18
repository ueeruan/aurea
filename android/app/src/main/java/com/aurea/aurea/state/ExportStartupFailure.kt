package com.aurea.aurea.state

import com.aurea.aurea.engine.ExportProgress

/** Admission can fail before replacing the previous native export session. */
internal object ExportStartupFailure {
    data class Snapshot(val running: Boolean, val finished: Boolean, val result: Int,
                        val total: Int, val done: Int, val fps: Float, val eta: Int,
                        val flags: Int, val message: String)

    fun snapshot(p: ExportProgress) = Snapshot(p.running, p.finished, p.result,
        p.framesTotal, p.framesDone, p.fps, p.etaSeconds, p.flags, p.message)

    fun isFresh(previous: Snapshot?, candidate: ExportProgress, code: Int,
                previousToken: String, candidateToken: String): Boolean {
        if (!candidate.finished || candidate.result != code) return false
        // A new session may fail with exactly the same message and flags.
        // A valid unchanged token identifies old progress even if it changed.
        if (previousToken.isNotBlank() && candidateToken.isNotBlank()) return previousToken != candidateToken
        return previous == null || previous != snapshot(candidate)
    }
}
