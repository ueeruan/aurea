import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:aurea/src/features/editor/application/renderer3d/scene_glb.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/model_import3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:flutter_test/flutter_test.dart';

/// A BANCADA DO PIOR CASO — onde o 3D do Aurea realmente quebra.
///
/// A pergunta que originou isto foi "trocamos o Filament?". A resposta
/// honesta só existe depois de saber ONDE a corrente arrebenta, e a
/// corrente tem quatro elos:
///
///   arquivo → IMPORTADOR → formato interno → PONTE GLB → Filament
///
/// O Filament é o último. Tudo o que ele recebe já passou por duas
/// conversões nossas — e é nessas duas que este teste mede.
///
/// Cada caso vira uma linha de tabela em `docs/motor3d-auditoria.md`, com
/// número e não com opinião. O que precisa de aparelho (memória de GPU,
/// FPS, iPhone 13) sai marcado como faltando, e não estimado.

// ============================================================ o escritor

/// Um GLB mínimo montado à mão, para fabricar os casos difíceis.
///
/// Escrever o arquivo em vez de baixar um pronto é o que torna a bancada
/// repetível: o mesmo byte, toda vez, em qualquer máquina.
class _Glb {
  final _bin = BytesBuilder(copy: false);
  final List<Map<String, Object?>> views = [];
  final List<Map<String, Object?>> accessors = [];
  final List<Map<String, Object?>> meshes = [];
  final List<Map<String, Object?>> materials = [];
  final List<Map<String, Object?>> nodes = [];
  final List<Map<String, Object?>> images = [];
  final List<Map<String, Object?>> textures = [];
  final List<Map<String, Object?>> skins = [];
  final List<Map<String, Object?>> animations = [];
  final Set<String> usadas = {};
  final Set<String> exigidas = {};

  int _view(Uint8List bytes) {
    while (_bin.length % 4 != 0) {
      _bin.addByte(0);
    }
    views.add({
      'buffer': 0,
      'byteOffset': _bin.length,
      'byteLength': bytes.length,
    });
    _bin.add(bytes);
    return views.length - 1;
  }

  int floats(List<double> v, String tipo, int comp, {bool bounds = false}) {
    final a = <String, Object?>{
      'bufferView': _view(Float32List.fromList(v).buffer.asUint8List()),
      'componentType': 5126,
      'count': v.length ~/ comp,
      'type': tipo,
    };
    if (bounds) {
      final lo = List.filled(comp, double.infinity);
      final hi = List.filled(comp, -double.infinity);
      for (var i = 0; i < v.length; i++) {
        final j = i % comp;
        if (v[i] < lo[j]) lo[j] = v[i];
        if (v[i] > hi[j]) hi[j] = v[i];
      }
      a['min'] = lo;
      a['max'] = hi;
    }
    accessors.add(a);
    return accessors.length - 1;
  }

  int uints(List<int> v) {
    accessors.add({
      'bufferView': _view(Uint32List.fromList(v).buffer.asUint8List()),
      'componentType': 5125,
      'count': v.length,
      'type': 'SCALAR',
    });
    return accessors.length - 1;
  }

  int ubytes(List<int> v, String tipo, int comp) {
    accessors.add({
      'bufferView': _view(Uint8List.fromList(v)),
      'componentType': 5121,
      'count': v.length ~/ comp,
      'type': tipo,
    });
    return accessors.length - 1;
  }

  int imagemPng(int lado) {
    final png = _png(lado);
    images.add({'bufferView': _view(png), 'mimeType': 'image/png'});
    textures.add({'source': images.length - 1});
    return textures.length - 1;
  }

