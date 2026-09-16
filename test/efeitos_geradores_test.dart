import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/pixel_effect.dart';
import 'package:flutter_test/flutter_test.dart';

const _geradores = [
  EffectType.nuvens,
  EffectType.xadrez,
  EffectType.listras,
  EffectType.pontos,
  EffectType.estrelas,
  EffectType.raios,
];

void main() {
  test('os seis geradores entraram com id, cor e prontos', () {
    for (final t in _geradores) {
      final ficha = effectSpecs[t]!;
      expect(effectTypeFromId(ficha.id), t, reason: t.name);
      expect(ficha.category, 'Generate', reason: t.name);
      // Todo gerador desenha ENTRE DUAS CORES: sem a segunda, o padrao
      // so sabe pintar por cima.
      expect(ficha.hasColor, isTrue, reason: t.name);
      expect(ficha.extraColors, 1, reason: t.name);
      expect(ficha.defaultExtraColors.length, 1, reason: t.name);
      expect(ficha.presets.length, 3, reason: t.name);
    }
  });

  test('todo gerador tem mistura e recorte', () {
    for (final t in _geradores) {
      final ficha = effectSpecs[t]!;
      expect(ficha.params.containsKey('mistura'), isTrue, reason: t.name);
      final recortar = ficha.params['recortar'];
      expect(recortar, isNotNull, reason: t.name);
      expect(recortar!.kind, ParamKind.toggle, reason: t.name);
      // Nascer neutro seria um efeito invisivel na lista: o gerador
      // aparece assim que entra.
      expect(ficha.params['mistura']!.initial, 1, reason: t.name);
      expect(recortar.initial, 0, reason: t.name);
    }
  });

  test('cada gerador tem o seu modo no shader, sem repetir', () {
    final modos = <int>{};
    for (final t in _geradores) {
      final kernel = pixelKernels[t]!;
      expect(kernel.mode, inInclusiveRange(50, 55), reason: t.name);
      expect(modos.add(kernel.mode), isTrue, reason: '${t.name} repete modo');
      final ficha = effectSpecs[t]!;
      for (final k in kernel.keys) {
        expect(ficha.params.containsKey(k), isTrue, reason: '${t.name}.$k');
      }
      // A ORDEM e o contrato com o shader: cada chave cai num campo.
      expect(kernel.keys.length, ficha.params.length, reason: t.name);
      expect(kernel.keys.toSet().length, kernel.keys.length, reason: t.name);
    }
    final todos = pixelKernels.values.map((k) => k.mode).toList();
    expect(todos.toSet().length, todos.length);
  });

  test('a busca acha os geradores pelos nomes de casa', () {
    // Nuvem tambem acha o Ruido Fractal, que ja fazia nuvem antes: os
    // dois na lista e o certo, e nao um roubar a busca do outro.
    expect(searchEffects('nuvem'), contains(EffectType.nuvens));
    expect(searchEffects('tabuleiro').first, EffectType.xadrez);
    expect(searchEffects('faixas'), contains(EffectType.listras));
    expect(searchEffects('bolinhas').first, EffectType.pontos);
    expect(searchEffects('ceu'), contains(EffectType.estrelas));
    expect(searchEffects('explosao'), contains(EffectType.raios));
  });

  test('os prontos só citam parâmetros que existem', () {
    for (final t in _geradores) {
      final ficha = effectSpecs[t]!;
      for (final pronto in ficha.presets) {
        for (final chave in pronto.valores.keys) {
          expect(
            ficha.params.containsKey(chave),
            isTrue,
            reason: '${t.name}: "${pronto.nome}" cita "$chave"',
          );
        }
      }
      for (final k in ficha.montar) {
        expect(ficha.params.containsKey(k), isTrue, reason: '${t.name}.$k');
      }
    }
  });
}
