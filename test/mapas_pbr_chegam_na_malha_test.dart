// OS MAPAS DO PBR CHEGAM NA MALHA QUE VAI PARA A PLACA?
//
// Este arquivo cobre a METADE DART do caminho — do GLB ate a `MalhaCrua3D`
// que a porta do motor recebe. Ele nao desenha nada: e a peneira que separa
// "o mapa nao foi lido" de "o mapa foi lido e a placa nao o usou", e roda no
// computador em segundos, sem emulador.
import 'dart:convert';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/motor3d_nativo.dart';
import 'package:aurea/src/features/editor/application/texture_cache.dart';
import 'package:aurea/src/features/editor/application/fonte_de_malha.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/model_import3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;

Uint8List _png(int r, int g, int b) {
  final im = img.Image(width: 4, height: 4, numChannels: 4);
  for (var y = 0; y < 4; y++) {
    for (var x = 0; x < 4; x++) {
      im.setPixelRgba(x, y, r, g, b, 255);
    }
  }
  return Uint8List.fromList(img.encodePng(im));
}

Uint8List _glb(Map<String, dynamic> gltf, Uint8List bin) {
  Uint8List encher(Uint8List d, int fill) {
    final resto = (4 - (d.length % 4)) % 4;
    if (resto == 0) return d;
    return Uint8List.fromList([...d, ...List.filled(resto, fill)]);
  }

  final json = encher(Uint8List.fromList(utf8.encode(jsonEncode(gltf))), 0x20);
  final binPad = encher(bin, 0);
  final total = 12 + 8 + json.length + 8 + binPad.length;
  final out = BytesBuilder()
    ..add(
      (ByteData(12)
            ..setUint32(0, 0x46546C67, Endian.little)
            ..setUint32(4, 2, Endian.little)
            ..setUint32(8, total, Endian.little))
          .buffer
          .asUint8List(),
    )
    ..add(
      (ByteData(8)
            ..setUint32(0, json.length, Endian.little)
            ..setUint32(4, 0x4E4F534A, Endian.little))
          .buffer
          .asUint8List(),
    )
    ..add(json)
    ..add(
      (ByteData(8)
            ..setUint32(0, binPad.length, Endian.little)
            ..setUint32(4, 0x004E4942, Endian.little))
          .buffer
          .asUint8List(),
    )
    ..add(binPad);
  return out.toBytes();
}

