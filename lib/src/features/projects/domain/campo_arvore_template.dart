import 'dart:math' as math;
import 'dart:ui';

import '../../editor/domain/camera3d.dart';
import '../../editor/domain/camera_cuts.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/model_asset3d.dart';
import '../../editor/domain/scene3d.dart';
import '../../editor/domain/video_project.dart';
import 'colina_tv_template.dart';
import 'malha_codigo.dart';
import 'textura_procedural.dart';

const campoDuration = Duration(seconds: 25);
const campoProjectId = 'campo_arvore_5_tomadas';
const campoShotNames = [
  '01 · Amanhecer',
  '02 · Caminho até a árvore',
  '03 · Órbita da copa',
  '04 · Entre os galhos',
  '05 · O campo desperta',
];
Duration _time(double s) => Duration(microseconds: (s * 1e6).round());

/// A single shared landscape, five real cameras and editable geometry.
/// Seeded generation makes opening, seeking and export reproducible.
VideoProject buildCampoArvoreTemplate({
  String? skyTexture,
  ModelAsset3D? scannedTree,
}) {
  final base = buildColinaTvTemplate();
  final landscape = base.layers.whereType<Scene3DLayer>().single.scene;
  final ground = colinaAltura(0, 40);
  final tree = _tree(ground);
  final warmTree = scannedTree == null
      ? null
      : ModelAsset3D({
          ...scannedTree.data,
          'materials': [
            for (final m in scannedTree.data['materials'] as List)
              {
                ...m as Map,
                'color': [1.0, .88, .46, 1.0],
                'unlit': true,
              },
          ],
        });
  final cameras = <Camera3D>[];
  for (var shot = 0; shot < 5; shot++) {
    Vec3 eye(double u) => switch (shot) {
      0 => Vec3(-430 + 100 * u, 540 - 25 * u, 1430 - 120 * u),
      1 => Vec3(-170 + 90 * u, 380 + 25 * u, 950 - 180 * u),
      2 => Vec3(
        920 * math.sin(-.15 + .65 * u),
        540 + 40 * u,
        40 + 920 * math.cos(-.15 + .65 * u),
      ),
      3 => Vec3(300 - 40 * u, 500 + 20 * u, 850 - 60 * u),
      _ => Vec3(450 + 160 * u, 640 + 180 * u, 1250 + 420 * u),
    };
    final aim = Vec3(0, ground + (shot == 3 ? 290 : 200), 40);
    AnimatedDouble track(double Function(double) f) => AnimatedDouble(f(0), [
      for (var i = 0; i <= 20; i++)
        Keyframe(time: _time(shot * 5 + i / 4), value: f(i / 20)),
    ]);
    cameras.add(
      Camera3D(
        id: 'campo_camera_$shot',
        name: campoShotNames[shot],
        posX: track((u) => eye(u).x),
        posY: track((u) => eye(u).y),
        posZ: track((u) => eye(u).z),
        poiX: AnimatedDouble(aim.x),
        poiY: AnimatedDouble(aim.y),
        poiZ: AnimatedDouble(aim.z),
        focalLength: AnimatedDouble(shot == 3 ? 36 : 32),
        dof: DepthOfField(
          enabled: shot == 3,
          focusDistance: track((u) => (eye(u) - aim).length),
          aperture: AnimatedDouble(4),
          blurLevel: AnimatedDouble(35),
        ),
      ),
    );
  }
  final scene = landscape.copyWith(
    ambient: .48,
    skyColor: const Color(0xffc7dbe4),
    groundColor: const Color(0xff6a7143),
    fogColor: const Color(0xffc5d0b7),
    fogDensity: .00012,
    fogStart: 850,
    lights: [
      Light3D(
        id: 'campo_sol',
        color: const Color(0xffffe1ac),
        direction: const Vec3(-.65, -.65, .4),
        intensity: AnimatedDouble(1.65),
        castsShadow: true,
        softness: .6,
      ),
      Light3D(
        id: 'campo_ceu',
        color: const Color(0xffc0d9f0),
        direction: const Vec3(.2, -.75, -.55),
        intensity: AnimatedDouble(.65),
      ),
    ],
    nodes: [
      for (final n in landscape.nodes)
        if (n.id.startsWith('colina_grama'))
          _denserGrass(n)
        else if (n.id.startsWith('colina_flor'))
          n,
      _meadow(),
      if (scannedTree == null)
        tree.first
      else
        SceneNode(
          id: 'campo_tronco',
          name: 'Carvalho · tronco escaneado',
          modelAsset: warmTree,
          size: 205,
          y: AnimatedDouble(ground + 168),
          z: AnimatedDouble(40),
          rotX: AnimatedDouble(-90),
        ),
      tree.last,
    ],
  );
  return VideoProject(
    id: campoProjectId,
    name: 'CAMPO · A árvore da manhã',
    createdAt: DateTime(2026, 9, 6),
    aspectRatio: 16 / 9,
    resolutionHeight: 720,
    fps: 30,
    markers: [
      for (var i = 0; i < 5; i++)
        Marker(
          time: Duration(seconds: i * 5),
          label: campoShotNames[i],
        ),
    ],
    layers: [
      Scene3DLayer(
        id: 'campo_cena',
        name: 'Campo e árvore · cinco câmeras',
        startTime: Duration.zero,
        duration: campoDuration,
        position: AnimatedOffset(const Offset(640, 360)),
        showHelpers: false,
        scene: scene,
        camera: cameras.first,
        extraCameras: cameras.skip(1).toList(),
        shots: [
          for (var i = 0; i < 5; i++)
            CameraShot(
              time: Duration(seconds: i * 5),
              cameraId: cameras[i].id,
            ),
        ],
      ),
      for (final l in base.layers)
        if (skyTexture == null && l.id == 'colina_ceu')
          l.copyLayer(duration: campoDuration),
      if (skyTexture != null) _sky(skyTexture),
    ],
  );
}

