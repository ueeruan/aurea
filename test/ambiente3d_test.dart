import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

/// O que o pacote "3D" promete: formas novas que existem de verdade,
/// um ambiente que se reflete (ceu em cima, chao embaixo, brilho no
/// horizonte), reflexo que cresce com o material e aparece mais na
/// borda que no centro, imagem que chega ao pintor com coordenadas
/// 0..1, e tudo isso sobrevivendo ao salvar e abrir.
void main() {
  group('formas novas', () {
    test('cada forma nova tem malha com faces', () {
      for (final k in [
        Element3DKind.plane,
        Element3DKind.capsule,
        Element3DKind.tube,
        Element3DKind.octahedron,
        Element3DKind.wedge,
        Element3DKind.dome,
      ]) {
        final m = element3DMesh(k);
        expect(m.verts, isNotEmpty, reason: '$k');
        expect(m.faces, isNotEmpty, reason: '$k');
        for (final f in m.faces) {
          expect(f.length, greaterThanOrEqualTo(3), reason: '$k');
          for (final i in f) {
            expect(i, inInclusiveRange(0, m.verts.length - 1), reason: '$k');
          }
        }
        expect(element3DLabel(k), isNotEmpty);
      }
    });

    test('o plano tem os dois lados, em sentidos opostos', () {
      final m = element3DMesh(Element3DKind.plane);
      expect(m.faces.length, 2);
      expect(m.faces[1], m.faces[0].reversed.toList());
    });

    test('a capsula e o tubo sao fechados: toda aresta em duas faces', () {
      for (final k in [Element3DKind.capsule, Element3DKind.tube,
          Element3DKind.octahedron, Element3DKind.dome]) {
        final m = element3DMesh(k);
        final uso = <String, int>{};
        for (final f in m.faces) {
          for (var i = 0; i < f.length; i++) {
            final a = f[i], b = f[(i + 1) % f.length];
            final chave = a < b ? '$a-$b' : '$b-$a';
            uso[chave] = (uso[chave] ?? 0) + 1;
          }
        }
        expect(uso.values.every((n) => n == 2), isTrue, reason: '$k');
      }
    });

    test('as formas antigas mantem o indice: o arquivo salvo nao muda', () {
      expect(Element3DKind.cube.index, 0);
      expect(Element3DKind.star.index, 8);
      expect(Element3DKind.plane.index, 9);
    });
  });

  group('ambiente', () {
    test('ceu em cima, chao embaixo, horizonte entre os dois', () {
      for (final k in EnvironmentKind.values) {
        final (tr, tg, tb) = environmentColor(k, 0, 1, 0);
        final (cr, cg, cb) = environmentColor(k, 0, -1, 0);
        final luzTopo = tr + tg + tb;
        final luzChao = cr + cg + cb;
        expect(luzTopo, greaterThan(luzChao), reason: '$k');
      }
    });

    test('o por do sol e mais quente no horizonte que no alto', () {
      final (hr, _, hb) = environmentColor(EnvironmentKind.porDoSol, 0, 0, 1);
      final (tr, _, tb) = environmentColor(EnvironmentKind.porDoSol, 0, 1, 0);
      expect(hr - hb, greaterThan(tr - tb));
    });

    test('a luz forte aparece so na direcao espelhada dela', () {
      final (a, _, _) = environmentColor(EnvironmentKind.ceu, 0, 0, 1,
          sunX: 0, sunY: 0, sunZ: 1, sunGain: 2, sunSharp: 80);
      final (b, _, _) = environmentColor(EnvironmentKind.ceu, 0, 0, 1,
          sunX: 0, sunY: 0, sunZ: -1, sunGain: 2, sunSharp: 80);
      expect(a, greaterThan(b + 1));
    });

    test('direcao nula nao explode', () {
      final (r, g, b) = environmentColor(EnvironmentKind.neon, 0, 0, 0);
      expect(r.isFinite && g.isFinite && b.isFinite, isTrue);
    });
  });

  group('reflexo no material', () {
    const cena = Scene3D(
      lights: [],
      tonemap: false,
      environment: EnvironmentKind.estudio,
      envReflect: 1,
    );
    const normal = Vec3(0, 0, -1);
    const ponto = Vec3.zero;

    Color cor(double refl, Vec3 vista, {double metal = 0}) => shadeFace(
          scene: cena,
          material: Material3D(
              baseColor: const Color(0xFF200000),
              reflectivity: refl,
              metallic: metal),
          normal: normal,
          point: ponto,
          t: Duration.zero,
          viewDir: vista,
        );

    test('sem reflexo nada muda; com reflexo a face ganha o ambiente', () {
      final semVista = shadeFace(
        scene: cena,
        material: const Material3D(
            baseColor: Color(0xFF200000), reflectivity: 1),
        normal: normal,
        point: ponto,
        t: Duration.zero,
      );
      final fosco = cor(0, const Vec3(0, 0, -1));
      expect(fosco.b, closeTo(semVista.b, 1e-9));
      final espelho = cor(1, const Vec3(0, 0, -1));
      // Vermelho escuro nao tem azul; o estudio cinza tem.
      expect(espelho.b, greaterThan(fosco.b + 0.05));
    });

    test('Fresnel: a borda reflete mais que o centro', () {
      final centro = cor(0.6, const Vec3(0, 0, -1));
      final borda = cor(0.6, const Vec3(0.995, 0, -0.1).normalized);
      expect(borda.b, greaterThan(centro.b));
    });

    test('metal tinge o reflexo com a propria cor', () {
      final metal = cor(1, const Vec3(0, 0, -1), metal: 1);
      final dieletrico = cor(1, const Vec3(0, 0, -1), metal: 0);
      // Base vermelha escura: o metal devolve o ambiente avermelhado, o
      // dieletrico devolve o ambiente como ele e (mais azul).
      expect(metal.b, lessThan(dieletrico.b));
    });
  });

  group('imagem no objeto', () {
    test('faces com imagem chegam ao pintor com uv em 0..1', () {
      final scene = Scene3D(
        lights: Scene3D.tresPontos,
        nodes: [
          SceneNode(
            kind: Element3DKind.cube,
            size: 100,
            material: const Material3D(imagePath: '/tmp/x.png'),
          ),
        ],
      );
      const cam = RenderCamera(position: Vec3(0, 0, 800));
      final frame = renderScene(scene, cam, const Size(400, 400), Duration.zero);
      expect(frame.opaque, isNotEmpty);
      for (final t in frame.opaque) {
        expect(t.texture, '/tmp/x.png');
        for (final uv in [t.uvA!, t.uvB!, t.uvC!]) {
          expect(uv.dx, inInclusiveRange(0.0, 1.0));
          expect(uv.dy, inInclusiveRange(0.0, 1.0));
        }
      }
      // A luz vira fator: sem imagem carregada a face sai clara, nunca
      // tingida pela cor base.
      final c = frame.opaque.first.color;
      expect(c.r, closeTo(c.g, 0.25));
    });

    test('sem imagem, nada de uv', () {
      final scene = Scene3D(
        nodes: [SceneNode(kind: Element3DKind.cube, size: 100)],
      );
      const cam = RenderCamera(position: Vec3(0, 0, 800));
      final frame = renderScene(scene, cam, const Size(400, 400), Duration.zero);
      expect(frame.opaque.every((t) => t.texture == null && t.uvA == null),
          isTrue);
    });
  });

  group('salvar e abrir', () {
    test('material, cena e elemento guardam reflexo, ambiente e imagem', () {
      final projeto = VideoProject(
        name: 'p',
        createdAt: DateTime(2026),
        layers: [
          Scene3DLayer(
            name: 'cena',
            startTime: Duration.zero,
            duration: const Duration(seconds: 2),
            scene: Scene3D(
              environment: EnvironmentKind.neon,
              envReflect: 0.4,
              nodes: [
                SceneNode(
                  kind: Element3DKind.capsule,
                  material: const Material3D(
                      reflectivity: 0.8, imagePath: '/a/b.png'),
                ),
              ],
            ),
          ),
          Element3DLayer(
            name: 'el',
            startTime: Duration.zero,
            duration: const Duration(seconds: 2),
            kind: Element3DKind.dome,
            reflect: 0.5,
            environment: EnvironmentKind.porDoSol,
            imagePath: '/c/d.jpg',
          ),
        ],
      );
      final volta = projectFromJson(projectToJson(projeto));
      final cena = volta.layers.whereType<Scene3DLayer>().first;
      expect(cena.scene.environment, EnvironmentKind.neon);
      expect(cena.scene.envReflect, closeTo(0.4, 1e-9));
      expect(cena.scene.nodes.first.kind, Element3DKind.capsule);
      expect(cena.scene.nodes.first.material.reflectivity, closeTo(0.8, 1e-9));
      expect(cena.scene.nodes.first.material.imagePath, '/a/b.png');
      final el = volta.layers.whereType<Element3DLayer>().first;
      expect(el.kind, Element3DKind.dome);
      expect(el.reflect, closeTo(0.5, 1e-9));
      expect(el.environment, EnvironmentKind.porDoSol);
      expect(el.imagePath, '/c/d.jpg');
      // Tirar a imagem tira mesmo.
      expect(el.copyElement3D(clearImage: true).imagePath, isNull);
    });
  });

  test('AnimatedDouble continua sendo o que o no anima', () {
    final n = SceneNode(x: AnimatedDouble(3));
    expect(n.x.base, 3);
  });
}
