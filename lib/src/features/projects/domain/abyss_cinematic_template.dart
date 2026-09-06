import 'dart:math' as math;
import 'dart:ui';

import '../../editor/domain/camera3d.dart';
import '../../editor/domain/camera_cuts.dart';
import '../../editor/domain/element3d.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/model_asset3d.dart';
import '../../editor/domain/scene3d.dart';
import '../../editor/domain/video_project.dart';

const abyssDuration = Duration(seconds: 14);
const abyssFps = 24;
Duration abyssTime(num seconds) =>
    Duration(microseconds: (seconds * 1000000).round());

// A slowed cinematic fall, not a real-time gravity simulation. One world
// trajectory drives every shot, so cutting never resets the actor's motion.
Vec3 abyssActorPosition(double t) {
  final fall = math.max(0.0, t - 1.7);
  final drift = (math.max(0.0, t - 1.2) / 3.6).clamp(0.0, 1.0);
  return Vec3(
    -295 + 295 * drift,
    112 - 32 * fall * fall,
    24 * math.sin(fall * .48),
  );
}

AnimatedDouble _sample(double Function(double) f) => AnimatedDouble(f(0), [
  for (var i = 0; i <= 56; i++)
    Keyframe(time: abyssTime(i / 4), value: f(i / 4)),
]);
AnimatedDouble _keys(List<(num, num)> values) =>
    AnimatedDouble(values.first.$2.toDouble(), [
      for (final v in values)
        Keyframe(time: abyssTime(v.$1), value: v.$2.toDouble()),
    ]);
double _noise(int i) {
  // Integer hash: reproducible geometry without random state or wall clock.
  var x = (i * 374761393 + 668265263) & 0x7fffffff;
  x = ((x ^ (x >> 13)) * 1274126177) & 0x7fffffff;
  return (x ^ (x >> 16)) / 0x7fffffff;
}

List<double> _v(Vec3 v) => [v.x, v.y, v.z];

Map<String, dynamic> _material(
  String name,
  int color, {
  double metallic = 0,
  double roughness = .8,
  double glow = 0,
}) {
  final c = Color(color);
  return {
    'name': name,
    'color': [c.r, c.g, c.b, 1.0],
    'metallic': metallic,
    'roughness': roughness,
    'emissive': glow,
  };
}

