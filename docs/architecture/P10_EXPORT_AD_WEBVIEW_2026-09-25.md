# Post-export crash — emulator diagnosis, 2026-09-25

Status: reproduced failure is in the Unity fullscreen advertisement's Chromium media session. Native export completed. No application fix or WebView upgrade has been validated; this is still an end-to-end post-export failure.

## Evidence

Read-only `adb logcat -d -v threadtime` and `adb shell dumpsys webviewupdate` on Aurea_API35:

- 17:48:50.181, PID 10845: `Engine.cpp:7087 export 63/63 (3 em voo): decode 85.0 render 3.0 leitura 0.8 encoder 14.1 ms/quadro`.
- 17:48:50.497: Android starts `com.aurea.aurea.debug/com.unity3d.ads.adplayer.FullScreenWebViewDisplay`; displayed at 17:48:50.802.
- 17:48:51.957: fatal exception on main thread, `org.chromium.base.JniAndroid$UncaughtExceptionException`. Native stack belongs to TrichromeLibrary. Cause: `java.lang.NullPointerException: Attempt to invoke virtual method 'void WV.GE.b()' on a null object reference` at `org.chromium.content.browser.MediaSessionImpl.mediaSessionPositionChanged(chromium-TrichromeWebViewGoogle6432.apk-stable-636771938:3)`.
- 17:48:51.967: Android force-finishes `FullScreenWebViewDisplay`.
- 17:48:53.497: restarted process PID 11455 initializes the LevelPlay provider.
- Active provider: `com.google.android.webview`, version `124.0.6367.219`, code `636771938`, target SDK 34. `dumpsys webviewupdate` reports it valid, enabled, and the sole installed provider.

## Application lifecycle inspection

`Exporter.publish()` first completes the MediaStore copy, then requests the export interstitial. Its success UI state is published after the advertisement callback. Therefore a fatal SDK/WebView exception can kill the UI after the video was already saved.

`LevelPlayAdsBackend.show()` rejects finishing/destroyed activities and uses the SDK's `showAd(activity)`. `release()` only clears pending load callbacks; it does **not** destroy the displayed advertisement, clear its show listener, or discard its reusable SDK object. `MainActivity.onPause()` only detaches the manager's weak activity reference. No app-owned destruction of the Unity WebView was found on this path. Fullscreen admission is guarded by the manager's existing flag.

Repository dependency declarations: LevelPlay 9.6.0, Unity Ads adapter 5.13.0. These are declarations, not a separately verified resolved dependency report.

## Attribution limits and next verification

The stack and activity establish the failing component: Chromium inside the Unity advertisement, after export. They do not establish whether the underlying defect is WebView alone, Unity's interaction with it, or a particular ad creative. A stale emulator WebView is a plausible compatibility factor, **not a proven root cause or proven fix**. The Chromium `mediaSessionPositionChanged` observer iteration also exists in current source, so source inspection alone cannot establish a fixed version:

- https://raw.githubusercontent.com/chromium/chromium/124.0.6367.219/content/public/android/java/src/org/chromium/content/browser/MediaSessionImpl.java
- https://raw.githubusercontent.com/chromium/chromium/main/content/public/android/java/src/org/chromium/content/browser/MediaSessionImpl.java

Next useful verification is the same export/ad lifecycle on an updated supported WebView and on a physical beta device, recording actual resolved SDK/provider versions and whether an ad displays/closes successfully. Repeated creative-dependent failures need an SDK reproducer. No SDK version, ad behavior, APK, emulator provider, or application source was changed during this investigation. An asynchronous fatal WebView JNI exception is outside the synchronous `showAd` try/catch.
