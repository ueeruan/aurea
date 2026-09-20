// OS SEIS TEMAS (20/09/2026).
//
// O que este arquivo protege, na ordem em que as coisas quebram:
//
//  1. o tema padrao continua PIXEL IDENTICO ao app de antes dos temas —
//     `AureaPaleta.aurea` so pode conter valores de `AureaColors`;
//  2. o que vai para o disco continua compativel com 'escuro'/'claro'/
//     'sistema', para ninguem perder a escolha na atualizacao;
//  3. a troca vale em tempo de execucao: `AmColors` (o cromo do editor) e
//     `AppColors` (o resto do app) leem a paleta em vigor, nao um const;
//  4. o editor continua ESCURO sob o tema claro;
//  5. nenhum tema deixa o cromo ilegivel (contraste WCAG medido aqui).
import 'dart:math' as math;

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/core/theme/app_theme.dart';
import 'package:aurea/src/core/theme/aurea_colors.dart';
import 'package:aurea/src/core/theme/aurea_paleta.dart';
import 'package:aurea/src/core/theme/tokens.dart';
import 'package:aurea/src/core/ui/am_colors.dart';
import 'package:aurea/src/features/settings/application/settings_controller.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// Luminancia relativa (WCAG 2.1).
double _lum(Color c) {
  double canal(double v) =>
      v <= 0.03928 ? v / 12.92 : math.pow((v + 0.055) / 1.055, 2.4).toDouble();
  return 0.2126 * canal(c.r) + 0.7152 * canal(c.g) + 0.0722 * canal(c.b);
}

double _contraste(Color a, Color b) {
  final x = _lum(a), y = _lum(b);
  return (math.max(x, y) + 0.05) / (math.min(x, y) + 0.05);
}

