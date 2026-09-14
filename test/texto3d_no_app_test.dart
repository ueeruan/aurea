// TEXTO 3D NO APP: a malha vira modelo com os tres metais e entra na cena
// presa a um nulo (pedido de 14/09/2026, "identico ao Element 3D").
import 'dart:io';

import 'package:aurea/src/features/editor/domain/fonte_truetype.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('o texto vira modelo com frente, chanfro e lateral em metal', () {
    final fonte = FonteTrueType.ler(
      File('assets/templates/dnyx/AureaMotionSans.ttf').readAsBytesSync(),
    );
    const t = Texto3D(texto: 'ELEMENT');
    final malha = malhaDoTexto3D(disporTexto3D(t, fonte), t, fonte.unidadesPorEm);
    for (final estilo in EstiloDoTexto3D.values) {
      final m = modeloDoTexto3D(malha, 'ELEMENT', estilo);
      expect(m.primitives, hasLength(3), reason: estilo.name);
      expect(m.triangleCount, malha.triangulos);
      final mats = m.data['materials'] as List;
      expect(mats, hasLength(3));
      if (estilo != EstiloDoTexto3D.brancoFosco) {
        for (final mat in mats) {
          expect((mat as Map)['metallic'], 1.0, reason: estilo.name);
        }
      }
      final p = m.primitives.first as Map;
      expect((p['positions'] as List).length, (p['normals'] as List).length);
      expect((p['lods'] as List).single, isNotEmpty, reason: 'rascunho');
    }
  });
}