/// A self-contained authored character: a weighted mesh bound to 17 joints.
/// The animation uses the same portable asset/pose engine as imported GLB,
/// not a custom painter or a prerecorded movie. Each limb is pose-editable.
ModelAsset3D buildAbyssExplorer() {
  final nodes = <Map<String, dynamic>>[
    {'name': 'Malha do explorador', 'skin': 0},
  ];
  final bind = <Vec3>[Vec3.zero];
  int bone(String name, int? parent, Vec3 offset) {
    final index = nodes.length;
    nodes.add({'name': name, 'parent': ?parent, 'translation': _v(offset)});
    bind.add((parent == null ? Vec3.zero : bind[parent]) + offset);
    return index;
  }

  final hips = bone('Quadril', null, Vec3.zero);
  final spine = bone('Coluna', hips, const Vec3(0, 25, 0));
  final chest = bone('Peito', spine, const Vec3(0, 26, 0));
  final neck = bone('Pescoco', chest, const Vec3(0, 27, 0));
  final head = bone('Cabeca', neck, const Vec3(0, 13, 0));
  final arms = <List<int>>[], legs = <List<int>>[];
  for (final sign in [-1.0, 1.0]) {
    final side = sign < 0 ? 'E' : 'D';
    final shoulder = bone('Ombro $side', chest, Vec3(sign * 27, 13, 0));
    final elbow = bone('Cotovelo $side', shoulder, const Vec3(0, -31, 0));
    final wrist = bone('Pulso $side', elbow, const Vec3(0, -29, 0));
    arms.add([shoulder, elbow, wrist]);
    final hip = bone('Coxa $side', hips, Vec3(sign * 14, -5, 0));
    final knee = bone('Joelho $side', hip, const Vec3(0, -43, 0));
    final ankle = bone('Tornozelo $side', knee, const Vec3(0, -39, 0));
    legs.add([hip, knee, ankle]);
  }
  final primitives = <Map<String, dynamic>>[];
  void ellipsoid(
    int joint,
    Vec3 center,
    Vec3 radius,
    int material, {
    int rings = 7,
    int sides = 12,
    bool visor = false,
    bool torso = false,
  }) {
    final positions = <List<double>>[], normals = <List<double>>[];
    final groups = <int, List<int>>{};
    for (var row = 0; row <= rings; row++) {
      final lat = math.pi * row / rings;
      for (var col = 0; col <= sides; col++) {
        final lon = 2 * math.pi * col / sides;
        final unit = Vec3(
          math.sin(lat) * math.cos(lon),
          math.cos(lat),
          math.sin(lat) * math.sin(lon),
        );
        positions.add(
          _v(
            bind[joint] +
                center +
                Vec3(unit.x * radius.x, unit.y * radius.y, unit.z * radius.z),
          ),
        );
        normals.add(
          _v(
            Vec3(
              unit.x / radius.x,
              unit.y / radius.y,
              unit.z / radius.z,
            ).normalized,
          ),
        );
      }
    }
    for (var row = 0; row < rings; row++) {
      for (var col = 0; col < sides; col++) {
        final a = row * (sides + 1) + col, b = a + sides + 1;
        final lat = math.pi * (row + .5) / rings;
        final lon = 2 * math.pi * (col + .5) / sides;
        final isVisor =
            visor && math.cos(lat).abs() < .52 && math.sin(lon) > .4;
        final indices = groups.putIfAbsent(isVisor ? 3 : material, () => []);
        if (row > 0) indices.addAll([a, a + 1, b]);
        if (row < rings - 1) indices.addAll([a + 1, b + 1, b]);
      }
    }
    for (final group in groups.entries) {
      primitives.add({
        'node': 0,
        'positions': positions,
        'normals': normals,
        'indices': group.value,
        'material': group.key,
        'joints': [
          for (var i = 0; i < positions.length; i++)
            [joint - 1, if (torso) chest - 1],
        ],
        'weights': [
          for (final p in positions)
            if (torso)
              [
                1 - ((p[1] - 35) / 32).clamp(0.0, 1.0),
                ((p[1] - 35) / 32).clamp(0.0, 1.0),
              ]
            else
              [1.0],
        ],
      });
    }
  }

  // Connected weighted tubes replace intersecting limb spheres. Material
  // bands share vertices and the elbow/knee blend across two real bones.
  void limb(List<int> chain, {required bool leg}) {
    final rows = leg
        ? <(double, double, double, int)>[
            (-4, .5, 0, 0),
            (0, 10, 0, 0),
            (8, 13, 0, 0),
            (22, 13, 0, 0),
            (34, 11, 0, 0),
            (39, 11, 0, 1),
            (43, 12, 0, 1),
            (47, 11, 0, 1),
            (53, 11, 0, 0),
            (65, 10, 0, 0),
            (76, 9, 0, 0),
            (81, 10, 1, 1),
            (87, 11, 7, 1),
            (94, 11, 10, 1),
            (99, .5, 10, 1),
          ]
        : <(double, double, double, int)>[
            (-7, .5, 0, 2),
            (-3, 8, 0, 2),
            (4, 11, 0, 2),
            (10, 10, 0, 0),
            (21, 9, 0, 0),
            (26, 8, 0, 1),
            (31, 8, 0, 1),
            (36, 8, 0, 1),
            (42, 9, 0, 0),
            (51, 8, 0, 0),
            (56, 7, 0, 1),
            (61, 7, 0, 1),
            (67, 8, 0, 1),
            (74, .5, 0, 1),
          ];
    const sides = 16;
    final positions = <List<double>>[],
        normals = <List<double>>[],
        weights = <List<double>>[];
    final groups = <int, List<int>>{};
    for (var r = 0; r < rows.length; r++) {
      final row = rows[r],
          prev = rows[math.max(0, r - 1)],
          next = rows[math.min(rows.length - 1, r + 1)];
      final slope = (next.$2 - prev.$2) / (next.$1 - prev.$1);
      final b = ((row.$1 - (leg ? 43 : 31) + 7) / 14).clamp(0.0, 1.0);
      final c = ((row.$1 - (leg ? 82 : 60) + 6) / 12).clamp(0.0, 1.0);
      for (var j = 0; j <= sides; j++) {
        final angle = j * 2 * math.pi / sides;
        positions.add(
          _v(
            bind[chain[0]] +
                Vec3(
                  row.$2 * math.cos(angle),
                  -row.$1,
                  row.$2 * math.sin(angle) + row.$3,
                ),
          ),
        );
        normals.add(
          _v(Vec3(math.cos(angle), slope, math.sin(angle)).normalized),
        );
        weights.add([1 - b, b * (1 - c), c]);
      }
      if (r == rows.length - 1) continue;
      final indices = groups.putIfAbsent(row.$4, () => []);
      for (var j = 0; j < sides; j++) {
        final a = r * (sides + 1) + j, b = a + sides + 1;
        indices.addAll([a, a + 1, b, a + 1, b + 1, b]);
      }
    }
    for (final group in groups.entries) {
      primitives.add({
        'node': 0,
        'positions': positions,
        'normals': normals,
        'indices': group.value,
        'material': group.key,
        'weights': weights,
        'joints': [
          for (var i = 0; i < positions.length; i++)
            [for (final j in chain) j - 1],
        ],
      });
    }
  }

  // Orange pressure suit, dark joints, ivory helmet, reflective smoked visor.
  ellipsoid(hips, const Vec3(0, -7, 0), const Vec3(25, 15, 15), 1);
  ellipsoid(
    spine,
    const Vec3(0, 12, 0),
    const Vec3(29, 36, 17),
    0,
    rings: 12,
    sides: 18,
    torso: true,
  );
  ellipsoid(chest, const Vec3(0, -3, -23), const Vec3(20, 27, 9), 1);
  ellipsoid(chest, const Vec3(0, 3, 20), const Vec3(15, 13, 2), 2);
  ellipsoid(
    chest,
    const Vec3(-8, 4, 23),
    const Vec3(3, 3, 1),
    4,
    rings: 4,
    sides: 6,
  );
  ellipsoid(neck, Vec3.zero, const Vec3(13, 8, 13), 1);
  ellipsoid(
    head,
    const Vec3(0, 10, 0),
    const Vec3(19, 23, 19),
    2,
    rings: 12,
    sides: 24,
    visor: true,
  );
  for (var side = 0; side < 2; side++) {
    limb(arms[side], leg: false);
    limb(legs[side], leg: true);
  }
  return ModelAsset3D({
    'version': 1,
    'format': 'aurea-procedural',
    'name': 'Explorador · rig de 17 ossos',
    'nodes': nodes,
    'primitives': primitives,
    'skins': [
      {
        'joints': [for (var i = 1; i < nodes.length; i++) i],
        'inverseBind': [
          for (final p in bind.skip(1))
            [
              1.0,
              0.0,
              0.0,
              0.0,
              0.0,
              1.0,
              0.0,
              0.0,
              0.0,
              0.0,
              1.0,
              0.0,
              -p.x,
              -p.y,
              -p.z,
              1.0,
            ],
        ],
      },
    ],
    'materials': [
      _material('Tecido · laranja resgate', 0xffe57537, roughness: .78),
      _material('Articulacoes · grafite', 0xff273039, metallic: .2),
      _material('Casco · marfim', 0xffd4dacd, metallic: .35, roughness: .3),
      _material(
        'Visor · titanio fumê',
        0xff243b4a,
        metallic: .92,
        roughness: .14,
      ),
      _material('Sinalizador', 0xff6ff4f0, glow: 1.8),
    ],
    'clips': [],
    'warnings': [],
  });
}

