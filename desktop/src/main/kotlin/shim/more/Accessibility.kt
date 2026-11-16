@file:Suppress("unused")

package android.view.accessibility

class AccessibilityManager private constructor() {
    val isEnabled: Boolean get() = false
    val isTouchExplorationEnabled: Boolean get() = false
    companion object { val app = AccessibilityManager() }
}
