import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/rotation_math.dart';

void main() {
  test('Scene 3D links preserve world pose across mixed rotation axes', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final scene = Scene3D(
      nodes: [
        SceneNode(
          id: 'p',
          name: 'p',
          isNull: true,
          x: AnimatedDouble(45),
          z: AnimatedDouble(70),
          rotX: AnimatedDouble(32),
          rotY: AnimatedDouble(67),
          scale: AnimatedDouble(2),
        ),
        SceneNode(
          id: 'child',
          name: 'child',
          x: AnimatedDouble(100),
          y: AnimatedDouble(90),
          z: AnimatedDouble(10),
          rotX: AnimatedDouble(20),
          rotZ: AnimatedDouble(80),
        ),
      ],
    );
    final layer = Scene3DLayer(
      id: 'scene',
      name: 'test',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      scene: scene,
    );
    final e = c.read(editorControllerProvider.notifier);
    e.openProject(
      VideoProject(name: 'test', createdAt: DateTime(2026), layers: [layer]),
    );
    final before = resolveNodeTransform(
      scene,
      scene.nodeById('child')!,
      Duration.zero,
    );
    for (final parent in ['p', null]) {
      e.setSceneNodeParent(
        'scene',
        'child',
        parent,
        preserveWorldAt: Duration.zero,
      );
      final updated =
          (c.read(editorControllerProvider).layers.single as Scene3DLayer)
              .scene;
      final after = resolveNodeTransform(
        updated,
        updated.nodeById('child')!,
        Duration.zero,
      );
      expect(after.position.x, closeTo(before.position.x, 1e-6));
      expect(after.position.y, closeTo(before.position.y, 1e-6));
      expect(after.position.z, closeTo(before.position.z, 1e-6));
      final a = rotationMatrix(after.rotX, after.rotY, after.rotZ).storage;
      final b = rotationMatrix(before.rotX, before.rotY, before.rotZ).storage;
      for (var i = 0; i < a.length; i++) {
        expect(a[i], closeTo(b[i], 1e-6));
      }
    }
  });
  test('deep null chain keeps all parents and stops cycles', () {
    final nodes = [
      for (var i = 0; i < 30; i++)
        SceneNode(
          id: '$i',
          name: '$i',
          isNull: true,
          parentId: i == 0 ? null : '${i - 1}',
          x: AnimatedDouble(1),
        ),
    ];
    final scene = Scene3D(nodes: nodes);
    expect(
      resolveNodeTransform(scene, nodes.last, Duration.zero).position.x,
      30,
    );
    final cyclic = scene.copyWith(
      nodes: [
        nodes.first.copyWith(parentId: '29'),
        ...nodes.skip(1),
      ],
    );
    expect(
      resolveNodeTransform(cyclic, nodes.last, Duration.zero).position.x,
      30,
    );
  });
  const t = Duration(seconds: 1);
  NullLayer n(
    String id, {
    Offset pos = Offset.zero,
    double z = 0,
    double rx = 0,
    double ry = 0,
  }) => NullLayer(
    id: id,
    name: id,
    startTime: Duration.zero,
    duration: const Duration(seconds: 5),
    position: AnimatedOffset(pos),
    positionZ: AnimatedDouble(z),
    rotationX: AnimatedDouble(rx),
    rotationY: AnimatedDouble(ry),
    is3D: true,
  );
  // O AUTO-KEY NASCE DESLIGADO (`docs/keyframe-explicito.md`). Este teste
  // fixava o padrao ANTIGO (`isTrue`), que era o defeito das marcas que
  // apareciam sozinhas; o que ele guarda de valioso — a rotacao gravando
  // os tres eixos — continua, com o interruptor ligado a mao.
  test('AutoKey nasce desligado; ligado, a rotacao escreve todos os eixos', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    final layer = n('n').copyLayer(
      rotation: AnimatedDouble(0, [
        Keyframe(time: Duration.zero, value: 0.0),
        Keyframe(time: const Duration(seconds: 2), value: 180.0),
      ]),
    );
    e.openProject(
      VideoProject(name: 'test', createdAt: DateTime(2026), layers: [layer]),
    );
    expect(c.read(autoKeyframeProvider), isFalse);
    c.read(autoKeyframeProvider.notifier).state = true;
    e.editRotation('n', t, 450);
    final result = c.read(editorControllerProvider).layers.single;
    expect(result.rotation.valueAt(t), 450);
    expect(result.rotationX.hasKeyframeAt(t), isTrue);
    expect(result.rotationY.hasKeyframeAt(t), isTrue);
    c.read(autoKeyframeProvider.notifier).state = false;
    e.editRotation('n', const Duration(milliseconds: 1500), 540);
    expect(c.read(edicaoPendenteProvider), isNotNull);
    expect(
      c
          .read(projetoVisivelProvider)
          .layers
          .single
          .rotation
          .valueAt(const Duration(milliseconds: 1500)),
      540,
    );
    expect(
      c.read(editorControllerProvider).layers.single.rotation.keyframes.length,
      3,
    );
  });
  test('nested nulls retain world position at nonzero Z when linked', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.openProject(
      VideoProject(
        name: 'test',
        createdAt: DateTime(2026),
        layers: [
          n('a', pos: const Offset(20, 30), z: 300, rx: 20, ry: 30),
          n('b', pos: const Offset(90, 80), z: 500),
          n('c', pos: const Offset(200, 160), z: 700),
          n('d', pos: const Offset(210, 170), z: 800),
        ],
      ),
    );
    e.linkProperty('b', LayerProp.parent, 'a', t);
    e.linkProperty('c', LayerProp.parent, 'b', t);
    e.linkProperty('d', LayerProp.parent, 'c', t);
    final p = c.read(editorControllerProvider);
    final r = effectiveTransform(p, p.layers.last, t);
    expect(r.pos.dx, closeTo(210, 1e-6));
    expect(r.pos.dy, closeTo(170, 1e-6));
    expect(r.z, closeTo(800, 1e-6));
    e.linkProperty('a', LayerProp.parent, 'd', t);
    expect(c.read(editorControllerProvider).links.length, 3);
  });
  test('bind rotation inverse is applied before current rotation', () {
    final child = n('child', pos: const Offset(100, 0));
    final p = VideoProject(
      name: 'test',
      createdAt: DateTime(2026),
      layers: [child, n('parent', rx: 90, ry: 90)],
      links: [
        PropertyLink(
          targetLayerId: 'child',
          targetProp: LayerProp.parent,
          sourceLayerId: 'parent',
          baseRotationY: 90,
        ),
      ],
    );
    final r = effectiveTransform(p, child, t);
    // Ry(90) * Rx(90) * Ry(-90) maps X to -Y.
    expect(r.pos.dx, closeTo(0, 1e-6));
    expect(r.pos.dy, closeTo(-100, 1e-6));
    expect(r.z, closeTo(0, 1e-6));
  });
}
