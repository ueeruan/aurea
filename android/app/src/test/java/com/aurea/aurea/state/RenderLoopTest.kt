package com.aurea.aurea.state

import android.view.Choreographer
import org.junit.Assert.assertEquals
import org.junit.Test

class RenderLoopTest {
    private class Scheduler : RenderLoop.Scheduler {
        val pending = mutableListOf<Choreographer.FrameCallback>()
        var delay = 0L
        override fun post(callback: Choreographer.FrameCallback, delayMs: Long) {
            pending.add(callback)
            delay = delayMs
        }
        override fun remove(callback: Choreographer.FrameCallback) { pending.removeAll { it === callback } }
        fun tick(time: Long) {
            val callbacks = pending.toList()
            pending.clear()
            callbacks.forEach { it.doFrame(time) }
        }
    }

    @Test fun wakingInsideIdleCallbackSchedulesOnlyOneSuccessor() {
        val scheduler = Scheduler()
        var frames = 0
        lateinit var loop: RenderLoop
        loop = RenderLoop({ frames++; loop.wake() }, scheduler)
        loop.start()
        loop.idleDelayMs = 250
        scheduler.tick(1)
        assertEquals(1, scheduler.pending.size)
        assertEquals(0L, scheduler.delay)
        repeat(20) { scheduler.tick((it + 2).toLong()) }
        assertEquals(21, frames)
        assertEquals(1, scheduler.pending.size)
        loop.stop()
        assertEquals(0, scheduler.pending.size)
    }

    @Test fun externalWakeReplacesDelayedCallbackAndStopInsideFrameDoesNotRepost() {
        val scheduler = Scheduler()
        lateinit var loop: RenderLoop
        var stop = false
        loop = RenderLoop({ if (stop) loop.stop() }, scheduler)
        loop.start()
        loop.idleDelayMs = 250
        scheduler.tick(1)
        assertEquals(250L, scheduler.delay)
        loop.wake()
        assertEquals(1, scheduler.pending.size)
        assertEquals(0L, scheduler.delay)
        stop = true
        scheduler.tick(2)
        assertEquals(0, scheduler.pending.size)
    }
}
