// AS FAIXAS DO PREVIEW ADAPTATIVO.
import 'package:aurea/src/features/editor/application/ui/preview_resolution.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('as seis faixas, da cheia a doze e meio por cento', () {
    // 75% e 33% existem para o celular que aguenta quase tudo, mas nao
    // tudo: antes delas so havia o cheio e a metade, e quem baixava
    // perdia nitidez sem precisar.
    expect(PreviewResolution.values.map((v) => v.scale), [
      1.0, .75, .5, .33, .25, .125,
    ]);
    expect(PreviewResolution.values.first.label, 'Full');
  });

  test('a escala so desce', () {
    for (var i = 1; i < PreviewResolution.values.length; i++) {
      expect(
        PreviewResolution.values[i].scale,
        lessThan(PreviewResolution.values[i - 1].scale),
        reason: 'a faixa $i nao e menor que a anterior',
      );
    }
  });

  test('o teto de particulas acompanha, e nunca sobe', () {
    final c = ProviderContainer();
    var anterior = 4;
    for (final r in PreviewResolution.values) {
      c.read(previewResolutionProvider.notifier).state = r;
      final n = c.read(nivelDasParticulasProvider);
      expect(n, lessThanOrEqualTo(anterior));
      expect(n, inInclusiveRange(0, 3));
      anterior = n;
    }
    c.dispose();
  });
}
