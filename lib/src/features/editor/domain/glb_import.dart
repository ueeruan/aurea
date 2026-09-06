import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';

import 'element3d.dart';

/// IMPORTAR .GLB.
///
/// Modelar em celular ninguem vai fazer. O que se faz e BAIXAR um modelo
/// pronto — e ate agora esse modelo nao entrava no aplicativo, o que
/// deixava a cena 3D presa nos oito solidos de sempre.
///
/// O .glb e o formato que o mundo usa para isso: um cabecalho, um pedaco
/// de JSON descrevendo a cena, e um pedaco binario com os numeros. Aqui
/// se le a geometria — posicoes e triangulos, com a transformacao do no
/// aplicada. Material, textura e animacao ficam de fora de proposito: o
/// renderizador do aplicativo tem material proprio, e prometer PBR
/// completo seria mentira.
class GlbResult {
  const GlbResult({
    required this.mesh,
    required this.name,
    required this.triangles,
    required this.mediumMesh,
    required this.lowMesh,
    this.report = const GlbReport(),
    this.author,
    this.license,
    this.sourceUrl,
    this.nodeNames = const [],
    this.animationNames = const [],
    this.warning,
  });

  final Element3DMesh mesh;
  final String name;
  final int triangles;
  final Element3DMesh mediumMesh;
  final Element3DMesh lowMesh;
  final GlbReport report;
  final String? author;
  final String? license;
  final String? sourceUrl;
  final List<String> nodeNames;
  final List<String> animationNames;

  /// O que foi ignorado, para a interface poder avisar em vez de
  /// entregar um modelo diferente do que a pessoa viu no navegador.
  final String? warning;
}

class GlbReport {
  const GlbReport({
    this.bytes = 0,
    this.meshes = 0,
    this.nodes = 0,
    this.materials = 0,
    this.textures = 0,
    this.animations = 0,
    this.triangles = 0,
    this.overBudget = false,
    this.lodCount = 3,
  });

  final int bytes;
  final int meshes;
  final int nodes;
  final int materials;
  final int textures;
  final int animations;
  final int triangles;
  final bool overBudget;
  final int lodCount;
}

class GlbException implements Exception {
  GlbException(this.message);
  final String message;
  @override
  String toString() => message;
}

const _magic = 0x46546C67; // "glTF"
const _chunkJson = 0x4E4F534A;
const _chunkBin = 0x004E4942;

/// Le um .glb inteiro em memoria.
///
/// O fluxo normal nunca bloqueia por orcamento: acima de 60 mil triangulos
/// abre com aviso. Passar [maxTriangles] explicitamente mantem um modo
/// estrito util para automacao e compatibilidade da API anterior.
GlbResult parseGlb(Uint8List bytes, {int? maxTriangles}) {
  if (bytes.length < 20) throw GlbException('Arquivo pequeno demais.');
  final data = ByteData.sublistView(bytes);

  if (data.getUint32(0, Endian.little) != _magic) {
    throw GlbException('Isso nao e um arquivo .glb.');
  }

  Map<String, dynamic>? gltf;
  Uint8List? bin;

  var offset = 12;
  while (offset + 8 <= bytes.length) {
    final len = data.getUint32(offset, Endian.little);
    final tipo = data.getUint32(offset + 4, Endian.little);
    final inicio = offset + 8;
    final fim = inicio + len;
    if (fim > bytes.length) break;

    if (tipo == _chunkJson) {
      final texto = utf8.decode(bytes.sublist(inicio, fim));
      final m = jsonDecode(texto);
      if (m is Map) gltf = m.cast<String, dynamic>();
    } else if (tipo == _chunkBin) {
      bin = Uint8List.sublistView(bytes, inicio, fim);
    }
    // Os pedacos sao alinhados em 4 bytes.
    offset = fim + ((4 - (len % 4)) % 4);
  }

  if (gltf == null) throw GlbException('O .glb nao tem a parte JSON.');
  return _parseDocument(
    gltf,
    [bin],
    sourceBytes: bytes.length,
    maxTriangles: maxTriangles,
  );
}

