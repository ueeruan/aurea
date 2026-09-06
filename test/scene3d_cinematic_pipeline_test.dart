import 'dart:convert';
import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/texture_cache.dart';
import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/camera_cuts.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/template_pack.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/widgets/scene3d_painter.dart';

Scene3D _triangle(List<List<double>> points, {Material3D? material}) => Scene3D(
  nodes: [
    SceneNode(
      size: 1,
      mesh: Element3DMesh(points, [
        [0, 1, 2],
      ]),
      material:
          material ??
          const Material3D(kind: MaterialKind.unlit, doubleSided: true),
    ),
  ],
);
const _camera = RenderCamera(
  position: Vec3.zero,
  target: Vec3(0, 0, -1),
  near: 1,
  far: 10,
);
const _size = Size(100, 100);
SceneFrame _frame(Scene3D scene) =>
    renderScene(scene, _camera, _size, Duration.zero);

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test('near-plane crossing preserves the visible quad instead of losing the triangle', () {
    final frame = _frame(
      _triangle([
        [-.1, -.1, -.5],
        [.2, -.1, -2],
        [0, .2, -2],
      ]),
    );
    expect(frame.triangles, 2);
    expect(frame.opaque.every((t) => t.depth >= 1), isTrue);
    expect(
      frame.opaque.every(
        (t) => [t.a, t.b, t.c].every((p) => p.dx.isFinite && p.dy.isFinite),
      ),
      isTrue,
    );
  });
  test('far-plane crossing clips and triangles wholly outside the depth range vanish', () {
    expect(
      _frame(
        _triangle([
          [-1, -1, -12],
          [1, -1, -5],
          [0, 1, -5],
        ]),
      ).triangles,
      2,
    );
    expect(
      _frame(
        _triangle([
          [-1, -1, -20],
          [1, -1, -20],
          [0, 1, -20],
        ]),
      ).triangles,
      0,
    );
    expect(
      _frame(
        _triangle([
          [-1, -1, .5],
          [1, -1, .5],
          [0, 1, .5],
        ]),
      ).triangles,
      0,
    );
  });
  test(
    'bucket collisions remain strictly far-to-near even in very deep scenes',
    () {
      final tris = [
        for (var i = 0; i < 200; i++)
          RenderTri(
            a: Offset.zero,
            b: const Offset(1, 0),
            c: const Offset(0, 1),
            depth: i == 199 ? 1e9 : 100 + i * .0001,
            color: const Color(0xffffffff),
            transparent: false,
          ),
      ];
      depthSort(tris);
      for (var i = 1; i < tris.length; i++) {
        expect(tris[i - 1].depth, greaterThanOrEqualTo(tris[i].depth));
      }
    },
  );
  test('fog is opt-in, depth-dependent, alpha-preserving and serialized', () {
    final base = _triangle(
      [
        [-1, -1, -2],
        [1, -1, -2],
        [0, 1, -2],
      ],
      material: const Material3D(
        baseColor: Color(0xffff0000),
        opacity: .4,
        kind: MaterialKind.unlit,
        doubleSided: true,
      ),
    );
    final fog = base.copyWith(
      fogDensity: 1,
      fogStart: 1,
      fogColor: const Color(0xff0000ff),
    );
    expect(base.fogAt(1e6), 0);
    expect(fog.fogAt(.5), 0);
    expect(fog.fogAt(5), greaterThan(fog.fogAt(2)));
    final tri = _frame(fog).transparent.single;
    expect(tri.colorA!.b, greaterThan(tri.colorA!.r));
    expect(tri.colorA!.a, closeTo(.4, .01));
    final project = VideoProject.empty('Fog').copyWith(
      layers: [
        Scene3DLayer(
          name: 'Fog',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          scene: fog,
        ),
      ],
    );
    final read = TemplatePack.decode(
      TemplatePack(name: 'Fog', project: project).encode(),
    )!;
    final restored = (read.project.layers.single as Scene3DLayer).scene;
    expect(restored.fogDensity, 1);
    expect(restored.fogStart, 1);
    expect(restored.fogColor, const Color(0xff0000ff));
  });
  test('cut points and roll keys from secondary cameras are visible on the timeline', () {
    final layer = Scene3DLayer(
      name: 'Cuts',
      startTime: Duration.zero,
      duration: const Duration(seconds: 10),
      extraCameras: [
        Camera3D(
          id: 'other',
          rotZ: AnimatedDouble(0, [
            Keyframe(time: const Duration(seconds: 3), value: 20),
          ]),
        ),
      ],
      shots: [
        const CameraShot(
          time: Duration(seconds: 2),
          cameraId: 'other',
          transition: Duration(seconds: 2),
        ),
      ],
    );
    expect(layer.moduleTimesUs, containsAll([2000000, 3000000, 4000000]));
  });
  test(
    'fog over a textured transparent surface preserves texture alpha',
    () async {
      final recorder = PictureRecorder();
      Canvas(recorder).drawRect(
        const Rect.fromLTWH(0, 0, 4, 4),
        Paint()..color = const Color(0x80ff0000),
      );
      final picture = recorder.endRecording(),
          texture = await picture.toImage(4, 4);
      final bytes = await texture.toByteData(format: ImageByteFormat.png);
      final uri =
          'data:image/png;base64,${base64Encode(bytes!.buffer.asUint8List())}';
      texture.dispose();
      picture.dispose();
      final cache = TextureCache.instance;
      addTearDown(cache.clear);
      expect(await cache.prepare(uri), isTrue);
      final scene = _triangle(
        [
          [-2, -2, -2],
          [2, -2, -2],
          [0, 2, -2],
        ],
        material: Material3D(
          imagePath: uri,
          kind: MaterialKind.unlit,
          doubleSided: true,
        ),
      ).copyWith(fogDensity: 10, fogColor: const Color(0xff0000ff));
      final r = PictureRecorder(), canvas = Canvas(r);
      Scene3DPainter(
        scene: scene,
        camera: Camera3D(),
        view: SceneView.camera,
        time: Duration.zero,
        overrideCamera: _camera,
      ).paint(canvas, _size);
      final pic = r.endRecording(), image = await pic.toImage(100, 100);
      final rgba = (await image.toByteData(
        format: ImageByteFormat.rawStraightRgba,
      ))!.buffer.asUint8List();
      final center = (50 * 100 + 50) * 4;
      expect(rgba[center + 2], greaterThan(230));
      expect(rgba[center + 3], inInclusiveRange(125, 131));
      image.dispose();
      pic.dispose();
    },
  );
}
