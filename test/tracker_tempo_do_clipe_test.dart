// O TRACKER SEGUE O QUADRO QUE O CLIPE MOSTRA: velocidade, reverso e Time
// Remap movem a camera junto com a imagem (pedido de 14/09/2026).
import 'dart:convert';
import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/algebra_numerica.dart';
import 'package:aurea/src/features/editor/domain/camera_solver3d.dart';
import 'package:aurea/src/features/editor/domain/cena_do_rastreio.dart';
import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:flutter_test/flutter_test.dart';

SolucaoCamera3D _solucao() => SolucaoCamera3D(
  largura: 640,
  altura: 360,
  focalPx: 700,
  // Camera andando em X, 10 unidades por quadro, e girando 0,02 rad.
  poses: [
    for (var q = 0; q <= 10; q++)
      () {
        final r = rotacaoDeVetor([0.0, 0.02 * q, 0.0]);
        final rc = r.aplicar([10.0 * q, 0, 0]);
        return PoseCamera(q, r, [-rc[0], -rc[1], -rc[2]]);
      }(),
  ],
  nuvem: {
    for (var i = 0; i < 10; i++) i: [i * 30.0, 0, 600],
  },
  erroPixels: .5,
  quadros: 11,
  fps: 10,
  inicioDaFonteUs: 0,
);

void main() {
  test('pose entre dois quadros: posicao e giro no meio', () {
    final p = poseNoQuadro(_solucao(), 4.5);
    expect(p.posicao[0], closeTo(45, 1e-6));
    final r4 = _solucao().poses[4].rotacao;
    final rel = p.rotacao * r4.transposta;
    final ang = math.acos(((rel.at(0, 0) + rel.at(1, 1) + rel.at(2, 2) - 1) / 2).clamp(-1.0, 1.0));
    expect(ang, closeTo(.01, 1e-6));
  });

  test('clipe em reverso: a camera anda de tras para frente', () {
    final cam = cameraDoRastreio(
      _solucao(),
      fonteNoTempo: (t) => const Duration(seconds: 1) - t,
      duracaoDaCamada: const Duration(seconds: 1),
    );
    expect(cam.posX.valueAt(Duration.zero), closeTo(100, 1e-6));
    expect(cam.posX.valueAt(const Duration(seconds: 1)), closeTo(0, 1e-6));
  });

  test('camera lenta: meio segundo de fonte em um segundo de clipe', () {
    final cam = cameraDoRastreio(
      _solucao(),
      fonteNoTempo: (t) => Duration(microseconds: t.inMicroseconds ~/ 2),
      duracaoDaCamada: const Duration(seconds: 1),
    );
    expect(cam.posX.valueAt(const Duration(seconds: 1)), closeTo(50, 1e-6));
    expect(cam.posX.valueAt(const Duration(milliseconds: 500)), closeTo(25, 1e-6));
  });

  test('trecho da fonte mostrado respeita velocidade e reverso', () {
    VideoLayer clipe({double speed = 1, bool reverse = false}) => VideoLayer(
      name: 'v',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      sourcePath: 'x.mp4',
      sourceOffset: const Duration(seconds: 3),
      speed: speed,
      reverse: reverse,
    );
    final (a, b) = trechoDaFonteMostrado(clipe(speed: 2));
    expect(a, const Duration(seconds: 3));
    expect(b.inMilliseconds, closeTo(7000, 20));
    final (c, d) = trechoDaFonteMostrado(clipe(reverse: true));
    expect(c.inMilliseconds, closeTo(3000, 20));
    expect(d.inMilliseconds, closeTo(5000, 20));
  });

  test('o inicio da fonte vai e volta no arquivo da solucao', () {
    final s = _solucao().copiarCom(inicioDaFonteUs: 3000000);
    expect(SolucaoCamera3D.decode(jsonEncodeSolucao(s))!.inicioDaFonteUs, 3000000);
  });
}

String jsonEncodeSolucao(SolucaoCamera3D s) => jsonEncode(s.toJson());