ModelPose3D _pose(double x, double y, double z) {
  final a = x * math.pi / 360, b = y * math.pi / 360, c = z * math.pi / 360;
  final sx = math.sin(a),
      cx = math.cos(a),
      sy = math.sin(b),
      cy = math.cos(b),
      sz = math.sin(c),
      cz = math.cos(c);
  return ModelPose3D(
    rotation: [
      sx * cy * cz - cx * sy * sz,
      cx * sy * cz + sx * cy * sz,
      cx * cy * sz - sx * sy * cz,
      cx * cy * cz + sx * sy * sz,
    ],
  );
}

ModelMotion3D _fallPose() => ModelMotion3D(
  clip: -1,
  loop: false,
  keys: [
    for (var i = 0; i <= 28; i++)
      ModelPoseKey3D(i / 2, () {
        final t = i / 2, f = ((t - .8) / 1.8).clamp(0.0, 1.0);
        final wave = math.sin(t * 2.1), lag = math.sin(t * 2.1 - .9);
        return {
          2: _pose(-8 * f, 8 * f * lag, 4 * f * wave),
          3: _pose(-9 * f, -12 * f * lag, 0),
          5: _pose(15 * f, 18 * f * wave, -8 * f),
          // Left then right shoulder / elbow / wrist.
          6: _pose(-20 * f, 15 * f, -(62 + 20 * wave) * f),
          7: _pose(-(35 + 24 * lag) * f, 0, -12 * f),
          8: _pose(12 * f, 0, -8 * f * wave),
          12: _pose(12 * f, -20 * f, (58 - 22 * lag) * f),
          13: _pose(-(42 - 22 * wave) * f, 0, 15 * f),
          14: _pose(-15 * f, 0, 9 * f * lag),
          9: _pose((18 + 12 * wave) * f, 0, -18 * f),
          10: _pose((26 + 18 * lag) * f, 0, 0),
          11: _pose(-12 * f, 0, 0),
          15: _pose((-16 - 10 * lag) * f, 0, 20 * f),
          16: _pose((48 - 22 * wave) * f, 0, 0),
          17: _pose(8 * f, 0, 0),
        };
      }()),
  ],
);

