import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

void main() {
  const dur = Duration(seconds: 5);

  NullLayer nl(String id, Offset pos,
          {double rot = 0, double rotY = 0, double z = 0}) =>
      NullLayer(
        id: id,
        name: id,
        startTime: Duration.zero,
        duration: dur,
        position: AnimatedOffset(pos),
        rotation: AnimatedDouble(rot),
        rotationY: AnimatedDouble(rotY),
        positionZ: AnimatedDouble(z),
        is3D: true,
      );

  PropertyLink parent(String child, String source, Offset base) =>
      PropertyLink(
        targetLayerId: child,
        targetProp: LayerProp.parent,
        sourceLayerId: source,
        offsetX: base.dx,
        offsetY: base.dy,
      );

  VideoProject proj(List<Layer> layers, List<PropertyLink> links) =>
      VideoProject(
        name: 'p',
        createdAt: DateTime(2026),
        layers: layers,
        links: links,
      );

  group('cadeia de parenting (objeto -> nulo1 -> nulo2)', () {
    test('mover o nulo 2 move o objeto atraves da cadeia', () {
      final obj = ShapeLayer(
        id: 'obj',
        name: 'obj',
        startTime: Duration.zero,
        duration: dur,
        position: AnimatedOffset(const Offset(100, 0)),
      );
      final p = proj(
        [obj, nl('n1', Offset.zero), nl('n2', const Offset(30, 40))],
        [
          parent('obj', 'n1', Offset.zero), // n1 estava em (0,0) no vinculo
          parent('n1', 'n2', Offset.zero), // n2 estava em (0,0) no vinculo
        ],
      );
      // n2 andou (30,40) desde o vinculo -> n1 anda junto -> obj tambem.
      final eff = effectiveTransform(p, obj, Duration.zero);
      expect(eff.pos.dx, closeTo(130, 1e-6));
      expect(eff.pos.dy, closeTo(40, 1e-6));
    });

    test('girar o nulo 2 em Z orbita o objeto no fim da cadeia', () {
      final obj = ShapeLayer(
        id: 'obj',
        name: 'obj',
        startTime: Duration.zero,
        duration: dur,
        position: AnimatedOffset(const Offset(100, 0)),
      );
      final p = proj(
        [obj, nl('n1', Offset.zero), nl('n2', Offset.zero, rot: 90)],
        [
          parent('obj', 'n1', Offset.zero),
          parent('n1', 'n2', Offset.zero),
        ],
      );
      final eff = effectiveTransform(p, obj, Duration.zero);
      // offset (100,0) girado 90 graus -> (0,100); rotacao herdada 90.
      expect(eff.pos.dx, closeTo(0, 1e-6));
      expect(eff.pos.dy, closeTo(100, 1e-6));
      expect(eff.rot, closeTo(90, 1e-6));
    });

    test('girar o nulo em Y (3D) empurra o objeto em profundidade', () {
      final obj = ShapeLayer(
        id: 'obj',
        name: 'obj',
        startTime: Duration.zero,
        duration: dur,
        position: AnimatedOffset(const Offset(100, 0)),
      );
      final p = proj(
        [obj, nl('n1', Offset.zero, rotY: 90)],
        [parent('obj', 'n1', Offset.zero)],
      );
      final eff = effectiveTransform(p, obj, Duration.zero);
      // offset (100,0,0) apos Ry(90): x -> 0, z -> -100 (vem pra frente).
      expect(eff.pos.dx, closeTo(0, 1e-6));
      expect(eff.z, closeTo(-100, 1e-6));
      expect(eff.rotY, closeTo(90, 1e-6));
    });

    test('ciclo de parenting nao trava (guarda de ciclo)', () {
      final p = proj(
        [nl('a', Offset.zero), nl('b', Offset.zero)],
        [
          parent('a', 'b', Offset.zero),
          parent('b', 'a', Offset.zero),
        ],
      );
      final eff =
          effectiveTransform(p, p.layerById('a')!, Duration.zero);
      expect(eff.pos, Offset.zero);
    });
  });
}