  Uint8List bytes({int? tamanhoDeclarado, List<int> extra = const []}) {
    final doc = <String, Object?>{
      'asset': {'version': '2.0'},
      'scene': 0,
      'scenes': [
        {
          'nodes': [for (var i = 0; i < nodes.length; i++) i],
        },
      ],
      'nodes': nodes,
      'meshes': meshes,
      'materials': materials,
      'accessors': accessors,
      'bufferViews': views,
      if (images.isNotEmpty) 'images': images,
      if (textures.isNotEmpty) 'textures': textures,
      if (skins.isNotEmpty) 'skins': skins,
      if (animations.isNotEmpty) 'animations': animations,
      if (usadas.isNotEmpty) 'extensionsUsed': usadas.toList(),
      if (exigidas.isNotEmpty) 'extensionsRequired': exigidas.toList(),
      'buffers': [
        {'byteLength': _bin.length},
      ],
    };
    var json = Uint8List.fromList(utf8.encode(jsonEncode(doc)));
    if (json.length % 4 != 0) {
      json = Uint8List.fromList([
        ...json,
        ...List.filled(4 - json.length % 4, 0x20),
      ]);
    }
    while (_bin.length % 4 != 0) {
      _bin.addByte(0);
    }
    final bin = _bin.toBytes();
    final total = 12 + 8 + json.length + 8 + bin.length;
    final out = BytesBuilder(copy: false);
    final cab = ByteData(12)
      ..setUint32(0, 0x46546c67, Endian.little)
      ..setUint32(4, 2, Endian.little)
      ..setUint32(8, tamanhoDeclarado ?? total, Endian.little);
    out.add(cab.buffer.asUint8List());
    final j = ByteData(8)
      ..setUint32(0, json.length, Endian.little)
      ..setUint32(4, 0x4e4f534a, Endian.little);
    out
      ..add(j.buffer.asUint8List())
      ..add(json);
    final b = ByteData(8)
      ..setUint32(0, bin.length, Endian.little)
      ..setUint32(4, 0x004e4942, Endian.little);
    out
      ..add(b.buffer.asUint8List())
      ..add(bin);
    if (extra.isNotEmpty) out.add(extra);
    return out.toBytes();
  }
}

/// Um PNG cinza de verdade, com zlib — o importador decodifica, então um
/// arquivo falso não mediria nada.
Uint8List _png(int lado) {
  // RUIDO, e nao um degrade. Um padrao regular comprime a quase nada e
  // mediria uma textura que nao existe: fotografia e mapa de normal sao
  // praticamente incompressiveis, e e esse o custo que derruba o
  // aparelho.
  final cru = BytesBuilder(copy: false);
  var semente = 0x9e3779b9;
  for (var y = 0; y < lado; y++) {
    cru.addByte(0);
    for (var x = 0; x < lado; x++) {
      semente = (semente * 1664525 + 1013904223) & 0x7fffffff;
      cru.addByte((semente >> 16) & 0xff);
    }
  }
  final idat = Uint8List.fromList(ZLibEncoder().convert(cru.toBytes()));
  Uint8List trecho(String tipo, List<int> dados) {
    final b = BytesBuilder(copy: false);
    final tam = ByteData(4)..setUint32(0, dados.length);
    b
      ..add(tam.buffer.asUint8List())
      ..add(ascii.encode(tipo))
      ..add(dados);
    final crc = _crc32([...ascii.encode(tipo), ...dados]);
    final c = ByteData(4)..setUint32(0, crc);
    b.add(c.buffer.asUint8List());
    return b.toBytes();
  }

  final ihdr = ByteData(13)
    ..setUint32(0, lado)
    ..setUint32(4, lado)
    ..setUint8(8, 8)
    ..setUint8(9, 0);
  return Uint8List.fromList([
    137, 80, 78, 71, 13, 10, 26, 10,
    ...trecho('IHDR', ihdr.buffer.asUint8List()),
    ...trecho('IDAT', idat),
    ...trecho('IEND', const []),
  ]);
}

int _crc32(List<int> dados) {
  var c = 0xffffffff;
  for (final b in dados) {
    c ^= b;
    for (var k = 0; k < 8; k++) {
      c = (c & 1) != 0 ? 0xedb88320 ^ (c >> 1) : c >> 1;
    }
  }
  return c ^ 0xffffffff;
}

// ============================================================== os casos

/// Uma grade de triângulos: o jeito barato de pedir muitos polígonos.
({List<double> pos, List<double> nor, List<double> uv, List<int> idx}) _grade(
  int lado,
) {
  final pos = <double>[], nor = <double>[], uv = <double>[];
  final idx = <int>[];
  for (var y = 0; y <= lado; y++) {
    for (var x = 0; x <= lado; x++) {
      pos.addAll([x / lado - .5, y / lado - .5, 0]);
      nor.addAll([0, 0, 1]);
      uv.addAll([x / lado, y / lado]);
    }
  }
  for (var y = 0; y < lado; y++) {
    for (var x = 0; x < lado; x++) {
      final a = y * (lado + 1) + x;
      idx.addAll([a, a + 1, a + lado + 1, a + 1, a + lado + 2, a + lado + 1]);
    }
  }
  return (pos: pos, nor: nor, uv: uv, idx: idx);
}

