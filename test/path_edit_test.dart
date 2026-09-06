import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/mask.dart';
import 'package:aurea/src/features/editor/domain/path_edit.dart';

BezierPath _quadrado() => BezierPath.rect(200, 200);
BezierPath _circulo() => BezierPath.ellipse(200, 200);

/// Amostra o caminho em muitos pontos — e assim que se prova que a
/// FORMA nao mudou, sem depender de qual vertice virou qual.
List<Offset> _amostrar(BezierPath p, {int por = 12}) {
  final out = <Offset>[];
  final metric = p.build().computeMetrics().toList();
  for (final m in metric) {
    for (var i = 0; i <= por; i++) {
      final t = m.length * i / por;
      final tan = m.getTangentForOffset(t);
      if (tan != null) out.add(tan.position);
    }
  }
  return out;
}

void main() {
  group('Mover', () {
    test('a ancora vai para onde o dedo pediu', () {
      final p = moveVertex(_quadrado(), 0, const Offset(10, 20));
      expect(p.vertices[0].p, const Offset(10, 20));
    });

    test('as alcas viajam com o no', () {
      final base = _circulo();
      final antes = base.vertices[1];
      final p = moveVertex(base, 1, antes.p + const Offset(50, 50));
      expect(p.vertices[1].inT, antes.inT);
      expect(p.vertices[1].outT, antes.outT);
    });

    test('indice fora da lista nao faz nada', () {
      final base = _quadrado();
      expect(moveVertex(base, 99, Offset.zero).vertices[0].p,
          base.vertices[0].p);
      expect(removeVertex(base, -1).vertices.length,
          base.vertices.length);
    });
  });

  group('Alcas', () {
    // Num no de curva as duas alcas ficam opostas: sem isso, arrastar um
    // lado deixa um bico no meio de uma curva que devia ser lisa.
    test('no de curva: a alca oposta acompanha', () {
      final base = _circulo();
      final i = 0;
      final v = base.vertices[i];
      expect(v.corner, isFalse);
      final comprimentoAntes = v.outT.distance;

      final p = moveHandle(base, i, Handle.entrada, v.p + const Offset(0, -40));
      final novo = p.vertices[i];
      expect(novo.inT, const Offset(0, -40));
      // Direcao invertida...
      expect(novo.outT.dx, closeTo(0, 1e-6));
      expect(novo.outT.dy, greaterThan(0));
      // ...e comprimento preservado.
      expect(novo.outT.distance, closeTo(comprimentoAntes, 1e-6));
    });

    test('no de canto: cada alca anda sozinha', () {
      final base = _quadrado();
      final v = base.vertices[0];
      expect(v.corner, isTrue);
      final p = moveHandle(base, 0, Handle.saida, v.p + const Offset(30, 0));
      expect(p.vertices[0].outT, const Offset(30, 0));
      expect(p.vertices[0].inT, v.inT);
    });

    // Um lado reto nao ganha curva sozinho so porque o outro foi puxado.
    test('alca zerada continua zerada', () {
      final base = BezierPath(vertices: [
        const PathVertex(p: Offset.zero, corner: false),
        const PathVertex(p: Offset(100, 0), corner: false),
        const PathVertex(p: Offset(100, 100), corner: false),
      ]);
      final p = moveHandle(base, 0, Handle.saida, const Offset(40, 0));
      expect(p.vertices[0].inT, Offset.zero);
    });

    test('achar a alca so vale para o no selecionado', () {
      final base = _circulo();
      final v = base.vertices[0];
      final ponta = v.p + v.outT;
      expect(handleAt(base, 0, ponta, 12), (0, Handle.saida));
      expect(handleAt(base, 1, ponta, 12), isNull);
      expect(handleAt(base, null, ponta, 12), isNull);
    });
  });

  group('Inserir no', () {
    // A propriedade que importa: inserir NAO deforma. Se deformasse,
    // acrescentar um no para ajustar um detalhe estragaria o resto.
    test('a forma continua a mesma', () {
      final base = _circulo();
      final antes = _amostrar(base);
      final depois = _amostrar(insertVertex(base, 1, 0.37));
      expect(depois.length, antes.length);
      for (var i = 0; i < antes.length; i++) {
        expect((depois[i] - antes[i]).distance, lessThan(0.5),
            reason: 'ponto $i saiu do lugar');
      }
    });

    test('sobra um no a mais, no lugar certo', () {
      final base = _quadrado();
      final p = insertVertex(base, 0, 0.5);
      expect(p.vertices.length, base.vertices.length + 1);
      // Entre o vertice 0 e o antigo 1.
      final meio = p.vertices[1].p;
      expect(meio.dx, closeTo(0, 1));
      expect(meio.dy, closeTo(-100, 1));
    });

    test('o no novo nasce como curva', () {
      expect(insertVertex(_quadrado(), 0, 0.5).vertices[1].corner, isFalse);
    });

    test('segmento que nao existe nao insere nada', () {
      final base = _quadrado();
      expect(insertVertex(base, 99, 0.5).vertices.length,
          base.vertices.length);
      expect(insertVertex(base, -1, 0.5).vertices.length,
          base.vertices.length);
    });
  });

  group('Tirar no', () {
    test('tira o pedido', () {
      final base = _quadrado();
      final p = removeVertex(base, 1);
      expect(p.vertices.length, 3);
      expect(p.vertices.any((v) => v.p == base.vertices[1].p), isFalse);
    });

    // Menos de tres nos deixa de ser area: o triangulo e o piso.
    test('o triangulo nao se desfaz', () {
      final tri = BezierPath(vertices: const [
        PathVertex(p: Offset.zero),
        PathVertex(p: Offset(100, 0)),
        PathVertex(p: Offset(50, 80)),
      ]);
      expect(removeVertex(tri, 0).vertices.length, 3);
    });
  });

  group('Canto e curva', () {
    test('virar canto zera as alcas', () {
      final p = toggleCorner(_circulo(), 0);
      expect(p.vertices[0].corner, isTrue);
      expect(p.vertices[0].inT, Offset.zero);
      expect(p.vertices[0].outT, Offset.zero);
    });

    // Virar curva sem calcular alca nao mudaria nada na tela — o no
    // continuaria bico, e a pessoa acharia que o botao esta quebrado.
    test('virar curva ja nasce com alca', () {
      final p = toggleCorner(_quadrado(), 1);
      expect(p.vertices[1].corner, isFalse);
      expect(p.vertices[1].outT.distance, greaterThan(1));
      expect(p.vertices[1].inT.distance, greaterThan(1));
    });

    test('as duas alcas nascem opostas', () {
      final v = toggleCorner(_quadrado(), 1).vertices[1];
      expect(v.inT.dx, closeTo(-v.outT.dx, 1e-9));
      expect(v.inT.dy, closeTo(-v.outT.dy, 1e-9));
    });

    test('a direcao segue os vizinhos', () {
      // Vertices do retangulo: (-100,-100) (100,-100) (100,100) (-100,100).
      // No vertice 1, a corda vai de (-100,-100) para (100,100).
      final v = toggleCorner(_quadrado(), 1).vertices[1];
      expect(v.outT.dx, closeTo(v.outT.dy, 1e-6));
      expect(v.outT.dx, greaterThan(0));
    });
  });

  group('Achar', () {
    test('o ponto mais perto cai na curva', () {
      final hit = nearestOnPath(_circulo(), const Offset(300, 0))!;
      expect(hit.point.dx, closeTo(100, 1));
      expect(hit.point.dy, closeTo(0, 1));
      expect(hit.distance, closeTo(200, 1));
    });

    test('o segmento devolvido e o que contem o ponto', () {
      final base = _quadrado();
      // Meio do lado de cima, entre os vertices 0 e 1.
      final hit = nearestOnPath(base, const Offset(0, -140))!;
      expect(hit.segment, 0);
      expect(hit.t, closeTo(0.5, 0.1));
    });

    test('inserir onde o dedo caiu poe o no debaixo do dedo', () {
      final base = _circulo();
      const dedo = Offset(220, 10);
      final hit = nearestOnPath(base, dedo)!;
      final p = insertVertex(base, hit.segment, hit.t);
      final novo = p.vertices[hit.segment + 1].p;
      expect((novo - hit.point).distance, lessThan(1));
    });

    test('caminho sem segmento nao tem ponto', () {
      expect(nearestOnPath(BezierPath(vertices: const []), Offset.zero),
          isNull);
    });

    test('achar no respeita o raio', () {
      final base = _quadrado();
      final v0 = base.vertices[0].p;
      expect(vertexAt(base, v0 + const Offset(5, 0), 12), 0);
      expect(vertexAt(base, v0 + const Offset(40, 0), 12), isNull);
    });
  });

  group('Caixa', () {
    test('envolve ancoras e alcas', () {
      final r = pathBounds(_quadrado());
      expect(r.left, closeTo(-100, 0.001));
      expect(r.right, closeTo(100, 0.001));
    });

    test('caminho vazio da caixa vazia', () {
      expect(pathBounds(BezierPath(vertices: const [])), Rect.zero);
    });
  });
}
