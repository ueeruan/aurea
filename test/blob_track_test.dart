import 'dart:convert';
import 'dart:typed_data';
import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/blob_track.dart';
import 'package:aurea/src/features/editor/domain/tracker2d.dart';

/// Uma mascara com retangulos ligados.
Uint8List _mask(int w, int h, List<Rect> caixas) {
  final m = Uint8List(w * h);
  for (final r in caixas) {
    for (var y = r.top.toInt(); y < r.bottom.toInt(); y++) {
      for (var x = r.left.toInt(); x < r.right.toInt(); x++) {
        if (x >= 0 && y >= 0 && x < w && y < h) m[y * w + x] = 1;
      }
    }
  }
  return m;
}

/// Um quadro com um retangulo claro sobre fundo escuro.
GrayFrame _quadro(int w, int h, Rect r, {int fundo = 20, int frente = 200}) {
  final px = Uint8List(w * h)..fillRange(0, w * h, fundo);
  for (var y = r.top.toInt(); y < r.bottom.toInt(); y++) {
    for (var x = r.left.toInt(); x < r.right.toInt(); x++) {
      if (x >= 0 && y >= 0 && x < w && y < h) px[y * w + x] = frente;
    }
  }
  return GrayFrame(px, w, h);
}

void main() {
  group('Achar as regioes', () {
    test('dois retangulos separados dao dois blobs', () {
      final m = _mask(100, 100, [
        const Rect.fromLTRB(5, 5, 25, 25),
        const Rect.fromLTRB(60, 60, 90, 90),
      ]);
      final r = labelRegions(m, 100, 100, minArea: 10);
      expect(r, hasLength(2));
    });

    test('a caixa envolve a regiao', () {
      final m = _mask(100, 100, [const Rect.fromLTRB(10, 20, 30, 50)]);
      final b = labelRegions(m, 100, 100, minArea: 10).single;
      expect(b.rect.left, 10);
      expect(b.rect.top, 20);
      expect(b.rect.right, 30);
      expect(b.rect.bottom, 50);
      expect(b.area, 20 * 30);
    });

    // O filtro de tamanho minimo e o que separa objeto de ruido: sem
    // ele, cada pixel solto vira um blob e a tela enche de caixinhas.
    test('regiao pequena demais e descartada', () {
      final m = _mask(100, 100, [
        const Rect.fromLTRB(5, 5, 7, 7),
        const Rect.fromLTRB(40, 40, 70, 70),
      ]);
      expect(labelRegions(m, 100, 100, minArea: 100), hasLength(1));
    });

    test('regiao grande demais tambem sai', () {
      final m = _mask(100, 100, [const Rect.fromLTRB(0, 0, 100, 100)]);
      expect(labelRegions(m, 100, 100, minArea: 10, maxArea: 500), isEmpty);
    });

    // Com limite, o que sobra tem de ser o que IMPORTA — nao o que
    // apareceu primeiro na varredura.
    test('com limite, sobram os maiores', () {
      final m = _mask(200, 200, [
        const Rect.fromLTRB(5, 5, 15, 15),
        const Rect.fromLTRB(30, 30, 90, 90),
        const Rect.fromLTRB(120, 120, 190, 190),
      ]);
      final r = labelRegions(m, 200, 200, minArea: 10, maxBlobs: 2);
      expect(r, hasLength(2));
      expect(r.first.area, greaterThan(r.last.area));
      expect(r.every((b) => b.area > 1000), isTrue);
    });

    test('mascara vazia nao acha nada', () {
      expect(labelRegions(Uint8List(100), 10, 10), isEmpty);
      expect(labelRegions(Uint8List(0), 0, 0), isEmpty);
    });
  });

  group('Fundir vizinhos', () {
    // Uma pessoa vira dois blobs quando o contraste falha no meio;
    // fundir devolve a pessoa inteira.
    test('caixas proximas viram uma', () {
      const a = Blob(id: -1, rect: Rect.fromLTRB(0, 0, 20, 20), area: 400);
      const b = Blob(id: -1, rect: Rect.fromLTRB(25, 0, 45, 20), area: 400);
      final r = mergeNearby([a, b], 10);
      expect(r, hasLength(1));
      expect(r.single.rect.right, 45);
      expect(r.single.area, 800);
    });

    test('caixas longe continuam separadas', () {
      const a = Blob(id: -1, rect: Rect.fromLTRB(0, 0, 20, 20), area: 400);
      const b = Blob(id: -1, rect: Rect.fromLTRB(200, 0, 220, 20), area: 400);
      expect(mergeNearby([a, b], 10), hasLength(2));
    });

    test('distancia zero nao funde nada', () {
      const a = Blob(id: -1, rect: Rect.fromLTRB(0, 0, 20, 20), area: 400);
      const b = Blob(id: -1, rect: Rect.fromLTRB(21, 0, 40, 20), area: 400);
      expect(mergeNearby([a, b], 0), hasLength(2));
    });
  });

  group('Identidade estavel', () {
    // O REQUISITO CRITICO: o blob 3 continua sendo o 3 no quadro
    // seguinte. Sem isso as caixas piscam e os numeros dancam.
    test('o mesmo objeto mantem o id ao andar', () {
      final m = BlobMatcher(smoothing: 0);
      final a = m.update([
        const Blob(id: -1, rect: Rect.fromLTRB(10, 10, 30, 30), area: 400),
      ]);
      final id = a.single.id;
      final b = m.update([
        const Blob(id: -1, rect: Rect.fromLTRB(14, 12, 34, 32), area: 400),
      ]);
      expect(b.single.id, id);
    });

    test('objeto novo ganha id novo', () {
      final m = BlobMatcher(smoothing: 0);
      final a = m.update([
        const Blob(id: -1, rect: Rect.fromLTRB(10, 10, 30, 30), area: 400),
      ]);
      final b = m.update([
        const Blob(id: -1, rect: Rect.fromLTRB(10, 10, 30, 30), area: 400),
        const Blob(id: -1, rect: Rect.fromLTRB(200, 200, 230, 230), area: 900),
      ]);
      expect(b, hasLength(2));
      expect(b.map((x) => x.id).toSet().length, 2);
      expect(b.map((x) => x.id), contains(a.single.id));
    });

    test('dois objetos nao trocam de id', () {
      final m = BlobMatcher(smoothing: 0);
      final a = m.update([
        const Blob(id: -1, rect: Rect.fromLTRB(10, 10, 30, 30), area: 400),
        const Blob(id: -1, rect: Rect.fromLTRB(150, 10, 170, 30), area: 400),
      ]);
      final esquerda = a.first.id;
      final direita = a.last.id;
      final b = m.update([
        const Blob(id: -1, rect: Rect.fromLTRB(14, 10, 34, 30), area: 400),
        const Blob(id: -1, rect: Rect.fromLTRB(146, 10, 166, 30), area: 400),
      ]);
      expect(b.firstWhere((x) => x.rect.left < 100).id, esquerda);
      expect(b.firstWhere((x) => x.rect.left > 100).id, direita);
    });

    // PERSISTENCIA: falha curta de deteccao nao pode apagar a caixa.
    test('o blob sobrevive alguns quadros sem deteccao', () {
      final m = BlobMatcher(persistence: 3, smoothing: 0);
      final a = m.update([
        const Blob(id: -1, rect: Rect.fromLTRB(10, 10, 30, 30), area: 400),
      ]);
      final id = a.single.id;
      expect(m.update(const []).single.id, id);
      expect(m.update(const []).single.id, id);
      expect(m.update(const []).single.id, id);
      // Passou da persistencia: agora some.
      expect(m.update(const []), isEmpty);
    });

    // Caixa tremendo e o que mais denuncia rastreio caseiro.
    test('a suavizacao segura a caixa', () {
      final duro = BlobMatcher(smoothing: 0);
      final macio = BlobMatcher(smoothing: 0.9);
      const p1 = Blob(id: -1, rect: Rect.fromLTRB(10, 10, 30, 30), area: 400);
      const p2 = Blob(id: -1, rect: Rect.fromLTRB(30, 10, 50, 30), area: 400);
      duro.update([p1]);
      macio.update([p1]);
      final d = duro.update([p2]).single.rect.left;
      final s = macio.update([p2]).single.rect.left;
      expect(d, 30);
      expect(s, lessThan(d));
      expect(s, greaterThan(10));
    });
  });

  group('Deteccao por modo', () {
    test('movimento acha o que mudou', () {
      final a = _quadro(80, 60, const Rect.fromLTRB(10, 10, 30, 30));
      final b = _quadro(80, 60, const Rect.fromLTRB(40, 10, 60, 30));
      final m = detectionMask(b,
          previous: a, by: BlobDetectBy.motion, threshold: 60);
      final r = labelRegions(m, 80, 60, minArea: 50);
      expect(r, isNotEmpty);
    });

    test('sem quadro anterior, movimento nao acha nada', () {
      final a = _quadro(80, 60, const Rect.fromLTRB(10, 10, 30, 30));
      final m = detectionMask(a, by: BlobDetectBy.motion);
      expect(m.every((v) => v == 0), isTrue);
    });

    test('brilho acha a regiao clara', () {
      final a = _quadro(80, 60, const Rect.fromLTRB(10, 10, 40, 40));
      final m = detectionMask(a, by: BlobDetectBy.brightness, threshold: 150);
      final r = labelRegions(m, 80, 60, minArea: 50).single;
      expect(r.rect.width, closeTo(30, 1));
    });

    test('bordas acham o contorno, nao o miolo', () {
      final a = _quadro(80, 60, const Rect.fromLTRB(20, 20, 50, 50));
      final m = detectionMask(a, by: BlobDetectBy.edges, threshold: 100);
      // O centro do retangulo e liso: nao e borda.
      expect(m[35 * 80 + 35], 0);
      // A beirada e.
      expect(m[35 * 80 + 20], 1);
    });
  });

  group('Analise gravada', () {
    List<GrayFrame> sequencia() => [
          for (var i = 0; i < 8; i++)
            _quadro(80, 60,
                Rect.fromLTWH(5 + i * 6.0, 20, 20, 20)),
        ];

    test('roda a sequencia inteira', () {
      final d = analyzeBlobs(sequencia(),
          fps: 12, threshold: 60, minArea: 50, sensitivity: 20);
      expect(d.frames, hasLength(8));
      expect(d.width, 80);
      expect(d.fps, 12);
    });

    // Rastreio depende do quadro anterior; guardando as caixas, desenhar
    // vira consulta — e ai o seek e instantaneo.
    test('a consulta por tempo cai no quadro certo', () {
      final d = analyzeBlobs(sequencia(), fps: 10, threshold: 60,
          minArea: 50, sensitivity: 20);
      expect(d.at(const Duration(milliseconds: 0)), d.frames[0]);
      expect(d.at(const Duration(milliseconds: 250)), d.frames[2]);
      // Fora do intervalo nao inventa caixa.
      expect(d.at(const Duration(seconds: 99)), isEmpty);
      expect(d.at(const Duration(seconds: -1)), isEmpty);
    });

    test('a ida e volta pelo arquivo preserva as caixas', () {
      final d = analyzeBlobs(sequencia(), fps: 12, threshold: 60,
          minArea: 50, sensitivity: 20);
      final volta = BlobTrackData.decode(
          const JsonEncoder().convert(d.toJson()))!;
      expect(volta.frames.length, d.frames.length);
      expect(volta.fps, d.fps);
      for (var i = 0; i < d.frames.length; i++) {
        expect(volta.frames[i].length, d.frames[i].length);
      }
    });

    test('arquivo estragado devolve null', () {
      expect(BlobTrackData.decode('nao e json'), isNull);
      expect(BlobTrackData.decode('{}'), isNull);
    });

    test('sequencia vazia nao quebra', () {
      final d = analyzeBlobs(const [], fps: 12);
      expect(d.isEmpty, isTrue);
      expect(d.at(Duration.zero), isEmpty);
    });
  });
}
