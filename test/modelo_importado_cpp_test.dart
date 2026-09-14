// "IMPORTACAO DE MODELS 3D... VEJA O QUE ESTA FALTANDO EM C++, ACHE NOVAS
// COISAS PRA C++" (dono, 14/09/2026).
//
// O meshoptimizer inteiro entrou no pacote nativo. Aqui ficam presos:
//   * o glTF comprimido com meshopt abre e sai IGUAL ao descomprimido;
//   * a solda junta o vertice-por-canto do FBX sem juntar o que difere
//     em osso ou UV;
//   * os niveis de detalhe da importacao substituem o "uma face a cada N"
//     do pintor de CPU, sem buraco;
//   * o OBJ sem MTL, com cor por vertice ou map_Kd com opcoes abre.
import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:aurea/src/features/editor/domain/malha_importada.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/model_import3d.dart';
import 'package:aurea/src/features/editor/domain/obj_import3d.dart';
import 'package:aurea_meshopt/aurea_meshopt.dart';
import 'package:flutter_test/flutter_test.dart';

/// Grade com relevo: [lado] x [lado] vertices.
({List<double> posicoes, List<int> indices}) _grade(int lado) {
  final p = <double>[];
  for (var y = 0; y < lado; y++) {
    for (var x = 0; x < lado; x++) {
      p.addAll([
        x / (lado - 1),
        y / (lado - 1),
        .08 * math.sin(x / 3) * math.cos(y / 4),
      ]);
    }
  }
  final idx = <int>[];
  for (var y = 0; y < lado - 1; y++) {
    for (var x = 0; x < lado - 1; x++) {
      final a = y * lado + x;
      idx.addAll([a, a + 1, a + lado, a + 1, a + lado + 1, a + lado]);
    }
  }
  return (posicoes: p, indices: idx);
}

Uint8List _glb(Map<String, Object?> doc, Uint8List bin) {
  var json = Uint8List.fromList(utf8.encode(jsonEncode(doc)));
  if (json.length % 4 != 0) {
    json = Uint8List.fromList([...json, ...List.filled(4 - json.length % 4, 0x20)]);
  }
  final padded = Uint8List((bin.length + 3) & ~3)..setAll(0, bin);
  final total = 12 + 8 + json.length + 8 + padded.length;
  final out = BytesBuilder(copy: false)
    ..add(
      (ByteData(12)
            ..setUint32(0, 0x46546c67, Endian.little)
            ..setUint32(4, 2, Endian.little)
            ..setUint32(8, total, Endian.little))
          .buffer
          .asUint8List(),
    )
    ..add(
      (ByteData(8)
            ..setUint32(0, json.length, Endian.little)
            ..setUint32(4, 0x4e4f534a, Endian.little))
          .buffer
          .asUint8List(),
    )
    ..add(json)
    ..add(
      (ByteData(8)
            ..setUint32(0, padded.length, Endian.little)
            ..setUint32(4, 0x004e4942, Endian.little))
          .buffer
          .asUint8List(),
    )
    ..add(padded);
  return out.toBytes();
}

Set<String> _triangulos(List posicoes, List indices) {
  String p(int i) {
    final v = posicoes[i] as List;
    return v.map((c) => (c as num).toStringAsFixed(4)).join(':');
  }

  final out = <String>{};
  for (var i = 0; i < indices.length; i += 3) {
    final t = [p(indices[i]), p(indices[i + 1]), p(indices[i + 2])];
    // Mesmo triangulo, mesmo enrolamento, qualquer rotacao dos cantos.
    final rot = [
      t,
      [t[1], t[2], t[0]],
      [t[2], t[0], t[1]],
    ]..sort((a, b) => a.join().compareTo(b.join()));
    out.add(rot.first.join('|'));
  }
  return out;
}

Map<String, dynamic> _modelo(
  List<List<double>> posicoes,
  List<int> indices, {
  Map<String, dynamic> extras = const {},
}) => {
  'version': 1,
  'format': 'teste',
  'name': 'grade',
  'nodes': [
    {'name': 'raiz'},
  ],
  'primitives': [
    {'node': 0, 'positions': posicoes, 'indices': indices, 'material': -1, ...extras},
  ],
  'skins': <Object>[],
  'clips': <Object>[],
  'materials': <Object>[],
  'warnings': <String>[],
};

