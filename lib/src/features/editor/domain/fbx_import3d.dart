import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:vector_math/vector_math_64.dart' as vm;

import 'model_asset3d.dart';
import 'model_import3d.dart';
import 'obj_import3d.dart';

/// Explicit subset: FBX 7.x static mesh, hierarchy, UV/normals, diffuse
/// materials and linear cluster skinning. Source FBX animation stacks are NOT
/// silently presented as playable clips. Export GLB for baked source clips.
/// O arquivo de uma textura FBX (RelativeFilename ou FileName), so o nome.
String? _fbxTextureFile(dynamic textura) {
  for (final campo in ['RelativeFilename', 'FileName', 'Filename']) {
    final n = textura.child(campo);
    if (n == null || n.values.isEmpty) continue;
    final v = n.values.first;
    if (v is! String || v.isEmpty) continue;
    return v.replaceAll(String.fromCharCode(92), '/').split('/').last;
  }
  return null;
}

ModelAsset3D importFbx3D(
  Uint8List bytes, {
  String name = 'Modelo FBX',
  Map<String, Uint8List> resources = const {},
  int maxTriangles = 150000,
}) {
  try {
    if (bytes.length > 64 * 1024 * 1024) modelFail('FBX acima de 64 MB.');
    final binary =
        bytes.length >= 27 &&
        ascii.decode(bytes.sublist(0, 18), allowInvalid: true) ==
            'Kaydara FBX Binary';
    final roots = binary
        ? _FbxBinary(bytes).read()
        : _FbxAscii(utf8.decode(bytes)).read();
    _Fbx? root(String key) => roots.where((n) => n.name == key).firstOrNull;
    final objects = root('Objects')?.children ?? [];
    final byId = {
      for (final o in objects)
        if (o.values.isNotEmpty && o.values.first is num)
          (o.values.first as num).toInt(): o,
    };
    final connections =
        root('Connections')?.children.where((n) => n.name == 'C').toList() ??
        [];
    final children = <int, List<int>>{}, parent = <int, int>{};
    for (final c in connections) {
      if (c.values.length < 3 || c.values[0] != 'OO') continue;
      final child = (c.values[1] as num).toInt(),
          p = (c.values[2] as num).toInt();
      children.putIfAbsent(p, () => []).add(child);
      if (byId[child]?.name == 'Model' && byId[p]?.name == 'Model') {
        if (parent.containsKey(child)) {
          modelFail('FBX: objeto com varios pais.');
        }
        parent[child] = p;
      }
    }
    final models = byId.entries.where((e) => e.value.name == 'Model').toList();
    if (models.length > 4096) modelFail('FBX acima de 4096 nos.');
    final index = {
      for (var i = 0; i < models.length; i++) models[i].key: i + 1,
    };
    final nodes = <Map<String, dynamic>>[];
    final global = _fbxProperties(root('GlobalSettings'));
    final up = (global['UpAxis']?.first as num? ?? 1).toInt();
    final upSign = (global['UpAxisSign']?.first as num? ?? 1).toDouble();
    final coord = (global['CoordAxis']?.first as num? ?? 0).toInt();
    final coordSign = (global['CoordAxisSign']?.first as num? ?? 1).toInt();
    final front = (global['FrontAxis']?.first as num? ?? (up == 2 ? 1 : 2))
        .toInt();
    final frontSign =
        (global['FrontAxisSign']?.first as num? ?? (up == 2 ? -1 : 1)).toInt();
    if (upSign != 1 ||
        coord != 0 ||
        coordSign != 1 ||
        !((up == 1 && front == 2 && frontSign == 1) ||
            (up == 2 && front == 1 && frontSign == -1))) {
      modelFail(
        'Convencao de eixos FBX nao suportada (up $up/$upSign, coord $coord/$coordSign, front $front/$frontSign). Exporte GLB ou FBX destro Y-up/Z-up.',
      );
    }
    // Canonical Y-up root, applied equally to skeleton and rigid geometry.
    final axis = vm.Matrix4.identity();
    if (up == 2) axis.rotateX(-math.pi / 2);
    if (up == 0) axis.rotateZ(math.pi / 2);
    if (upSign < 0) axis.rotateZ(math.pi);
    nodes.add({'name': 'Eixos FBX', 'matrix': axis.storage.toList()});
    for (final entry in models) {
      final p = _fbxProperties(entry.value);
      for (final key in [
        'RotationOffset',
        'RotationPivot',
        'ScalingOffset',
        'ScalingPivot',
      ]) {
        if (_fbxVec(p, key, [0, 0, 0]).any((v) => v.abs() > 1e-9)) {
          modelFail(
            'FBX com pivos/offsets especiais. Converta para GLB com transformacoes aplicadas.',
          );
        }
      }
      final order = (p['RotationOrder']?.first as num? ?? 0).toInt();
      if (order != 0) {
        modelFail('FBX com ordem de rotacao diferente de XYZ. Exporte GLB.');
      }
      final rotation =
          _fbxRotation(_fbxVec(p, 'PreRotation', [0, 0, 0])) *
          _fbxRotation(_fbxVec(p, 'Lcl Rotation', [0, 0, 0])) *
          (_fbxRotation(_fbxVec(p, 'PostRotation', [0, 0, 0]))..conjugate());
      final seen = <int>{};
      var cursor = entry.key;
      while (parent.containsKey(cursor)) {
        if (!seen.add(cursor) || seen.length > 256) {
          modelFail('FBX com ciclo ou hierarquia profunda demais.');
        }
        cursor = parent[cursor]!;
      }
      nodes.add({
        'name': _fbxName(entry.value),
        'parent': index[parent[entry.key]] ?? 0,
        'translation': _fbxVec(p, 'Lcl Translation', [0, 0, 0]),
        'rotation': [rotation.x, rotation.y, rotation.z, rotation.w],
        'scale': _fbxVec(p, 'Lcl Scaling', [1, 1, 1]),
      });
    }
    final warnings = <String>{
      'FBX: suporte parcial. Clipes originais, constraints, morphs e materiais avancados exigem conversao para GLB.',
    };
    if (objects.any((o) => o.name == 'AnimationStack')) {
      warnings.add(
        'Este FBX tem animacoes que nao foram importadas. E possivel criar poses e keyframes novos no rig.',
      );
    }
    // TEXTURAS: a imagem base entra pelo nome do arquivo selecionado junto
    // com o FBX (as conexoes OP ligam textura -> material). Recusar o
    // arquivo inteiro por ter textura era jogar fora a malha.
    final texturaDoMaterial = <int, String>{};
    for (final c in connections) {
      if (c.values.length < 4 || c.values[0] != 'OP') continue;
      final child = (c.values[1] as num).toInt();
      final pai = (c.values[2] as num).toInt();
      if (byId[child]?.name != 'Texture' || byId[pai]?.name != 'Material') {
        continue;
      }
      final arquivo = _fbxTextureFile(byId[child]!);
      if (arquivo == null) continue;
      final prop = c.values[3].toString();
      if (prop.contains('Diffuse') || !texturaDoMaterial.containsKey(pai)) {
        texturaDoMaterial[pai] = arquivo;
      }
    }
    final temTextura = objects.any((o) => o.name == 'Texture');
    if (temTextura) {
      warnings.add(
        'FBX com texturas: so a imagem base e aplicada, pelo arquivo selecionado junto; outros mapas nao entram.',
      );
    }
    String? imagemDe(String? arquivo) {
      if (arquivo == null) return null;
      final base = arquivo.toLowerCase();
      for (final entry in resources.entries) {
        final nome = entry.key.replaceAll(String.fromCharCode(92), '/').split('/').last.toLowerCase();
        if (nome == base) {
          return 'data:application/octet-stream;base64,${base64Encode(entry.value)}';
        }
      }
      warnings.add(
        'Textura $arquivo nao encontrada: selecione o arquivo junto com o FBX.',
      );
      return null;
    }
    // Sem conexoes legiveis mas com UMA imagem selecionada: vale para todos.
    final unicaImagem = texturaDoMaterial.isEmpty && temTextura && resources.length == 1
        ? 'data:application/octet-stream;base64,${base64Encode(resources.values.single)}'
        : null;
    final materials = <Map<String, dynamic>>[];
    final materialIndex = <int, int>{};
    for (final e in byId.entries.where((e) => e.value.name == 'Material')) {
      final p = _fbxProperties(e.value);
      final color = _fbxVec(p, 'DiffuseColor', [.8, .8, .8]);
      final factor = (p['DiffuseFactor']?.first as num? ?? 1).toDouble();
      materialIndex[e.key] = materials.length;
      final imagem = imagemDe(texturaDoMaterial[e.key]) ?? unicaImagem;
      materials.add({
        'name': _fbxName(e.value),
        // Com imagem a cor vira branco: a textura carrega a cor.
        'color': imagem != null
            ? [1.0, 1.0, 1.0, 1.0]
            : [for (final c in color) (c * factor).clamp(0.0, 1.0), 1.0],
        'metallic': 0.0,
        'roughness': .6,
        'image': ?imagem,
      });
    }
    final primitives = <Map<String, dynamic>>[],
        skins = <Map<String, dynamic>>[];
    var triangleCount = 0;
    for (final model in models) {
      final ni = index[model.key]!;
      final modelMaterials = [
        for (final id in children[model.key] ?? <int>[])
          if (materialIndex.containsKey(id)) materialIndex[id]!,
      ];
      final p = _fbxProperties(model.value);
      final geom = vm.Matrix4.compose(
        modelVector(_fbxVec(p, 'GeometricTranslation', [0, 0, 0])),
        _fbxRotation(_fbxVec(p, 'GeometricRotation', [0, 0, 0])),
        modelVector(_fbxVec(p, 'GeometricScaling', [1, 1, 1])),
      );
      for (final gi in children[model.key] ?? <int>[]) {
        final g = byId[gi];
        if (g?.name != 'Geometry' || g!.values.last != 'Mesh') continue;
        final raw = g.array('Vertices'), polys = g.array('PolygonVertexIndex');
        if (raw.length % 3 != 0 || raw.length > 1500000) {
          modelFail('Vertices FBX invalidos ou acima do limite.');
        }
        final base = [
          for (var i = 0; i < raw.length; i += 3)
            [for (var j = 0; j < 3; j++) (raw[i + j] as num).toDouble()],
        ];
        if (base.any((v) => v.any((c) => !c.isFinite))) {
          modelFail('FBX contem coordenadas invalidas.');
        }
        final rawUv = g.child('LayerElementUV'),
            rawNormals = g.child('LayerElementNormal');
        final uvLayer = rawUv == null || rawUv.array('UV').isEmpty
            ? null
            : rawUv;
        final normalLayer =
            rawNormals == null || rawNormals.array('Normals').isEmpty
            ? null
            : rawNormals;
        final matLayer = g.child('LayerElementMaterial');
        List<double>? attribute(
          _Fbx? layer,
          String array,
          String indexArray,
          int components,
          int cp,
          int corner,
          int polygon,
        ) {
          if (layer == null) return null;
          final mapping = layer.child('MappingInformationType')?.values.first;
          var at = switch (mapping) {
            'ByVertice' || 'ByVertex' || 'ByControlPoint' => cp,
            'ByPolygonVertex' => corner,
            'ByPolygon' => polygon,
            'AllSame' => 0,
            _ => modelFail('Mapeamento FBX nao suportado: $mapping'),
          };
          if (layer.child('ReferenceInformationType')?.values.first ==
              'IndexToDirect') {
            at = (layer.array(indexArray)[at] as num).toInt();
          }
          final values = layer.array(array);
          if (at < 0 || (at + 1) * components > values.length) {
            modelFail(
              'Atributo FBX $array fora dos limites ($mapping, indice $at, ${values.length} valores).',
            );
          }
          return [
            for (var j = 0; j < components; j++)
              (values[at * components + j] as num).toDouble(),
          ];
        }

        final controlWeights = List.generate(
          base.length,
          (_) => <(int, double)>[],
        );
        int? skinIndex;
        for (final deformerId in children[gi] ?? <int>[]) {
          final deformer = byId[deformerId];
          if (deformer?.values.last != 'Skin') continue;
          final joints = <int>[], inverse = <List<double>>[];
          for (final clusterId in children[deformerId] ?? <int>[]) {
            final cluster = byId[clusterId];
            if (cluster?.values.last != 'Cluster') continue;
            final mode = cluster!.child('Mode')?.values.first;
            if (mode != null && mode != 'Normalize' && mode != 'TotalOne') {
              modelFail('FBX: modo de skin aditivo nao suportado.');
            }
            final bone = (children[clusterId] ?? <int>[])
                .where(index.containsKey)
                .firstOrNull;
            if (bone == null) modelFail('FBX: cluster sem osso.');
            final link = cluster.array('TransformLink'),
                bind = cluster.array('Transform');
            if (link.length != 16 || bind.length != 16) {
              modelFail('FBX: matrizes de bind ausentes.');
            }
            final matrix = vm.Matrix4.fromList(modelDoubles(link));
            if (matrix.invert() == 0) {
              modelFail('FBX: matriz de bind singular.');
            }
            matrix.multiply(vm.Matrix4.fromList(modelDoubles(bind)));
            final joint = joints.length;
            joints.add(index[bone]!);
            inverse.add(matrix.storage.toList());
            final ids = cluster.array('Indexes'),
                weights = cluster.array('Weights');
            if (ids.length != weights.length) {
              modelFail('FBX: pesos inconsistentes.');
            }
            for (var i = 0; i < ids.length; i++) {
              final cp = (ids[i] as num).toInt(),
                  w = (weights[i] as num).toDouble();
              if (cp < 0 || cp >= base.length || !w.isFinite || w < 0) {
                modelFail('FBX: peso ou vertice invalido.');
              }
              if (w > 0) controlWeights[cp].add((joint, w));
            }
          }
          if (joints.length > 256) modelFail('FBX acima de 256 ossos.');
          if (joints.isNotEmpty) {
            if (skinIndex != null || nodes[ni]['skin'] != null) {
              modelFail(
                'FBX com multiplos skins por objeto: converta para GLB.',
              );
            }
            skinIndex = skins.length;
            skins.add({'joints': joints, 'inverseBind': inverse});
            nodes[ni]['skin'] = skinIndex;
          }
        }
        final buckets = <int, Map<String, dynamic>>{};
        var corners = <int>[], cornerIndex = 0, polygon = 0;
        for (final v in polys) {
          final rawIndex = (v as num).toInt(),
              cp = rawIndex < 0 ? -rawIndex - 1 : rawIndex;
          if (cp < 0 || cp >= base.length) {
            modelFail('FBX: indice de poligono invalido.');
          }
          corners.add(cp);
          if (rawIndex >= 0) continue;
          if (corners.length < 3 || corners.length > 4096) {
            modelFail('FBX: poligono invalido.');
          }
          final materialValues = matLayer?.array('Materials') ?? [];
          final materialSlot = materialValues.isEmpty
              ? -1
              : (materialValues[matLayer
                                    ?.child('MappingInformationType')
                                    ?.values
                                    .first ==
                                'AllSame'
                            ? 0
                            : polygon]
                        as num)
                    .toInt();
          final mi = materialSlot >= 0 && materialSlot < modelMaterials.length
              ? modelMaterials[materialSlot]
              : -1;
          final bucket = buckets.putIfAbsent(
            mi,
            () => {
              'node': ni,
              'material': mi,
              'positions': <List<double>>[],
              'indices': <int>[],
              if (uvLayer != null) 'uv': <List<double>>[],
              if (normalLayer != null) 'normals': <List<double>>[],
              if (skinIndex != null) 'joints': <List<int>>[],
              if (skinIndex != null) 'weights': <List<double>>[],
            },
          );
          final vertices = bucket['positions'] as List<List<double>>,
              face = <int>[];
          for (var j = 0; j < corners.length; j++) {
            final cp = corners[j];
            final value = geom.transformed3(modelVector(base[cp]));
            face.add(vertices.length);
            vertices.add([value.x, value.y, value.z]);
            final uv = attribute(
              uvLayer,
              'UV',
              'UVIndex',
              2,
              cp,
              cornerIndex + j,
              polygon,
            );
            if (uv != null) (bucket['uv'] as List).add([uv[0], 1 - uv[1]]);
            final normal = attribute(
              normalLayer,
              'Normals',
              'NormalsIndex',
              3,
              cp,
              cornerIndex + j,
              polygon,
            );
            if (normal != null) {
              final nm = vm.Matrix3.zero()..copyNormalMatrix(geom);
              final n = nm.transformed(modelVector(normal))..normalize();
              (bucket['normals'] as List).add([n.x, n.y, n.z]);
            }
            if (skinIndex != null) {
              final influences = controlWeights[cp];
              if (influences.isEmpty || influences.length > 8) {
                modelFail(
                  'FBX: vertice sem peso ou com mais de 8 influencias.',
                );
              }
              (bucket['joints'] as List).add([
                for (final w in influences) w.$1,
              ]);
              (bucket['weights'] as List).add([
                for (final w in influences) w.$2,
              ]);
            }
          }
          final triangles = triangulateModelPolygon(vertices, face);
          (bucket['indices'] as List<int>).addAll(triangles);
          triangleCount += triangles.length ~/ 3;
          if (triangleCount > maxTriangles) {
            modelFail('FBX acima de ${maxTriangles ~/ 1000} mil triangulos.');
          }
          cornerIndex += corners.length;
          polygon++;
          corners = [];
        }
        if (corners.isNotEmpty) modelFail('FBX: poligono incompleto.');
        primitives.addAll(buckets.values);
      }
    }
    if (primitives.isEmpty) modelFail('FBX sem malhas poligonais suportadas.');
    return ModelAsset3D({
      'version': 1,
      'format': 'fbx7',
      'name': name,
      'nodes': nodes,
      'primitives': primitives,
      'materials': materials,
      'skins': skins,
      'clips': [],
      'warnings': warnings.toList(),
    });
  } on ModelImportException {
    rethrow;
  } catch (e) {
    modelFail(
      'FBX invalido ou variante nao suportada (${e.runtimeType}). Exporte GLB 2.0.',
    );
  }
}

