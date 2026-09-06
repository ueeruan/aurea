import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

/// TEXTURAS E RUIDO PROCEDURAIS para os modelos montados em codigo.
///
/// Tudo determinista: hash inteiro, sem random nem relogio, entao abrir
/// um modelo duas vezes da exatamente a mesma imagem — e os testes podem
/// cobrar isso.

/// Hash inteiro em [0, 1).
double ruido(int i) {
  var x = (i * 374761393 + 668265263) & 0x7fffffff;
  x = ((x ^ (x >> 13)) * 1274126177) & 0x7fffffff;
  return (x ^ (x >> 16)) / 0x7fffffff;
}

double suave(double t) => t * t * (3 - 2 * t);

/// Ruido de valor 2D em trelica, [oitavas] oitavas.
double fbm(double x, double z, {int oitavas = 3, int semente = 0}) {
  var soma = 0.0, peso = 1.0, total = 0.0;
  var fx = x, fz = z;
  for (var o = 0; o < oitavas; o++) {
    final ix = fx.floor(), iz = fz.floor();
    final tx = suave(fx - ix), tz = suave(fz - iz);
    double canto(int a, int b) =>
        ruido(a * 73856093 ^ b * 19349663 ^ (semente + o) * 83492791);
    final v = (canto(ix, iz) * (1 - tx) + canto(ix + 1, iz) * tx) * (1 - tz) +
        (canto(ix, iz + 1) * (1 - tx) + canto(ix + 1, iz + 1) * tx) * tz;
    soma += v * peso;
    total += peso;
    peso *= .5;
    fx *= 2.03;
    fz *= 2.03;
  }
  return soma / total;
}

int canal8(double v) => (v * 255).round().clamp(0, 255);

/// PNG minimo em Dart puro (RGB, zlib do dart:io), como data URI: o
/// cache de texturas le `data:` direto, e o projeto salvo carrega a
/// textura junto — sem depender de arquivo no aparelho.
String pngDataUri(int w, int h, void Function(int x, int y, Uint8List rgb) pixel) {
  final raw = Uint8List(h * (1 + w * 3));
  final rgb = Uint8List(3);
  var k = 0;
  for (var y = 0; y < h; y++) {
    raw[k++] = 0; // filtro: nenhum
    for (var x = 0; x < w; x++) {
      pixel(x, y, rgb);
      raw[k++] = rgb[0];
      raw[k++] = rgb[1];
      raw[k++] = rgb[2];
    }
  }
  final idat = ZLibEncoder(level: 6).convert(raw);
  final out = BytesBuilder();
  out.add(const [137, 80, 78, 71, 13, 10, 26, 10]);
  void chunk(String tipo, List<int> dados) {
    final t = ascii.encode(tipo);
    final len = ByteData(4)..setUint32(0, dados.length);
    out.add(len.buffer.asUint8List());
    final corpo = Uint8List(t.length + dados.length)
      ..setAll(0, t)
      ..setAll(t.length, dados);
    out.add(corpo);
    final crc = ByteData(4)..setUint32(0, _crc32(corpo));
    out.add(crc.buffer.asUint8List());
  }

  final ihdr = ByteData(13)
    ..setUint32(0, w)
    ..setUint32(4, h)
    ..setUint8(8, 8)
    ..setUint8(9, 2)
    ..setUint8(10, 0)
    ..setUint8(11, 0)
    ..setUint8(12, 0);
  chunk('IHDR', ihdr.buffer.asUint8List());
  chunk('IDAT', idat);
  chunk('IEND', const []);
  return 'data:image/png;base64,${base64Encode(out.toBytes())}';
}

final List<int> _crcTabela = List.generate(256, (n) {
  var c = n;
  for (var k = 0; k < 8; k++) {
    c = (c & 1) != 0 ? 0xEDB88320 ^ (c >> 1) : c >> 1;
  }
  return c;
});

int _crc32(List<int> dados) {
  var c = 0xFFFFFFFF;
  for (final b in dados) {
    c = _crcTabela[(c ^ b) & 0xFF] ^ (c >> 8);
  }
  return (c ^ 0xFFFFFFFF) & 0xFFFFFFFF;
}
