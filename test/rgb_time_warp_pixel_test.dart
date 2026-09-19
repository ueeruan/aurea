// A REGRA DE COMPOSICAO DOS TRES CANAIS, MEDIDA NO PIXEL.
//
// O RGB Time Warp empilha tres imagens isoladas por canal: o vermelho
// entra normal e verde e azul entram por SOMA. Este arquivo prova a REGRA
// em que o efeito se apoia — que somar premultiplicado com o alfa em 1
// devolve exatamente o canal, sem escurecer nem estourar, e que a ordem
// importa.
//
// Ele monta a mesma pilha que o palco monta (mesmos filtros de canal,
// mesmo `BlendMode.plus`) num gravador de quadro proprio — o caminho de
// widget nao entra aqui porque o `BlendMask` do app fotografa a camada, e
// a foto nao termina dentro de um teste.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

Future<ui.Image> _liso(int r, int g, int b, {int lado = 8}) async {
  final px = Uint8List(lado * lado * 4);
  for (var i = 0; i < px.length; i += 4) {
    px[i] = r;
    px[i + 1] = g;
    px[i + 2] = b;
    px[i + 3] = 255;
  }
  final buf = await ui.ImmutableBuffer.fromUint8List(px);
  final d = ui.ImageDescriptor.raw(
    buf,
    width: lado,
    height: lado,
    pixelFormat: ui.PixelFormat.rgba8888,
  );
  final codec = await d.instantiateCodec();
  return (await codec.getNextFrame()).image;
}

/// O MESMO filtro de canal do palco (ver `isoDeCanal`).
ColorFilter _soCanal(int canal) {
  const zeros = [0.0, 0.0, 0.0, 0.0, 0.0];
  final linhas = [
    canal == 0 ? const [1.0, 0.0, 0.0, 0.0, 0.0] : zeros,
    canal == 1 ? const [0.0, 1.0, 0.0, 0.0, 0.0] : zeros,
    canal == 2 ? const [0.0, 0.0, 1.0, 0.0, 0.0] : zeros,
    const [0.0, 0.0, 0.0, 1.0, 0.0],
  ];
  return ColorFilter.matrix([for (final l in linhas) ...l]);
}

Future<({int r, int g, int b, int a})> _compor(
  List<ui.Image> fontes, {
  int lado = 8,
}) async {
  final rec = ui.PictureRecorder();
  final canvas = Canvas(rec);
  for (var c = 0; c < fontes.length; c++) {
    final tinta = Paint()
      ..colorFilter = _soCanal(c)
      ..blendMode = c == 0 ? BlendMode.srcOver : BlendMode.plus;
    canvas.drawImageRect(
      fontes[c],
      Rect.fromLTWH(0, 0, lado.toDouble(), lado.toDouble()),
      Rect.fromLTWH(0, 0, lado.toDouble(), lado.toDouble()),
      tinta,
    );
  }
  final img = await rec.endRecording().toImage(lado, lado);
  final dados = await img.toByteData(format: ui.ImageByteFormat.rawRgba);
  img.dispose();
  final p = dados!.buffer.asUint8List();
  final meio = ((lado ~/ 2) * lado + lado ~/ 2) * 4;
  return (r: p[meio], g: p[meio + 1], b: p[meio + 2], a: p[meio + 3]);
}

void main() {
  test('os tres canais somados devolvem a cor de cada um', () async {
    final vermelho = await _liso(200, 10, 20);
    final verde = await _liso(30, 180, 40);
    final azul = await _liso(50, 60, 200);
    final q = await _compor([vermelho, verde, azul]);
    // CADA CANAL VEIO DA SUA IMAGEM: o R da primeira, o G da segunda, o
    // B da terceira — e nao um apagando o outro.
    expect(q.r, closeTo(200, 2));
    expect(q.g, closeTo(180, 2));
    expect(q.b, closeTo(200, 2));
    expect(q.a, closeTo(255, 2));
    vermelho.dispose();
    verde.dispose();
    azul.dispose();
  });

  test('DOIS canais: o segundo nao apaga o primeiro', () async {
    // O caso que a ordem errada estragaria: somar com o alfa ja em 1
    // devolve o canal, e nao escurece o que veio antes.
    final vermelho = await _liso(220, 0, 0);
    final verde = await _liso(0, 160, 0);
    final q = await _compor([vermelho, verde]);
    expect(q.r, closeTo(220, 2));
    expect(q.g, closeTo(160, 2));
    expect(q.b, closeTo(0, 2));
    vermelho.dispose();
    verde.dispose();
  });

  test('canais tirados do MESMO quadro devolvem o quadro', () async {
    // Deslocamento zero nos tres = a imagem de agora, intacta. E o
    // neutro do efeito.
    final img = await _liso(120, 90, 60);
    final q = await _compor([img, img, img]);
    expect(q.r, closeTo(120, 2));
    expect(q.g, closeTo(90, 2));
    expect(q.b, closeTo(60, 2));
    img.dispose();
  });
}
