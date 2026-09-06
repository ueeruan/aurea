import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/scene3d_painter.dart';
import 'package:aurea/src/features/projects/application/modelos_empacotados.dart';
import 'package:aurea/src/features/projects/domain/deriva_template.dart';
import 'package:aurea/src/features/projects/domain/monolito_template.dart';

/// FERRAMENTA DE MESA: quanto custa um quadro da cena 3D no PINTOR EM
/// CPU — o que roda quando o Flutter GPU nao esta disponivel.
///
/// Nao afirma nada (nao e teste): imprime triangulos e milissegundos por
/// quadro para se decidir com numero, e nao com impressao. Resultados de
/// desktop nao permitem extrapolar custos de um iPhone sem medir o aparelho.
void main() {
  Future<void> medir(String nome, VideoProject p, {bool rascunho = false}) async {
    final layer = p.layers.whereType<Scene3DLayer>().single;
    final cena = rascunho
        ? layer.scene.copyWith(draftMode: true)
        : layer.scene;
    const tamanho = Size(1280, 720);
    var tris = 0;
    // Aquece (a primeira avaliacao de um modelo monta a malha).
    renderScene(cena, layer.camera.renderAt(Duration.zero), tamanho,
        Duration.zero);

    final relogioCena = Stopwatch()..start();
    const amostras = 5;
    late SceneFrame frame;
    for (var i = 0; i < amostras; i++) {
      final t = Duration(milliseconds: 300 * i);
      frame = renderScene(
        cena,
        layer.camera.renderAt(t),
        tamanho,
        t,
      );
      tris = frame.opaque.length + frame.transparent.length;
    }
    relogioCena.stop();

    final rec = ui.PictureRecorder();
    final canvas = Canvas(rec);
    final relogioPintura = Stopwatch()..start();
    for (var i = 0; i < amostras; i++) {
      final t = Duration(milliseconds: 300 * i);
      Scene3DPainter(
        scene: cena,
        camera: layer.camera,
        view: SceneView.camera,
        time: t,
      ).paint(canvas, tamanho);
    }
    relogioPintura.stop();
    rec.endRecording().dispose();

    final geometria = relogioCena.elapsedMilliseconds / amostras;
    final total = relogioPintura.elapsedMilliseconds / amostras;
    // ignore: avoid_print
    print('$nome: $tris triangulos visiveis | geometria ${geometria.round()} ms '
        '| quadro inteiro ${total.round()} ms | ${(1000 / total).toStringAsFixed(1)} fps no desktop');
  }

  test('custo do quadro no pintor em CPU', () async {
    final astronauta = await carregarAstronautaDe('assets/models/monolito');
    await medir('DERIVA sem modelo ', buildDerivaTemplate());
    await medir('DERIVA com modelo ', buildDerivaTemplate(astronauta: astronauta));
    final modelos = await carregarMonolitoModelosDe('assets/models/monolito');
    await medir('MONOLITO sem modelos', buildMonolitoTemplate());
    await medir('MONOLITO com modelos', buildMonolitoTemplate(modelos: modelos));
    // O MESMO, EM RASCUNHO — que e como o preview desenha enquanto toca.
    await medir(
      'DERIVA tocando   ',
      buildDerivaTemplate(astronauta: astronauta),
      rascunho: true,
    );
    await medir(
      'MONOLITO tocando ',
      buildMonolitoTemplate(modelos: modelos),
      rascunho: true,
    );
  }, timeout: const Timeout(Duration(minutes: 5)));
}
