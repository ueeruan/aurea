// AS FAISCAS (sistema auxiliar, 16/09): cada particula solta filhas ao
// longo do caminho. E o que separa uma chuva de pontos de um fogo de
// artificio — e e o que o dono pediu ao apontar o Particular.
//
// A regra que nao pode cair: a simulacao continua PURA. A faisca e
// funcao de (semente, pai, indice da faisca, tempo); arrastar o
// cabecote para tras tem de devolver o mesmo quadro.
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/widgets/particles_painter.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

const int _lado = 260;

ParticlesLayer _camada({int auxCount = 0}) => ParticlesLayer(
  name: 'p',
  startTime: Duration.zero,
  duration: const Duration(seconds: 5),
  count: 24,
  seed: 5,
  speed: 90,
  size: 12,
  lifetimeMs: 2000,
  depth: 200,
  emitW: 120,
  emitH: 120,
  emitter: 1,
  twinkle: false,
  glow: 0,
  color: const Color(0xFFFFFFFF),
  auxCount: auxCount,
  auxLifeMs: 600,
  auxSpeed: 70,
  auxSize: 0.5,
);

Future<Uint8List> _quadro(ParticlesLayer l, Duration t) async {
  final rec = ui.PictureRecorder();
  final r = Rect.fromLTWH(0, 0, _lado.toDouble(), _lado.toDouble());
  final canvas = Canvas(rec, r);
  canvas.drawRect(r, Paint()..color = const Color(0xFF000000));
  ParticlesPainter(
    layer: l,
    time: t,
  ).paint(canvas, Size(_lado.toDouble(), _lado.toDouble()));
  final img = await rec.endRecording().toImage(_lado, _lado);
  final bd = await img.toByteData(format: ui.ImageByteFormat.rawRgba);
  img.dispose();
  return bd!.buffer.asUint8List();
}

int _acesos(Uint8List px) {
  var n = 0;
  for (var i = 0; i < px.length; i += 4) {
    if (px[i] > 40) n++;
  }
  return n;
}

void main() {
  const t = Duration(milliseconds: 1400);

  test('sem faiscas, nada muda (projeto antigo abre igual)', () async {
    expect(_camada().auxCount, 0, reason: 'o padrao tem de ser desligado');
    final a = _acesos(await _quadro(_camada(), t));
    expect(a, greaterThan(0), reason: 'a nuvem nem desenhou');
  });

  test('ligar as faiscas acende mais pontos na tela', () async {
    final sem = _acesos(await _quadro(_camada(), t));
    final com = _acesos(await _quadro(_camada(auxCount: 6), t));
    expect(
      com,
      greaterThan(sem),
      reason: 'as faiscas nao apareceram',
    );
  });

  test('a faisca e DETERMINISTICA: o mesmo instante, o mesmo quadro', () async {
    final l = _camada(auxCount: 6);
    final a = await _quadro(l, t);
    // Vai para outro instante e VOLTA — como arrastar o cabecote.
    await _quadro(l, const Duration(milliseconds: 2600));
    final b = await _quadro(l, t);
    expect(a, equals(b), reason: 'o scrub para tras mudou o quadro');
  });

  test('faisca nenhuma nasce antes de auxStart', () async {
    // Comecando em 90% da vida do pai, no inicio da vida nao ha faisca.
    final tarde = ParticlesLayer(
      name: 'p',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      count: 24,
      seed: 5,
      speed: 90,
      size: 12,
      lifetimeMs: 2000,
      depth: 200,
      emitter: 1,
      twinkle: false,
      glow: 0,
      lifeRandom: 0,
      color: const Color(0xFFFFFFFF),
      auxCount: 8,
      auxLifeMs: 300,
      auxStart: 0.9,
    );
    final cedo = _acesos(await _quadro(tarde, const Duration(milliseconds: 5)));
    final so = _acesos(
      await _quadro(_camada(), const Duration(milliseconds: 5)),
    );
    expect(cedo, so, reason: 'faisca saiu antes da hora');
  });
}