void main() {
  test('GLB comprimido com meshopt abre igual ao descomprimido', () {
    const lado = 20;
    final g = _grade(lado);
    final posBytes = Float32List.fromList(g.posicoes).buffer.asUint8List();
    final idxBytes = Uint32List.fromList(g.indices).buffer.asUint8List();
    final posComp = encodeMeshoptVertices(posBytes, 12)!;
    final idxComp = encodeMeshoptTriangles(
      Uint32List.fromList(g.indices),
      lado * lado,
    )!;
    final alinhado = (posComp.length + 3) & ~3;
    final bin = Uint8List(alinhado + idxComp.length)
      ..setAll(0, posComp)
      ..setAll(alinhado, idxComp);
    final doc = <String, Object?>{
      'asset': {'version': '2.0'},
      'extensionsUsed': ['EXT_meshopt_compression'],
      'extensionsRequired': ['EXT_meshopt_compression'],
      'buffers': [
        {'byteLength': bin.length},
        {
          'byteLength': posBytes.length + idxBytes.length,
          'extensions': {
            'EXT_meshopt_compression': {'fallback': true},
          },
        },
      ],
      'bufferViews': [
        {
          'buffer': 1,
          'byteOffset': 0,
          'byteLength': posBytes.length,
          'byteStride': 12,
          'extensions': {
            'EXT_meshopt_compression': {
              'buffer': 0,
              'byteOffset': 0,
              'byteLength': posComp.length,
              'byteStride': 12,
              'count': lado * lado,
              'mode': 'ATTRIBUTES',
            },
          },
        },
        {
          'buffer': 1,
          'byteOffset': posBytes.length,
          'byteLength': idxBytes.length,
          'extensions': {
            'EXT_meshopt_compression': {
              'buffer': 0,
              'byteOffset': alinhado,
              'byteLength': idxComp.length,
              'byteStride': 4,
              'count': g.indices.length,
              'mode': 'TRIANGLES',
            },
          },
        },
      ],
      'accessors': [
        {
          'bufferView': 0,
          'componentType': 5126,
          'count': lado * lado,
          'type': 'VEC3',
        },
        {
          'bufferView': 1,
          'componentType': 5125,
          'count': g.indices.length,
          'type': 'SCALAR',
        },
      ],
      'meshes': [
        {
          'primitives': [
            {
              'attributes': {'POSITION': 0},
              'indices': 1,
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
    };
    final asset = importGltf3D(_glb(doc, bin));
    final p = asset.primitives.single as Map;
    final esperado = [
      for (var i = 0; i < lado * lado; i++)
        [g.posicoes[i * 3], g.posicoes[i * 3 + 1], g.posicoes[i * 3 + 2]],
    ];
    expect(
      _triangulos(p['positions'] as List, p['indices'] as List),
      _triangulos(esperado, g.indices),
    );
    expect(asset.warnings.where((w) => w.contains('meshopt')), isEmpty);
  });

  test('primitiva sem POSITION e pulada, o resto do modelo abre', () {
    final pos = Float32List.fromList([0, 0, 0, 1, 0, 0, 0, 1, 0]);
    final bin = pos.buffer.asUint8List();
    final doc = <String, Object?>{
      'asset': {'version': '2.0'},
      'buffers': [
        {'byteLength': bin.length},
      ],
      'bufferViews': [
        {'buffer': 0, 'byteLength': bin.length},
      ],
      'accessors': [
        {'bufferView': 0, 'componentType': 5126, 'count': 3, 'type': 'VEC3'},
      ],
      'meshes': [
        {
          'primitives': [
            {'attributes': <String, Object?>{}},
            {
              'attributes': {'POSITION': 0},
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
    };
    final asset = importGltf3D(_glb(doc, bin));
    expect(asset.primitives, hasLength(1));
    expect(asset.warnings, contains('Primitiva sem POSITION foi ignorada.'));
  });

  test('solda: vertice por canto vira vertice por ponto, sem mexer na forma', () {
    const lado = 12;
    final g = _grade(lado);
    // Como o FBX entrega: um vertice por canto de triangulo.
    final soltos = [
      for (final i in g.indices)
        [g.posicoes[i * 3], g.posicoes[i * 3 + 1], g.posicoes[i * 3 + 2]],
    ];
    final data = _modelo(soltos, [for (var i = 0; i < soltos.length; i++) i]);
    final antes = _triangulos(soltos, [for (var i = 0; i < soltos.length; i++) i]);
    otimizarMalhasImportadas(data);
    final p = (data['primitives'] as List).single as Map;
    expect((p['positions'] as List).length, lado * lado);
    expect(_triangulos(p['positions'] as List, p['indices'] as List), antes);
  });

  test('solda nao junta vertices com ossos diferentes', () {
    final pos = [
      [0.0, 0.0, 0.0],
      [1.0, 0.0, 0.0],
      [0.0, 1.0, 0.0],
      [0.0, 0.0, 0.0],
      [1.0, 0.0, 0.0],
      [0.0, 1.0, 0.0],
    ];
    final data = _modelo(
      pos,
      [0, 1, 2, 3, 4, 5],
      extras: {
        'joints': [
          [0, 0, 0, 0],
          [0, 0, 0, 0],
          [0, 0, 0, 0],
          [1, 0, 0, 0],
          [1, 0, 0, 0],
          [1, 0, 0, 0],
        ],
        'weights': [
          for (var i = 0; i < 6; i++) [1.0, 0.0, 0.0, 0.0],
        ],
      },
    );
    otimizarMalhasImportadas(data);
    final p = (data['primitives'] as List).single as Map;
    expect((p['positions'] as List).length, 6);
    final ossos = p['joints'] as List;
    expect(ossos.map((j) => (j as List).first).toSet(), {0, 1});
    expect((ossos.first as List).first, isA<int>());
  });

  test('niveis de detalhe: o rascunho da CPU fica fechado, nao furado', () {
    const lado = 60;
    final g = _grade(lado);
    final pos = [
      for (var i = 0; i < lado * lado; i++)
        [g.posicoes[i * 3], g.posicoes[i * 3 + 1], g.posicoes[i * 3 + 2]],
    ];
    final data = _modelo(pos, g.indices);
    otimizarMalhasImportadas(data);
    final p = (data['primitives'] as List).single as Map;
    final lods = p['lods'] as List;
    expect(lods, hasLength(2));
    final cheio = (p['indices'] as List).length ~/ 3;
    expect((lods[0] as List).length ~/ 3, lessThan(cheio * .4));
    expect((lods[1] as List).length ~/ 3, lessThan((lods[0] as List).length ~/ 3));
    final n = (p['positions'] as List).length;
    for (final l in lods) {
      expect((l as List).every((i) => (i as int) < n), isTrue);
    }

    final asset = ModelAsset3D(data);
    final quadro = asset.evaluate(Duration.zero, const ModelMotion3D(clip: -1));
    final teto = cheio ~/ 3;
    final rascunho = quadro.rascunho(teto);
    expect(rascunho.mesh.faces.length, lessThanOrEqualTo(teto));

    double area(List<List<double>> v, List<List<int>> faces) {
      var total = 0.0;
      for (final f in faces) {
        final a = v[f[0]], b = v[f[1]], c = v[f[2]];
        final ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        final vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        final cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
        total += math.sqrt(cx * cx + cy * cy + cz * cz) / 2;
      }
      return total;
    }

    final areaCheia = area(quadro.mesh.verts, quadro.mesh.faces);
    final areaRascunho = area(rascunho.mesh.verts, rascunho.mesh.faces);
    expect(areaRascunho, closeTo(areaCheia, areaCheia * .06));
    // O modelo sem niveis (salvo antes deles) continua no passo antigo.
    final antigo = ModelAsset3D(_modelo(pos, g.indices));
    final quadroAntigo = antigo.evaluate(Duration.zero, const ModelMotion3D(clip: -1));
    final passo = quadroAntigo.rascunho(teto);
    expect(passo.mesh.faces.length, lessThanOrEqualTo(teto));
    expect(area(passo.mesh.verts, passo.mesh.faces), lessThan(areaCheia * .5));
  });

  test('OBJ sem MTL, com cor por vertice e map_Kd com opcoes abre', () {
    const obj = '''
mtllib sumiu.mtl
usemtl pele
v 0 0 0 1 0 0
v 1 0 0 0 1 0
v 0 1 0 0 0 1
f 1 2 3
''';
    final asset = importObj3D(obj);
    final p = asset.primitives.single as Map;
    final posicoes = p['positions'] as List;
    // Cor por vertice nao divide a posicao.
    expect((posicoes[1] as List)[0], closeTo(1, 1e-9));
    expect(asset.warnings.any((w) => w.contains('sumiu.mtl')), isTrue);
    expect(asset.warnings.any((w) => w.contains('pele')), isTrue);
    expect(texturaDoMapKd(['-s', '1', '1', '1', 'tex.png']), 'tex.png');
    expect(texturaDoMapKd(['-bm', '2', 'minha', 'textura.jpg']), 'minha textura.jpg');
    expect(texturaDoMapKd(['-clamp', 'on', 'a.png']), 'a.png');
    expect(texturaDoMapKd(['-o', '0.5', 'b.png']), 'b.png');
    expect(texturaDoMapKd(['c.png']), 'c.png');
  });
}
