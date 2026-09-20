// O GIRO POR LETRA DO TEXTO 3D: cada letra gira em torno do proprio centro.
import 'dart:io';

import 'package:aurea/src/features/editor/domain/fonte_truetype.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d_animado.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  final fonte = FonteTrueType.ler(
    File('assets/templates/dnyx/AureaMotionSans.ttf').readAsBytesSync(),
  );
  ModelAsset3D modelo(Texto3D t) => modeloDoTexto3DPorLetra(
    disporTexto3D(t, fonte), t, fonte.unidadesPorEm, t.texto, EstiloDoTexto3D.ouro);
  double profundidade(ModelAsset3D m) {
    final f = m.evaluate(Duration.zero, const ModelMotion3D());
    var lo = 1e9, hi = -1e9;
    for (final v in f.mesh.verts) {
      if (v[2] < lo) lo = v[2];
      if (v[2] > hi) hi = v[2];
    }
    return hi - lo;
  }

  test('sem giro nao ha matriz por letra', () {
    final m = modelo(const Texto3D(texto: 'AB'));
    expect(m.temAnimacaoDeTexto, isFalse);
    expect(matrizesDoTextoAnimado(m.data, Duration.zero, null), isNull);
  });

  test('girar as letras em Y muda a geometria avaliada', () {
    final reto = profundidade(modelo(const Texto3D(texto: 'AUREA')));
    final m = modelo(const Texto3D(texto: 'AUREA', rotLetraY: 90));
    final girado = profundidade(m);
    // ignore: avoid_print
    print('PROFUNDIDADE reta=$reto girada90Y=$girado');
    expect(girado, greaterThan(reto * 1.5));
    expect(m.temAnimacaoDeTexto, isTrue);
    expect(matrizesDoTextoAnimado(m.data, Duration.zero, null)!
        .whereType<Object>().length, 5);
  });

  test('o giro entra na igualdade e nao refaz a malha da letra', () {
    const a = Texto3D(texto: 'X');
    final b = a.copyWith(rotLetraZ: 30);
    expect(a == b, isFalse);
    expect(b.temRotacaoPorLetra, isTrue);
    expect(a.soGeometria == b.soGeometria, isTrue);
  });
}
