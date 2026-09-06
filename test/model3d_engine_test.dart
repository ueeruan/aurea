import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui';

import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/model_import3d.dart';
import 'package:aurea/src/features/editor/domain/obj_import3d.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';

class GltfFixture {
  final chunks = BytesBuilder();
  final views = <Map<String, dynamic>>[], accessors = <Map<String, dynamic>>[];
  int add(List<double> data, String type, int components) {
    final bytes = ByteData(data.length * 4);
    for (var i = 0; i < data.length; i++) {
      bytes.setFloat32(i * 4, data[i], Endian.little);
    }
    final view = views.length;
    views.add({
      'buffer': 0,
      'byteOffset': chunks.length,
      'byteLength': bytes.lengthInBytes,
    });
    chunks.add(bytes.buffer.asUint8List());
    accessors.add({
      'bufferView': view,
      'componentType': 5126,
      'count': data.length ~/ components,
      'type': type,
    });
    return accessors.length - 1;
  }

  Map<String, dynamic> document(Map<String, dynamic> body) => {
    'asset': {'version': '2.0'},
    'buffers': [
      {
        'byteLength': chunks.length,
        'uri':
            'data:application/octet-stream;base64,${base64Encode(chunks.toBytes())}',
      },
    ],
    'bufferViews': views,
    'accessors': accessors,
    ...body,
  };
}

Map<String, dynamic> rigDocument() {
  final f = GltfFixture();
  final pos = f.add([-.5, 0, 0, .5, 0, 0, -.5, 2, 0, .5, 2, 0], 'VEC3', 3);
  final normals = f.add(
    [
      for (var i = 0; i < 4; i++) ...[0.0, 0.0, 1.0],
    ],
    'VEC3',
    3,
  );
  final uv = f.add([0, 1, 1, 1, 0, 0, 1, 0], 'VEC2', 2);
  final joints = f.add(
    [0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0],
    'VEC4',
    4,
  );
  final weights = f.add(
    [
      for (var i = 0; i < 4; i++) ...[1.0, 0.0, 0.0, 0.0],
    ],
    'VEC4',
    4,
  );
  final inverse = f.add(
    [
      1,
      0,
      0,
      0,
      0,
      1,
      0,
      0,
      0,
      0,
      1,
      0,
      0,
      0,
      0,
      1,
      1,
      0,
      0,
      0,
      0,
      1,
      0,
      0,
      0,
      0,
      1,
      0,
      0,
      -1,
      0,
      1,
    ],
    'MAT4',
    16,
  );
  final times = f.add([0, 1], 'SCALAR', 1);
  final rotation = f.add(
    [0, 0, 0, 1, 0, 0, math.sqrt(.5), math.sqrt(.5)],
    'VEC4',
    4,
  );
  return f.document({
    'scene': 0,
    'scenes': [
      {
        'name': 'Rig de teste',
        'nodes': [0, 1],
      },
    ],
    'nodes': [
      {'mesh': 0, 'skin': 0, 'name': 'Malha'},
      {
        'children': [2],
        'name': 'Base',
      },
      {
        'translation': [0, 1, 0],
        'name': 'Ponta',
      },
    ],
    'meshes': [
      {
        'primitives': [
          {
            'attributes': {
              'POSITION': pos,
              'NORMAL': normals,
              'TEXCOORD_0': uv,
              'JOINTS_0': joints,
              'WEIGHTS_0': weights,
            },
            'mode': 5,
            'material': 0,
          },
        ],
      },
    ],
    'skins': [
      {
        'joints': [1, 2],
        'inverseBindMatrices': inverse,
      },
    ],
    'materials': [
      {
        'pbrMetallicRoughness': {
          'baseColorFactor': [1, .1, .2, 1],
          'metallicFactor': .8,
          'roughnessFactor': .2,
        },
        'doubleSided': true,
      },
    ],
    'animations': [
      {
        'name': 'Dobrar',
        'samplers': [
          {'input': times, 'output': rotation},
        ],
        'channels': [
          {
            'sampler': 0,
            'target': {'node': 2, 'path': 'rotation'},
          },
        ],
      },
    ],
  });
}

