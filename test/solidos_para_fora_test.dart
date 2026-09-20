// O LADO DE FORA E O DE FORA — medido em todo solido nativo.
//
// POR QUE ESTE TESTE EXISTE. O enrolamento das faces e a unica coisa da
// geometria que o pintor de CPU nao enxerga: ele acende a normal virada para
// quem olha, entao um solido inteiro enrolado ao contrario sai igualzinho na
// tela. Na GPU, que descarta a face de tras, o mesmo solido virava um buraco.
//
// Foi assim que "importei e nao aparece nada" chegou: o cubo, a esfera e o
// toro do catalogo estavam virados para dentro desde sempre, e o defeito so
// apareceu quando o desenho passou a ser feito na placa de video.
//
// A CONTA. O volume assinado (teorema da divergencia) de um solido fechado
// com as faces para fora e POSITIVO. E um numero do proprio solido: nao
// depende de normal declarada, de centroide nem de eixo. Negativo quer dizer
// enrolado ao contrario.
//
// O QUE NAO E COBRADO AQUI. Os abertos e os de enrolamento misto — o plano,
// a placa, o cubo chanfrado — nao delimitam volume nenhum, e o volume deles
// e zero. Eles nao entram na conta, e o motor desenha os dois lados (ver a
// nota do `CULL_MODE_NONE` no `renderizador_3d.cpp`).
import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:flutter_test/flutter_test.dart';

/// O volume assinado, pela soma das piramides de cada triangulo da face.
double _volumeAssinado(Element3DMesh m) {
  var v = 0.0;
  for (final f in m.faces) {
    if (f.length < 3) continue;
    final a = m.verts[f[0]];
    for (var i = 1; i + 1 < f.length; i++) {
      final b = m.verts[f[i]], c = m.verts[f[i + 1]];
      v += a[0] * (b[1] * c[2] - b[2] * c[1]) -
          a[1] * (b[0] * c[2] - b[2] * c[0]) +
          a[2] * (b[0] * c[1] - b[1] * c[0]);
    }
  }
  return v / 6;
}

/// OS SOLIDOS FECHADOS DO CATALOGO. Cada um tem de estar virado para fora.
const _fechados = <Element3DKind>[
  Element3DKind.pyramid,
  Element3DKind.cone,
  Element3DKind.sphere,
  Element3DKind.cylinder,
  Element3DKind.prism,
  Element3DKind.diamond,
  Element3DKind.torus,
  Element3DKind.star,
  Element3DKind.capsule,
  Element3DKind.tube,
  Element3DKind.octahedron,
  Element3DKind.wedge,
  Element3DKind.dome,
  Element3DKind.crown,
  Element3DKind.crownFine,
  Element3DKind.lente,
  Element3DKind.anelDeLuz,
  Element3DKind.diafragma,
];

void main() {
  test('todo solido fechado do catalogo esta virado para fora', () {
    final invertidos = <String>[];
    for (final kind in _fechados) {
      final m = element3DMesh(kind);
      final volume = _volumeAssinado(m);
      expect(
        m.verts,
        isNotEmpty,
        reason: '${kind.name} nao tem vertice nenhum',
      );
      // O piso separa "virado para dentro" de "sem volume": um solido
      // fechado de raio 1 tem volume da ordem de 1, e nao de 1e-6.
      if (volume <= 1e-4) invertidos.add('${kind.name}=$volume');
    }
    expect(
      invertidos,
      isEmpty,
      reason:
          'enrolados para dentro (a GPU descarta a face de fora): '
          '${invertidos.join(", ")}',
    );
  });

  test('nenhum solido tem triangulo degenerado', () {
    for (final kind in Element3DKind.values) {
      final Element3DMesh m;
      try {
        m = element3DMesh(kind);
      } catch (_) {
        continue;
      }
      for (final f in m.faces) {
        expect(f.length, greaterThanOrEqualTo(3), reason: kind.name);
        for (final i in f) {
          expect(i, inInclusiveRange(0, m.verts.length - 1), reason: kind.name);
        }
      }
      for (final v in m.verts) {
        for (final x in v) {
          expect(x.isFinite, isTrue, reason: '${kind.name} com coordenada nao finita');
        }
      }
      if (m.normals != null) {
        expect(m.normals!.length, m.verts.length, reason: kind.name);
        for (final n in m.normals!) {
          // A NORMAL UNITARIA, com a folga de quem soma floats.
          final c = math.sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
          expect(c, closeTo(1, 1e-3), reason: '${kind.name} com normal torta');
        }
      }
    }
  });
}
