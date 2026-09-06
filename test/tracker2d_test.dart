import 'dart:math' as math;
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/tracker2d.dart';

/// Um quadro com um borrao claro em [cx],[cy] sobre fundo com textura.
///
/// A textura importa: fundo liso tem variancia zero, e ai NENHUM
/// rastreador funciona — nem este nem o do After Effects.
GrayFrame _quadro(double cx, double cy,
    {int w = 120, int h = 90, int brilho = 0}) {
  final px = Uint8List(w * h);
  for (var y = 0; y < h; y++) {
    for (var x = 0; x < w; x++) {
      // Textura de fundo: xadrez suave.
      var v = 60 + ((x ~/ 7 + y ~/ 7) % 2) * 25;
      final d = math.sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
      if (d < 8) v = 220;
      px[y * w + x] = (v + brilho).clamp(0, 255);
    }
  }
  return GrayFrame(px, w, h);
}

void main() {
  group('Semelhanca', () {
    test('o mesmo pedaco casa consigo mesmo', () {
      final a = _quadro(60, 45);
      expect(ncc(a, const Offset(60, 45), a, const Offset(60, 45), 10),
          closeTo(1, 0.001));
    });

    // Normalizada: a cena escurecer NAO pode parecer que o objeto andou.
    test('mudar a luz nao muda a semelhanca', () {
      final a = _quadro(60, 45);
      final b = _quadro(60, 45, brilho: 40);
      expect(ncc(a, const Offset(60, 45), b, const Offset(60, 45), 10),
          greaterThan(0.95));
    });

    // Area lisa nao da para rastrear, e fingir que da e o que faz o
    // rastreio escorregar sem ninguem entender por que.
    test('area lisa devolve zero', () {
      final liso = GrayFrame(Uint8List(100 * 100)..fillRange(0, 10000, 128),
          100, 100);
      expect(ncc(liso, const Offset(50, 50), liso, const Offset(50, 50), 8),
          0);
    });
  });

  group('Achar o pedaco', () {
    test('acha o alvo que andou', () {
      final a = _quadro(40, 45);
      final b = _quadro(52, 45);
      final (p, s) = matchPatch(a, const Offset(40, 45), b);
      expect(p.dx, closeTo(52, 2));
      expect(p.dy, closeTo(45, 2));
      expect(s, greaterThan(0.7));
    });

    test('acha tambem na diagonal', () {
      final a = _quadro(40, 30);
      final b = _quadro(50, 40);
      final (p, _) = matchPatch(a, const Offset(40, 30), b);
      expect(p.dx, closeTo(50, 2));
      expect(p.dy, closeTo(40, 2));
    });
  });

  group('Seguir a sequencia', () {
    test('acompanha o alvo em cada quadro', () {
      final frames = [
        for (var i = 0; i < 6; i++) _quadro(40 + i * 6.0, 45),
      ];
      final track = trackSequence(frames, const Offset(40, 45));
      expect(track, hasLength(6));
      expect(track.last.position.dx, closeTo(70, 3));
      for (final p in track) {
        expect(p.confidence, greaterThan(0.5));
      }
    });

    test('sequencia vazia devolve vazio', () {
      expect(trackSequence(const [], Offset.zero), isEmpty);
    });

    // Um salto e sempre pior que um travamento: perdido, fica onde
    // estava em vez de pular para o outro lado da tela.
    test('perdendo o alvo, trava em vez de saltar', () {
      final frames = [
        _quadro(40, 45),
        GrayFrame(Uint8List(120 * 90), 120, 90),
      ];
      final track = trackSequence(frames, const Offset(40, 45));
      expect(track.last.position, const Offset(40, 45));
      expect(track.last.confidence, lessThan(0.5));
    });
  });

  group('Suavizar', () {
    test('a media movel tira o tremor e mantem o rumo', () {
      // Uma panoramica de 1 px por quadro, com tremor de +-3.
      final pontos = [
        for (var i = 0; i < 40; i++)
          Offset(i * 1.0 + (i.isEven ? 3 : -3), 0)
      ];
      final suave = smoothPath(pontos, janela: 9);
      // O rumo sobrevive...
      expect(suave.last.dx - suave.first.dx, greaterThan(25));
      // ...e o tremor sumiu.
      var maiorSalto = 0.0;
      for (var i = 1; i < suave.length; i++) {
        maiorSalto =
            math.max(maiorSalto, (suave[i].dx - suave[i - 1].dx).abs());
      }
      expect(maiorSalto, lessThan(2));
    });

    test('janela 1 nao mexe em nada', () {
      final p = [const Offset(0, 0), const Offset(9, 9)];
      expect(smoothPath(p, janela: 1), p);
    });

    test('lista vazia continua vazia', () {
      expect(smoothPath(const []), isEmpty);
    });
  });

  group('Estabilizar', () {
    List<TrackPoint> tremido() => [
          for (var i = 0; i < 30; i++)
            TrackPoint(
              frame: i,
              position: Offset(i.isEven ? 4 : -4, i % 3 == 0 ? 3 : -3),
              confidence: 1,
            ),
        ];

    test('o deslocamento cancela o tremor', () {
      final offs = stabilizeOffsets(tremido(), janela: 9);
      expect(offs, hasLength(30));
      // Cada deslocamento aponta contra o tremor daquele quadro.
      var contra = 0;
      for (var i = 0; i < 30; i++) {
        if (offs[i].dx.sign != tremido()[i].position.dx.sign) contra++;
      }
      expect(contra, greaterThan(20));
    });

    // A imagem nao pode escorregar para um canto ao longo do clipe.
    test('a soma dos deslocamentos e praticamente zero', () {
      final offs = stabilizeOffsets(tremido(), janela: 9);
      final soma = offs.fold(Offset.zero, (Offset a, b) => a + b);
      expect(soma.dx.abs(), lessThan(2));
      expect(soma.dy.abs(), lessThan(2));
    });

    test('rastreio vazio nao gera deslocamento', () {
      expect(stabilizeOffsets(const []), isEmpty);
    });

    // Estabilizar sem ampliar mostra o vazio nas bordas — e o defeito
    // que denuncia estabilizacao caseira na hora.
    test('a ampliacao cobre o maior deslocamento', () {
      final z = stabilizeZoom(
          const [Offset(10, 0), Offset(-10, 0)], 100, 100);
      expect(z, closeTo(1.2, 0.001));
    });

    test('sem deslocamento, nao amplia', () {
      expect(stabilizeZoom(const [Offset.zero], 100, 100), 1);
      expect(stabilizeZoom(const [], 100, 100), 1);
    });

    test('nunca reduz', () {
      expect(stabilizeZoom(const [Offset(1, 1)], 100, 100),
          greaterThanOrEqualTo(1));
    });
  });
}
