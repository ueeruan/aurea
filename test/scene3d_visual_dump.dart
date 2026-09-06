// Dump visual do renderizador da Cena 3D: pinta alguns quadros e grava
// PNG, para conferir o resultado sem depender do aparelho.
//
// Rodar:  flutter test test/scene3d_visual_dump.dart
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/presentation/widgets/scene3d_painter.dart';

const _out =
    r'C:\Users\SnyX\AppData\Local\Temp\claude\C--Users-SnyX-Documents-Projetos---Claude-Aurea\c74a663a-6520-43d0-bb20-71b9ea4f5cc0\scratchpad';

Future<void> _dump(String name, CustomPainter painter, Size size) async {
  final recorder = ui.PictureRecorder();
  final canvas = Canvas(recorder);
  canvas.drawRect(
      Offset.zero & size, Paint()..color = const Color(0xFF12151A));
  painter.paint(canvas, size);
  final picture = recorder.endRecording();
  final image =
      await picture.toImage(size.width.round(), size.height.round());
  final bytes = await image.toByteData(format: ui.ImageByteFormat.png);
  File('$_out\\$name.png').writeAsBytesSync(bytes!.buffer.asUint8List());
}

void main() {
  const size = Size(720, 720);

  testWidgets('dump', (tester) async {
    // 1. Cena demo pela camera.
    await _dump(
      'r_demo',
      Scene3DPainter(
        scene: Scene3D.demo,
        camera: Camera3D(),
        view: SceneView.camera,
        time: Duration.zero,
      ),
      size,
    );

    // 2. DOIS CUBOS CRUZADOS — o teste que a spec define como aprovacao.
    final cruzados = Scene3D(
      nodes: [
        SceneNode(
          name: 'A',
          kind: Element3DKind.cube,
          size: 130,
          x: AnimatedDouble(-70),
          z: AnimatedDouble(-70),
          rotY: AnimatedDouble(18),
          material: const Material3D(baseColor: Color(0xFF7C62FF)),
        ),
        SceneNode(
          name: 'B',
          kind: Element3DKind.cube,
          size: 130,
          x: AnimatedDouble(70),
          z: AnimatedDouble(70),
          rotY: AnimatedDouble(-14),
          material: const Material3D(baseColor: Color(0xFFB8FF3D)),
        ),
      ],
      lights: [Light3D(), Light3D(kind: Light3DKind.ambient)],
      ambient: 0.2,
    );
    await _dump(
      'r_cruzados',
      Scene3DPainter(
        scene: cruzados,
        camera: Camera3D(posZ: AnimatedDouble(700)),
        view: SceneView.camera,
        time: Duration.zero,
      ),
      size,
    );

    // 3. Vista de TOPO com ajudas: grade do chao, frustum, plano de foco
    //    e linhas de profundidade.
    await _dump(
      'r_topo',
      Scene3DPainter(
        scene: cruzados.copyWith(showFloorGrid: true),
        camera: Camera3D(
          posZ: AnimatedDouble(700),
          posY: AnimatedDouble(160),
          dof: DepthOfField(
              enabled: true, focusDistance: AnimatedDouble(700)),
        ),
        view: SceneView.top,
        time: Duration.zero,
        showHelpers: true,
        selectedNodeId: cruzados.nodes.first.id,
      ),
      size,
    );

    // 4. BOKEH: pontos de luz fora de foco com iris hexagonal e ganho de
    //    realce alto — tem de virar hexagono brilhante, nao borrao.
    final luzes = Scene3D(
      nodes: [
        for (var i = 0; i < 9; i++)
          SceneNode(
            name: 'Luz $i',
            kind: Element3DKind.sphere,
            size: 26,
            x: AnimatedDouble((i % 3 - 1) * 190.0),
            y: AnimatedDouble((i ~/ 3 - 1) * 150.0),
            z: AnimatedDouble(-700.0 - (i % 3) * 120),
            material: const Material3D(
                baseColor: Color(0xFFFFFFFF), kind: MaterialKind.unlit),
          ),
        SceneNode(
          name: 'Foco',
          kind: Element3DKind.cube,
          size: 120,
          z: AnimatedDouble(120),
          material: const Material3D(baseColor: Color(0xFF7C62FF)),
        ),
      ],
      lights: [Light3D()],
      ambient: 0.15,
    );
    await _dump(
      'r_bokeh',
      Scene3DPainter(
        scene: luzes,
        camera: Camera3D(
          posZ: AnimatedDouble(700),
          dof: DepthOfField(
            enabled: true,
            focusDistance: AnimatedDouble(580),
            aperture: AnimatedDouble(55),
            irisShape: IrisShape.hexagon,
            highlightGain: AnimatedDouble(70),
            highlightThreshold: AnimatedDouble(0.55),
            diffractionFringe: AnimatedDouble(40),
          ),
        ),
        view: SceneView.camera,
        time: Duration.zero,
      ),
      size,
    );

    // 5. GRADE 3D: 125 instancias da mesma malha em UMA chamada.
    final enxame = Scene3D(
      nodes: [
        SceneNode(
          name: 'Enxame',
          kind: Element3DKind.cube,
          size: 22,
          rotY: AnimatedDouble(20),
          material: const Material3D(baseColor: Color(0xFF35C4E7)),
          instances: [
            for (var x = 0; x < 5; x++)
              for (var y = 0; y < 5; y++)
                for (var z = 0; z < 5; z++)
                  Vec3((x - 2) * 110.0, (y - 2) * 110.0, (z - 2) * 110.0),
          ],
        ),
      ],
      lights: [Light3D()],
    );
    await _dump(
      'r_enxame',
      Scene3DPainter(
        scene: enxame,
        camera: Camera3D(
          posX: AnimatedDouble(520),
          posY: AnimatedDouble(380),
          posZ: AnimatedDouble(760),
        ),
        view: SceneView.camera,
        time: Duration.zero,
      ),
      size,
    );

    final frame = renderScene(
        enxame,
        Camera3D().renderAt(Duration.zero),
        size,
        Duration.zero);
    // ignore: avoid_print
    print('enxame: ${frame.drawCalls} chamadas, '
        '${frame.triangles} triangulos');
  });
}