String _fbxName(_Fbx n) => n.values.length > 1
    ? '${n.values[1]}'.split('\u0000').first.replaceFirst(RegExp(r'^\w+::'), '')
    : n.name;
Map<String, List<dynamic>> _fbxProperties(_Fbx? n) => {
  for (final p in n?.child('Properties70')?.children ?? <_Fbx>[])
    if (p.values.length >= 5)
      p.values.first as String: p.values.skip(4).toList(),
};
List<double> _fbxVec(
  Map<String, List<dynamic>> p,
  String key,
  List<double> fallback,
) => p[key] == null ? fallback : modelDoubles(p[key]!.take(3).toList());
vm.Quaternion _fbxRotation(List<double> r) {
  final x = vm.Quaternion.axisAngle(vm.Vector3(1, 0, 0), r[0] * math.pi / 180),
      y = vm.Quaternion.axisAngle(vm.Vector3(0, 1, 0), r[1] * math.pi / 180),
      z = vm.Quaternion.axisAngle(vm.Vector3(0, 0, 1), r[2] * math.pi / 180);
  return z * y * x;
}

class _Fbx {
  _Fbx(this.name, this.values, this.children);
  final String name;
  final List<dynamic> values;
  final List<_Fbx> children;
  _Fbx? child(String key) => children.where((n) => n.name == key).firstOrNull;
  List<dynamic> array(String key) {
    final n = child(key);
    if (n == null) return [];
    if (n.values.isNotEmpty && n.values.first is List) {
      return n.values.first as List;
    }
    return n.child('a')?.values ?? [];
  }
}

