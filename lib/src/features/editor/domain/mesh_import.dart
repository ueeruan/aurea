import 'element3d.dart';

/// IMPORTACAO DE MODELOS 3D: OBJ (Wavefront) e FBX em texto (ASCII).
///
/// O modelo vira um [Element3DMesh] como os solidos nativos: vertices
/// em [-0.5, 0.5] no maior eixo, centrado, Y para baixo (convencao de
/// tela do app). Faces de qualquer numero de lados sao mantidas como
/// poligonos; o pintor faz o leque de triangulos.
///
/// FBX binario nao e lido (o formato e proprietario e comprimido):
/// exporte como "FBX ASCII" ou OBJ.

/// Acima disso o app avisa que o modelo e pesado (pode travar em
/// celulares fracos).
const int kMeshFacesHeavy = 15000;

/// Acima disso as faces extras sao descartadas: o pintor por software
/// nao aguenta mais que isso num celular.
const int kMeshFacesMax = 80000;

class MeshImportResult {
  const MeshImportResult({
    required this.mesh,
    required this.format,
    required this.faceCount,
    required this.vertexCount,
    this.truncated = false,
  });

  final Element3DMesh mesh;
  final String format;
  final int faceCount;
  final int vertexCount;
  final bool truncated;

  bool get heavy => faceCount > kMeshFacesHeavy;
}

class MeshImportException implements Exception {
  const MeshImportException(this.message);
  final String message;
  @override
  String toString() => message;
}

/// Assinatura do FBX binario: "Kaydara FBX Binary  \x00".
bool looksLikeBinaryFbx(List<int> bytes) {
  if (bytes.length < 18) return false;
  const assinatura = 'Kaydara FBX Binary';
  for (var i = 0; i < assinatura.length; i++) {
    if (bytes[i] != assinatura.codeUnitAt(i)) return false;
  }
  return true;
}

/// Le o texto de um modelo pela extensao ("obj" ou "fbx").
MeshImportResult importMeshText(String text, {required String extension}) {
  final ext = extension.toLowerCase().replaceFirst('.', '');
  switch (ext) {
    case 'obj':
      return _obj(text);
    case 'fbx':
      return _fbxAscii(text);
    default:
      throw MeshImportException(
          'Formato .$ext nao suportado. Use OBJ ou FBX (ASCII).');
  }
}

