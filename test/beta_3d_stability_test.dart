import 'package:aurea_render/aurea_render.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';

import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter_test/flutter_test.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/panorama_cache.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/environment_radiance.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/packed_model_vectors.dart';
import 'package:aurea/src/features/editor/domain/panorama3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';

import 'model3d_engine_test.dart' show GltfFixture, readFixture;

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  test(
    'Z joins position keyframes, interpolation, curves, remove and reset',
    () {
      final c = ProviderContainer();
      addTearDown(c.dispose);
      final e = c.read(editorControllerProvider.notifier);
      e.addShapeLayer(Duration.zero);
      final id = c.read(editorControllerProvider).layers.first.id;
      Layer layer() => c.read(editorControllerProvider).layers.first;
      e.toggle3D(id);
      e.toggleKeyframe(id, Duration.zero, LayerProp.position);
      e.toggleKeyframe(id, const Duration(seconds: 2), LayerProp.position);
      e.editPositionZ(id, const Duration(seconds: 2), 200);
      expect(layer().positionZ.keyframes, hasLength(2));
      e.setSegmentEase(id, LayerProp.position, Duration.zero, Easing.linear);
      expect(
        layer().positionZ.valueAt(const Duration(seconds: 1)),
        closeTo(100, 1e-6),
      );
      e.toggleKeyframe(id, const Duration(seconds: 2), LayerProp.position);
      expect(layer().positionZ.keyframes, hasLength(1));
      e.resetProp(id, LayerProp.position);
      expect(layer().positionZ.keyframes, isEmpty);
      expect(layer().positionZ.base, 0);
    },
  );
  test('null parenting rejects cycles beyond 32 ancestors', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addScene3DLayer(Duration.zero);
    final sceneId = c.read(editorControllerProvider).layers.first.id;
    final ids = [for (var i = 0; i < 40; i++) e.addSceneNull(sceneId)];
    for (var i = 1; i < ids.length; i++) {
      e.setSceneNodeParent(sceneId, ids[i], ids[i - 1]);
    }
    e.setSceneNodeParent(sceneId, ids.first, ids.last);
    final scene =
        (c.read(editorControllerProvider).layers.first as Scene3DLayer).scene;
    expect(scene.nodeById(ids.first)!.parentId, isNull);
    expect(scene.nodeById(ids.last)!.parentId, ids[ids.length - 2]);
  });
  test('packed vectors share contiguous storage and preserve JSON', () {
    final v = PackedModelVectors(2, 3);
    v[0] = [1, 2, 3];
    v[1][2] = 9;
    expect(v.data.lengthInBytes, 48);
    expect(jsonDecode(jsonEncode(v)), [
      [1, 2, 3],
      [0, 0, 9],
    ]);
    expect(() => v[2], throwsRangeError);
    expect(() => v[1] = [1], throwsArgumentError);
  });
  test('glTF over old 150k triangle cap imports and evaluates intact', () {
    const triangles = 150001;
    final f = GltfFixture();
    final pos = f.add(
      [
        for (var i = 0; i < triangles; i++) ...[
          0.0,
          0.0,
          0.0,
          1.0,
          0.0,
          0.0,
          0.0,
          1.0,
          0.0,
        ],
      ],
      'VEC3',
      3,
    );
    final watch = Stopwatch()..start();
    final asset = readFixture(
      f.document({
        'meshes': [
          {
            'primitives': [
              {
                'attributes': {'POSITION': pos},
              },
            ],
          },
        ],
        'nodes': [
          {'mesh': 0},
        ],
        'scenes': [
          {
            'nodes': [0],
          },
        ],
        'scene': 0,
      }),
    );
    expect(asset.triangleCount, triangles);
    expect(
      (asset.primitives.first as Map)['positions'],
      isA<PackedModelVectors>(),
    );
    final frame = asset.evaluate(Duration.zero, const ModelMotion3D());
    expect(frame.mesh.faces.length, triangles);
    expect(frame.mesh.verts.length, triangles * 3);
    expect(frame.mesh.verts, isA<PackedModelVectors>());
    // Diagnostic, not a device performance guarantee.
    // ignore: avoid_print
    print(
      '150001 triangles import + evaluate: ${watch.elapsedMilliseconds} ms',
    );
  }, timeout: const Timeout(Duration(minutes: 2)));
  test('native curved solids have finite unit normals and reuse mesh', () {
    for (final kind in [
      Element3DKind.sphere,
      Element3DKind.cylinder,
      Element3DKind.capsule,
      Element3DKind.torus,
    ]) {
      final mesh = element3DMesh(kind);
      expect(identical(mesh, element3DMesh(kind)), isTrue);
      expect(mesh.normals!.length, mesh.verts.length);
      for (final n in mesh.normals!) {
        expect(
          math.sqrt(n.fold<double>(0, (sum, v) => sum + v * v)),
          closeTo(1, 1e-6),
        );
      }
    }
  });
  test('environment bake preserves HDR values and spatial detail', () {
    final pixels = environmentRadiance(EnvironmentKind.estudio, width: 128);
    expect(pixels.every((v) => v.isFinite), isTrue);
    expect(pixels.reduce(math.max), greaterThan(1));
    expect(pixels.toSet().length, greaterThan(100));
  });
  test(
    'urban HDR decodes in compatibility renderer with detailed radiance',
    () async {
      final pano = preparePanorama(
        path: File('assets/environments/urban_street_04_1k.hdr').absolute.path,
      );
      final cache = PanoramaCache.instance;
      expect(await cache.prepare(pano), isTrue);
      final sample = cache.samplerFor(pano)!;
      final values = [
        for (var i = 0; i < 48; i++)
          sample(Vec3(math.sin(i), math.cos(i * .7), math.cos(i)), 0),
      ];
      expect(
        values.every((v) => v.r.isFinite && v.g.isFinite && v.b.isFinite),
        isTrue,
      );
      expect(values.toSet().length, greaterThan(30));
    },
  );
  test('new particles start as a quiet fine white 3D field', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    c.read(editorControllerProvider.notifier).addParticulasLayer(Duration.zero);
    final layer =
        c.read(editorControllerProvider).layers.first as ParticulasLayer;
    expect(layer.is3D, isTrue);
    // A NUVEM PADRAO CONTINUA SENDO O CAMPO FINO E CLARO: pontos de
    // tamanho 1, sem brilho, cobrindo a composicao.
    final q = layer.parametros;
    expect(q.tamanho, 1);
    expect(q.brilho, 0);
    expect(q.forma, FormaDaParticula.esfera);
    expect(q.maximo, 1200);
    expect(q.velocidade, 0);
  });
}
