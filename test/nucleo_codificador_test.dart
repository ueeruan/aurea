import 'dart:io';
import 'dart:typed_data';

import 'package:aurea_core/aurea_core.dart';
import 'package:flutter_test/flutter_test.dart';

/// O NUCLEO C++, PRIMEIRA FATIA: a conversao de cor do codificador tem de
/// dar os MESMOS bytes do caminho em Kotlin (VideoEncoder.rgbaToNv12), para
/// o video exportado sair igual ao de antes.
Uint8List _nv12Kotlin(Uint8List rgba, int w, int h) {
  int c(int v) => v < 0 ? 0 : (v > 255 ? 255 : v);
  final out = Uint8List(w * h * 3 ~/ 2);
  var yi = 0, uvi = w * h, p = 0;
  for (var j = 0; j < h; j++) {
    for (var i = 0; i < w; i++) {
      final r = rgba[p], g = rgba[p + 1], b = rgba[p + 2];
      p += 4;
      out[yi++] = c(((47 * r + 157 * g + 16 * b + 128) >> 8) + 16);
      if (j.isEven && i.isEven) {
        out[uvi++] = c(((-26 * r - 87 * g + 113 * b + 128) >> 8) + 128);
        out[uvi++] = c(((113 * r - 102 * g - 11 * b + 128) >> 8) + 128);
      }
    }
  }
  return out;
}

void main() {
  test('o nucleo carrega no PC', () {
    expect(nucleoCarregado, isTrue);
  });

  test('RGBA -> NV12 BT.709 igual ao Kotlin, byte a byte', () {
    const w = 64, h = 38;
    final rgba = Uint8List(w * h * 4);
    for (var i = 0; i < rgba.length; i++) {
      rgba[i] = (i * 37 + (i >> 3) * 11) & 0xFF;
    }
    expect(rgbaParaNv12(rgba, w, h), _nv12Kotlin(rgba, w, h));
    // Dimensao impar e recusada (o codificador arredonda antes).
    expect(rgbaParaNv12(Uint8List(3 * 2 * 4), 3, 2), isNull);
  });

  test('fora do Android o codificador do nucleo nao se oferece', () {
    if (!Platform.isAndroid) expect(CodificadorNativo.disponivel, isFalse);
  });
}