/// UMA PLACA com os cinco mapas ligados.
Uint8List _placaComTodosOsMapas() {
  final pos = Float32List.fromList([
    -1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0,
  ]);
  final nor = Float32List.fromList([0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1]);
  final uv = Float32List.fromList([0, 1, 1, 1, 1, 0, 0, 0]);
  final idx = Uint16List.fromList([0, 1, 2, 0, 2, 3]);
  final imagens = [
    _png(255, 0, 0), // cor
    _png(128, 128, 255), // normal
    _png(0, 60, 240), // metal-rugosidade
    _png(255, 255, 0), // emissiva
    _png(200, 200, 200), // oclusao
  ];

  final bin = BytesBuilder();
  final views = <Map<String, dynamic>>[];
  int vista(List<int> bytes) {
    while (bin.length % 4 != 0) {
      bin.addByte(0);
    }
    final inicio = bin.length;
    bin.add(bytes);
    views.add({
      'buffer': 0,
      'byteOffset': inicio,
      'byteLength': bytes.length,
    });
    return views.length - 1;
  }

  final vPos = vista(pos.buffer.asUint8List());
  final vNor = vista(nor.buffer.asUint8List());
  final vUv = vista(uv.buffer.asUint8List());
  final vIdx = vista(idx.buffer.asUint8List());
  final vImg = [for (final png in imagens) vista(png)];
  final bytes = bin.toBytes();

  return _glb({
    'asset': {'version': '2.0'},
    'scene': 0,
    'scenes': [
      {'nodes': [0]},
    ],
    'nodes': [
      {'name': 'Placa', 'mesh': 0},
    ],
    'meshes': [
      {
        'name': 'Placa',
        'primitives': [
          {
            'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2},
            'indices': 3,
            'material': 0,
          },
        ],
      },
    ],
    'materials': [
      {
        'name': 'Completo',
        'pbrMetallicRoughness': {
          'baseColorFactor': [1, 1, 1, 1],
          'baseColorTexture': {'index': 0},
          'metallicFactor': 1,
          'roughnessFactor': 1,
          'metallicRoughnessTexture': {'index': 2},
        },
        'normalTexture': {'index': 1},
        'emissiveFactor': [1, 0.3, 0],
        'emissiveTexture': {'index': 3},
        'occlusionTexture': {'index': 4, 'strength': 0.75},
      },
    ],
    'images': [
      for (final v in vImg) {'bufferView': v, 'mimeType': 'image/png'},
    ],
    'textures': [
      for (var i = 0; i < vImg.length; i++) {'source': i},
    ],
    'buffers': [
      {'byteLength': bytes.length},
    ],
    'bufferViews': views,
    'accessors': [
      {
        'bufferView': vPos,
        'componentType': 5126,
        'count': 4,
        'type': 'VEC3',
        'min': [-1, -1, 0],
        'max': [1, 1, 0],
      },
      {'bufferView': vNor, 'componentType': 5126, 'count': 4, 'type': 'VEC3'},
      {'bufferView': vUv, 'componentType': 5126, 'count': 4, 'type': 'VEC2'},
      {'bufferView': vIdx, 'componentType': 5123, 'count': 6, 'type': 'SCALAR'},
    ],
  }, bytes);
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('o analisador guarda os cinco mapas e a cor do emissivo', () {
    final asset = importGltf3D(_placaComTodosOsMapas());
    final m = (asset.data['materials'] as List).first as Map;
    // ignore: avoid_print
    print('CHAVES DO MATERIAL: ${m.keys.toList()}');
    expect(m['image'], isNotNull, reason: 'cor');
    expect(m['normalImage'], isNotNull, reason: 'relevo');
    expect(m['metalRoughImage'], isNotNull, reason: 'metal/rugosidade');
    expect(m['emissiveImage'], isNotNull, reason: 'brilho proprio');
    expect(m['occlusionImage'], isNotNull, reason: 'oclusao');
    expect(m['emissiveColor'], [1, 0.3, 0]);
    expect(m['occlusionStrength'], closeTo(0.75, 1e-9));
    expect(asset.warnings, isEmpty, reason: 'nao pode sobrar aviso: $m');
  });

  test('o Material3D da superficie carrega os caminhos', () {
    final asset = importGltf3D(_placaComTodosOsMapas());
    final quadro = asset.evaluate(Duration.zero, const ModelMotion3D());
    final material = quadro.materials.first;
    // ignore: avoid_print
    print(
      'MATERIAL cor=${material.imagePath != null} '
      'normal=${material.normalPath != null} '
      'mr=${material.metalRoughPath != null} '
      'emi=${material.emissivePath != null} '
      'ocl=${material.occlusionPath != null} '
      'corEmissiva=${material.emissiveColor}',
    );
    expect(material.imagePath, isNotNull);
    expect(material.normalPath, isNotNull);
    expect(material.metalRoughPath, isNotNull);
    expect(material.emissivePath, isNotNull);
    expect(material.occlusionPath, isNotNull);
    expect(material.emissiveColor, isNotNull);
  });

  test('a malha crua sai com os cinco mapas quando o cache ja os tem', () async {
    final asset = importGltf3D(_placaComTodosOsMapas());
    final quadro = asset.evaluate(Duration.zero, const ModelMotion3D());
    final material = quadro.materials.first;

    // O CACHE E ABASTECIDO A MAO: `ui.instantiateImageCodec` nao existe no
    // teste de host sem binding grafico, e o que se quer medir aqui e o
    // TRANSPORTE — do cache ate a malha —, e nao a decodificacao.
    for (final caminho in [
      material.imagePath!,
      material.normalPath!,
      material.metalRoughPath!,
      material.emissivePath!,
      material.occlusionPath!,
    ]) {
      TextureCache.instance.putRgba(
        caminho,
        Uint8List(4 * 4 * 4)..fillRange(0, 4 * 4 * 4, 200),
        4,
        4,
      );
    }

    final no = SceneNode(name: asset.name, modelAsset: asset, size: 120);
    final malhas = malhasCruas3DDe(
      CacheDeMalhas().doNo(
        no,
        Duration.zero,
        lodDaReceita: (n) => null,
        assinaturaDoMaterial: assinaturaDoMaterial3D,
      )!,
    );
    expect(malhas, isNotEmpty, reason: 'nao saiu malha nenhuma');
    final malha = malhas.first;
    // ignore: avoid_print
    print(
      'MALHA CRUA cor=${malha.texturaCor != null} '
      'normal=${malha.texturaNormal != null} '
      'mr=${malha.texturaMetalicoRugosidade != null} '
      'emi=${malha.texturaEmissiva != null} '
      'ocl=${malha.texturaOclusao != null} '
      'oclusaoForca=${malha.forcaDaOclusao} '
      'emissivo=(${malha.emissivo.r},${malha.emissivo.g},${malha.emissivo.b}) '
      'uvs=${malha.uvs != null}',
    );
    expect(malha.uvs, isNotNull, reason: 'sem UV nenhum mapa serve');
    expect(malha.texturaCor, isNotNull, reason: 'cor');
    expect(malha.texturaNormal, isNotNull, reason: 'relevo');
    expect(malha.texturaMetalicoRugosidade, isNotNull, reason: 'metal/rug');
    expect(malha.texturaEmissiva, isNotNull, reason: 'brilho proprio');
    expect(malha.texturaOclusao, isNotNull, reason: 'oclusao');
    expect(malha.forcaDaOclusao, closeTo(0.75, 1e-6));
    // A COR DO EMISSIVO E A DO ARQUIVO (laranja), e nao a cor base (branca).
    expect(malha.emissivo.r, greaterThan(malha.emissivo.b + 40));
  });

  test('a assinatura muda quando o mapa chega ao cache', () {
    const antes = Material3D(imagePath: 'x:um-mapa-que-nao-existe');
    final a = assinaturaDoMaterial3D(antes);
    TextureCache.instance.putRgba(
      'x:um-mapa-que-nao-existe',
      Uint8List(4),
      1,
      1,
    );
    final b = assinaturaDoMaterial3D(antes);
    // ignore: avoid_print
    print('ASSINATURA antes=$a depois=$b');
    // SEM ISTO, a malha que nasceu sem textura ficaria na placa para sempre.
    expect(a, isNot(b));
  });

  tearDownAll(() {
    // Uma imagem de verdade nunca entrou; limpar so zera os mapas.
    TextureCache.instance.clear();
    // ignore: unnecessary_statements
    ui.Image;
  });
}