/// World-space mesh builder; restore the model's one-time normalization so
/// authored cliffs/ledges stay in the same coordinates as all camera tracks.
class _WorldMesh {
  final List<Map<String, dynamic>> primitives = [];
  final List<Map<String, dynamic>> materials;
  _WorldMesh(this.materials);
  void triangle(Vec3 a, Vec3 b, Vec3 c, int material, {Vec3? facing}) {
    var normal = (b - a).cross(c - a).normalized;
    if (facing != null && normal.dot(facing) < 0) {
      final swap = b;
      b = c;
      c = swap;
      normal = normal * -1;
    }
    primitives.add({
      'node': 0,
      'positions': [_v(a), _v(b), _v(c)],
      'normals': [_v(normal), _v(normal), _v(normal)],
      'indices': [0, 1, 2],
      'material': material,
    });
  }

  void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, int m, {Vec3? facing}) {
    triangle(a, b, c, m, facing: facing);
    triangle(a, c, d, m, facing: facing);
  }

  SceneNode node(String id, String name) {
    final lo = [double.infinity, double.infinity, double.infinity],
        hi = [-double.infinity, -double.infinity, -double.infinity];
    for (final p in primitives) {
      for (final v in p['positions'] as List) {
        for (var j = 0; j < 3; j++) {
          lo[j] = math.min(lo[j], (v[j] as num).toDouble());
          hi[j] = math.max(hi[j], (v[j] as num).toDouble());
        }
      }
    }
    return SceneNode(
      id: id,
      name: name,
      size: math.max(hi[0] - lo[0], math.max(hi[1] - lo[1], hi[2] - lo[2])) / 2,
      x: AnimatedDouble((lo[0] + hi[0]) / 2),
      y: AnimatedDouble((lo[1] + hi[1]) / 2),
      z: AnimatedDouble((lo[2] + hi[2]) / 2),
      modelAsset: ModelAsset3D({
        'version': 1,
        'name': name,
        'nodes': [
          {'name': name},
        ],
        'primitives': primitives,
        'materials': materials,
        'skins': [],
        'clips': [],
      }),
    );
  }
}

