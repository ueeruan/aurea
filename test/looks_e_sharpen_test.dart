// LOOKS E S_SHARPEN (camada de ajuste do After, pedido de 14/09/2026).
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/efeitos_do_after.dart';
import 'package:aurea/src/features/editor/domain/pixel_effect.dart';
import 'package:flutter_test/flutter_test.dart';

const _identidade = [
  1.0, 0, 0, 0, 0, //
  0, 1.0, 0, 0, 0,
  0, 0, 1.0, 0, 0,
  0, 0, 0, 1.0, 0,
];

void main() {
  test('Looks com forca 0 e a identidade exata, para todo look', () {
    final n = effectSpecs[EffectType.looks]!.params['look']!.options.length;
    for (var look = 0; look < n; look++) {
      expect(matrizDoLook(look, 0), _identidade, reason: 'look $look');
    }
  });

  test('forca mistura linearmente e cada look muda a imagem', () {
    final n = effectSpecs[EffectType.looks]!.params['look']!.options.length;
    for (var look = 0; look < n; look++) {
      final cheio = matrizDoLook(look, 100);
      final meio = matrizDoLook(look, 50);
      for (var i = 0; i < 20; i++) {
        expect(meio[i], closeTo((cheio[i] + _identidade[i]) / 2, 1e-9));
      }
      expect(cheio, isNot(_identidade), reason: 'look $look nao muda nada');
    }
    // Noir tira a cor: as tres linhas ficam iguais.
    final noir = matrizDoLook(8, 100);
    expect(noir.sublist(0, 3), noir.sublist(5, 8));
    expect(matrizDoLook(99, 100), matrizDoLook(11, 100), reason: 'look fora');
  });

  test('S_Sharpen roda no shader com as chaves da ficha e nasce afiando', () {
    final spec = effectSpecs[EffectType.sSharpen]!;
    expect(pixelKernels[EffectType.sSharpen]!.mode, 47);
    for (final k in pixelKernels[EffectType.sSharpen]!.keys) {
      expect(spec.params.containsKey(k), isTrue, reason: k);
    }
    expect(spec.params['amount']!.initial, greaterThan(0));
    expect(searchEffects('sharpen'), contains(EffectType.sSharpen));
    expect(searchEffects('magic bullet'), contains(EffectType.looks));
  });
}
