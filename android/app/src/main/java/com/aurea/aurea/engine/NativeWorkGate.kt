package com.aurea.aurea.engine

import java.util.concurrent.locks.ReentrantReadWriteLock
import kotlin.concurrent.read
import kotlin.concurrent.write

/** Keeps asynchronous JNI calls alive until they release their native handle. */
internal class NativeWorkGate {
    private val lock = ReentrantReadWriteLock()
    @Volatile private var stopped = false

    fun <T> run(fallback: T, work: () -> T): T = lock.read {
        if (stopped) fallback else work()
    }

    // A read lock permits a cancellation request while an encoder is opening.
    // Teardown waits for both calls, then permanently rejects late IO work.
    fun close(cancelOngoing: () -> Unit = {}, teardown: () -> Unit) {
        // Reject queued imports immediately, even while an older worker still owns a lease.
        stopped = true
        try {
            lock.read { cancelOngoing() }
        } finally {
            lock.write { teardown() }
        }
    }
}