_Glb _malha(
  int lado, {
  int materiais = 1,
  int objetos = 1,
  int? textura,
  bool transparente = false,
  bool comEsqueleto = false,
  bool comAnimacao = false,
  bool comMorph = false,
}) {
  final g = _Glb();
  final m = _grade(lado);
  final pos = g.floats(m.pos, 'VEC3', 3, bounds: true);
  final nor = g.floats(m.nor, 'VEC3', 3);
  final uv = g.floats(m.uv, 'VEC2', 2);
  final idx = g.uints(m.idx);
  final vertices = m.pos.length ~/ 3;

  final tex = textura == null ? null : g.imagemPng(textura);
  for (var i = 0; i < materiais; i++) {
    g.materials.add({
      'name': 'mat$i',
      'pbrMetallicRoughness': {
        'baseColorFactor': [i / materiais, .5, .5, transparente ? .4 : 1.0],
        'metallicFactor': .1,
        'roughnessFactor': .8,
        if (tex != null && i == 0) 'baseColorTexture': {'index': tex},
      },
      if (transparente) 'alphaMode': 'BLEND',
    });
  }

  int? morph;
  if (comMorph) {
    morph = g.floats([for (var i = 0; i < vertices * 3; i++) 0.05], 'VEC3', 3);
  }
  int? juntas, pesos;
  if (comEsqueleto) {
    juntas = g.ubytes([
      for (var i = 0; i < vertices; i++) ...[0, 1, 0, 0],
    ], 'VEC4', 4);
    pesos = g.floats([
      for (var i = 0; i < vertices; i++) ...[1.0, 0.0, 0.0, 0.0],
    ], 'VEC4', 4);
  }

  g.meshes.add({
    'primitives': [
      for (var i = 0; i < materiais; i++)
        {
          'attributes': {
            'POSITION': pos,
            'NORMAL': nor,
            'TEXCOORD_0': uv,
            'JOINTS_0': ?juntas,
            'WEIGHTS_0': ?pesos,
          },
          'indices': idx,
          'material': i,
          if (morph != null)
            'targets': [
              {'POSITION': morph},
            ],
        },
    ],
  });

  if (comEsqueleto) {
    final inv = g.floats([
      for (var j = 0; j < 2; j++)
        ...[1.0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1],
    ], 'MAT4', 16);
    g.nodes
      ..add({'name': 'osso0', 'translation': <double>[0, 0, 0]})
      ..add({'name': 'osso1', 'translation': <double>[0, .5, 0]});
    g.skins.add({
      'joints': [0, 1],
      'inverseBindMatrices': inv,
    });
  }
  for (var i = 0; i < objetos; i++) {
    g.nodes.add({
      'name': 'obj$i',
      'mesh': 0,
      if (comEsqueleto && i == 0) 'skin': 0,
      'translation': <double>[i * 1.5, 0, 0],
    });
  }
  if (comAnimacao) {
    final t = g.floats([0, .5, 1], 'SCALAR', 1);
    final v = g.floats([0, 0, 0, 0, 1, 0, 0, 2, 0], 'VEC3', 3);
    g.animations.add({
      'name': 'andar',
      'samplers': [
        {'input': t, 'output': v, 'interpolation': 'LINEAR'},
      ],
      'channels': [
        {
          'sampler': 0,
          'target': {'node': comEsqueleto ? 0 : 0, 'path': 'translation'},
        },
      ],
    });
  }
  return g;
}

// =============================================================== medida

class _Resultado {
  _Resultado(this.nome, this.bytesDoArquivo);
  final String nome;
  final int bytesDoArquivo;
  bool abriu = false;
  String? recusa;
  int msImportar = 0;
  int msPonte = 0;
  int triangulos = 0;
  int primitivas = 0;
  int materiais = 0;
  int esqueletos = 0;
  int animacoes = 0;
  int memoriaEstimada = 0;
  int bytesDaPonte = 0;
  int bytesDeTextura = 0;
  bool ponteTemEsqueleto = false;
  bool ponteTemAnimacao = false;
  List<String> avisos = const [];
}

