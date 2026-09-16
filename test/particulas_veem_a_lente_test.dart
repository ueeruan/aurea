// AS PARTICULAS VEEM A LENTE (relato do dono, 16/09).
//
// A nuvem projetava com uma focal cravada em 1200: numa camera
// grande-angular o resto da cena abria o angulo e as particulas nao,
// como se estivessem coladas num vidro na frente da lente. A lente
// ativa chega ao pintor agora.
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/widgets/particles_painter.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

const int _lado = 300;

/// Quantos pixels desenhados a nuvem ocupa com esta lente.
Future<int> _ocupacao(double focal) async {
  final camada = ParticlesLayer(
    name: 'p',
    startTime: Duration.zero,
    duration: const Duration(seconds: 5),
    count: 160,
    seed: 3,
    speed: 0,
    size: 10,
    depth: 1600,
    emitW: 900,
    emitH: 900,
    twinkle: false,
    glow: 0,
    color: const Color(0xFFFFFFFF),
  );
  final rec = ui.PictureRecorder();
  final canvas = Canvas(
    rec,
    Rect.fromLTWH(0, 0, _lado.toDouble(), _lado.toDouble()),
  );
  canvas.drawRect(
    Rect.fromLTWH(0, 0, _lado.toDouble(), _lado.toDouble()),
    Paint()..color = const Color(0xFF000000),
  );
  ParticlesPainter(
    layer: camada,
    time: const Duration(milliseconds: 900),
    focal: focal,
  ).paint(canvas, Size(_lado.toDouble(), _lado.toDouble()));
  final img = await rec.endRecording().toImage(_lado, _lado);
  final bd = await img.toByteData(format: ui.ImageByteFormat.rawRgba);
  img.dispose();
  final px = bd!.buffer.asUint8List();
  var n = 0;
  for (var i = 0; i < px.length; i += 4) {
    if (px[i] > 40) n++;
  }
  return n;
}

void main() {
  test('a lente muda o campo de visao da nuvem', () async {
    final normal = await _ocupacao(1200);
    final grandeAngular = await _ocupacao(400);
    final teleobjetiva = await _ocupacao(3000);

    expect(normal, greaterThan(0), reason: 'a nuvem nem desenhou');
    // Grande-angular espalha a nuvem: mais particulas saem do quadro e
    // as que ficam sao maiores. Teleobjetiva fecha o angulo.
    expect(
      grandeAngular,
      isNot(closeTo(normal, normal * 0.05)),
      reason: 'a lente curta nao mudou nada — a focal nao chegou',
    );
    expect(
      teleobjetiva,
      isNot(closeTo(normal, normal * 0.05)),
      reason: 'a lente longa nao mudou nada — a focal nao chegou',
    );
  });

  test('lente invalida cai na neutra em vez de sumir com a nuvem', () async {
    final neutra = await _ocupacao(1200);
    expect(await _ocupacao(0), neutra);
    expect(await _ocupacao(double.nan), neutra);
  });
}