/// Le um `.gltf` textual. [binaries] preserva o indice de cada entrada de
/// `buffers`; [binary] continua aceito como atalho compativel para o indice
/// zero. Data URI base64 e resolvida aqui mesmo.
GlbResult parseGltf(
  String source, {
  Uint8List? binary,
  List<Uint8List?>? binaries,
  int? maxTriangles,
}) {
  final decoded = jsonDecode(source);
  if (decoded is! Map) throw GlbException('Isso nao e um arquivo .gltf.');
  final gltf = decoded.cast<String, dynamic>();
  final descriptions = (gltf['buffers'] as List?) ?? const [];
  final resolved = List<Uint8List?>.filled(
    descriptions.isEmpty ? 1 : descriptions.length,
    null,
  );
  if (binaries != null) {
    for (var i = 0; i < math.min(resolved.length, binaries.length); i++) {
      resolved[i] = binaries[i];
    }
  }
  if (binary != null) resolved[0] = binary;
  for (var i = 0; i < descriptions.length; i++) {
    if (resolved[i] != null) continue;
    final uri = ((descriptions[i] as Map)['uri'] as String?) ?? '';
    if (uri.startsWith('data:')) {
      final comma = uri.indexOf(',');
      if (comma >= 0) resolved[i] = base64Decode(uri.substring(comma + 1));
    }
  }
  return _parseDocument(
    gltf,
    resolved,
    sourceBytes:
        utf8.encode(source).length +
        resolved.fold(0, (sum, bytes) => sum + (bytes?.length ?? 0)),
    maxTriangles: maxTriangles,
  );
}

GlbResult _parseDocument(
  Map<String, dynamic> gltf,
  List<Uint8List?> bins, {
  required int sourceBytes,
  int? maxTriangles,
}) {
  final malhas = (gltf['meshes'] as List?) ?? const [];
  if (malhas.isEmpty) throw GlbException('O modelo nao tem malha nenhuma.');

  final acessores = (gltf['accessors'] as List?) ?? const [];
  final vistas = (gltf['bufferViews'] as List?) ?? const [];
  final nodes = (gltf['nodes'] as List?) ?? const [];

  final verts = <List<double>>[];
  final faces = <List<int>>[];
  var ignoradas = 0;

  // Transformacao de cada no que aponta para uma malha. Sem ela, um
  // modelo montado de varias pecas vem todo empilhado na origem.
  final porMalha = <int, List<List<double>>>{};
  final parents = <int, int>{};
  for (var i = 0; i < nodes.length; i++) {
    final no = (nodes[i] as Map).cast<String, dynamic>();
    for (final child in (no['children'] as List?) ?? const []) {
      parents[(child as num).toInt()] = i;
    }
  }
  for (var i = 0; i < nodes.length; i++) {
    final no = (nodes[i] as Map).cast<String, dynamic>();
    final idx = (no['mesh'] as num?)?.toInt();
    if (idx == null) continue;
    porMalha.putIfAbsent(idx, () => []).add(_worldMatrix(nodes, parents, i));
  }

  for (var mi = 0; mi < malhas.length; mi++) {
    final malha = (malhas[mi] as Map).cast<String, dynamic>();
    final primitivas = (malha['primitives'] as List?) ?? const [];
    final matrizes = porMalha[mi] ?? [_identidade()];

    for (final pr in primitivas) {
      final prim = (pr as Map).cast<String, dynamic>();
      // 4 = TRIANGLES. Os outros modos (faixa, leque, linha) sao raros
      // em modelo baixado e nao valem o risco de desenhar errado.
      final modo = (prim['mode'] as num?)?.toInt() ?? 4;
      if (modo != 4) {
        ignoradas++;
        continue;
      }
      final atributos = (prim['attributes'] as Map?)?.cast<String, dynamic>();
      final posIdx = (atributos?['POSITION'] as num?)?.toInt();
      if (posIdx == null) {
        ignoradas++;
        continue;
      }

      final pos = _readVec3(acessores, vistas, bins, posIdx);
      final idxAcc = (prim['indices'] as num?)?.toInt();
      final indices = idxAcc == null
          ? [for (var i = 0; i < pos.length; i++) i]
          : _readIndices(acessores, vistas, bins, idxAcc);

      for (final m in matrizes) {
        final base = verts.length;
        for (final p in pos) {
          verts.add(_apply(m, p));
        }
        for (var i = 0; i + 2 < indices.length; i += 3) {
          faces.add([
            base + indices[i],
            base + indices[i + 1],
            base + indices[i + 2],
          ]);
        }
      }
    }
  }

  if (faces.isEmpty) {
    throw GlbException('Nao achei triangulos nesse arquivo.');
  }
  const defaultBudget = 60000;
  final budget = maxTriangles ?? defaultBudget;
  final overBudget = faces.length > budget;
  if (overBudget && maxTriangles != null) {
    throw GlbException(
      'Modelo pesado demais: ${faces.length} triangulos (o limite e '
      '$budget). Simplifique antes de trazer.',
    );
  }

  final normalizados = _normalize(verts);
  final nome =
      (((gltf['meshes'] as List).first as Map)['name'] as String?) ?? 'Modelo';

  final mesh = Element3DMesh(normalizados, faces);
  final warnings = <String>[];
  if (ignoradas != 0) {
    warnings.add(
      '$ignoradas parte(s) ficaram de fora: o Aurea le '
      'triangulos, e essas usam outro formato.',
    );
  }
  if (overBudget) {
    warnings.add(
      'Modelo acima do orcamento: ${faces.length} triangulos '
      '(recomendado: $budget). Aberto com LOD automatico.',
    );
  }
  final materialCount = ((gltf['materials'] as List?) ?? const []).length;
  final textureCount = ((gltf['textures'] as List?) ?? const []).length;
  final skinCount = ((gltf['skins'] as List?) ?? const []).length;
  if (materialCount > 0 || textureCount > 0) {
    warnings.add(
      'Materiais e texturas foram catalogados; o material movel '
      'do Aurea e aplicado ao modelo importado.',
    );
  }
  if (skinCount > 0) {
    warnings.add(
      '$skinCount skin(s) catalogada(s); skinning ainda nao e '
      'avaliado pelo backend Canvas.',
    );
  }
  final extras = (gltf['extras'] as Map?)?.cast<String, dynamic>();
  final asset = (gltf['asset'] as Map?)?.cast<String, dynamic>() ?? const {};
  final animations = (gltf['animations'] as List?) ?? const [];
  return GlbResult(
    mesh: mesh,
    mediumMesh: _lod(mesh, 2),
    lowMesh: _lod(mesh, 4),
    name: nome,
    triangles: faces.length,
    author: extras?['author'] as String?,
    license: (extras?['license'] ?? asset['copyright']) as String?,
    sourceUrl: extras?['url'] as String?,
    nodeNames: [
      for (var i = 0; i < nodes.length; i++)
        ((nodes[i] as Map)['name'] as String?) ?? 'No ${i + 1}',
    ],
    animationNames: [
      for (var i = 0; i < animations.length; i++)
        ((animations[i] as Map)['name'] as String?) ?? 'Clipe ${i + 1}',
    ],
    report: GlbReport(
      bytes: sourceBytes,
      meshes: malhas.length,
      nodes: nodes.length,
      materials: materialCount,
      textures: textureCount,
      animations: animations.length,
      triangles: faces.length,
      overBudget: overBudget,
    ),
    warning: warnings.isEmpty ? null : warnings.join(' '),
  );
}