_Resultado _medir(String nome, Uint8List glb) {
  final r = _Resultado(nome, glb.length);
  final t0 = DateTime.now();
  ModelAsset3D asset;
  try {
    asset = importGltf3D(glb);
  } on ModelImportException catch (e) {
    r.recusa = e.message;
    r.msImportar = DateTime.now().difference(t0).inMilliseconds;
    return r;
  }
  r.msImportar = DateTime.now().difference(t0).inMilliseconds;
  r.abriu = true;
  r.triangulos = asset.triangleCount;
  r.primitivas = asset.primitives.length;
  r.materiais = (asset.data['materials'] as List? ?? const []).length;
  r.esqueletos = asset.skins.length;
  r.animacoes = asset.clips.length;
  r.memoriaEstimada = asset.estimatedBytes;
  r.avisos = asset.warnings;
  // A TEXTURA VIRA TEXTO. O importador guarda a imagem como data URI em
  // base64 dentro de uma String do Dart: dois bytes por caractere, mais
  // um terco do base64. E o multiplicador que ninguem ve.
  for (final m in (asset.data['materials'] as List? ?? const [])) {
    final img = (m as Map)['image'] as String?;
    // UM BYTE POR CARACTERE: base64 e ASCII, e a maquina virtual guarda
    // texto ASCII em um byte. Medir dois dobrava o numero e teria feito
    // a auditoria acusar o dobro do custo real.
    if (img != null) r.bytesDeTextura += img.length;
  }

  // A SEGUNDA CONVERSAO: o que sobra do modelo quando ele vira o GLB que
  // o Filament realmente carrega.
  final t1 = DateTime.now();
  final ponte = encodeNodeGlb(SceneNode(name: nome, modelAsset: asset));
  r.msPonte = DateTime.now().difference(t1).inMilliseconds;
  r.bytesDaPonte = ponte.length;
  final texto = utf8.decode(ponte.sublist(20, 20 + 4096.clamp(0, ponte.length - 20)), allowMalformed: true);
  r.ponteTemEsqueleto = texto.contains('JOINTS_0') || texto.contains('"skins"');
  r.ponteTemAnimacao = texto.contains('"animations"');
  return r;
}

String _mb(int bytes) => (bytes / 1048576).toStringAsFixed(1);