SceneNode _cliffs() {
  final mesh = _WorldMesh([
    for (var i = 0; i < 8; i++)
      _material(
        'Basalto ${i + 1}',
        Color.from(
          alpha: 1,
          red: .055 + i * .008,
          green: .095 + i * .011,
          blue: .115 + i * .014,
        ).toARGB32(),
      ),
    _material('Mineral · azul glacial', 0xff3c858d, glow: .6),
  ]);
  const sectors = 32, rows = 38;
  Vec3 at(int row, int sector) {
    final angle = (sector % sectors) * math.pi * 2 / sectors;
    final radius =
        680 +
        60 * math.sin(angle * 3) +
        row * 3 +
        100 * _noise(row * sectors + sector % sectors);
    return Vec3(
      math.cos(angle) * radius,
      100 - row * 180,
      math.sin(angle) * radius,
    );
  }

  for (var row = 0; row < rows; row++) {
    for (var col = 0; col < sectors; col++) {
      final a = at(row, col),
          b = at(row, col + 1),
          c = at(row + 1, col + 1),
          d = at(row + 1, col);
      final inward = Vec3(-a.x, 0, -a.z);
      final material = (_noise(row * 91 + col * 7) * 8).floor();
      mesh.quad(a, b, c, d, material, facing: inward);
      // Broken mineral seams, not luminous concentric rings.
      if (col % 7 == 2 && row % 5 != 0) {
        final n = inward.normalized * 3;
        mesh.quad(
          a + n,
          a + (b - a) * .012 + n,
          d + (c - d) * .018 + n,
          d + n,
          8,
          facing: inward,
        );
      }
    }
  }
  return mesh.node('abyss_cliffs', 'Abismo · paredes de basalto');
}

SceneNode _ledge() {
  final mesh = _WorldMesh([
    _material('Borda · pedra umida', 0xff41474a),
    _material('Fratura · rocha escura', 0xff202b31),
  ]);
  const a = Vec3(-1050, 0, -185),
      b = Vec3(-305, 0, -110),
      c = Vec3(-245, 0, 95),
      d = Vec3(-1050, 0, 210),
      e = Vec3(-900, -250, -130),
      f = Vec3(-410, -85, -50),
      g = Vec3(-900, -280, 160);
  mesh.quad(a, b, c, d, 0, facing: const Vec3(0, 1, 0));
  mesh.quad(b, a, e, f, 1, facing: const Vec3(0, 0, -1));
  mesh.quad(c, b, f, g, 1, facing: const Vec3(1, 0, 0));
  mesh.quad(d, c, g, e, 1, facing: const Vec3(0, 0, 1));
  return mesh.node('abyss_ledge', 'Borda · plataforma fraturada');
}