class _FbxBinary {
  _FbxBinary(this.bytes) : data = ByteData.sublistView(bytes);
  final Uint8List bytes;
  final ByteData data;
  int p = 27, totalNodes = 0;
  late bool wide;
  void need(int count) {
    if (count < 0 || p + count > bytes.length) {
      modelFail('FBX binario truncado.');
    }
  }

  int uint32() {
    need(4);
    final v = data.getUint32(p, Endian.little);
    p += 4;
    return v;
  }

  int integer() {
    if (!wide) return uint32();
    need(8);
    final v = data.getUint64(p, Endian.little);
    p += 8;
    return v;
  }

  List<_Fbx> read() {
    final version = data.getUint32(23, Endian.little);
    if (version < 7000 || version > 7700) {
      modelFail('Versao FBX nao suportada.');
    }
    wide = version >= 7500;
    return nodes(bytes.length, 0);
  }

  List<_Fbx> nodes(int end, int depth) {
    if (depth > 128) modelFail('FBX profundo demais.');
    final out = <_Fbx>[];
    while (p + (wide ? 25 : 13) <= end) {
      final offset = integer(), count = integer(), length = integer();
      need(1);
      final size = bytes[p++];
      if (offset == 0) break;
      if (++totalNodes > 100000 ||
          offset > end ||
          offset <= p ||
          count > 2000000) {
        modelFail('FBX com limites invalidos.');
      }
      need(size);
      final name = utf8.decode(bytes.sublist(p, p + size));
      p += size;
      final propertyEnd = p + length;
      if (propertyEnd > offset) modelFail('Propriedades FBX fora do bloco.');
      final values = <dynamic>[];
      for (var i = 0; i < count; i++) {
        values.add(property());
      }
      if (p != propertyEnd) {
        modelFail('Bloco de propriedades FBX inconsistente.');
      }
      final children = p < offset ? nodes(offset, depth + 1) : <_Fbx>[];
      p = offset;
      out.add(_Fbx(name, values, children));
    }
    return out;
  }

