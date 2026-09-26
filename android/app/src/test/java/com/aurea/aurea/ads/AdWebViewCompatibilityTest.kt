package com.aurea.aurea.ads

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AdWebViewCompatibilityTest {
    @Test fun reproducedProviderNeverInitializesLevelPlay() {
        assertFalse(AdWebViewCompatibility.allowsLevelPlay("124.0.6367.219"))
        assertFalse(AdWebViewCompatibility.allowsLevelPlay(" 124.0.6367.219 "))
        assertFalse(AdWebViewCompatibility.allowsLevelPlay("124.0.6367.219 (stable)"))
    }
    @Test fun missingProviderDoesNotInitializeSdk() {
        assertFalse(AdWebViewCompatibility.allowsLevelPlay(null))
        assertFalse(AdWebViewCompatibility.allowsLevelPlay(" "))
    }
    @Test fun otherVersionsAreNotSilentlyDisabled() {
        assertTrue(AdWebViewCompatibility.allowsLevelPlay("140.0.7339.0"))
        assertTrue(AdWebViewCompatibility.allowsLevelPlay("124.0.6367.220"))
    }
}
