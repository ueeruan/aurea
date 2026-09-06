import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/pixel_sort.dart';

/// Uma linha de pixels cinza, dada pelos valores 0..255.
Uint8List _linha(List<int> cinzas) {
  final out = Uint8List(cinzas.length * 4);
  for (var i = 0; i < cinzas.length; i++) {
    out[i * 4] = cinzas[i];
    out[i * 4 + 1] = cinzas[i];
    out[i * 4 + 2] = cinzas[i];
    out[i * 4 + 3] = 255;
  }
  return out;
}

List<int> _cinzas(Uint8List rgba) =>
    [for (var i = 0; i < rgba.length ~/ 4; i++) rgba[i * 4]];

void main() {
  group('trecho acima do limiar', () {
    test('ordena so o trecho claro, do escuro para o claro', () {
      // 10 | 200 250 120 180 | 20 — o trecho claro esta desordenado.
      final px = _linha([10, 200, 250, 120, 180, 20]);
      pixelSortRows(px, 6, 1, const PixelSortSpec(threshold: 0.4));
      expect(_cinzas(px), [10, 120, 180, 200, 250, 20]);
    });

    test('reverso ordena do claro para o escuro', () {
      final px = _linha([10, 200, 250, 120, 180, 20]);
      pixelSortRows(
          px, 6, 1, const PixelSortSpec(threshold: 0.4, reverse: true));
      expect(_cinzas(px), [10, 250, 200, 180, 120, 20]);
    });

    test('abaixo do limiar ordena o escuro e deixa o claro quieto', () {
      final px = _linha([90, 30, 60, 240, 220, 5, 70]);
      pixelSortRows(
          px, 7, 1, const PixelSortSpec(threshold: 0.4, above: false));
      // Dois trechos escuros: [90,30,60] e [5,70]; o claro fica.
      expect(_cinzas(px), [30, 60, 90, 240, 220, 5, 70]);
    });

    test('dois trechos separados nao se misturam', () {
      final px = _linha([250, 200, 0, 240, 210, 0]);
      pixelSortRows(px, 6, 1, const PixelSortSpec(threshold: 0.5));
      expect(_cinzas(px), [200, 250, 0, 210, 240, 0]);
    });
  });

  group('limites do trecho', () {
    test('o comprimento maximo quebra o trecho', () {
      final px = _linha([250, 240, 230, 220, 210, 200]);
      // Linha de 6, maximo de metade: dois trechos de 3.
      pixelSortRows(
          px, 6, 1, const PixelSortSpec(threshold: 0.5, maxRun: 0.5));
      expect(_cinzas(px), [230, 240, 250, 200, 210, 220]);
    });

    test('pixel transparente nunca entra num trecho', () {
      final px = _linha([250, 240, 230, 220]);
      px[2 * 4 + 3] = 0; // o terceiro e transparente: fora da imagem
      pixelSortRows(px, 4, 1, const PixelSortSpec(threshold: 0.5));
      // So [250,240] ordena; o transparente e o que vem depois ficam.
      expect(_cinzas(px), [240, 250, 230, 220]);
    });

    test('reinicio aleatorio e deterministico pela semente', () {
      Uint8List roda(int seed) {
        final px = _linha([for (var i = 0; i < 40; i++) 255 - i * 3]);
        return pixelSortRows(
            px, 40, 1, PixelSortSpec(threshold: 0.2, restart: 60, seed: seed));
      }

      expect(_cinzas(roda(7)), _cinzas(roda(7)));
      expect(_cinzas(roda(7)), isNot(_cinzas(roda(8))));
    });
  });

  group('chave', () {
    test('luminancia, matiz e saturacao ficam em 0..1', () {
      for (final c in [
        (0, 0, 0),
        (255, 255, 255),
        (255, 0, 0),
        (0, 255, 0),
        (0, 0, 255),
        (12, 200, 90)
      ]) {
        for (final k in PixelSortKey.values) {
          final v = pixelSortKeyOf(c.$1, c.$2, c.$3, k);
          expect(v, inInclusiveRange(0, 1), reason: '$c $k');
        }
      }
    });

    test('cinza nao tem matiz nem saturacao', () {
      expect(pixelSortKeyOf(128, 128, 128, PixelSortKey.hue), 0);
      expect(pixelSortKeyOf(128, 128, 128, PixelSortKey.saturation), 0);
    });
  });

  group('matte suavizado', () {
    test('um pixel escuro isolado nao quebra o trecho', () {
      // Sem suavizar, o 40 no meio corta o trecho em dois.
      final semBlur = _linha([250, 200, 40, 210, 240]);
      pixelSortRows(semBlur, 5, 1, const PixelSortSpec(threshold: 0.5));
      expect(_cinzas(semBlur), [200, 250, 40, 210, 240]);

      // Com o matte suavizado o 40 entra no trecho e tudo ordena junto.
      // maxRun em 1: o comprimento maximo nao pode cortar a linha de 5.
      final comBlur = _linha([250, 200, 40, 210, 240]);
      pixelSortRows(comBlur, 5, 1,
          const PixelSortSpec(threshold: 0.5, matteBlur: 1, maxRun: 1));
      expect(_cinzas(comBlur), [40, 200, 210, 240, 250]);
    });
  });

  group('polar', () {
    test('radial: fora da janela angular nada muda', () {
      const w = 24, h = 24;
      final px = Uint8List(w * h * 4);
      for (var i = 0; i < w * h; i++) {
        final v = (i * 37) % 256; // ruido claro/escuro
        px[i * 4] = v;
        px[i * 4 + 1] = v;
        px[i * 4 + 2] = v;
        px[i * 4 + 3] = 255;
      }
      final antes = Uint8List.fromList(px);
      final out = pixelSortPolar(
          px,
          w,
          h,
          const PixelSortSpec(
              mode: PixelSortMode.radial,
              threshold: 0.2,
              startAngle: 0,
              degreesSorted: 0.0001,
              innerRadius: 0));
      // Janela quase nula: o resultado e a imagem de entrada.
      expect(out, antes);
    });

    test('circular devolve um buffer do mesmo tamanho', () {
      const w = 20, h = 16;
      final px = _linha([for (var i = 0; i < w * h; i++) (i * 53) % 256]);
      final out = pixelSortPolar(px, w, h,
          const PixelSortSpec(mode: PixelSortMode.circular, threshold: 0.3));
      expect(out.length, w * h * 4);
    });
  });

  test('o matte e branco onde o limiar passa', () {
    final px = _linha([10, 200, 250]);
    final m = pixelSortMatte(px, 3, 1, const PixelSortSpec(threshold: 0.5));
    expect(_cinzas(m), [0, 255, 255]);
  });
}