  dynamic property() {
    need(1);
    final type = String.fromCharCode(bytes[p++]);
    if (type == 'S' || type == 'R') {
      final size = uint32();
      need(size);
      final v = bytes.sublist(p, p + size);
      p += size;
      return type == 'S' ? utf8.decode(v, allowMalformed: true) : v;
    }
    if ('fdlibc'.contains(type)) {
      final count = uint32(), encoding = uint32(), length = uint32();
      final stride = {'f': 4, 'd': 8, 'l': 8, 'i': 4, 'b': 1, 'c': 1}[type]!;
      final expected = count * stride;
      if (expected > 32 * 1024 * 1024) modelFail('Array FBX acima de 32 MB.');
      need(length);
      var raw = bytes.sublist(p, p + length);
      p += length;
      if (encoding == 1) {
        final sink = _FbxBytes(expected);
        final decoder = ZLibDecoder().startChunkedConversion(sink);
        decoder.add(raw);
        decoder.close();
        raw = sink.bytes.takeBytes();
      } else if (encoding != 0) {
        modelFail('Compressao FBX nao suportada.');
      }
      if (raw.length != expected) modelFail('Array FBX com tamanho incorreto.');
      final d = ByteData.sublistView(raw);
      return [
        for (var i = 0; i < count; i++)
          switch (type) {
            'f' => d.getFloat32(i * stride, Endian.little),
            'd' => d.getFloat64(i * stride, Endian.little),
            'l' => d.getInt64(i * stride, Endian.little),
            'i' => d.getInt32(i * stride, Endian.little),
            _ => d.getUint8(i),
          },
      ];
    }
    final size = {'Y': 2, 'C': 1, 'I': 4, 'F': 4, 'D': 8, 'L': 8}[type];
    if (size == null) modelFail('Propriedade FBX desconhecida: $type');
    need(size);
    final at = p;
    p += size;
    return switch (type) {
      'Y' => data.getInt16(at, Endian.little),
      'C' => data.getUint8(at),
      'I' => data.getInt32(at, Endian.little),
      'F' => data.getFloat32(at, Endian.little),
      'D' => data.getFloat64(at, Endian.little),
      _ => data.getInt64(at, Endian.little),
    };
  }
}

