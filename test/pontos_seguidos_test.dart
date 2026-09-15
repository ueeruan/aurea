import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aurea/src/features/editor/domain/pontos_seguidos.dart';
import 'package:aurea/src/features/editor/domain/tracker2d.dart';
import 'package:flutter_test/flutter_test.dart';

/// OS PONTOS QUE ALIMENTAM O RASTREIO DE CAMERA.
///
/// Aqui se testa a parte que olha para os pixels. O solver ja tem os
/// proprios testes e recebe coordenadas prontas; se estas coordenadas
/// estiverem erradas, ele resolve com perfeicao a cena errada.

/// Uma textura estavel: manchas suaves, com canto em toda parte.
double _textura(double x, double y) {
  var v = 0.0;
  for (var i = 1; i <= 4; i++) {
    final f = i * 0.13;
    v += math.sin(x * f + i * 1.7) * math.cos(y * f * 1.31 + i * 0.9) / i;
  }
  return v;
}

GrayFrame _quadro(int w, int h, double deslocX, double deslocY, {double escala = 1}) {
  final px = Uint8List(w * h);
  for (var y = 0; y < h; y++) {
    for (var x = 0; x < w; x++) {
      final sx = (x - w / 2) / escala + w / 2 + deslocX;
      final sy = (y - h / 2) / escala + h / 2 + deslocY;
      px[y * w + x] = (128 + 90 * _textura(sx, sy)).round().clamp(0, 255);
    }
  }
  return GrayFrame(px, w, h);
}

void main() {
  group('cantos', () {
    test('acha cantos espalhados e respeita a distancia minima', () {
      final f = _quadro(160, 100, 0, 0);
      final cantos = detectarCantos(f, maximo: 40, distanciaMinima: 10);
      expect(cantos.length, greaterThan(10));
      for (var i = 0; i < cantos.length; i++) {
        for (var j = i + 1; j < cantos.length; j++) {
          expect(
            (cantos[i] - cantos[j]).distance,
            greaterThanOrEqualTo(10 - 1e-9),
            reason: 'dois cantos colados nao acrescentam informacao',
          );
        }
      }
      // Nenhum na margem: um molde meio fora da imagem casa com qualquer
      // coisa.
      for (final c in cantos) {
        expect(c.dx, greaterThan(10));
        expect(c.dy, greaterThan(10));
        expect(c.dx, lessThan(150));
        expect(c.dy, lessThan(90));
      }
    });

    test('nao acha canto onde nao ha nada', () {
      final liso = GrayFrame(Uint8List(160 * 100)..fillRange(0, 16000, 120), 160, 100);
      expect(detectarCantos(liso), isEmpty);
    });

    test('evita nascer em cima de quem ja esta sendo seguido', () {
      final f = _quadro(160, 100, 0, 0);
      final primeiros = detectarCantos(f, maximo: 20, distanciaMinima: 12);
      final novos = detectarCantos(
        f,
        maximo: 20,
        distanciaMinima: 12,
        evitar: primeiros,
      );
      for (final n in novos) {
        for (final p in primeiros) {
          expect((n - p).distance, greaterThanOrEqualTo(12 - 1e-9));
        }
      }
    });
  });

  group('seguir', () {
    test('segue um deslocamento conhecido, com precisao de subpixel', () {
      const dx = 1.4, dy = -0.8;
      final frames = [
        for (var i = 0; i < 10; i++) _quadro(200, 120, dx * i, dy * i),
      ];
      final pontos = seguirPontos(
        frames,
        maximoDePontos: 40,
        distanciaMinima: 12,
        busca: 10,
      );
      expect(pontos.length, greaterThan(8));

      // A imagem foi amostrada DESLOCADA, entao o conteudo anda para o
      // lado contrario do deslocamento da amostragem.
      var conferidos = 0;
      for (final p in pontos) {
        final a = p.em(p.primeiroQuadro);
        final b = p.em(p.primeiroQuadro + 5);
        if (a == null || b == null) continue;
        expect(b.dx - a.dx, closeTo(-dx * 5, 1.2));
        expect(b.dy - a.dy, closeTo(-dy * 5, 1.2));
        conferidos++;
      }
      expect(conferidos, greaterThan(5));
    });

    test('repoe pontos quando o time encolhe', () {
      // Deslocamento grande: muitos pontos saem pela borda e morrem.
      final frames = [
        for (var i = 0; i < 24; i++) _quadro(200, 120, 6.0 * i, 0),
      ];
      final pontos = seguirPontos(
        frames,
        maximoDePontos: 40,
        distanciaMinima: 12,
        busca: 12,
      );
      // Alguem nasceu depois do primeiro quadro — sem reposicao, um
      // travelling longo termina sem nenhum ponto.
      expect(pontos.any((p) => p.primeiroQuadro > 0), isTrue);
      // E ha cobertura ate perto do fim.
      expect(pontos.any((p) => p.ultimoQuadro >= 20), isTrue);
    });

    test('descarta o que foi visto de relance', () {
      final frames = [
        for (var i = 0; i < 12; i++) _quadro(200, 120, 1.0 * i, 0),
      ];
      final pontos = seguirPontos(frames, duracaoMinima: 8);
      for (final p in pontos) {
        expect(p.duracao, greaterThanOrEqualTo(8));
      }
    });

    test('sequencia curta demais nao devolve nada em vez de quebrar', () {
      expect(seguirPontos(const []), isEmpty);
      expect(seguirPontos([_quadro(80, 60, 0, 0)]), isEmpty);
    });
  });

  test('o refino subpixel melhora um casamento inteiro', () {
    final a = _quadro(160, 100, 0, 0);
    final b = _quadro(160, 100, -0.5, 0);
    const alvo = Offset(80, 50);
    final (achado, _) = matchPatch(a, alvo, b, patch: 7, busca: 6);
    final fino = refinarSubpixel(a, alvo, b, achado, 7);
    // O deslocamento real do conteudo e +0,5 px em x.
    expect((fino.dx - alvo.dx - 0.5).abs(), lessThan((achado.dx - alvo.dx - 0.5).abs() + 1e-9));
    expect(fino.dy, closeTo(alvo.dy, 1.0));
  });
}