Element3DMesh _lod(Element3DMesh mesh, int stride) => Element3DMesh(
  mesh.verts,
  [for (var i = 0; i < mesh.faces.length; i += stride) mesh.faces[i]],
);

List<double> _identidade() => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];

/// A matriz de um no: ou vem pronta, ou se monta de escala, rotacao e
/// translacao (nessa ordem, que e a do glTF).
List<double> _matrixOf(Map<String, dynamic> no) {
  final m = no['matrix'];
  if (m is List && m.length == 16) {
    return [for (final v in m) (v as num).toDouble()];
  }

  final t = (no['translation'] as List?) ?? const [0, 0, 0];
  final r = (no['rotation'] as List?) ?? const [0, 0, 0, 1];
  final s = (no['scale'] as List?) ?? const [1, 1, 1];

  final x = (r[0] as num).toDouble();
  final y = (r[1] as num).toDouble();
  final z = (r[2] as num).toDouble();
  final w = (r[3] as num).toDouble();
  final sx = (s[0] as num).toDouble();
  final sy = (s[1] as num).toDouble();
  final sz = (s[2] as num).toDouble();

  // Rotacao do quaternio, ja multiplicada pela escala (coluna a coluna).
  final r00 = (1 - 2 * (y * y + z * z)) * sx;
  final r01 = (2 * (x * y - z * w)) * sy;
  final r02 = (2 * (x * z + y * w)) * sz;
  final r10 = (2 * (x * y + z * w)) * sx;
  final r11 = (1 - 2 * (x * x + z * z)) * sy;
  final r12 = (2 * (y * z - x * w)) * sz;
  final r20 = (2 * (x * z - y * w)) * sx;
  final r21 = (2 * (y * z + x * w)) * sy;
  final r22 = (1 - 2 * (x * x + y * y)) * sz;

  // Guardada em ordem de COLUNA, como o glTF manda.
  return [
    r00, r10, r20, 0, //
    r01, r11, r21, 0,
    r02, r12, r22, 0,
    (t[0] as num).toDouble(), (t[1] as num).toDouble(),
    (t[2] as num).toDouble(), 1,
  ];
}

List<double> _worldMatrix(
  List<dynamic> nodes,
  Map<int, int> parents,
  int index, {
  Set<int>? seen,
}) {
  final chain = seen ?? <int>{};
  if (!chain.add(index)) return _identidade();
  final local = _matrixOf((nodes[index] as Map).cast<String, dynamic>());
  final parent = parents[index];
  if (parent == null) return local;
  return _multiply4(_worldMatrix(nodes, parents, parent, seen: chain), local);
}