VideoProject buildAbyssCinematicTemplate() {
  Camera3D shot(
    String id,
    String name,
    Vec3 Function(double) position,
    Vec3 Function(double) target,
    double lens, {
    double Function(double)? roll,
  }) => Camera3D(
    id: id,
    name: name,
    posX: _sample((t) => position(t).x),
    posY: _sample((t) => position(t).y),
    posZ: _sample((t) => position(t).z),
    poiX: _sample((t) => target(t).x),
    poiY: _sample((t) => target(t).y),
    poiZ: _sample((t) => target(t).z),
    focalLength: AnimatedDouble(lens),
    rotZ: _sample(roll ?? (t) => 0),
  );
  final cameras = [
    shot(
      'abyss_cam_1',
      '01 · A borda / 28 mm',
      (t) => Vec3(330 - t * 14, 270 - t * 22, 640 - t * 10),
      (t) => Vec3(-300, 12 - math.max(0, t - 1.7) * 55, 0),
      28,
    ),
    shot(
      'abyss_cam_2',
      '02 · Queda / travelling 38 mm',
      (t) => abyssActorPosition(t) + const Vec3(350, 80, 570),
      (t) => abyssActorPosition(t) + const Vec3(0, 8, 0),
      38,
      roll: (t) => -6 + 2 * math.sin(t),
    ),
    shot(
      'abyss_cam_3',
      '03 · Vertigem / orbita 30 mm',
      (t) =>
          abyssActorPosition(t) +
          Vec3(
            420 * math.sin(.7 - (t - 6.5) * .32),
            90,
            420 * math.cos(.7 - (t - 6.5) * .32),
          ),
      abyssActorPosition,
      30,
      roll: (t) => 8 + (t - 6.5) * 7,
    ),
    shot(
      'abyss_cam_4',
      '04 · O vazio / zenital 26 mm',
      (t) => Vec3(95, abyssActorPosition(10.5).y + 820, 190),
      abyssActorPosition,
      26,
      roll: (t) => -10 - (t - 10.5) * 2,
    ),
  ];
  return VideoProject(
    id: 'abyss_cinematic_template',
    name: 'ABISMO · Queda cinematografica',
    createdAt: DateTime(2026, 9, 5),
    aspectRatio: 1280 / 536,
    resolutionHeight: 804,
    fps: abyssFps,
    markers: [
      for (var i = 0; i < 4; i++)
        Marker(
          time: abyssTime([0, 3.25, 6.5, 10.5][i]),
          label: cameras[i].name,
        ),
    ],
    layers: [
      Scene3DLayer(
        id: 'abyss_scene',
        name: 'ABISMO · cena 3D editavel',
        position: AnimatedOffset(const Offset(960, 402)),
        startTime: Duration.zero,
        duration: abyssDuration,
        showHelpers: false,
        camera: cameras.first,
        extraCameras: cameras.skip(1).toList(),
        shots: [
          for (var i = 0; i < 4; i++)
            CameraShot(
              time: abyssTime([0, 3.25, 6.5, 10.5][i]),
              cameraId: cameras[i].id,
            ),
        ],
        opacity: _keys([(0, 0), (.4, 1), (13.15, 1), (14, 0)]),
        scene: Scene3D(
          showFloorGrid: false,
          background: const Color(0xff071016),
          fogColor: const Color(0xff071016),
          fogDensity: .0011,
          fogStart: 350,
          ambient: .38,
          skyColor: const Color(0xffb4d5e5),
          groundColor: const Color(0xff183945),
          environment: EnvironmentKind.noite,
          envReflect: .35,
          lights: [
            Light3D(
              id: 'abyss_key',
              color: const Color(0xffffd5a6),
              direction: const Vec3(-.45, -.55, -.7),
              intensity: AnimatedDouble(1.45),
            ),
            Light3D(
              id: 'abyss_rim',
              color: const Color(0xff73d6ef),
              direction: const Vec3(.5, .25, .8),
              intensity: AnimatedDouble(1.1),
            ),
            Light3D(
              id: 'abyss_fill',
              color: const Color(0xff8199c1),
              direction: const Vec3(.7, -.2, -.4),
              intensity: AnimatedDouble(.28),
            ),
          ],
          nodes: [
            _cliffs(),
            _ledge(),
            SceneNode(
              id: 'abyss_explorer',
              name: 'Explorador · animar rig',
              size: 110,
              modelAsset: buildAbyssExplorer(),
              modelMotion: _fallPose(),
              x: _sample((t) => abyssActorPosition(t).x),
              y: _sample((t) => abyssActorPosition(t).y),
              z: _sample((t) => abyssActorPosition(t).z),
              rotX: _keys([
                (0, 0),
                (1, 0),
                (3, 25),
                (6, -18),
                (9, 30),
                (14, -25),
              ]),
              rotY: _keys([(0, 10), (2, -12), (5, -35), (9, 25), (14, -30)]),
              rotZ: _keys([
                (0, 0),
                (1, 0),
                (3, -22),
                (6, 32),
                (9, 140),
                (14, 260),
              ]),
            ),
            SceneNode(
              id: 'abyss_dust',
              name: 'Suspensao · poeira mineral',
              kind: Element3DKind.octahedron,
              size: 1.8,
              material: const Material3D(
                baseColor: Color(0xff92a7a8),
                kind: MaterialKind.unlit,
              ),
              instances: [
                for (var i = 0; i < 260; i++)
                  Vec3(
                    (_noise(i * 3 + 2000) - .5) * 1200,
                    100 - _noise(i * 3 + 2001) * 6200,
                    (_noise(i * 3 + 2002) - .5) * 1200,
                  ),
              ],
            ),
            for (var i = 0; i < 12; i++)
              SceneNode(
                id: 'abyss_debris_$i',
                name: 'Fragmento ${i + 1}',
                kind: Element3DKind.diamond,
                size: 4 + _noise(i + 800) * 9,
                scale: _keys([(0, 0), (1.6, 0), (2.2, 1), (14, 1)]),
                material: const Material3D(
                  baseColor: Color(0xff455359),
                  roughness: .95,
                ),
                x: _sample(
                  (t) => abyssActorPosition(t).x - 110 + _noise(i + 300) * 300,
                ),
                y: _sample(
                  (t) =>
                      abyssActorPosition(t).y -
                      180 +
                      _noise(i + 400) * 500 -
                      t * i * 3,
                ),
                z: AnimatedDouble(-200 + _noise(i + 500) * 500),
                rotX: _sample((t) => t * (10 + i * 4)),
                rotZ: _sample((t) => t * (15 - i * 5)),
              ),
          ],
        ),
      ),
    ],
  );
}
