package com.aurea.pc

import java.awt.KeyboardFocusManager
import java.awt.event.KeyEvent

/** Shift/Ctrl/Alt pressionados agora (o passo dos números arrastados da timeline). */
object PcKeys {
    @Volatile var shift = false; private set
    @Volatile var ctrl = false; private set
    @Volatile var alt = false; private set

    fun install() {
        KeyboardFocusManager.getCurrentKeyboardFocusManager().addKeyEventDispatcher { e ->
            shift = e.isShiftDown; ctrl = e.isControlDown; alt = e.isAltDown
            if (e.id == KeyEvent.KEY_RELEASED) when (e.keyCode) {
                KeyEvent.VK_SHIFT -> shift = false
                KeyEvent.VK_CONTROL -> ctrl = false
                KeyEvent.VK_ALT -> alt = false
            }
            false
        }
    }
}
