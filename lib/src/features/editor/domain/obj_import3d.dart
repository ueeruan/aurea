import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';

import 'model_asset3d.dart';
import 'model_import3d.dart';

/// Wavefront OBJ/MTL, including split UV/normal indices, negative indices,
/// groups and concave polygons. OBJ has no skeleton or animation channels.
ModelAsset3D importObj3D(
  String text, {
  String name = 'Modelo OBJ',
  Map<String, Uint8List> resources = const {},
}) {
  final positions = <List<double>>[],
      uv = <List<double>>[],
      normals = <List<double>>[];
  final materials = <Map<String, dynamic>>[], materialIds = <String, int>{};
  final warnings = <String>{
    'OBJ nao carrega rig ou clipes. Use GLB para animacao esqueletica.',
  };
  final groups = <String, Map<int, List<List<String>>>>{};
  var group = name, material = -1;
  List<double> numbers(List<String> v, int count) {
    if (v.length < count) modelFail('OBJ com vetor incompleto.');
    return [
      for (var i = 0; i < count; i++)
        double.tryParse(v[i])?.isFinite == true
            ? double.parse(v[i])
            : modelFail('OBJ contem coordenada invalida.'),
    ];
  }

  void mtl(String file) {
    final bytes = resources[file];
    // MTL AUSENTE NAO BARRA O MODELO. A geometria esta inteira no OBJ; sem
    // o MTL ela entra com material padrao e um aviso — antes a importacao
    // inteira falhava por causa de um arquivo de cor.
    if (bytes == null) {
      warnings.add(
        'Material ausente: $file. O modelo entrou com material padrao; '
        'selecione tambem o MTL e as texturas para as cores.',
      );
      return;
    }
    Map<String, dynamic>? current;
    for (final line in const LineSplitter().convert(utf8.decode(bytes))) {
      final parts = line.trim().split(RegExp(r'\s+'));
      final op = parts.first, rest = parts.skip(1).toList();
      if (op == 'newmtl') {
        final label = rest.join(' ');
        current = {
          'name': label,
          'color': [1.0, 1.0, 1.0, 1.0],
          'roughness': .6,
          'metallic': 0.0,
        };
        materialIds[label] = materials.length;
        materials.add(current);
      } else if (current != null) {
        if (op == 'Kd') {
          current['color'] = [
            ...numbers(rest, 3),
            (current['color'] as List)[3],
          ];
        }
        if (op == 'd' || op == 'Tr') {
          final alpha = numbers(rest, 1).first;
          (current['color'] as List)[3] = (op == 'Tr' ? 1 - alpha : alpha)
              .clamp(0.0, 1.0);
          current['alpha'] = 'BLEND';
        }
        if (op == 'Ns') {
          current['roughness'] = math
              .sqrt(2 / (numbers(rest, 1).first.clamp(0, 10000) + 2))
              .clamp(.04, 1.0);
        }
        if (op == 'Pm') {
          current['metallic'] = numbers(rest, 1).first.clamp(0.0, 1.0);
        }
        if (op == 'Pr') {
          current['roughness'] = numbers(rest, 1).first.clamp(0.0, 1.0);
        }
        if (op == 'map_Kd') {
          // OPCOES DO MAP_KD (-s, -o, -bm...): o arquivo e o que sobra
          // no fim. As opcoes nao sao aplicadas, a textura sim.
          final path = texturaDoMapKd(rest);
          final bytes = path == null ? null : resources[path];
          if (path == null || bytes == null) {
            warnings.add(
              'Textura ausente: ${path ?? rest.join(' ')}. A cor do material foi mantida.',
            );
          } else {
            if (rest.first.startsWith('-')) {
              warnings.add('Opcoes do map_Kd (escala, deslocamento) nao sao aplicadas.');
            }
            current['image'] =
                'data:application/octet-stream;base64,${base64Encode(bytes)}';
          }
        }
        if (['map_Bump', 'bump', 'map_Ks', 'map_Ns', 'disp'].contains(op)) {
          warnings.add('Mapas adicionais do MTL nao sao aplicados.');
        }
      }
    }
  }

  for (final raw in const LineSplitter().convert(
    text.replaceAll('\\\r\n', ' ').replaceAll('\\\n', ' '),
  )) {
    final line = raw.split('#').first.trim();
    if (line.isEmpty) continue;
    final parts = line.split(RegExp(r'\s+')), op = parts.first;
    final rest = parts.skip(1).toList();
    switch (op) {
      case 'v':
        final v = numbers(rest, 3);
        // `v x y z w` e coordenada homogenea; `v x y z r g b` (seis ou
        // sete numeros) e COR POR VERTICE, a extensao que ZBrush, MeshLab
        // e fotogrametria exportam. Dividir pela "cor vermelha" achatava o
        // modelo — e vermelho zero parava a importacao.
        if (rest.length == 4) {
          final w = numbers(rest.skip(3).toList(), 1).first;
          if (w == 0) modelFail('Vertice OBJ com w zero.');
          for (var i = 0; i < 3; i++) {
            v[i] /= w;
          }
        } else if (rest.length >= 6) {
          warnings.add('Cores por vertice do OBJ nao sao aplicadas.');
        }
        positions.add(v);
      case 'vt':
        uv.add(numbers(rest, 2));
      case 'vn':
        normals.add(numbers(rest, 3));
      case 'o':
      case 'g':
        group = rest.isEmpty ? name : rest.join(' ');
      case 'mtllib':
        mtl(rest.join(' '));
      case 'usemtl':
        material = materialIds[rest.join(' ')] ?? -1;
        if (material < 0) {
          warnings.add(
            'Material OBJ nao definido: ${rest.join(' ')}. Usado o material padrao.',
          );
        }
      case 'f':
        if (rest.length < 3 || rest.length > 4096) {
          modelFail('Poligono OBJ invalido ou acima de 4096 cantos.');
        }
        final resolved = <String>[];
        for (final token in rest) {
          final tuple = token.split('/');
          int index(String value, int length) {
            final i = int.tryParse(value);
            if (i == null || i == 0) modelFail('Indice OBJ invalido.');
            final result = i < 0 ? length + i : i - 1;
            if (result < 0 || result >= length) {
              modelFail('Indice OBJ fora dos limites.');
            }
            return result;
          }

          final vi = index(tuple[0], positions.length);
          final ti = tuple.length > 1 && tuple[1].isNotEmpty
              ? index(tuple[1], uv.length)
              : -1;
          final ni = tuple.length > 2 && tuple[2].isNotEmpty
              ? index(tuple[2], normals.length)
              : -1;
          resolved.add('$vi/$ti/$ni');
        }
        groups
            .putIfAbsent(group, () => {})
            .putIfAbsent(material, () => [])
            .add(resolved);
    }
  }
  final nodes = <Map<String, dynamic>>[], primitives = <Map<String, dynamic>>[];
  for (final g in groups.entries) {
    final node = nodes.length;
    nodes.add({'name': g.key});
    for (final bucket in g.value.entries) {
      final vertices = <List<double>>[],
          tex = <List<double>>[],
          ns = <List<double>>[],
          indices = <int>[];
      final unique = <String, int>{};
      var hasUv = true, hasNormals = true;
      for (final polygon in bucket.value) {
        final face = <int>[];
        for (final key in polygon) {
          face.add(
            unique.putIfAbsent(key, () {
              final tuple = key.split('/').map(int.parse).toList();
              vertices.add(positions[tuple[0]]);
              if (tuple[1] < 0) {
                hasUv = false;
                tex.add([0, 0]);
              } else {
                final v = uv[tuple[1]];
                tex.add([v[0], 1 - v[1]]);
              }
              if (tuple[2] < 0) {
                hasNormals = false;
                ns.add([0, 0, 1]);
              } else {
                ns.add(normals[tuple[2]]);
              }
              return vertices.length - 1;
            }),
          );
        }
        indices.addAll(triangulateModelPolygon(vertices, face));
      }
      if (bucket.key >= 0 && materials[bucket.key]['image'] != null && !hasUv) {
        modelFail('OBJ texturizado sem coordenadas UV em todas as faces.');
      }
      primitives.add({
        'node': node,
        'positions': vertices,
        'indices': indices,
        'material': bucket.key,
        if (hasUv) 'uv': tex,
        if (hasNormals) 'normals': ns,
      });
    }
  }
  if (primitives.isEmpty) modelFail('OBJ sem faces.');
  return ModelAsset3D({
    'version': 1,
    'format': 'obj',
    'name': name,
    'nodes': nodes,
    'primitives': primitives,
    'materials': materials,
    'skins': [],
    'clips': [],
    'warnings': warnings.toList(),
  });
}

