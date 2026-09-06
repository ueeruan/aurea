import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/extrude3d.dart';
import 'package:aurea/src/features/editor/domain/mask.dart';

const _quadrado = [
  Offset(0, 0),
  Offset(100, 0),
  Offset(100, 100),
  Offset(0, 100),
];

/// Um "C": concavo. E onde a triangulacao ingenua por leque falha.
const _cee = [
  Offset(0, 0),
  Offset(100, 0),
  Offset(100, 30),
  Offset(40, 30),
  Offset(40, 70),
  Offset(100, 70),
  Offset(100, 100),
  Offset(0, 100),
];

double _areaTri(Offset a, Offset b, Offset c) =>
    ((b.dx - a.dx) * (c.dy - a.dy) - (b.dy - a.dy) * (c.dx - a.dx)).abs() /
    2;

void main() {
  group('Area com sinal', () {
    test('inverter a ordem inverte o sinal', () {
      final a = signedArea(_quadrado);
      final b = signedArea(_quadrado.reversed.toList());
      expect(a.abs(), closeTo(10000, 0.001));
      expect(a.sign, isNot(b.sign));
    });
  });

  group('Corte de orelha', () {
    test('um quadrado da dois triangulos', () {
      expect(earClip(_quadrado), hasLength(2));
    });

    // A prova que importa: a soma das partes tem de dar o todo. Se um
    // triangulo escapar para fora da forma, a area passa.
    test('os triangulos somam a area do poligono', () {
      for (final poly in [_quadrado, _cee]) {
        final tris = earClip(poly);
        var soma = 0.0;
        for (final t in tris) {
          soma += _areaTri(poly[t[0]], poly[t[1]], poly[t[2]]);
        }
        expect(soma, closeTo(signedArea(poly).abs(), 0.01),
            reason: 'poligono de ${poly.length} pontos');
      }
    });

    test('o C concavo da n-2 triangulos', () {
      expect(earClip(_cee), hasLength(_cee.length - 2));
    });

    test('a ordem de entrada nao muda a area coberta', () {
      final tris = earClip(_cee.reversed.toList());
      final poly = _cee.reversed.toList();
      var soma = 0.0;
      for (final t in tris) {
        soma += _areaTri(poly[t[0]], poly[t[1]], poly[t[2]]);
      }
      expect(soma, closeTo(signedArea(_cee).abs(), 0.01));
    });

    test('menos de tres pontos nao da triangulo', () {
      expect(earClip(const [Offset.zero, Offset(1, 1)]), isEmpty);
      expect(earClip(const []), isEmpty);
    });

    test('todo indice devolvido existe', () {
      for (final t in earClip(_cee)) {
        for (final i in t) {
          expect(i, inInclusiveRange(0, _cee.length - 1));
        }
      }
    });
  });

  group('Limpar o contorno', () {
    // Vertice duplicado vira triangulo de area zero, e triangulo de
    // area zero pisca na tela.
    test('tira pontos praticamente iguais', () {
      final r = dedupeOutline(const [
        Offset(0, 0),
        Offset(0, 0.1),
        Offset(50, 0),
        Offset(50, 0),
      ]);
      expect(r, hasLength(2));
    });

    test('fecha o laco tirando a repeticao do fim', () {
      final r = dedupeOutline(const [
        Offset(0, 0),
        Offset(50, 0),
        Offset(50, 50),
        Offset(0, 0),
      ]);
      expect(r, hasLength(3));
    });
  });

  group('Extrudar', () {
    test('sai com frente e verso', () {
      final m = extrudeOutline(_quadrado, depth: 40);
      expect(m.verts, hasLength(8));
      final zs = m.verts.map((v) => v[2]).toSet();
      expect(zs, hasLength(2));
    });

    // A convencao do renderizador e meia-extensao ~1: sem normalizar, um
    // logo de 800 px entraria na cena do tamanho de um predio.
    test('a malha sai normalizada', () {
      final m = extrudeOutline(_quadrado, depth: 40);
      final xs = m.verts.map((v) => v[0].abs()).reduce((a, b) => a > b ? a : b);
      final ys = m.verts.map((v) => v[1].abs()).reduce((a, b) => a > b ? a : b);
      expect(xs, closeTo(1, 0.001));
      expect(ys, closeTo(1, 0.001));
    });

    test('a espessura acompanha a profundidade pedida', () {
      double espessura(double d) {
        final m = extrudeOutline(_quadrado, depth: d);
        final zs = m.verts.map((v) => v[2]).toList();
        return zs.reduce((a, b) => a > b ? a : b) -
            zs.reduce((a, b) => a < b ? a : b);
      }

      expect(espessura(80), closeTo(espessura(40) * 2, 0.001));
    });

    test('tem duas tampas e uma parede por aresta', () {
      final m = extrudeOutline(_quadrado, depth: 20);
      final paredes = m.faces.where((f) => f.length == 4).length;
      final tampas = m.faces.where((f) => f.length == 3).length;
      expect(paredes, 4);
      expect(tampas, 4);
    });

    test('todo indice de face existe', () {
      final m = extrudeOutline(_cee, depth: 30);
      for (final f in m.faces) {
        for (final i in f) {
          expect(i, inInclusiveRange(0, m.verts.length - 1));
        }
      }
    });

    test('contorno degenerado devolve malha vazia', () {
      final m = extrudeOutline(const [Offset.zero, Offset(0, 0.01)]);
      expect(m.verts, isEmpty);
      expect(m.faces, isEmpty);
    });

    test('caminho bezier tambem extruda', () {
      final m = extrudePath(BezierPath.ellipse(200, 200), depth: 25);
      expect(m.verts.length, greaterThan(12));
      expect(m.faces.length, greaterThan(12));
    });
  });
}