SceneNode _denserGrass(SceneNode node) {
  final variant = int.parse(node.id.split('_').last);
  final r = math.Random(945 + variant);
  final height = 22.0 + 12 * ruido(variant * 7 + 1);
  return node.copyWith(
    instances: [
      ...node.instances,
      for (var i = 0; i < 220; i++)
        (() {
          final x = r.nextDouble() * 2100 - 1050;
          final z = r.nextDouble() * 1700 - 450;
          return Vec3(x, colinaAltura(x, z) + height / 2, z);
        })(),
    ],
  );
}

List<SceneNode> _tree(double floor) {
  final random = math.Random(73163);
  final bark = pngDataUri(128, 256, (x, y, rgb) {
    final ridge = fbm(x / 10, y / 70, semente: 23);
    final crack = math
        .pow((math.sin(x * .69 + fbm(x / 19, y / 43) * 5) + 1) / 2, 8)
        .toDouble();
    final n = .25 + ridge * .20 - crack * .13 + (ruido(x * 311 + y) - .5) * .06;
    rgb[0] = canal8(n * 1.13);
    rgb[1] = canal8(n);
    rgb[2] = canal8(n * .77);
  });
  final wood = MalhaCodigo([
    materialCodigo(
      'Casca · sulcos naturais',
      0xffffffff,
      imagem: bark,
      rugosidade: .98,
    ),
  ]);
  final foliage = MalhaCodigo([
    for (final color in [
      0xff3e5721,
      0xff4e6928,
      0xff617b31,
      0xff758c3e,
      0xff304c24,
      0xff5a732e,
    ])
      materialCodigo(
        'Folha de carvalho',
        color,
        rugosidade: .78,
        doisLados: true,
        semLuz: true,
      ),
  ]);
  void branch(Vec3 a, Vec3 b, double r0, double r1, int sides) {
    final direction = (b - a).normalized;
    final side = direction
        .cross(
          direction.y.abs() > .9 ? const Vec3(1, 0, 0) : const Vec3(0, 1, 0),
        )
        .normalized;
    final up = direction.cross(side).normalized;
    final lo = <int>[], hi = <int>[];
    for (var j = 0; j <= sides; j++) {
      final angle = 2 * math.pi * j / sides;
      final normal = side * math.cos(angle) + up * math.sin(angle);
      lo.add(
        wood.vertice(0, a + normal * r0, normal, uv: Offset(j / sides, 0)),
      );
      hi.add(
        wood.vertice(0, b + normal * r1, normal, uv: Offset(j / sides, 1)),
      );
    }
    for (var j = 0; j < sides; j++) {
      wood.tri(0, lo[j], hi[j], hi[j + 1]);
      wood.tri(0, lo[j], hi[j + 1], lo[j + 1]);
    }
  }

  void leaf(Vec3 p, double size) {
    final az = random.nextDouble() * math.pi * 2;
    final tilt = -.3 + random.nextDouble() * 1.2;
    final axis = Vec3(
      math.cos(az) * math.cos(tilt),
      math.sin(tilt),
      math.sin(az) * math.cos(tilt),
    );
    final side = axis.cross(const Vec3(0, 1, 0)).normalized;
    final normal = side.cross(axis).normalized;
    final stem = p - axis * size, tip = p + axis * size;
    final left = p + side * (size * .48), right = p - side * (size * .48);
    final ridge = p + normal * (size * .16);
    final material = random.nextInt(6);
    // Smooth canopy normals approximate leaf translucency in the compatible
    // renderer, avoiding black paper-like backs on the foliage.
    final litNormal = Vec3(
      (p.x / 300) * .4,
      .85,
      ((p.z - 40) / 300) * .4,
    ).normalized;
    final indices = [
      for (final v in [stem, left, tip, right, ridge])
        foliage.vertice(material, v, litNormal),
    ];
    for (var i = 0; i < 4; i++) {
      foliage.tri(material, indices[i], indices[(i + 1) % 4], indices[4]);
    }
  }

  final root = Vec3(0, floor - 4, 40);
  final fork = root + const Vec3(8, 145, -5);
  branch(root, root + const Vec3(-6, 70, 0), 25, 20, 14);
  branch(root + const Vec3(-6, 70, 0), fork, 20, 14, 12);
  for (var i = 0; i < 8; i++) {
    final a = i * math.pi / 4;
    final x = math.cos(a) * 75, z = 40 + math.sin(a) * 75;
    branch(
      Vec3(x, colinaAltura(x, z) - 3, z),
      root + const Vec3(0, 28, 0),
      3,
      14,
      7,
    );
  }
  for (var limb = 0; limb < 11; limb++) {
    final az = limb * 2.399963 + random.nextDouble() * .3;
    final radius = 70 + random.nextDouble() * 110;
    final high = 260 + random.nextDouble() * 105;
    final elbow =
        root +
        Vec3(
          math.cos(az) * radius * .43,
          high * .74,
          math.sin(az) * radius * .43,
        );
    final end = root + Vec3(math.cos(az) * radius, high, math.sin(az) * radius);
    branch(fork, elbow, 11 - limb * .45, 6, 9);
    branch(elbow, end, 6, 2.3, 7);
    for (var twig = 0; twig < 9; twig++) {
      final ta = random.nextDouble() * math.pi * 2;
      final spread = 25 + random.nextDouble() * 60;
      final center =
          end +
          Vec3(
            math.cos(ta) * spread,
            10 + random.nextDouble() * 48,
            math.sin(ta) * spread,
          );
      branch(end, center, 2.3, .6, 5);
      for (var k = 0; k < 67; k++) {
        final angle = random.nextDouble() * math.pi * 2;
        final h = random.nextDouble() * 2 - 1;
        final r = math.pow(random.nextDouble(), 1 / 3).toDouble();
        final disk = math.sqrt(1 - h * h);
        final p =
            center +
            Vec3(
              math.cos(angle) * disk * 57 * r,
              h * 60 * r - 20,
              math.sin(angle) * disk * 57 * r,
            );
        leaf(p, 6 + random.nextDouble() * 5);
      }
    }
  }
  return [
    wood.no('campo_tronco', 'Carvalho · tronco e galhos'),
    foliage.no('campo_folhas', 'Carvalho · 6.633 folhas'),
  ];
}

