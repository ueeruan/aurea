// BANCADA: `drawImage` COM DESLOCAMENTO PERDE O ALFA?
//
// O Drop Shadow morreu tres vezes porque a sombra saia branca ou sumia, e
// a pista que sobrou foi esta: num `PictureRecorder` limpo, a MESMA
// imagem desenhada em `Offset(21,21)` voltava com alfa 0, e em
// `Offset.zero` voltava certa.
//
// Se isso for verdade do motor, metade dos efeitos que desenham deslocado
// esta condenada. Se for da bancada (imagem crua por `ImageDescriptor.raw`,
// codec descartado logo depois, canvas do gravador), entao o mecanismo
// bom e outro.
//
// Esta bancada elimina as duas suspeitas de uma vez:
//   * a imagem vem de BYTES PNG por `ui.instantiateImageCodec`, que e o
//     caminho que o app usa de verdade (nao `ImageDescriptor.raw`);
//   * o desenho acontece em TRES superficies: o canvas do gravador, um
//     `RepaintBoundary` de verdade (o caminho que o palco usa) e um
//     segundo gravador onde a imagem chega por `drawPicture`.
//
// Le sempre o pixel do MEIO da imagem desenhada, nunca a borda.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_test/flutter_test.dart';

/// PNG 64x64 vermelho opaco, montado byte a byte.
Uint8List _pngSolido(int w, int h, int r, int g, int b, int a) {
  final cru = BytesBuilder();
  for (var y = 0; y < h; y++) {
    cru.addByte(0);
    for (var x = 0; x < w; x++) {
      cru
        ..addByte(r)
        ..addByte(g)
        ..addByte(b)
        ..addByte(a);
    }
  }
  return _png(w, h, cru.toBytes());
}