final _numero = RegExp(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?');

MeshImportResult _obj(String text) {
  final verts = <List<double>>[];
  final faces = <List<int>>[];
  for (final linhaBruta in text.split('\n')) {
    final linha = linhaBruta.trim();
    if (linha.isEmpty || linha.startsWith('#')) continue;
    final partes = linha.split(RegExp(r'\s+'));
    final tag = partes.first;
    if (tag == 'v' && partes.length >= 4) {
      final x = double.tryParse(partes[1]);
      final y = double.tryParse(partes[2]);
      final z = double.tryParse(partes[3]);
      if (x == null || y == null || z == null) continue;
      verts.add([x, y, z]);
    } else if (tag == 'f' && partes.length >= 4) {
      final face = <int>[];
      for (var i = 1; i < partes.length; i++) {
        final tok = partes[i].split('/').first;
        final idx = int.tryParse(tok);
        if (idx == null || idx == 0) continue;
        // 1-based; negativo conta do fim.
        final v = idx > 0 ? idx - 1 : verts.length + idx;
        if (v >= 0) face.add(v);
      }
      if (face.length >= 3) faces.add(face);
    }
  }
  if (verts.isEmpty || faces.isEmpty) {
    throw const MeshImportException(
        'O OBJ nao tem vertices e faces que o app entenda.');
  }
  return _finaliza(verts, faces, 'OBJ');
}

MeshImportResult _fbxAscii(String text) {
  if (text.contains('Kaydara FBX Binary')) {
    throw const MeshImportException(
        'FBX binario nao e suportado: exporte como FBX ASCII ou OBJ.');
  }
  final verts = <List<double>>[];
  final faces = <List<int>>[];
  var pos = 0;
  while (true) {
    final iv = text.indexOf('Vertices:', pos);
    if (iv < 0) break;
    final ip = text.indexOf('PolygonVertexIndex:', iv);
    if (ip < 0) break;
    final fimIdx = _fimDaLista(text, ip + 'PolygonVertexIndex:'.length);
    final segV = _corpoDaLista(text.substring(iv + 'Vertices:'.length, ip));
    final segP = _corpoDaLista(
        text.substring(ip + 'PolygonVertexIndex:'.length, fimIdx));
    final base = verts.length;
    final nums = [
      for (final m in _numero.allMatches(segV)) double.parse(m.group(0)!)
    ];
    for (var i = 0; i + 2 < nums.length; i += 3) {
      verts.add([nums[i], nums[i + 1], nums[i + 2]]);
    }
    var face = <int>[];
    for (final m in _numero.allMatches(segP)) {
      final raw = int.tryParse(m.group(0)!);
      if (raw == null) continue;
      if (raw < 0) {
        // Indice negativo fecha o poligono: valor real = -raw - 1.
        face.add(base + (-raw - 1));
        if (face.length >= 3) faces.add(face);
        face = <int>[];
      } else {
        face.add(base + raw);
      }
    }
    if (face.length >= 3) faces.add(face);
    pos = fimIdx;
  }
  if (verts.isEmpty || faces.isEmpty) {
    throw const MeshImportException(
        'O FBX nao tem geometria em texto que o app entenda (e binario?).');
  }
  return _finaliza(verts, faces, 'FBX');
}

/// No FBX 7 a lista vem como "*N { a: 1,2,3 }": tira o tamanho e o "a:".
String _corpoDaLista(String s) {
  var t = s;
  final a = t.indexOf('a:');
  if (a >= 0) {
    final chave = t.indexOf('{');
    if (chave >= 0 && chave < a) t = t.substring(a + 2);
  }
  final fecha = t.indexOf('}');
  if (fecha >= 0) t = t.substring(0, fecha);
  return t;
}

/// Fim da lista de indices: o "}" que fecha (FBX 7) ou a proxima linha
/// com um nome de campo (FBX 6, listas sem chaves).
int _fimDaLista(String text, int inicio) {
  final chave = text.indexOf('{', inicio);
  final quebra = text.indexOf('\n', inicio);
  if (chave >= 0 && (quebra < 0 || chave < quebra)) {
    final fecha = text.indexOf('}', chave);
    return fecha < 0 ? text.length : fecha + 1;
  }
  // FBX 6: os numeros seguem em linhas sem chaves ate o proximo campo.
  final campo = RegExp(r'\n\s*[A-Za-z]+\s*:').firstMatch(text.substring(inicio));
  return campo == null ? text.length : inicio + campo.start;
}

MeshImportResult _finaliza(
    List<List<double>> verts, List<List<int>> facesBrutas, String format) {
  var faces = facesBrutas.where((f) => f.every((i) => i < verts.length)).toList();
  final total = faces.length;
  var truncated = false;
  if (faces.length > kMeshFacesMax) {
    faces = faces.sublist(0, kMeshFacesMax);
    truncated = true;
  }
  if (faces.isEmpty) {
    throw const MeshImportException('As faces do modelo apontam para vertices que nao existem.');
  }
  // Normaliza: centro no meio da caixa, maior eixo = 1, Y para baixo.
  var minX = double.infinity, minY = double.infinity, minZ = double.infinity;
  var maxX = -double.infinity, maxY = -double.infinity, maxZ = -double.infinity;
  for (final v in verts) {
    if (v[0] < minX) minX = v[0];
    if (v[0] > maxX) maxX = v[0];
    if (v[1] < minY) minY = v[1];
    if (v[1] > maxY) maxY = v[1];
    if (v[2] < minZ) minZ = v[2];
    if (v[2] > maxZ) maxZ = v[2];
  }
  final cx = (minX + maxX) / 2, cy = (minY + maxY) / 2, cz = (minZ + maxZ) / 2;
  var maior = [maxX - minX, maxY - minY, maxZ - minZ]
      .reduce((a, b) => a > b ? a : b);
  if (maior < 1e-9) maior = 1;
  final k = 1 / maior;
  final normalizados = <List<double>>[
    for (final v in verts) [(v[0] - cx) * k, -(v[1] - cy) * k, (v[2] - cz) * k],
  ];
  return MeshImportResult(
    mesh: Element3DMesh(normalizados, faces),
    format: format,
    faceCount: total,
    vertexCount: verts.length,
    truncated: truncated,
  );
}