void main() {
  final resultados = <_Resultado>[];

  tearDownAll(() {
    final b = StringBuffer()
      ..writeln('# Auditoria do motor 3D — a bancada do pior caso')
      ..writeln()
      ..writeln('Gerado por `test/motor3d_auditoria_test.dart`. Cada linha é')
      ..writeln('um GLB fabricado no próprio teste e levado pelo caminho')
      ..writeln('real do aplicativo: importador → formato interno → ponte')
      ..writeln('GLB → (Filament).')
      ..writeln()
      ..writeln('Memória é a estimativa do próprio `ModelAsset3D`, que conta')
      ..writeln('o custo no heap do Dart — não a memória de GPU. FPS, memória')
      ..writeln('de GPU e iPhone 13 **não estão aqui**: precisam de aparelho.')
      ..writeln()
      ..writeln(
        '| Asset | Arquivo | Abriu | Import | Ponte | Triângulos | Prims | Mats | Esq. | Anim. | Heap estimado | Textura no heap | GLB da ponte | Esq. na ponte | Anim. na ponte |',
      )
      ..writeln(
        '| --- | ---: | :--: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--: | :--: |',
      );
    for (final r in resultados) {
      b.writeln(
        '| ${r.nome} | ${_mb(r.bytesDoArquivo)} MB | ${r.abriu ? "sim" : "**NÃO**"} '
        '| ${r.msImportar} ms | ${r.msPonte} ms | ${r.triangulos} | ${r.primitivas} '
        '| ${r.materiais} | ${r.esqueletos} | ${r.animacoes} '
        '| ${_mb(r.memoriaEstimada)} MB | ${_mb(r.bytesDeTextura)} MB '
        '| ${_mb(r.bytesDaPonte)} MB '
        '| ${r.ponteTemEsqueleto ? "sim" : "**não**"} '
        '| ${r.ponteTemAnimacao ? "sim" : "**não**"} |',
      );
    }
    b.writeln();
    for (final r in resultados.where((r) => r.recusa != null)) {
      b.writeln('- **${r.nome}** recusado: ${r.recusa}');
    }
    b.writeln();
    for (final r in resultados.where((r) => r.avisos.isNotEmpty)) {
      b.writeln('- **${r.nome}**: ${r.avisos.join(" / ")}');
    }
    final f = File('docs/motor3d-auditoria.md');
    f.parent.createSync(recursive: true);
    f.writeAsStringSync(b.toString());
  });

  test('simples: um plano de doze triangulos', () {
    final r = _medir('Simples', _malha(2).bytes());
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
    expect(r.triangulos, 8);
  });

  test('medio: cinquenta mil triangulos', () {
    final r = _medir('Médio (50k tri)', _malha(160).bytes());
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
    expect(r.triangulos, greaterThan(40000));
  });

  test('complexo: muitos materiais no mesmo objeto', () {
    final r = _medir('Complexo (60 materiais)', _malha(40, materiais: 60).bytes());
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
  });

  test('muitos objetos: quinhentos nos', () {
    final r = _medir('Muitos objetos (500)', _malha(8, objetos: 500).bytes());
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
  });

  test('pesado: duzentos e cinquenta mil triangulos', () {
    final r = _medir('Pesado (250k tri)', _malha(360).bytes());
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
  });

  test('personagem rigado', () {
    final r = _medir(
      'Personagem (skin)',
      _malha(40, comEsqueleto: true, comAnimacao: true).bytes(),
    );
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
    expect(r.esqueletos, greaterThan(0), reason: 'o importador leu o esqueleto');

    // O ACHADO: o esqueleto e a animacao chegam ao formato interno e NAO
    // chegam ao Filament. A ponte escreve so posicao, normal e UV.
    expect(
      r.ponteTemEsqueleto,
      isFalse,
      reason: 'se isto virar verdadeiro, a ponte passou a levar skin — '
          'atualize a auditoria',
    );
  });

  test('morph targets', () {
    final r = _medir('Morph targets', _malha(30, comMorph: true).bytes());
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
  });

  test('transparencia', () {
    final r = _medir('Transparência', _malha(30, transparente: true).bytes());
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
  });

  test('textura 2K', () {
    final r = _medir('Textura 2048²', _malha(20, textura: 2048).bytes());
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
  });

  test('textura 4K', () {
    final r = _medir('Textura 4096²', _malha(20, textura: 4096).bytes());
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
  });

  test('GLB com bytes sobrando no fim abre', () {
    // O QUE ACONTECE COM ARQUIVO REAL. Vários exportadores e CDNs deixam
    // bytes depois do último bloco; o cabeçalho continua descrevendo o
    // conteúdo. Cortar pelo tamanho declarado abre o arquivo sem
    // afrouxar nada — o cabeçalho que promete MAIS do que existe
    // continua sendo recusado.
    final g = _malha(4);
    final r = _medir('GLB com cauda', g.bytes(extra: List.filled(16, 0)));
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
    expect(r.triangulos, 32);
  });

  test('Draco e recusado com mensagem', () {
    final g = _malha(10)
      ..usadas.add('KHR_draco_mesh_compression')
      ..exigidas.add('KHR_draco_mesh_compression');
    final r = _medir('Draco (comprimido)', g.bytes());
    resultados.add(r);
    expect(r.abriu, isFalse);
    expect(r.recusa, contains('Draco'));
  });

  test('KTX2/BasisU e recusado', () {
    final g = _malha(10)
      ..usadas.add('KHR_texture_basisu')
      ..exigidas.add('KHR_texture_basisu');
    final r = _medir('KTX2 / BasisU', g.bytes());
    resultados.add(r);
    expect(r.abriu, isFalse);
  });

  // 14/09/2026: o meshoptimizer inteiro entrou no pacote nativo, com o
  // decodificador do EXT_meshopt_compression. Arquivo que declara a
  // extensao abre; o comprimido de verdade e comparado vertice a vertice
  // em test/modelo_importado_cpp_test.dart.
  test('meshopt abre', () {
    final g = _malha(10)
      ..usadas.add('EXT_meshopt_compression')
      ..exigidas.add('EXT_meshopt_compression');
    final r = _medir('Meshopt', g.bytes());
    resultados.add(r);
    expect(r.abriu, isTrue, reason: r.recusa);
  });
}
