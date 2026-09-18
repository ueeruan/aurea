import 'dart:math' as math;
import 'dart:ui';

import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/camera_cuts.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

const forestShots = [
  '01 • O caminho encantado',
  '02 • Vida sob as folhas',
  '03 • O coração da floresta',
  '04 • A água e a luz',
  '05 • O bosque desperta',
];
Duration ft(double s) => Duration(microseconds: (s * 1000000).round());
// Blender's Z-up meters -> Aurea Y-up centimeters.
Vec3 world(double x, double y, double z) => Vec3(x * 100, z * 100, -y * 100);

VideoProject magicForestProject(ModelAsset3D model, Map meta) {
  AnimatedDouble track(double Function(double) f, double start, double end) =>
      AnimatedDouble(f(start), [
        for (var j = 0; j <= 24; j++)
          Keyframe(
            time: ft(start + (end - start) * j / 24),
            value: f(start + (end - start) * j / 24),
          ),
      ]);
  final cameras = <Camera3D>[];
  for (var i = 0; i < 5; i++) {
    final start = i * 3.0, end = start + 3;
    Vec3 eye(double t) {
      final u = (t - start) / 3;
      return switch (i) {
        0 => world(.1 - .35 * u, -7.5 + 1.6 * u, 1.6 + .1 * u),
        1 => world(-2.15 + .32 * u, -2.8 + .25 * u, .64 + .08 * u),
        2 => world(-3.7 + .9 * u, -1.3 + .4 * u, 2.2 + .2 * u),
        3 => world(1.10 + .08 * u, -4.6 + 1.3 * u, 1.35 + .15 * u),
        _ => world(.8 + 1.0 * u, -6.8 - 1.6 * u, 3.0 + 1.8 * u),
      };
    }

    Vec3 aim(double t) => switch (i) {
      0 => world(0, 3, 3.1),
      1 => world(-1.10, -1.1, .34),
      2 => world(0, 3, 3.4),
      3 => world(1.5, 3, .35),
      _ => world(0, 3, 3.6),
    };
    cameras.add(
      Camera3D(
        id: 'forest_camera_$i',
        name: forestShots[i],
        posX: track((t) => eye(t).x, start, end),
        posY: track((t) => eye(t).y, start, end),
        posZ: track((t) => eye(t).z, start, end),
        poiX: track((t) => aim(t).x, start, end),
        poiY: track((t) => aim(t).y, start, end),
        poiZ: track((t) => aim(t).z, start, end),
        focalLength: AnimatedDouble(
          i == 1
              ? 42
              : i == 3
              ? 34
              : 30,
        ),
        dof: DepthOfField(
          enabled: i == 1,
          focusDistance: track((t) => (eye(t) - aim(t)).length, start, end),
          aperture: AnimatedDouble(5),
          blurLevel: AnimatedDouble(35),
        ),
      ),
    );
  }
  final center = (meta['center'] as List).cast<num>();
  final rng = math.Random(9012);
  final scene = Scene3D(
    background: const Color(0xff142d31),
    showFloorGrid: false,
    ambient: .20,
    skyColor: const Color(0xff698c98),
    groundColor: const Color(0xff1e2819),
    fogDensity: .0011,
    fogStart: 350,
    fogColor: const Color(0xff345960),
    nodes: [
      SceneNode(
        id: 'forest_geometry',
        name: 'Bosque • árvores, raízes, folhas, cogumelos e riacho',
        modelAsset: model,
        size: (meta['size'] as num).toDouble(),
        x: AnimatedDouble(center[0] * 100),
        y: AnimatedDouble(center[1] * 100),
        z: AnimatedDouble(center[2] * 100),
      ),
      for (var i = 0; i < 22; i++)
        (() {
          final x = rng.nextDouble() * 4 - 2,
              y = rng.nextDouble() * 7 - 2,
              z = .4 + rng.nextDouble() * 2.4;
          return SceneNode(
            id: 'forest_firefly_$i',
            name: 'Vagalume ${i + 1}',
            kind: Element3DKind.sphere,
            size: 1.5,
            material: const Material3D(
              baseColor: Color(0xffffd681),
              emissive: 1,
              roughness: .5,
            ),
            x: track((t) => (x + .16 * math.sin(t * .6 + i)) * 100, 0, 15),
            y: track((t) => (z + .12 * math.sin(t * .9 + i)) * 100, 0, 15),
            z: track((t) => -(y + .20 * math.cos(t * .5 + i)) * 100, 0, 15),
          );
        })(),
    ],
    lights: [
      Light3D(
        id: 'forest_moon',
        color: const Color(0xffb6e3ee),
        direction: const Vec3(-.4, -1, -.25),
        intensity: AnimatedDouble(1.45),
        castsShadow: true,
        softness: .5,
      ),
      Light3D(
        id: 'forest_amber',
        kind: Light3DKind.point,
        color: const Color(0xffffc071),
        position: world(-1, 2, 3),
        range: 1100,
        intensity: AnimatedDouble(2.0),
      ),
      Light3D(
        id: 'forest_blue',
        kind: Light3DKind.point,
        color: const Color(0xff57edc8),
        position: world(-1.1, -1, .45),
        range: 330,
        intensity: AnimatedDouble(1.2),
      ),
      Light3D(
        id: 'forest_stream',
        kind: Light3DKind.point,
        color: const Color(0xff6bb8d4),
        position: world(3, 3, 2),
        range: 1100,
        intensity: AnimatedDouble(.8),
      ),
    ],
  );
  return VideoProject(
    id: 'forest_magic_15s_20260912',
    name: 'LÚMEN • Floresta mágica',
    createdAt: DateTime(2026, 9, 12, 15),
    aspectRatio: 9 / 16,
    resolutionHeight: 1280,
    fps: 30,
    markers: [
      for (var i = 0; i < 5; i++)
        Marker(time: ft(i * 3.0), label: forestShots[i]),
    ],
    layers: [
      Scene3DLayer(
        id: 'forest_scene',
        name: 'Floresta mágica • 5 câmeras',
        startTime: Duration.zero,
        duration: const Duration(seconds: 15),
        position: AnimatedOffset(const Offset(360, 640)),
        showHelpers: false,
        scene: scene,
        camera: cameras.first,
        extraCameras: cameras.skip(1).toList(),
        shots: [
          for (var i = 0; i < 5; i++)
            CameraShot(time: ft(i * 3.0), cameraId: cameras[i].id),
        ],
      ),
    ],
  );
}
