// "ADICIONE ESSES EFEITOS" (dono, 14/09/2026): a camada de ajuste de um
// edit no After Effects com Magic Bullet Looks, S_Sharpen, S_Flicker,
// S_MathOps, S_FilmDamage 2, Hue/Saturation e Brightness & Contrast.
//
// Cada efeito novo tem de: devolver a imagem intacta nos valores neutros
// (pixel a pixel, pelo shader), bater com a conta de referencia em Dart
// e ser achado pela busca como a pessoa digita — com ou sem acento.
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('busca', () {
    test('ignora acento e caixa dos dois lados', () {
      expect(normalizarBusca('Saturação'), 'saturacao');
      expect(normalizarBusca('PARTÍCULAS'), 'particulas');
      // "partículas" nao achava o sinonimo "particulas".
      expect(searchEffects('partículas'), contains(EffectType.ccScatterize));
      expect(searchEffects('PARTICULAS'), contains(EffectType.ccScatterize));
      expect(searchEffects('oscilacao'), contains(EffectType.oscillate));
      expect(searchEffects('Oscilação'), contains(EffectType.oscillate));
      expect(searchEffects('zzzz'), isEmpty);
    });

    test('Brightness & Contrast pelo nome, em portugues e por "brilho"', () {
      for (final termo in [
        'Brightness & Contrast',
        'brightness & contrast',
        'brilho e contraste',
        'Brilho e Contraste',
        'brilho',
      ]) {
        expect(
          searchEffects(termo),
          contains(EffectType.brightnessContrast),
          reason: termo,
        );
      }
    });
  });
}
