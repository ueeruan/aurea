package com.aurea.aurea.engine

import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.*
import org.junit.Test

class NativeWorkGateTest {
    @Test fun shutdownCancelsWorkerBeforeWaitingForItsLease() {
        val gate = NativeWorkGate()
        val workers = Executors.newFixedThreadPool(2)
        val entered = CountDownLatch(1)
        val cancelled = CountDownLatch(1)
        val exited = CountDownLatch(1)
        try {
            val work = workers.submit { gate.run(Unit) {
                entered.countDown()
                check(cancelled.await(5, TimeUnit.SECONDS))
                assertEquals(-1, gate.run(-1) { fail("new work admitted during shutdown"); 1 })
                exited.countDown()
            } }
            assertTrue(entered.await(5, TimeUnit.SECONDS))
            val close = workers.submit { gate.close(cancelOngoing = { cancelled.countDown() }) {
                assertEquals(0L, exited.count)
            } }
            work.get(5, TimeUnit.SECONDS)
            close.get(5, TimeUnit.SECONDS)
        } finally { cancelled.countDown(); workers.shutdownNow() }
    }
    @Test fun teardownWaitsForEncoderOpenAndRejectsLateWork() {
        val gate = NativeWorkGate()
        val workers = Executors.newFixedThreadPool(3)
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val closed = CountDownLatch(1)
        try {
            val start = workers.submit<Int> { gate.run(-1) {
                entered.countDown()
                check(release.await(5, TimeUnit.SECONDS))
                42
            } }
            assertTrue(entered.await(5, TimeUnit.SECONDS))
            // Cancellation can enter while encoder-open holds a lease.
            val cancel = workers.submit<Int> { gate.run(-1) { 7 } }
            assertEquals(7, cancel.get(5, TimeUnit.SECONDS))
            val shutdown = workers.submit { gate.close { closed.countDown() } }
            assertFalse("must not destroy an in-use native handle", closed.await(100, TimeUnit.MILLISECONDS))
            release.countDown()
            assertEquals(42, start.get(5, TimeUnit.SECONDS))
            shutdown.get(5, TimeUnit.SECONDS)
            assertEquals(-1, gate.run(-1) { fail("late work touched freed handle"); 42 })
        } finally {
            release.countDown()
            workers.shutdownNow()
        }
    }

    @Test fun thrownIoWorkReleasesItsNativeLease() {
        val gate = NativeWorkGate()
        runCatching { gate.run(0) { throw java.io.IOException("encoder unavailable") } }
        var destroyed = false
        gate.close { destroyed = true }
        assertTrue(destroyed)
        assertEquals(0, gate.run(0) { 9 })
    }
}