/// Matrizes glTF em ordem de coluna: pai * filho.
List<double> _multiply4(List<double> a, List<double> b) {
  final out = List<double>.filled(16, 0);
  for (var column = 0; column < 4; column++) {
    for (var row = 0; row < 4; row++) {
      var value = 0.0;
      for (var k = 0; k < 4; k++) {
        value += a[k * 4 + row] * b[column * 4 + k];
      }
      out[column * 4 + row] = value;
    }
  }
  return out;
}

List<double> _apply(List<double> m, List<double> p) => [
  m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
  m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
  m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14],
];

/// Centro na origem e maior meia-extensao = 1 — a convencao do
/// renderizador. Sem isso, um modelo em metros entraria do tamanho de um
/// grao e um em milimetros, do tamanho de um predio.
List<List<double>> _normalize(List<List<double>> verts) {
  if (verts.isEmpty) return verts;
  final min = [double.infinity, double.infinity, double.infinity];
  final max = [-double.infinity, -double.infinity, -double.infinity];
  for (final v in verts) {
    for (var i = 0; i < 3; i++) {
      min[i] = math.min(min[i], v[i]);
      max[i] = math.max(max[i], v[i]);
    }
  }
  final centro = [for (var i = 0; i < 3; i++) (min[i] + max[i]) / 2];
  var meia = 1e-6;
  for (var i = 0; i < 3; i++) {
    meia = math.max(meia, (max[i] - min[i]) / 2);
  }
  return [
    for (final v in verts)
      [
        (v[0] - centro[0]) / meia,
        // O glTF tem y para CIMA; a tela tem y para baixo.
        -(v[1] - centro[1]) / meia,
        (v[2] - centro[2]) / meia,
      ],
  ];
}

(Uint8List, int, int) _viewOf(
  List<dynamic> vistas,
  List<Uint8List?> bins,
  int viewIndex,
  int extra,
) {
  final v = (vistas[viewIndex] as Map).cast<String, dynamic>();
  final bufferIndex = (v['buffer'] as num?)?.toInt() ?? 0;
  if (bufferIndex < 0 ||
      bufferIndex >= bins.length ||
      bins[bufferIndex] == null) {
    throw GlbException(
      'O buffer $bufferIndex referenciado pelo modelo '
      'nao foi encontrado.',
    );
  }
  final bin = bins[bufferIndex]!;
  final inicio = ((v['byteOffset'] as num?)?.toInt() ?? 0) + extra;
  final stride = (v['byteStride'] as num?)?.toInt() ?? 0;
  return (bin, inicio, stride);
}

List<List<double>> _readVec3(
  List<dynamic> acessores,
  List<dynamic> vistas,
  List<Uint8List?> bins,
  int index,
) {
  final acc = (acessores[index] as Map).cast<String, dynamic>();
  final count = (acc['count'] as num).toInt();
  final tipo = (acc['componentType'] as num).toInt();
  if (tipo != 5126) {
    throw GlbException('Posicoes que nao sao float nao sao suportadas.');
  }
  final viewIndex = (acc['bufferView'] as num?)?.toInt();
  if (viewIndex == null) return List.generate(count, (_) => [0.0, 0.0, 0.0]);

  final (buf, inicio, stride) = _viewOf(
    vistas,
    bins,
    viewIndex,
    (acc['byteOffset'] as num?)?.toInt() ?? 0,
  );
  final passo = stride == 0 ? 12 : stride;
  final data = ByteData.sublistView(buf);

  return [
    for (var i = 0; i < count; i++)
      [
        data.getFloat32(inicio + i * passo, Endian.little),
        data.getFloat32(inicio + i * passo + 4, Endian.little),
        data.getFloat32(inicio + i * passo + 8, Endian.little),
      ],
  ];
}

List<int> _readIndices(
  List<dynamic> acessores,
  List<dynamic> vistas,
  List<Uint8List?> bins,
  int index,
) {
  final acc = (acessores[index] as Map).cast<String, dynamic>();
  final count = (acc['count'] as num).toInt();
  final tipo = (acc['componentType'] as num).toInt();
  final viewIndex = (acc['bufferView'] as num?)?.toInt();
  if (viewIndex == null) return const [];

  final (buf, inicio, _) = _viewOf(
    vistas,
    bins,
    viewIndex,
    (acc['byteOffset'] as num?)?.toInt() ?? 0,
  );
  final data = ByteData.sublistView(buf);

  return switch (tipo) {
    5121 => [for (var i = 0; i < count; i++) data.getUint8(inicio + i)],
    5123 => [
      for (var i = 0; i < count; i++)
        data.getUint16(inicio + i * 2, Endian.little),
    ],
    5125 => [
      for (var i = 0; i < count; i++)
        data.getUint32(inicio + i * 4, Endian.little),
    ],
    _ => throw GlbException('Formato de indice desconhecido ($tipo).'),
  };
}
