import 'dart:ui' show Brightness;

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/storage/prefs.dart';
import '../../../core/theme/aurea_paleta.dart';
import '../../editor/domain/modo_de_transcricao.dart';

/// Preferencias do app, persistidas em SharedPreferences.
class AppSettings {
  const AppSettings({
    this.defaultAspectKey = padraoDaProporcao,
    this.defaultFps = 30,
    this.defaultResolution = 1080,
    this.defaultLayerSeconds = 3,
    this.saveToGallery = true,
    this.hapticFeedback = true,
    this.themeMode = 'escuro',
    this.modoDeTranscricao = ModoDeTranscricao.auto,
  });

  /// Onde as legendas automaticas sao transcritas: nuvem, aparelho ou
  /// automatico (pela internet).
  final ModoDeTranscricao modoDeTranscricao;

  /// O TEMA ESCOLHIDO, do jeito que vai para o disco.
  ///
  /// 'escuro' (= Aurea) | 'claro' (= Light) | 'sistema' | o `name` de um
  /// [AureaTemaId] ('aureaDark', 'midnight', 'oled', 'graphite'). Os dois
  /// primeiros sao os nomes de quando so havia dois temas, e ficaram para
  /// ninguem perder a escolha na atualizacao. Continua `String` (e nao o
  /// enum) porque 'sistema' nao e um tema, e uma regra.
  final String themeMode;

  /// O tema que [themeMode] da, com o brilho do aparelho para 'sistema'.
  /// Valor desconhecido cai no padrao.
  AureaTemaId temaId(Brightness sistema) =>
      AureaPaleta.resolver(themeMode, sistema);

  bool get temaSegueOSistema => themeMode == AureaPaleta.modoSistema;

  /// A PROPORCAO DE QUEM NUNCA MEXEU NOS AJUSTES: 9:16, o video de
  /// celular. Quem escolheu outra nos Ajustes continua com a sua (ela esta
  /// gravada; so a falta de escolha cai aqui).
  static const padraoDaProporcao = '9:16';

  /// Valores padrao usados ao criar um projeto novo.
  final String defaultAspectKey;
  final int defaultFps;
  final int defaultResolution;

  /// Quantos segundos uma camada nova dura (texto, forma, foto).
  final int defaultLayerSeconds;

  /// Ao exportar, salvar copia na galeria do dispositivo.
  final bool saveToGallery;

  final bool hapticFeedback;

  AppSettings copyWith({
    String? defaultAspectKey,
    int? defaultFps,
    int? defaultResolution,
    int? defaultLayerSeconds,
    bool? saveToGallery,
    bool? hapticFeedback,
    String? themeMode,
    ModoDeTranscricao? modoDeTranscricao,
  }) {
    return AppSettings(
      defaultAspectKey: defaultAspectKey ?? this.defaultAspectKey,
      defaultFps: defaultFps ?? this.defaultFps,
      defaultResolution: defaultResolution ?? this.defaultResolution,
      defaultLayerSeconds: defaultLayerSeconds ?? this.defaultLayerSeconds,
      saveToGallery: saveToGallery ?? this.saveToGallery,
      hapticFeedback: hapticFeedback ?? this.hapticFeedback,
      themeMode: themeMode ?? this.themeMode,
      modoDeTranscricao: modoDeTranscricao ?? this.modoDeTranscricao,
    );
  }
}

class SettingsController extends Notifier<AppSettings> {
  static const _kAspect = 'settings.defaultAspect';
  static const _kFps = 'settings.defaultFps';
  static const _kResolution = 'settings.defaultResolution';
  static const _kLayerSeconds = 'settings.defaultLayerSeconds';
  static const _kSaveToGallery = 'settings.saveToGallery';
  static const _kHaptics = 'settings.haptics';
  static const _kTema = 'settings.tema';
  static const _kTranscricao = 'settings.transcricao';

  @override
  AppSettings build() {
    final prefs = ref.read(sharedPreferencesProvider);
    return AppSettings(
      defaultAspectKey:
          prefs.getString(_kAspect) ?? AppSettings.padraoDaProporcao,
      defaultFps: prefs.getInt(_kFps) ?? 30,
      defaultResolution: prefs.getInt(_kResolution) ?? 1080,
      defaultLayerSeconds: (prefs.getInt(_kLayerSeconds) ?? 3).clamp(1, 30),
      saveToGallery: prefs.getBool(_kSaveToGallery) ?? true,
      hapticFeedback: prefs.getBool(_kHaptics) ?? true,
      themeMode: prefs.getString(_kTema) ?? 'escuro',
      modoDeTranscricao: ModoDeTranscricao.deNome(
        prefs.getString(_kTranscricao),
      ),
    );
  }

  void setThemeMode(String modo) {
    state = state.copyWith(themeMode: modo);
    ref.read(sharedPreferencesProvider).setString(_kTema, modo);
  }

  /// Escolhe um dos temas. A raiz do app observa [AppSettings.themeMode] e
  /// remonta a arvore com a paleta nova — nao precisa reabrir o app.
  void setTema(AureaTemaId id) => setThemeMode(AureaPaleta.modoDe(id));

  /// Volta a seguir o brilho do aparelho (Aurea ou Light).
  void seguirOSistema() => setThemeMode(AureaPaleta.modoSistema);

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

  void setDefaultLayerSeconds(int segundos) {
    final preso = segundos.clamp(1, 30);
    state = state.copyWith(defaultLayerSeconds: preso);
    ref.read(sharedPreferencesProvider).setInt(_kLayerSeconds, preso);
  }

  void setSaveToGallery(bool value) {
    state = state.copyWith(saveToGallery: value);
    ref.read(sharedPreferencesProvider).setBool(_kSaveToGallery, value);
  }

  void setModoDeTranscricao(ModoDeTranscricao modo) {
    state = state.copyWith(modoDeTranscricao: modo);
    ref.read(sharedPreferencesProvider).setString(_kTranscricao, modo.name);
  }

  void setHapticFeedback(bool value) {
    state = state.copyWith(hapticFeedback: value);
    ref.read(sharedPreferencesProvider).setBool(_kHaptics, value);
  }
}

final settingsControllerProvider =
    NotifierProvider<SettingsController, AppSettings>(SettingsController.new);
