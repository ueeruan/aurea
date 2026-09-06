import 'dart:math' as math;

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/camera3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';

Duration _s(num v) => Duration(milliseconds: (v * 1000).round());

SceneNode _no(
  String id, {
  double x = 0,
  double y = 0,
  double z = 0,
  double rotY = 0,
  double scale = 1,
  String? parent,
  bool isNull = false,
}) =>
    SceneNode(
      id: id,
      name: id,
      x: AnimatedDouble(x),
      y: AnimatedDouble(y),
      z: AnimatedDouble(z),
      rotY: AnimatedDouble(rotY),
      scale: AnimatedDouble(scale),
      parentId: parent,
      isNull: isNull,
    );

void main() {
  group('Nulo dentro da cena', () {
    // O rig de orbita: um nulo girando em Y com o filho deslocado em Z
    // faz o filho dar a VOLTA, em vez de girar no proprio eixo.
    test('girar o nulo em Y orbita o filho', () {
      final cena = Scene3D(nodes: [
        _no('pivo', isNull: true, rotY: 90),
        _no('cubo', z: 100, parent: 'pivo'),
      ]);
      final xf = resolveNodeTransform(
          cena, cena.nodeById('cubo')!, Duration.zero);
      // 90 graus em Y leva o que estava em +Z para +X (ou -X, conforme a
      // convencao) — o que importa e ter SAIDO do eixo Z.
      expect(xf.position.x.abs(), closeTo(100, 0.001));
      expect(xf.position.z.abs(), lessThan(0.001));
    });

    // Volta exatamente ao ponto inicial: sem isso, a orbita "deriva" e
    // a animacao de 0 a 360 nao fecha.
    test('uma volta inteira volta ao mesmo lugar', () {
      Scene3D com(double ang) => Scene3D(nodes: [
            _no('pivo', isNull: true, rotY: ang),
            _no('cubo', z: 100, parent: 'pivo'),
          ]);
      final a = resolveNodeTransform(
          com(0), com(0).nodeById('cubo')!, Duration.zero);
      final b = resolveNodeTransform(
          com(360), com(360).nodeById('cubo')!, Duration.zero);
      expect(b.position.x, closeTo(a.position.x, 1e-6));
      expect(b.position.y, closeTo(a.position.y, 1e-6));
      expect(b.position.z, closeTo(a.position.z, 1e-6));
    });

    test('a posicao do filho soma a do pai', () {
      final cena = Scene3D(nodes: [
        _no('pivo', x: 50, y: 20, isNull: true),
        _no('cubo', x: 10, parent: 'pivo'),
      ]);
      final xf = resolveNodeTransform(
          cena, cena.nodeById('cubo')!, Duration.zero);
      expect(xf.position.x, closeTo(60, 1e-6));
      expect(xf.position.y, closeTo(20, 1e-6));
    });

    test('a escala do pai multiplica a do filho e o afasta', () {
      final cena = Scene3D(nodes: [
        _no('pivo', scale: 2, isNull: true),
        _no('cubo', x: 100, scale: 3, parent: 'pivo'),
      ]);
      final xf = resolveNodeTransform(
          cena, cena.nodeById('cubo')!, Duration.zero);
      expect(xf.scale, closeTo(6, 1e-6));
      expect(xf.position.x, closeTo(200, 1e-6));
    });

    test('tres filhos giram juntos em torno do pivo', () {
      final cena = Scene3D(nodes: [
        _no('pivo', isNull: true, rotY: 180),
        _no('a', z: 100, parent: 'pivo'),
        _no('b', z: 200, parent: 'pivo'),
        _no('c', z: 300, parent: 'pivo'),
      ]);
      for (final (id, dist) in [('a', 100.0), ('b', 200.0), ('c', 300.0)]) {
        final xf = resolveNodeTransform(
            cena, cena.nodeById(id)!, Duration.zero);
        expect(xf.position.z, closeTo(-dist, 0.001), reason: id);
      }
    });

    test('cadeia de tres niveis acumula', () {
      final cena = Scene3D(nodes: [
        _no('a', x: 10, isNull: true),
        _no('b', x: 20, parent: 'a', isNull: true),
        _no('c', x: 30, parent: 'b'),
      ]);
      final xf =
          resolveNodeTransform(cena, cena.nodeById('c')!, Duration.zero);
      expect(xf.position.x, closeTo(60, 1e-6));
    });

    // Um ciclo de parentesco travaria o quadro. Melhor desenhar errado
    // do que congelar o aplicativo.
    test('ciclo de parentesco nao trava', () {
      final cena = Scene3D(nodes: [
        _no('a', x: 1, parent: 'b'),
        _no('b', x: 1, parent: 'a'),
      ]);
      final xf =
          resolveNodeTransform(cena, cena.nodeById('a')!, Duration.zero);
      expect(xf.position.x.isFinite, isTrue);
    });

    test('pai que nao existe e ignorado', () {
      final cena = Scene3D(nodes: [_no('a', x: 7, parent: 'fantasma')]);
      final xf =
          resolveNodeTransform(cena, cena.nodeById('a')!, Duration.zero);
      expect(xf.position.x, closeTo(7, 1e-6));
    });

    test('sem pai, o transform e o proprio', () {
      final cena = Scene3D(nodes: [_no('a', x: 5, rotY: 30, scale: 2)]);
      final xf =
          resolveNodeTransform(cena, cena.nodeById('a')!, Duration.zero);
      expect(xf.position.x, closeTo(5, 1e-6));
      expect(xf.rotY, closeTo(30, 1e-6));
      expect(xf.scale, closeTo(2, 1e-6));
    });
  });

  group('Camera com pai', () {
    RenderCamera camZ(double z) =>
        RenderCamera(position: Vec3(0, 0, z), target: Vec3.zero);

    test('orbita em torno da origem do pai', () {
      final r = applyParentToCamera(
          camZ(800), const NodeTransform(rotY: 90));
      expect(r.position.x.abs(), closeTo(800, 0.001));
      expect(r.position.z.abs(), lessThan(0.001));
    });

    // A REGRA que o documento cobra: camera nao herda escala. Herdar e o
    // bug que faz o enquadramento explodir quando alguem escala o nulo.
    test('escalar o pai NAO muda o enquadramento', () {
      final semEscala = applyParentToCamera(
          camZ(800), const NodeTransform(position: Vec3(100, 0, 0)));
      final comEscala = applyParentToCamera(camZ(800),
          const NodeTransform(position: Vec3(100, 0, 0), scale: 5));
      expect(comEscala.position.x, closeTo(semEscala.position.x, 1e-9));
      expect(comEscala.position.z, closeTo(semEscala.position.z, 1e-9));
      expect(comEscala.focalLength, closeTo(semEscala.focalLength, 1e-9));
    });

    test('mover o pai move a camera junto', () {
      final r = applyParentToCamera(
          camZ(800), const NodeTransform(position: Vec3(0, 50, 0)));
      expect(r.position.y, closeTo(50, 1e-6));
      expect(r.target.y, closeTo(50, 1e-6));
    });

    test('a lente nao e tocada', () {
      const cam = RenderCamera(focalLength: 35, filmWidth: 36);
      final r = applyParentToCamera(cam, const NodeTransform(rotY: 45));
      expect(r.focalLength, 35);
      expect(r.filmWidth, 36);
    });

    test('uma volta inteira volta ao ponto inicial', () {
      final a = applyParentToCamera(camZ(800), NodeTransform.identity);
      final b =
          applyParentToCamera(camZ(800), const NodeTransform(rotY: 360));
      expect(b.position.z, closeTo(a.position.z, 1e-6));
      expect(b.position.x, closeTo(a.position.x, 1e-6));
    });
  });

  group('Camera da cena, ponta a ponta', () {
    Scene3DLayer camadaCom(Scene3D cena, {String? paiComp}) => Scene3DLayer(
          name: 'Cena',
          startTime: Duration.zero,
          duration: _s(5),
          scene: cena,
          camera: Camera3D(
            posX: AnimatedDouble(0),
            posY: AnimatedDouble(0),
            posZ: AnimatedDouble(800),
          ),
          cameraParentLayerId: paiComp,
        );

    test('sem pai nenhum, a camera e a de sempre', () {
      final l = camadaCom(const Scene3D());
      expect(l.cameraAt(Duration.zero).position.z, closeTo(800, 1e-6));
    });

    test('nulo INTERNO orbita a camera', () {
      final cena = Scene3D(
        nodes: [_no('pivo', isNull: true, rotY: 90)],
        cameraParentId: 'pivo',
      );
      final r = camadaCom(cena).cameraAt(Duration.zero);
      expect(r.position.x.abs(), closeTo(800, 0.001));
      expect(r.position.z.abs(), lessThan(0.001));
    });

    test('nulo da COMPOSICAO orbita a camera', () {
      final l = camadaCom(const Scene3D(), paiComp: 'nulo');
      final r = l.cameraAt(Duration.zero,
          external: const NodeTransform(rotY: 90));
      expect(r.position.x.abs(), closeTo(800, 0.001));
    });

    test('os dois juntos compoem', () {
      final cena = Scene3D(
        nodes: [_no('pivo', isNull: true, rotY: 45)],
        cameraParentId: 'pivo',
      );
      final r = camadaCom(cena, paiComp: 'nulo').cameraAt(
        Duration.zero,
        external: const NodeTransform(rotY: 45),
      );
      // 45 + 45 = 90: saiu inteiro do eixo Z.
      expect(r.position.z.abs(), lessThan(0.5));
      expect(r.position.x.abs(), closeTo(800, 0.5));
    });

    test('a distancia ate o alvo nao muda ao orbitar', () {
      final l = camadaCom(const Scene3D(), paiComp: 'nulo');
      for (final ang in [0.0, 30.0, 120.0, 275.0]) {
        final r = l.cameraAt(Duration.zero,
            external: NodeTransform(rotY: ang));
        final d = math.sqrt(r.position.x * r.position.x +
            r.position.y * r.position.y +
            r.position.z * r.position.z);
        expect(d, closeTo(800, 0.001), reason: 'angulo $ang');
      }
    });
  });
}