class _FbxBytes implements Sink<List<int>> {
  _FbxBytes(this.limit);
  final int limit;
  final bytes = BytesBuilder();
  @override
  void add(List<int> data) {
    if (bytes.length + data.length > limit) {
      modelFail('Array FBX descompactado excede o tamanho declarado.');
    }
    bytes.add(data);
  }

  @override
  void close() {}
}

class _FbxAscii {
  _FbxAscii(String source)
    : tokens = RegExp(r'"(?:\\.|[^"\\])*"|;[^\r\n]*|[\r\n]+|[{}:,]|[^\s{}:,]+')
          .allMatches(source)
          .map((m) => m.group(0)!)
          .where((s) => !s.startsWith(';'))
          .toList();
  final List<String> tokens;
  int p = 0, count = 0;
  bool newline(String s) => s.startsWith('\r') || s.startsWith('\n');
  List<_Fbx> read([int depth = 0]) {
    if (depth > 128) modelFail('FBX ASCII profundo demais.');
    final out = <_Fbx>[];
    while (p < tokens.length) {
      if (newline(tokens[p])) {
        p++;
        continue;
      }
      if (tokens[p] == '}') {
        p++;
        break;
      }
      final name = tokens[p++];
      if (p >= tokens.length || tokens[p++] != ':') {
        modelFail('Sintaxe FBX ASCII invalida.');
      }
      if (++count > 100000) modelFail('FBX ASCII com nos demais.');
      final values = <dynamic>[];
      while (p < tokens.length &&
          tokens[p] != '{' &&
          tokens[p] != '}' &&
          !newline(tokens[p])) {
        final v = tokens[p++];
        if (v == ',') continue;
        if (v.startsWith('*')) continue;
        values.add(
          v.startsWith('"')
              ? v.substring(1, v.length - 1).replaceAll(r'\"', '"')
              : num.tryParse(v) ?? v,
        );
      }
      // ASCII arrays may wrap lines after commas.
      if (name == 'a') {
        while (p < tokens.length && tokens[p] != '}') {
          final v = tokens[p++];
          if (v == ',' || newline(v)) continue;
          values.add(num.tryParse(v) ?? modelFail('Array ASCII invalido.'));
        }
      }
      List<_Fbx> children = [];
      if (p < tokens.length && tokens[p] == '{') {
        p++;
        children = read(depth + 1);
      }
      out.add(_Fbx(name, values, children));
    }
    return out;
  }
}