SceneNode _meadow() {
  const extent = 6500.0;
  final texture = pngDataUri(1024, 1024, (px, py, rgb) {
    final x = (px / 1024 * 2 - 1) * extent, z = (py / 1024 * 2 - 1) * extent;
    final n = fbm(x / 90, z / 90, semente: 313);
    final large = fbm(x / 750, z / 750, semente: 871);
    final shadow = math.exp(
      -math.pow((x + 160) / 200, 2) - math.pow((z - 190) / 250, 2),
    );
    final grain = ruido(px * 71 + py * 117) * .04;
    rgb[0] = canal8((.23 + n * .1 + grain) * (1 - .4 * shadow));
    rgb[1] = canal8((.33 + n * .16 + large * .07 + grain) * (1 - .42 * shadow));
    rgb[2] = canal8((.10 + n * .065) * (1 - .3 * shadow));
  });
  final m = MalhaCodigo([
    materialCodigo(
      'Prado · relva e sombra difusa',
      0xffffffff,
      imagem: texture,
      rugosidade: 1,
    ),
  ]);
  // Denser tessellation near the hero, progressively coarser toward horizon.
  const segments = 72;
  double coord(int i) {
    final q = i / segments * 2 - 1;
    return q.sign * q.abs() * q.abs() * extent;
  }

  for (var j = 0; j <= segments; j++) {
    for (var i = 0; i <= segments; i++) {
      final x = coord(i), z = coord(j);
      final dx = colinaAltura(x + 5, z) - colinaAltura(x - 5, z);
      final dz = colinaAltura(x, z + 5) - colinaAltura(x, z - 5);
      m.vertice(
        0,
        Vec3(x, colinaAltura(x, z), z),
        Vec3(-dx, 10, -dz).normalized,
        uv: Offset((x / extent + 1) / 2, (z / extent + 1) / 2),
      );
    }
  }
  for (var j = 0; j < segments; j++) {
    for (var i = 0; i < segments; i++) {
      final a = j * (segments + 1) + i,
          b = a + 1,
          d = a + segments + 1,
          c = d + 1;
      m.tri(0, a, c, b);
      m.tri(0, a, d, c);
    }
  }
  return m.no('campo_prado', 'Prado · relevo contínuo');
}