Uint8List _png(int w, int h, Uint8List cru) {
  final z = ZLibEncoder().convert(cru);
  final saida = BytesBuilder()
    ..add(const [0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
  final ihdr = BytesBuilder()
    ..add(_u32(w))
    ..add(_u32(h))
    ..add(const [8, 6, 0, 0, 0]);
  saida
    ..add(_bloco('IHDR', ihdr.toBytes()))
    ..add(_bloco('IDAT', z))
    ..add(_bloco('IEND', Uint8List(0)));
  return saida.toBytes();
}

List<int> _u32(int v) => [(v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255];

Uint8List _bloco(String tipo, Uint8List dados) {
  final t = tipo.codeUnits;
  final corpo = Uint8List(4 + dados.length)
    ..setRange(0, 4, t)
    ..setRange(4, 4 + dados.length, dados);
  final b = BytesBuilder()
    ..add(_u32(dados.length))
    ..add(corpo)
    ..add(_u32(_crc(corpo)));
  return b.toBytes();
}

int _crc(Uint8List d) {
  var c = 0xFFFFFFFF;
  for (final byte in d) {
    c ^= byte;
    for (var i = 0; i < 8; i++) {
      c = (c & 1) != 0 ? (c >> 1) ^ 0xEDB88320 : c >> 1;
    }
  }
  return (c ^ 0xFFFFFFFF) & 0xFFFFFFFF;
}

/// Deflate "stored" — sem compressao, e o que a bancada do PNG ja usava.
class ZLibEncoder {
  Uint8List convert(Uint8List dados) {
    final b = BytesBuilder()..add(const [0x78, 0x01]);
    const maxBloco = 65535;
    var off = 0;
    while (off < dados.length) {
      final n = (dados.length - off) > maxBloco ? maxBloco : dados.length - off;
      final ultimo = off + n >= dados.length ? 1 : 0;
      b
        ..addByte(ultimo)
        ..addByte(n & 255)
        ..addByte((n >> 8) & 255)
        ..addByte(~n & 255)
        ..addByte((~n >> 8) & 255);
      b.add(dados.sublist(off, off + n));
      off += n;
    }
    var a = 1, s = 0;
    for (final v in dados) {
      a = (a + v) % 65521;
      s = (s + a) % 65521;
    }
    b
      ..addByte((s >> 8) & 255)
      ..addByte(s & 255)
      ..addByte((a >> 8) & 255)
      ..addByte(a & 255);
    return b.toBytes();
  }
}

/// Le o pixel (x,y) de um `ui.Image` em RGBA cru.
Future<List<int>> _pixel(ui.Image im, int x, int y) async {
  final dados = await im.toByteData(format: ui.ImageByteFormat.rawRgba);
  final bytes = dados!.buffer.asUint8List();
  final i = (y * im.width + x) * 4;
  return [bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]];
}

void main() {
  const lado = 64;
  const desloc = 20.0;
  const tela = 120;
  const deslocInt = 20;

  late ui.Image vermelha;

  setUpAll(() async {
    final bytes = _pngSolido(lado, lado, 255, 0, 0, 255);
    final codec = await ui.instantiateImageCodec(bytes);
    vermelha = (await codec.getNextFrame()).image;
  });

  test('controle: a imagem decodificada de PNG tem os pixels certos', () async {
    final p = await _pixel(vermelha, 32, 32);
    expect(p, [255, 0, 0, 255], reason: 'a fonte da bancada tem de estar opaca');
  });

  test('grava em Offset.zero e le o centro — alfa 255', () async {
    final gravador = ui.PictureRecorder();
    Canvas(gravador).drawImage(vermelha, Offset.zero, Paint());
    final quadro = await gravador.endRecording().toImage(tela, tela);
    final p = await _pixel(quadro, lado ~/ 2, lado ~/ 2);
    expect(p, [255, 0, 0, 255]);
  });

  test('grava em Offset(20,20) e le o centro da imagem — alfa 255', () async {
    final gravador = ui.PictureRecorder();
    Canvas(gravador).drawImage(vermelha, const Offset(desloc, desloc), Paint());
    final quadro = await gravador.endRecording().toImage(tela, tela);
    final p = await _pixel(quadro, desloc.toInt() + lado ~/ 2, desloc.toInt() + lado ~/ 2);
    expect(
      p,
      [255, 0, 0, 255],
      reason: 'se o alfa cair aqui, o deslocamento e que quebra o desenho',
    );
  });

  test('grava as duas juntas: as duas sobrevivem', () async {
    final gravador = ui.PictureRecorder();
    final c = Canvas(gravador)
      ..drawImage(vermelha, Offset.zero, Paint())
      ..drawImage(vermelha, const Offset(desloc + 40, desloc), Paint());
    final quadro = await gravador.endRecording().toImage(tela, tela);
    expect(await _pixel(quadro, lado ~/ 2, lado ~/ 2), [255, 0, 0, 255]);
    expect(await _pixel(quadro, desloc.toInt() + 40 + lado ~/ 2, desloc.toInt() + lado ~/ 2),
        [255, 0, 0, 255]);
  });

  test('via drawPicture: deslocamento sobrevive a um segundo gravador',
      () async {
    final interno = ui.PictureRecorder();
    Canvas(interno).drawImage(vermelha, const Offset(desloc, desloc), Paint());
    final pic = interno.endRecording();
    final externo = ui.PictureRecorder();
    Canvas(externo).drawPicture(pic);
    final quadro = await externo.endRecording().toImage(tela, tela);
    expect(await _pixel(quadro, desloc.toInt() + lado ~/ 2, desloc.toInt() + lado ~/ 2),
        [255, 0, 0, 255]);
  });

  testWidgets('num RepaintBoundary de verdade, o deslocamento pinta opaco',
      (tester) async {
    final chave = GlobalKey();
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: Center(
          child: RepaintBoundary(
            key: chave,
            child: SizedBox(
              width: tela.toDouble(),
              height: tela.toDouble(),
              child: CustomPaint(
                painter: _Desenha(vermelha, const Offset(desloc, desloc)),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();

    await tester.runAsync(() async {
      final limite =
          chave.currentContext!.findRenderObject()! as RenderRepaintBoundary;
      final quadro = await limite.toImage();
      final p = await _pixel(quadro, desloc.toInt() + lado ~/ 2, desloc.toInt() + lado ~/ 2);
      expect(
        p,
        [255, 0, 0, 255],
        reason: 'este e o caminho que o palco usa; o alfa tem de sobreviver',
      );
    });
  });
}

class _Desenha extends CustomPainter {
  _Desenha(this.imagem, this.onde);
  final ui.Image imagem;
  final Offset onde;

  @override
  void paint(Canvas canvas, Size size) {
    canvas.drawImage(imagem, onde, Paint());
  }

  @override
  bool shouldRepaint(_Desenha old) => old.imagem != imagem || old.onde != onde;
}
