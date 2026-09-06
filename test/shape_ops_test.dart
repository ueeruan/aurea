import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/shape_ops.dart';

Path _rect(double w, double h) =>
    Path()..addRect(Rect.fromCenter(center: Offset.zero, width: w, height: h));

Path _circle(double r) =>
    Path()..addOval(Rect.fromCircle(center: Offset.zero, radius: r));

/// Raio medio dos pontos amostrados a partir do centro.
double _raioMedio(Path p) {
  final pts = [for (final (list, _) in samplePath(p)) ...list];
  if (pts.isEmpty) return 0;
  final c = polyCenter(pts);
  var soma = 0.0;
  for (final pt in pts) {
    soma += (pt - c).distance;
  }
  return soma / pts.length;
}

/// Perimetro total do caminho.
double _perimetro(Path p) {
  var total = 0.0;
  for (final m in p.computeMetrics()) {
    total += m.length;
  }
  return total;
}

void main() {
  group('Amostragem', () {
    test('um retangulo fechado sai como poligono fechado', () {
      final polys = samplePath(_rect(100, 60));
      expect(polys.length, 1);
      expect(polys.first.$2, isTrue, reason: 'deveria estar fechado');
      expect(polys.first.$1.length, greaterThan(10));
    });

    test('passo menor da mais pontos', () {
      final grosso = samplePath(_circle(50), step: 20).first.$1.length;
      final fino = samplePath(_circle(50), step: 2).first.$1.length;
      expect(fino, greaterThan(grosso * 3));
    });

    test('caminho vazio nao gera poligono', () {
      expect(samplePath(Path()), isEmpty);
    });
  });

  group('Deslocar caminho', () {
    // Deslocar anda na NORMAL; escalar afasta do centro. Num circulo os
    // dois coincidem, e por isso o circulo e o caso que da para medir.
    test('para fora, o raio cresce pelo tanto pedido', () {
      final antes = _raioMedio(_circle(50));
      final depois = _raioMedio(offsetPath(_circle(50), 10));
      expect(depois - antes, closeTo(10, 1.5));
    });

    test('para dentro, o raio encolhe', () {
      final antes = _raioMedio(_circle(50));
      final depois = _raioMedio(offsetPath(_circle(50), -12));
      expect(antes - depois, closeTo(12, 1.5));
    });

    test('quantidade zero devolve o caminho intacto', () {
      final p = _rect(80, 40);
      expect(identical(offsetPath(p, 0), p), isTrue);
    });
  });

  group('Arredondar cantos', () {
    // Um retangulo tem quatro quinas; arredondar tem de encurtar o
    // contorno, porque o arco corta o canto.
    test('encurta o perimetro do retangulo', () {
      final antes = _perimetro(_rect(200, 120));
      final depois = _perimetro(roundCorners(_rect(200, 120), 20));
      expect(depois, lessThan(antes));
    });

    // Onde nao ha quina, nao ha o que arredondar — e mexer ali so
    // estragaria a curva que ja era lisa.
    test('quase nao mexe num circulo', () {
      final antes = _perimetro(_circle(60));
      final depois = _perimetro(roundCorners(_circle(60), 15));
      expect(depois, closeTo(antes, antes * 0.06));
    });

    test('raio zero devolve o caminho intacto', () {
      final p = _rect(50, 50);
      expect(identical(roundCorners(p, 0), p), isTrue);
    });
  });

  group('Zig zag', () {
    test('alonga o contorno', () {
      final antes = _perimetro(_circle(60));
      final depois = _perimetro(zigZag(_circle(60), 12, 0.5));
      expect(depois, greaterThan(antes * 1.2));
    });

    test('a onda suave alonga menos que a serra', () {
      final serra = _perimetro(zigZag(_circle(60), 12, 0.5));
      final onda = _perimetro(zigZag(_circle(60), 12, 0.5, smooth: true));
      expect(onda, lessThan(serra));
    });

    test('amplitude zero devolve o caminho intacto', () {
      final p = _circle(30);
      expect(identical(zigZag(p, 0, 1), p), isTrue);
    });
  });

  group('Inchar e encolher', () {
    test('positivo empurra a borda para fora', () {
      final antes = _raioMedio(_circle(50));
      final depois = _raioMedio(puckerBloat(_circle(50), 0.5));
      expect(depois, greaterThan(antes));
    });

    test('negativo puxa para dentro', () {
      final antes = _raioMedio(_circle(50));
      final depois = _raioMedio(puckerBloat(_circle(50), -0.5));
      expect(depois, lessThan(antes));
    });

    test('quantidade zero devolve o caminho intacto', () {
      final p = _circle(40);
      expect(identical(puckerBloat(p, 0), p), isTrue);
    });
  });

  group('Torcer', () {
    // O centro fica parado e a borda gira: num circulo, o raio nao muda.
    test('gira sem mudar o raio', () {
      final antes = _raioMedio(_circle(50));
      final depois = _raioMedio(twist(_circle(50), 90));
      expect(depois, closeTo(antes, antes * 0.05));
    });

    test('muda a forma de um retangulo', () {
      final antes = _rect(120, 40).getBounds();
      final depois = twist(_rect(120, 40), 60).getBounds();
      expect(depois.height, greaterThan(antes.height));
    });

    test('angulo zero devolve o caminho intacto', () {
      final p = _rect(60, 60);
      expect(identical(twist(p, 0), p), isTrue);
    });
  });

  group('Baguncar o caminho', () {
    // Invariante: mesma semente, mesmo caminho. Exportar duas vezes nao
    // pode dar formas diferentes.
    test('e deterministico pela semente', () {
      final a = wigglePath(_circle(50), 10, seed: 7).getBounds();
      final b = wigglePath(_circle(50), 10, seed: 7).getBounds();
      expect(a, b);
    });

    test('semente diferente da forma diferente', () {
      final a = wigglePath(_circle(50), 10, seed: 1).getBounds();
      final b = wigglePath(_circle(50), 10, seed: 2).getBounds();
      expect(a == b, isFalse);
    });

    test('a evolucao muda a bagunca sem sortear de novo', () {
      final a = wigglePath(_circle(50), 10, seed: 3).getBounds();
      final b =
          wigglePath(_circle(50), 10, seed: 3, evolution: 5).getBounds();
      expect(a == b, isFalse);
    });

    test('quantidade zero devolve o caminho intacto', () {
      final p = _circle(30);
      expect(identical(wigglePath(p, 0), p), isTrue);
    });
  });

  group('Combinar caminhos', () {
    Path esq() => Path()
      ..addRect(const Rect.fromLTWH(0, 0, 100, 100));
    Path dir() => Path()
      ..addRect(const Rect.fromLTWH(50, 0, 100, 100));

    test('uniao cobre os dois', () {
      final r = mergePaths([esq(), dir()], MergeMode.union).getBounds();
      expect(r.left, closeTo(0, 0.01));
      expect(r.right, closeTo(150, 0.01));
    });

    test('subtracao tira o segundo do primeiro', () {
      final r = mergePaths([esq(), dir()], MergeMode.subtract).getBounds();
      expect(r.left, closeTo(0, 0.01));
      expect(r.right, closeTo(50, 0.01));
    });

    test('intersecao deixa so o que se sobrepoe', () {
      final r = mergePaths([esq(), dir()], MergeMode.intersect).getBounds();
      expect(r.left, closeTo(50, 0.01));
      expect(r.right, closeTo(100, 0.01));
    });

    // O furo de verdade: excluir deixa um vazio onde os dois se cruzam,
    // em vez de pintar por cima com a cor do fundo.
    test('excluir faz furo na sobreposicao', () {
      final r = mergePaths([esq(), dir()], MergeMode.exclude);
      expect(r.contains(const Offset(25, 50)), isTrue);
      expect(r.contains(const Offset(75, 50)), isFalse,
          reason: 'a sobreposicao deveria estar vazada');
      expect(r.contains(const Offset(125, 50)), isTrue);
    });

    test('um caminho so passa direto; nenhum devolve vazio', () {
      final um = esq();
      expect(identical(mergePaths([um], MergeMode.union), um), isTrue);
      expect(mergePaths([], MergeMode.union).getBounds(), Rect.zero);
    });
  });
}