ModelAsset3D readFixture(Map<String, dynamic> doc) => importGltf3D(
  Uint8List.fromList(utf8.encode(jsonEncode(doc))),
  binary: false,
);

void main() {
  test(
    'skin deforma pelos ossos e inverse bind; seek reverso determinista',
    () {
      final asset = readFixture(rigDocument());
      expect(asset.joints, {1, 2});
      expect(asset.triangleCount, 2);
      expect(asset.clipNames, ['Dobrar']);
      const motion = ModelMotion3D(loop: false);
      final bind = asset.evaluate(Duration.zero, motion);
      expect(bind.mesh.verts[3][0], closeTo(.5, 1e-6));
      expect(bind.mesh.verts[3][1], closeTo(1, 1e-6));
      final end = asset.evaluate(const Duration(seconds: 1), motion);
      expect(end.mesh.verts[3][0], closeTo(-1, 1e-6));
      expect(end.mesh.verts[3][1], closeTo(.5, 1e-6));
      expect(asset.evaluate(Duration.zero, motion).mesh.verts, bind.mesh.verts);
      expect(bind.materials.first.metallic, .8);
      expect(bind.uvs[3], const Offset(1, 0));
    },
  );
  test('loop, velocidade e pose aditiva sao efetivos', () {
    final asset = readFixture(rigDocument());
    final base = asset.evaluate(Duration.zero, const ModelMotion3D());
    expect(
      asset
          .evaluate(const Duration(seconds: 1), const ModelMotion3D())
          .mesh
          .verts,
      base.mesh.verts,
    );
    final animated = asset.evaluate(
      const Duration(milliseconds: 500),
      const ModelMotion3D(speed: 2, loop: false),
    );
    expect(animated.mesh.verts[3][0], closeTo(-1, 1e-6));
    final posed = asset.evaluate(
      Duration.zero,
      const ModelMotion3D(
        clip: -1,
        keys: [
          ModelPoseKey3D(0, {
            2: ModelPose3D(translation: [1, 0, 0]),
          }),
        ],
      ),
    );
    expect(posed.mesh.verts[3][0], closeTo(1.5, 1e-6));
    expect(posed.mesh.verts[0], base.mesh.verts[0]);
  });
  test('projeto preserva asset, rig, clipe, material e poses ao reabrir', () {
    final asset = readFixture(rigDocument());
    final motion = const ModelMotion3D(loop: false).withPose(.5, {
      2: const ModelPose3D(translation: [.2, 0, 0]),
    });
    final node = SceneNode(modelAsset: asset, modelMotion: motion);
    final project = VideoProject.empty('Rig').copyWith(
      layers: [
        Scene3DLayer(
          name: 'Cena',
          startTime: Duration.zero,
          scene: Scene3D(nodes: [node]),
          duration: const Duration(seconds: 2),
        ),
      ],
    );
    final restored = projectFromJson(
      jsonDecode(jsonEncode(projectToJson(project))),
    );
    final layer = restored.layers.single as Scene3DLayer;
    expect(layer.moduleTimesUs, contains(500000));
    final r = layer.scene.nodes.single;
    expect(
      r.modelAsset!
          .evaluate(const Duration(milliseconds: 500), r.modelMotion)
          .mesh
          .verts,
      asset.evaluate(const Duration(milliseconds: 500), motion).mesh.verts,
    );
    expect(node.duplicate().modelMotion, same(motion));
  });
  test('renderizador usa malha animada, UV e normais suaves', () {
    final asset = readFixture(rigDocument());
    final node = SceneNode(
      modelAsset: asset,
      modelMotion: const ModelMotion3D(loop: false),
    );
    final scene = Scene3D(nodes: [node]);
    const cam = RenderCamera(position: Vec3(0, 0, 500), target: Vec3.zero);
    final a = renderScene(scene, cam, const Size(400, 400), Duration.zero);
    final b = renderScene(
      scene,
      cam,
      const Size(400, 400),
      const Duration(seconds: 1),
    );
    expect(a.triangles, 2);
    expect(b.triangles, 2);
    expect(a.opaque.first.colorA, isNotNull);
    expect(
      a.opaque.map((t) => [t.a, t.b, t.c]).toList(),
      isNot(b.opaque.map((t) => [t.a, t.b, t.c]).toList()),
    );
  });
  test('scenes ativas, matriz e pais nao duplicam nem empilham pecas', () {
    final d = rigDocument();
    d['skins'] = [];
    d['animations'] = [];
    d['nodes'] = [
      {
        'children': [1],
        'translation': [5, 0, 0],
      },
      {
        'mesh': 0,
        'translation': [0, 2, 0],
      },
      {
        'mesh': 0,
        'translation': [100, 100, 0],
      },
    ];
    d['scenes'] = [
      {
        'nodes': [0],
      },
    ];
    final asset = readFixture(d);
    expect(asset.primitives, hasLength(1));
    final f = asset.evaluate(Duration.zero, const ModelMotion3D());
    expect(f.mesh.verts[0], [-.5, -1.0, 0.0]);
  });
  test('rejeita ciclo, buffers truncados, extensoes obrigatorias e tempos repetidos', () {
    final cycle = rigDocument();
    cycle['nodes'][2]['children'] = [1];
    expect(() => readFixture(cycle), throwsA(isA<ModelImportException>()));
    final truncated = rigDocument();
    truncated['bufferViews'][0]['byteLength'] = 4;
    expect(() => readFixture(truncated), throwsA(isA<ModelImportException>()));
    final compressed = rigDocument();
    compressed['extensionsRequired'] = ['KHR_draco_mesh_compression'];
    expect(() => readFixture(compressed), throwsA(isA<ModelImportException>()));
  });
  test(
    'CUBICSPLINE escala tangentes pelo intervalo, SLERP segue arco curto',
    () {
      final c = {
        'times': [0, 2],
        'values': [
          [0, 0, 0],
          [0, 0, 0],
          [2, 0, 0],
          [0, 0, 0],
          [2, 0, 0],
          [0, 0, 0],
        ],
        'interpolation': 'CUBICSPLINE',
        'path': 'translation',
      };
      expect(sampleModelChannel(c, 1)[0], closeTo(1.5, 1e-9));
      expect(modelSlerp([0, 0, 0, 1], [0, 0, 0, -1], .5), [0, 0, 0, 1]);
      expect(
        sampleModelChannel({
          ...c,
          'interpolation': 'STEP',
          'values': [
            [0, 0, 0],
            [2, 0, 0],
          ],
        }, 1),
        [0, 0, 0],
      );
    },
  );
  test('OBJ preserva grupos, indices negativos, UVs e poligono concavo', () {
    final model = importObj3D(
      'v 0 0 0\nv 2 0 0\nv 2 2 0\nv 1 1 0\nv 0 2 0\n'
      'o Concavo\nf -5 -4 -3 -2 -1\n',
    );
    expect(model.triangleCount, 3);
    expect(model.nodes.single['name'], 'Concavo');
    final mesh = model.evaluate(Duration.zero, const ModelMotion3D()).mesh;
    var area = 0.0;
    for (final f in mesh.faces) {
      final a = mesh.verts[f[0]], b = mesh.verts[f[1]], c = mesh.verts[f[2]];
      area +=
          ((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]))
              .abs() /
          2;
    }
    expect(area, closeTo(3, 1e-9));
  });
  test(
    'OBJ/MTL sem recurso nao resulta em importacao silenciosamente quebrada',
    () {
      expect(
        () => importObj3D(
          'mtllib material.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3',
        ),
        throwsA(isA<ModelImportException>()),
      );
      expect(
        () => importObj3D('v 0 0 0\nf 1 2 3'),
        throwsA(isA<ModelImportException>()),
      );
    },
  );
}