Scene3DLayer _sky(String texture) {
  final m = MalhaCodigo([
    materialCodigo(
      'Céu da manhã',
      0xffffffff,
      imagem: texture,
      semLuz: true,
      doisLados: true,
    ),
  ]);
  final a = m.vertice(
    0,
    const Vec3(-500, -281.25, 0),
    const Vec3(0, 0, 1),
    uv: const Offset(0, 1),
  );
  final b = m.vertice(
    0,
    const Vec3(500, -281.25, 0),
    const Vec3(0, 0, 1),
    uv: const Offset(1, 1),
  );
  final c = m.vertice(
    0,
    const Vec3(500, 281.25, 0),
    const Vec3(0, 0, 1),
    uv: const Offset(1, 0),
  );
  final d = m.vertice(
    0,
    const Vec3(-500, 281.25, 0),
    const Vec3(0, 0, 1),
    uv: const Offset(0, 0),
  );
  m.tri(0, a, b, c);
  m.tri(0, a, c, d);
  return Scene3DLayer(
    id: 'campo_ceu',
    name: 'Céu · luz da manhã',
    startTime: Duration.zero,
    duration: campoDuration,
    position: AnimatedOffset(const Offset(640, 360)),
    showHelpers: false,
    camera: Camera3D(
      posZ: AnimatedDouble(1000),
      focalLength: AnimatedDouble(36),
    ),
    scene: Scene3D(
      nodes: [m.no('campo_celeste', 'Céu texturizado')],
      tonemap: false,
      showFloorGrid: false,
    ),
  );
}
