import 'package:aurea/src/features/editor/domain/aparecer_sumir.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/pixel_effect.dart';
import 'package:flutter_test/flutter_test.dart';

Duration _s(double v) =>
    Duration(microseconds: (v * Duration.microsecondsPerSecond).round());

EffectInstance _fade({double entrada = .5, double saida = .5, int curva = 0}) =>
    EffectInstance(type: EffectType.aparecerSumir).comValores({
      'entrada': entrada,
      'saida': saida,
      'curva': curva.toDouble(),
    });

extension on EffectInstance {
  EffectInstance comValores(Map<String, double> valores) => copyWith(
    params: {
      ...params,
      for (final e in valores.entries) e.key: AnimatedDouble(e.value),
    },
  );
}

void main() {
  group('aparecer e sumir', () {
    const total = Duration(seconds: 4);

    test('sobe no começo, fica cheio no meio e cai no fim', () {
      final e = _fade();
      expect(opacidadeDoAparecerSumir(e, Duration.zero, total), 0);
      expect(opacidadeDoAparecerSumir(e, _s(.25), total), closeTo(.5, .001));
      expect(opacidadeDoAparecerSumir(e, _s(.5), total), 1);
      expect(opacidadeDoAparecerSumir(e, _s(2), total), 1);
      expect(opacidadeDoAparecerSumir(e, _s(3.75), total), closeTo(.5, .001));
      expect(opacidadeDoAparecerSumir(e, _s(4), total), 0);
    });

    test('só uma ponta: a outra não mexe em nada', () {
      final so = _fade(entrada: 1, saida: 0);
      expect(opacidadeDoAparecerSumir(so, _s(.5), total), closeTo(.5, .001));
      expect(opacidadeDoAparecerSumir(so, _s(4), total), 1);
    });

    test('camada curta divide o tempo entre as duas pontas', () {
      // Tres segundos de entrada e tres de saida numa camada de dois:
      // sem o aperto, a camada nunca chegaria a aparecer.
      final e = _fade(entrada: 3, saida: 3);
      const curta = Duration(seconds: 2);
      expect(opacidadeDoAparecerSumir(e, _s(1), curta), closeTo(1, .001));
      expect(opacidadeDoAparecerSumir(e, _s(.5), curta), closeTo(.5, .001));
      expect(opacidadeDoAparecerSumir(e, _s(1.5), curta), closeTo(.5, .001));
    });

    test('a curva suave sai do zero devagar e ainda bate nas pontas', () {
      final suave = _fade(curva: 1);
      final reta = _fade();
      expect(opacidadeDoAparecerSumir(suave, Duration.zero, total), 0);
      expect(opacidadeDoAparecerSumir(suave, _s(.5), total), 1);
      final meio = opacidadeDoAparecerSumir(suave, _s(.125), total);
      expect(meio, lessThan(opacidadeDoAparecerSumir(reta, _s(.125), total)));
      expect(meio, greaterThan(0));
    });

    test('camada sem duração não apaga nada', () {
      expect(opacidadeDoAparecerSumir(_fade(), _s(1), Duration.zero), 1);
    });

    test('fora do intervalo continua nos limites', () {
      final e = _fade();
      expect(opacidadeDoAparecerSumir(e, _s(-1), total), 0);
      expect(opacidadeDoAparecerSumir(e, _s(9), total), 0);
    });
  });

  group('as fichas novas', () {
    test('os três entraram no catálogo com id próprio', () {
      for (final t in [
        EffectType.dissolver,
        EffectType.pena,
        EffectType.aparecerSumir,
      ]) {
        final ficha = effectSpecs[t]!;
        expect(ficha.params, isNotEmpty, reason: t.name);
        expect(effectTypeFromId(ficha.id), t);
        expect(ficha.presets, isNotEmpty, reason: '${t.name} sem prontos');
      }
      expect(effectSpecs[EffectType.dissolver]!.id, 'dissolve');
      expect(effectSpecs[EffectType.pena]!.id, 'feather');
      expect(effectSpecs[EffectType.aparecerSumir]!.id, 'fade_in_out');
    });

    test('a busca acha pelos nomes de casa', () {
      for (final busca in ['chuvisco', 'esfumar', 'sumir']) {
        expect(
          searchEffects(busca),
          isNotEmpty,
          reason: 'ninguém acha por "$busca"',
        );
      }
      expect(searchEffects('chuvisco').first, EffectType.dissolver);
      expect(searchEffects('esfumar').first, EffectType.pena);
    });

    test('o que o shader lê existe na ficha, na ordem do núcleo', () {
      for (final t in [EffectType.dissolver, EffectType.pena]) {
        final kernel = pixelKernels[t]!;
        final ficha = effectSpecs[t]!;
        expect(kernel.mode, greaterThan(47), reason: 'modo novo');
        for (final k in kernel.keys) {
          expect(ficha.params.containsKey(k), isTrue, reason: '${t.name}.$k');
        }
      }
      // Dois efeitos diferentes nunca podem cair no mesmo modo.
      final modos = pixelKernels.values.map((k) => k.mode).toList();
      expect(modos.toSet().length, modos.length);
    });
  });
}