/// Ear clipping on the dominant plane; a fan is wrong for concave faces.
List<int> triangulateModelPolygon(List<List<double>> vertices, List<int> face) {
  if (face.length == 3) return face;
  final normal = [0.0, 0.0, 0.0];
  for (var i = 0; i < face.length; i++) {
    final a = vertices[face[i]], b = vertices[face[(i + 1) % face.length]];
    normal[0] += (a[1] - b[1]) * (a[2] + b[2]);
    normal[1] += (a[2] - b[2]) * (a[0] + b[0]);
    normal[2] += (a[0] - b[0]) * (a[1] + b[1]);
  }
  var axis = 0;
  for (var i = 1; i < 3; i++) {
    if (normal[i].abs() > normal[axis].abs()) axis = i;
  }
  final x = (axis + 1) % 3, y = (axis + 2) % 3;
  double cross(int a, int b, int c) =>
      (vertices[b][x] - vertices[a][x]) * (vertices[c][y] - vertices[a][y]) -
      (vertices[b][y] - vertices[a][y]) * (vertices[c][x] - vertices[a][x]);
  final sign = normal[axis] < 0 ? -1.0 : 1.0;
  final remaining = [...face], result = <int>[];
  while (remaining.length > 3) {
    var found = false;
    for (var i = 0; i < remaining.length; i++) {
      final a = remaining[(i + remaining.length - 1) % remaining.length],
          b = remaining[i],
          c = remaining[(i + 1) % remaining.length];
      if (cross(a, b, c) * sign <= 1e-12) continue;
      if (remaining.any(
        (p) =>
            p != a &&
            p != b &&
            p != c &&
            cross(a, b, p) * sign >= -1e-12 &&
            cross(b, c, p) * sign >= -1e-12 &&
            cross(c, a, p) * sign >= -1e-12,
      )) {
        continue;
      }
      result.addAll([a, b, c]);
      remaining.removeAt(i);
      found = true;
      break;
    }
    if (!found) {
      modelFail(
        'Poligono degenerado, nao plano ou auto-intersectante. Triangule antes de exportar.',
      );
    }
  }
  return [...result, ...remaining];
}

/// O ARQUIVO de uma linha `map_Kd` depois das opcoes (`-s 1 1 1`, `-bm 2`,
/// `-clamp on`...). Cada opcao consome um numero conhecido de valores; o
/// que sobra e o caminho (que pode ter espacos).
String? texturaDoMapKd(List<String> partes) {
  const valores = {
    '-blendu': 1,
    '-blendv': 1,
    '-bm': 1,
    '-boost': 1,
    '-cc': 1,
    '-clamp': 1,
    '-imfchan': 1,
    '-mm': 2,
    '-o': 3,
    '-s': 3,
    '-t': 3,
    '-texres': 1,
    '-type': 1,
  };
  var i = 0;
  while (i < partes.length && partes[i].startsWith('-')) {
    final n = valores[partes[i]];
    if (n == null) {
      i++;
      continue;
    }
    // -o, -s e -t aceitam de 1 a 3 numeros.
    var consumidos = 0;
    i++;
    while (consumidos < n &&
        i < partes.length &&
        double.tryParse(partes[i]) != null) {
      i++;
      consumidos++;
    }
    if (n == 1 && consumidos == 0 && i < partes.length) i++;
  }
  if (i >= partes.length) return null;
  return partes.sublist(i).join(' ');
}