void main() {
  // A paleta e um estatico global: um teste que troca de tema tem de
  // devolver o padrao, ou contamina o proximo arquivo da suite.
  tearDown(() => AureaPaleta.ativa = AureaPaleta.aurea);

  group('o tema padrao nao muda um pixel', () {
    test('Aurea e a tabela da marca, sem hexadecimal proprio', () {
      const p = AureaPaleta.aurea;
      expect(p.background, AureaColors.bg);
      expect(p.surface, AureaColors.surface);
      expect(p.panel, AureaColors.surfaceHigh);
      expect(p.chip, AureaColors.chip);
      expect(p.divider, AureaColors.border);
      expect(p.textPrimary, AureaColors.text);
      expect(p.textSecondary, AureaColors.muted);
      expect(p.primary, AureaColors.brand);
      expect(p.accent, AureaColors.accent);
      expect(p.keyframe, AureaColors.keyframe);
      expect(p.selected, AureaColors.brandDeep);
      expect(p.stage, AureaColors.stage);
      expect(p.playhead, AureaColors.playhead);
      expect(p.danger, AureaColors.danger);
      expect(p.brightness, Brightness.dark);
    });

    test('AmColors sob o tema Aurea da os mesmos valores de antes', () {
      AureaPaleta.ativa = AureaPaleta.aurea;
      expect(AmColors.bg, AureaColors.stage);
      expect(AmColors.topBar, AureaColors.chrome);
      expect(AmColors.panel, AureaColors.chrome);
      expect(AmColors.panelHigh, AureaColors.chromeHigh);
      expect(AmColors.chip, AureaColors.chip);
      expect(AmColors.pilula, AureaColors.pill);
      expect(AmColors.campo, AureaColors.field);
      expect(AmColors.accent, AureaColors.accent);
      expect(AmColors.accentDim, AureaColors.accentDim);
      expect(AmColors.action, AureaColors.brand);
      expect(AmColors.onAction, AureaColors.text);
      expect(AmColors.actionDim, const Color(0xFF16304A));
      expect(AmColors.selection, AureaColors.brandDeep);
      expect(AmColors.selectionText, AureaColors.selectionText);
      expect(AmColors.teal, AureaColors.brandLight);
      expect(AmColors.tealBright, AureaColors.brandSoft);
      expect(AmColors.pink, AureaColors.danger);
      expect(AmColors.cabecote, AureaColors.playhead);
      expect(AmColors.text, AureaColors.text);
      expect(AmColors.muted, AureaColors.muted);
      expect(AmColors.hairline, AureaColors.border);
    });

    test('AureaTokens.dePaleta(aurea) e a instancia `dark`, sem copia', () {
      expect(AureaTokens.dePaleta(AureaPaleta.aurea), same(AureaTokens.dark));
    });
  });

  group('o que vai para o disco', () {
    test('os dois temas antigos gravam os nomes antigos', () {
      expect(AureaPaleta.modoDe(AureaTemaId.aurea), 'escuro');
      expect(AureaPaleta.modoDe(AureaTemaId.light), 'claro');
      expect(AureaPaleta.modoDe(AureaTemaId.midnight), 'midnight');
    });

    test('o que estava gravado antes dos temas continua valendo', () {
      expect(
        AureaPaleta.resolver('escuro', Brightness.light),
        AureaTemaId.aurea,
      );
      expect(AureaPaleta.resolver('claro', Brightness.dark), AureaTemaId.light);
      expect(
        AureaPaleta.resolver('sistema', Brightness.light),
        AureaTemaId.light,
      );
      expect(
        AureaPaleta.resolver('sistema', Brightness.dark),
        AureaTemaId.aurea,
      );
    });

    test('nome desconhecido ou vazio cai no padrao, nao quebra', () {
      expect(AureaPaleta.resolver('neon', Brightness.dark), AureaTemaId.aurea);
      expect(AureaPaleta.resolver(null, Brightness.dark), AureaTemaId.aurea);
    });

    test('ida e volta por todos os temas', () {
      for (final id in AureaTemaId.values) {
        expect(
          AureaPaleta.resolver(AureaPaleta.modoDe(id), Brightness.dark),
          id,
          reason: id.name,
        );
      }
    });
  });

  group('a troca vale em tempo de execucao', () {
    test('AppTheme.tema escreve a paleta em vigor antes de montar', () {
      AppTheme.tema(paleta: AureaPaleta.midnight);
      expect(AureaPaleta.ativa, AureaPaleta.midnight);
      expect(AppColors.background, AureaPaleta.midnight.background);
      expect(AmColors.panel, AureaPaleta.midnight.background);
      expect(AmColors.accent, AureaPaleta.midnight.accent);

      AppTheme.tema(paleta: AureaPaleta.graphite);
      expect(AppColors.background, AureaPaleta.graphite.background);
      expect(AmColors.accent, AureaPaleta.graphite.accent);
      expect(AmColors.hairline, AureaPaleta.graphite.divider);
    });

    test('os tokens do editor acompanham (uma instancia por tema)', () {
      AureaPaleta.ativa = AureaPaleta.oled;
      expect(AureaTokens.motion.bg, AureaPaleta.oled.background);
      // Guardado por tema: AureaTheme avisa os dependentes quando `tokens`
      // muda, e uma instancia nova por build reconstruiria o editor a toa.
      expect(AureaTokens.motion, same(AureaTokens.motion));
    });

    test('AppColors.modoClaro sai do brilho da paleta, sem segunda bandeira', () {
      AppTheme.tema(paleta: AureaPaleta.light);
      expect(AppColors.modoClaro, isTrue);
      AppTheme.tema(paleta: AureaPaleta.oled);
      expect(AppColors.modoClaro, isFalse);
    });
  });

  group('o editor continua escuro no tema claro', () {
    test('a sub-paleta do editor sob Light e a do tema Aurea', () {
      expect(AureaPaleta.light.editor, AureaPaleta.aurea);
      for (final p in AureaPaleta.todas.where((p) => !p.claro)) {
        expect(p.editor, same(p), reason: p.nome);
      }
    });

    test('sob Light o cromo do editor fica escuro e o app fica claro', () {
      AppTheme.tema(paleta: AureaPaleta.light);
      expect(AppColors.background, AureaColors.lightBg);
      expect(AmColors.panel, AureaColors.chrome);
      expect(AmColors.bg, AureaColors.stage);
      expect(AureaTokens.motion.bg, AureaColors.bg);
    });
  });

  group('nenhum tema fica ilegivel', () {
    test('texto, apagado, acento e rotulo de botao passam em AA', () {
      for (final p in AureaPaleta.todas) {
        expect(
          _contraste(p.textPrimary, p.background),
          greaterThanOrEqualTo(4.5),
          reason: '${p.nome}: texto sobre fundo',
        );
        expect(
          _contraste(p.textSecondary, p.surface),
          greaterThanOrEqualTo(4.5),
          reason: '${p.nome}: apagado sobre superficie',
        );
        expect(
          _contraste(p.accent, p.background),
          greaterThanOrEqualTo(4.5),
          reason: '${p.nome}: acento sobre fundo',
        );
        expect(
          _contraste(p.onPrimary, p.primary),
          greaterThanOrEqualTo(4.5),
          reason: '${p.nome}: rotulo sobre preenchimento',
        );
        expect(
          _contraste(p.danger, p.background),
          greaterThanOrEqualTo(4.5),
          reason: '${p.nome}: erro sobre fundo',
        );
      }
    });

    test('o palco e a barra de camada sao escuros em TODO tema', () {
      // Os rotulos da barra e as alcas do palco sao brancos fixos.
      for (final p in AureaPaleta.todas) {
        expect(
          _contraste(const Color(0xFFFFFFFF), p.stage),
          greaterThanOrEqualTo(7),
          reason: '${p.nome}: palco',
        );
        expect(
          _contraste(const Color(0xFFFFFFFF), p.clip),
          greaterThanOrEqualTo(7),
          reason: '${p.nome}: barra de camada',
        );
      }
    });

    test('OLED separa palco e cromo pela linha, nao pelo tom', () {
      const p = AureaPaleta.oled;
      expect(p.stage, p.background, reason: 'os dois sao #000 de proposito');
      expect(
        _contraste(p.divider, p.background),
        greaterThan(1.5),
        reason: 'a moldura da composicao tem de continuar visivel',
      );
    });
  });

  group('Ajustes grava e o app le', () {
    late SharedPreferences prefs;

    Future<ProviderContainer> abrir(Map<String, Object> inicial) async {
      SharedPreferences.setMockInitialValues(inicial);
      prefs = await SharedPreferences.getInstance();
      final c = ProviderContainer(
        overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
      );
      addTearDown(c.dispose);
      return c;
    }

    testWidgets('escolher um tema grava o nome e muda o estado', (_) async {
      final c = await abrir({});
      final ctl = c.read(settingsControllerProvider.notifier);
      expect(c.read(settingsControllerProvider).themeMode, 'escuro');

      ctl.setTema(AureaTemaId.midnight);
      expect(c.read(settingsControllerProvider).themeMode, 'midnight');
      expect(prefs.getString('settings.tema'), 'midnight');

      ctl.seguirOSistema();
      expect(c.read(settingsControllerProvider).temaSegueOSistema, isTrue);
      expect(prefs.getString('settings.tema'), 'sistema');

      ctl.setTema(AureaTemaId.aurea);
      expect(prefs.getString('settings.tema'), 'escuro');
    });

    testWidgets('o tema gravado volta na proxima abertura', (_) async {
      final c = await abrir({'settings.tema': 'graphite'});
      final s = c.read(settingsControllerProvider);
      expect(s.themeMode, 'graphite');
      expect(s.temaId(Brightness.light), AureaTemaId.graphite);
      expect(s.temaSegueOSistema, isFalse);
    });
  });
}
