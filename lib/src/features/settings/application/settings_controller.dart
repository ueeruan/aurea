import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/storage/prefs.dart';

/// Preferencias do app, persistidas em SharedPreferences.
class AppSettings {
  const AppSettings({
    this.defaultAspectKey = '16:9',
    this.defaultFps = 30,
    this.defaultResolution = 1080,
    this.saveToGallery = true,
    this.hapticFeedback = true,
  });

  /// Valores padrao usados ao criar um projeto novo.
  final String defaultAspectKey;
  final int defaultFps;
  final int defaultResolution;

  /// Ao exportar, salvar copia na galeria do dispositivo.
  final bool saveToGallery;

  final bool hapticFeedback;

  AppSettings copyWith({
    String? defaultAspectKey,
    int? defaultFps,
    int? defaultResolution,
    bool? saveToGallery,
    bool? hapticFeedback,
  }) {
    return AppSettings(
      defaultAspectKey: defaultAspectKey ?? this.defaultAspectKey,
      defaultFps: defaultFps ?? this.defaultFps,
      defaultResolution: defaultResolution ?? this.defaultResolution,
      saveToGallery: saveToGallery ?? this.saveToGallery,
      hapticFeedback: hapticFeedback ?? this.hapticFeedback,
    );
  }
}

class SettingsController extends Notifier<AppSettings> {
  static const _kAspect = 'settings.defaultAspect';
  static const _kFps = 'settings.defaultFps';
  static const _kResolution = 'settings.defaultResolution';
  static const _kSaveToGallery = 'settings.saveToGallery';
  static const _kHaptics = 'settings.haptics';

  @override
  AppSettings build() {
    final prefs = ref.read(sharedPreferencesProvider);
    return AppSettings(
      defaultAspectKey: prefs.getString(_kAspect) ?? '16:9',
      defaultFps: prefs.getInt(_kFps) ?? 30,
      defaultResolution: prefs.getInt(_kResolution) ?? 1080,
      saveToGallery: prefs.getBool(_kSaveToGallery) ?? true,
      hapticFeedback: prefs.getBool(_kHaptics) ?? true,
    );
  }

  void setDefaultAspect(String key) {
    state = state.copyWith(defaultAspectKey: key);
    ref.read(sharedPreferencesProvider).setString(_kAspect, key);
  }

  void setDefaultFps(int fps) {
    state = state.copyWith(defaultFps: fps);
    ref.read(sharedPreferencesProvider).setInt(_kFps, fps);
  }

  void setDefaultResolution(int height) {
    state = state.copyWith(defaultResolution: height);
    ref.read(sharedPreferencesProvider).setInt(_kResolution, height);
  }

  void setSaveToGallery(bool value) {
    state = state.copyWith(saveToGallery: value);
    ref.read(sharedPreferencesProvider).setBool(_kSaveToGallery, value);
  }

  void setHapticFeedback(bool value) {
    state = state.copyWith(hapticFeedback: value);
    ref.read(sharedPreferencesProvider).setBool(_kHaptics, value);
  }
}

final settingsControllerProvider =
    NotifierProvider<SettingsController, AppSettings>(SettingsController.new);
