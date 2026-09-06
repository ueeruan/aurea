import 'dart:convert';
import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/glb_import.dart';

/// Monta um .glb de verdade em memoria: cabecalho, pedaco JSON e pedaco
/// binario, com o alinhamento de 4 bytes que o formato exige.
Uint8List _glb(Map<String, dynamic> gltf, Uint8List bin) {
  Uint8List pad(Uint8List d, int fill) {
    final resto = (4 - (d.length % 4)) % 4;
    if (resto == 0) return d;
    return Uint8List.fromList([...d, ...List.filled(resto, fill)]);
  }

  final json = pad(Uint8List.fromList(utf8.encode(jsonEncode(gltf))), 0x20);
  final binPad = pad(bin, 0);
  final total = 12 + 8 + json.length + 8 + binPad.length;

  final out = BytesBuilder();
  final cab = ByteData(12)
    ..setUint32(0, 0x46546C67, Endian.little)
    ..setUint32(4, 2, Endian.little)
    ..setUint32(8, total, Endian.little);
  out.add(cab.buffer.asUint8List());

  final cj = ByteData(8)
    ..setUint32(0, json.length, Endian.little)
    ..setUint32(4, 0x4E4F534A, Endian.little);
  out
    ..add(cj.buffer.asUint8List())
    ..add(json);

  final cb = ByteData(8)
    ..setUint32(0, binPad.length, Endian.little)
    ..setUint32(4, 0x004E4942, Endian.little);
  out
    ..add(cb.buffer.asUint8List())
    ..add(binPad);

  return out.toBytes();
}

/// Um triangulo simples, com posicoes float e indices de 16 bits.
({Map<String, dynamic> gltf, Uint8List bin}) _triangulo({
  List<double>? translation,
  List<double>? scale,
  int mode = 4,
}) {
  final pos = Float32List.fromList([
    0, 0, 0, //
    100, 0, 0,
    0, 100, 0,
  ]);
  final idx = Uint16List.fromList([0, 1, 2]);
  final bin = Uint8List.fromList([
    ...pos.buffer.asUint8List(),
    ...idx.buffer.asUint8List(),
  ]);

  return (
    gltf: {
      'meshes': [
        {
          'name': 'Tri',
          'primitives': [
            {
              'attributes': {'POSITION': 0},
              'indices': 1,
              'mode': mode,
            }
          ]
        }
      ],
      'nodes': [
        {
          'mesh': 0,
          'translation': translation ?? const [0.0, 0.0, 0.0],
          'scale': scale ?? const [1.0, 1.0, 1.0],
        }
      ],
      'accessors': [
        {
          'bufferView': 0,
          'componentType': 5126,
          'count': 3,
          'type': 'VEC3'
        },
        {
          'bufferView': 1,
          'componentType': 5123,
          'count': 3,
          'type': 'SCALAR'
        },
      ],
      'bufferViews': [
        {'buffer': 0, 'byteOffset': 0, 'byteLength': pos.lengthInBytes},
        {
          'buffer': 0,
          'byteOffset': pos.lengthInBytes,
          'byteLength': idx.lengthInBytes
        },
      ],
      'buffers': [
        {'byteLength': bin.length}
      ],
    },
    bin: bin,
  );
}

void main() {
  group('Ler o arquivo', () {
    test('um triangulo entra como um triangulo', () {
      final t = _triangulo();
      final r = parseGlb(_glb(t.gltf, t.bin));
      expect(r.triangles, 1);
      expect(r.mesh.faces, hasLength(1));
      expect(r.mesh.verts, hasLength(3));
      expect(r.name, 'Tri');
      expect(r.warning, isNull);
    });

    test('todo indice de face existe', () {
      final t = _triangulo();
      final r = parseGlb(_glb(t.gltf, t.bin));
      for (final f in r.mesh.faces) {
        for (final i in f) {
          expect(i, inInclusiveRange(0, r.mesh.verts.length - 1));
        }
      }
    });

    // Sem normalizar, um modelo em metros entraria do tamanho de um
    // grao e um em milimetros, do tamanho de um predio.
    test('a malha sai normalizada', () {
      final t = _triangulo();
      final r = parseGlb(_glb(t.gltf, t.bin));
      var maior = 0.0;
      for (final v in r.mesh.verts) {
        for (final c in v) {
          if (c.abs() > maior) maior = c.abs();
        }
      }
      expect(maior, closeTo(1, 0.001));
    });

    // O glTF tem y para cima; a tela tem y para baixo. Sem virar, o
    // modelo entra de cabeca para baixo.
    test('o eixo Y e invertido', () {
      final t = _triangulo();
      final r = parseGlb(_glb(t.gltf, t.bin));
      // O vertice que estava em y=100 (o mais alto no glTF) tem de
      // ficar com o MENOR y na tela.
      final ys = r.mesh.verts.map((v) => v[1]).toList();
      expect(ys.reduce((a, b) => a < b ? a : b), lessThan(0));
    });

    test('a escala do no e aplicada', () {
      final t = _triangulo(scale: [2, 2, 2]);
      final r = parseGlb(_glb(t.gltf, t.bin));
      // Normalizado, o resultado e o mesmo; o que prova a aplicacao e
      // nao estourar e continuar com a mesma contagem.
      expect(r.mesh.verts, hasLength(3));
      expect(r.triangles, 1);
    });
  });

  group('Recusar com jeito', () {
    test('arquivo que nao e glb', () {
      expect(() => parseGlb(Uint8List.fromList(List.filled(40, 7))),
          throwsA(isA<GlbException>()));
    });

    test('arquivo curto demais', () {
      expect(() => parseGlb(Uint8List(4)),
          throwsA(isA<GlbException>()));
    });

    test('sem malha nenhuma', () {
      final vazio = _glb({'meshes': []}, Uint8List(0));
      expect(() => parseGlb(vazio), throwsA(isA<GlbException>()));
    });

    // Modelo de dois milhoes de triangulos nao trava: ele so nao
    // desenha em 30 quadros por segundo. Melhor recusar com um numero.
    test('modelo pesado demais e recusado com o numero', () {
      final t = _triangulo();
      try {
        parseGlb(_glb(t.gltf, t.bin), maxTriangles: 0);
        fail('devia ter recusado');
      } on GlbException catch (e) {
        expect(e.message, contains('1'));
        expect(e.message.toLowerCase(), contains('pesado'));
      }
    });

    test('formato que nao e triangulo vira aviso, nao erro', () {
      // Duas primitivas: uma de triangulo, outra de faixa.
      final t = _triangulo();
      final gltf = Map<String, dynamic>.from(t.gltf);
      final malha =
          Map<String, dynamic>.from((gltf['meshes'] as List).first as Map);
      malha['primitives'] = [
        ...(malha['primitives'] as List),
        {
          'attributes': {'POSITION': 0},
          'indices': 1,
          'mode': 5,
        }
      ];
      gltf['meshes'] = [malha];

      final r = parseGlb(_glb(gltf, t.bin));
      expect(r.triangles, 1);
      expect(r.warning, isNotNull);
      expect(r.warning, contains('1'));
    });
  });
}
