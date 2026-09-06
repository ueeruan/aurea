import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';

import 'model_asset3d.dart';

class ModelImportException implements Exception {
  const ModelImportException(this.message);
  final String message;
  @override
  String toString() => message;
}

Never modelFail(String message) => throw ModelImportException(message);

/// Bounded parser: malformed assets fail BEFORE the scene is mutated. No
/// arbitrary URI/network access is performed here; the import service supplies
/// only files selected by the user or contained in the model's directory.
ModelAsset3D importGltf3D(
  Uint8List bytes, {
  bool binary = true,
  Map<String, Uint8List> resources = const {},
}) {
  if (bytes.length > 96 * 1024 * 1024) {
    modelFail('Modelo acima de 96 MB. Reduza texturas e geometria.');
  }
  try {
    Map<String, dynamic>? doc;
    Uint8List? bin;
    if (binary) {
      if (bytes.length < 20) modelFail('GLB incompleto.');
      final h = ByteData.sublistView(bytes);
      if (h.getUint32(0, Endian.little) != 0x46546c67 ||
          h.getUint32(4, Endian.little) != 2) {
        modelFail('Somente GLB/glTF 2.0 e suportado.');
      }
      if (h.getUint32(8, Endian.little) != bytes.length) {
        modelFail('Tamanho do GLB invalido ou arquivo incompleto.');
      }
      var p = 12;
      while (p < bytes.length) {
        if (p + 8 > bytes.length) {
          modelFail('Cabecalho de bloco GLB incompleto.');
        }
        final len = h.getUint32(p, Endian.little),
            type = h.getUint32(p + 4, Endian.little);
        if (len % 4 != 0 || p + 8 + len > bytes.length) {
          modelFail('Bloco GLB fora dos limites.');
        }
        final chunk = Uint8List.sublistView(bytes, p + 8, p + 8 + len);
        if (p == 12 && type != 0x4e4f534a) {
          modelFail('O primeiro bloco GLB precisa ser JSON.');
        }
        if (type == 0x4e4f534a) {
          if (doc != null) modelFail('GLB com JSON duplicado.');
          doc = (jsonDecode(utf8.decode(chunk)) as Map).cast<String, dynamic>();
        } else if (type == 0x004e4942) {
          if (bin != null) modelFail('GLB com BIN duplicado.');
          bin = chunk;
        }
        p += 8 + len;
      }
    } else {
      doc = (jsonDecode(utf8.decode(bytes)) as Map).cast<String, dynamic>();
    }
    if (doc == null) modelFail('JSON do modelo ausente.');
    final version = doc['asset']?['version'] as String?;
    if (version != '2.0') modelFail('Somente glTF 2.0 e suportado.');
    return _GltfReader(doc, bin, resources).read();
  } on ModelImportException {
    rethrow;
  } catch (e) {
    modelFail(
      'Modelo invalido: estrutura, indices ou recursos inconsistentes (${e.runtimeType}).',
    );
  }
}

class _GltfReader {
  _GltfReader(this.doc, this.bin, this.resources);
  final Map<String, dynamic> doc;
  final Uint8List? bin;
  final Map<String, Uint8List> resources;
  final warnings = <String>{};
  late final List<Uint8List> buffers;
  final Map<int, List<List<double>>> _accessors = {};
  List<dynamic> list(String key) => doc[key] as List? ?? [];
  Uint8List resource(String uri) {
    if (uri.startsWith('data:')) {
      final result = UriData.parse(uri).contentAsBytes();
      if (result.length > 64 * 1024 * 1024) {
        modelFail('Recurso embutido acima de 64 MB.');
      }
      return result;
    }
    final value = resources[uri] ?? resources[Uri.decodeComponent(uri)];
    if (value == null) {
      modelFail(
        'Arquivo complementar ausente: $uri. Selecione tambem .bin e texturas, ou exporte GLB.',
      );
    }
    return value;
  }

  Uint8List view(int i) {
    final v = list('bufferViews')[i];
    final data = buffers[v['buffer'] as int];
    final offset = v['byteOffset'] as int? ?? 0,
        length = v['byteLength'] as int;
    if (offset < 0 || length < 0 || offset + length > data.length) {
      modelFail('bufferView fora do arquivo.');
    }
    return Uint8List.sublistView(data, offset, offset + length);
  }

  List<List<double>> accessor(int index, {String? type}) {
    final a = list('accessors')[index];
    if (type != null && a['type'] != type) {
      modelFail('Atributo esperado: $type.');
    }
    if (_accessors.containsKey(index)) return _accessors[index]!;
    final count = a['count'] as int;
    if (count < 1 || count > 1000000) {
      modelFail('Accessor vazio ou acima do limite de 1 milhao de valores.');
    }
    final components = {
      'SCALAR': 1,
      'VEC2': 2,
      'VEC3': 3,
      'VEC4': 4,
      'MAT4': 16,
    }[a['type']];
    if (components == null) {
      modelFail('Tipo de accessor nao suportado: ${a['type']}.');
    }
    final component = a['componentType'] as int;
    final size = {
      5120: 1,
      5121: 1,
      5122: 2,
      5123: 2,
      5125: 4,
      5126: 4,
    }[component];
    if (size == null) modelFail('Tipo numerico do accessor invalido.');
    double number(ByteData data, int offset, {bool normalize = true}) {
      num v = switch (component) {
        5120 => data.getInt8(offset),
        5121 => data.getUint8(offset),
        5122 => data.getInt16(offset, Endian.little),
        5123 => data.getUint16(offset, Endian.little),
        5125 => data.getUint32(offset, Endian.little),
        _ => data.getFloat32(offset, Endian.little),
      };
      if (normalize && a['normalized'] == true) {
        v = switch (component) {
          5120 => math.max(v / 127, -1),
          5121 => v / 255,
          5122 => math.max(v / 32767, -1),
          5123 => v / 65535,
          _ => v,
        };
      }
      if (!v.isFinite) modelFail('Modelo contem NaN ou infinito.');
      return v.toDouble();
    }

    final values = List.generate(
      count,
      (_) => List<double>.filled(components, 0),
    );
    if (a['bufferView'] != null) {
      final vi = a['bufferView'] as int;
      final data = ByteData.sublistView(view(vi));
      final offset = a['byteOffset'] as int? ?? 0;
      final stride =
          list('bufferViews')[vi]['byteStride'] as int? ?? size * components;
      if (stride < size * components ||
          stride % size != 0 ||
          offset < 0 ||
          offset + (count - 1) * stride + size * components >
              data.lengthInBytes) {
        modelFail('Accessor ultrapassa o bufferView.');
      }
      for (var i = 0; i < count; i++) {
        for (var c = 0; c < components; c++) {
          values[i][c] = number(data, offset + i * stride + c * size);
        }
      }
    } else if (a['sparse'] == null) {
      modelFail('Accessor sem buffer nem dados sparse.');
    }
    final sparse = a['sparse'];
    if (sparse != null) {
      final n = sparse['count'] as int;
      if (n < 0 || n > count) modelFail('Contagem sparse invalida.');
      final indices = sparse['indices'], vv = sparse['values'];
      final ct = indices['componentType'] as int;
      final step = {5121: 1, 5123: 2, 5125: 4}[ct];
      if (step == null) modelFail('Indices sparse invalidos.');
      final ix = ByteData.sublistView(view(indices['bufferView'] as int));
      final va = ByteData.sublistView(view(vv['bufferView'] as int));
      final io = indices['byteOffset'] as int? ?? 0,
          vo = vv['byteOffset'] as int? ?? 0;
      if (io < 0 ||
          vo < 0 ||
          io + n * step > ix.lengthInBytes ||
          vo + n * components * size > va.lengthInBytes) {
        modelFail('Sparse ultrapassa o bufferView.');
      }
      var previous = -1;
      for (var i = 0; i < n; i++) {
        final at = io + i * step;
        final target = ct == 5121
            ? ix.getUint8(at)
            : ct == 5123
            ? ix.getUint16(at, Endian.little)
            : ix.getUint32(at, Endian.little);
        if (target <= previous || target >= count) {
          modelFail('Indices sparse fora de ordem ou limites.');
        }
        previous = target;
        for (var c = 0; c < components; c++) {
          values[target][c] = number(va, vo + (i * components + c) * size);
        }
      }
    }
    return _accessors[index] = values;
  }

  ModelAsset3D read() {
    const supported = {
      'KHR_materials_unlit',
      'KHR_mesh_quantization',
      'KHR_texture_transform',
    };
    for (final e in list('extensionsRequired')) {
      if (!supported.contains(e)) {
        modelFail(
          'Extensao obrigatoria nao suportada: $e. Exporte GLB sem compressao Draco/Meshopt e com texturas PNG/JPEG.',
        );
      }
    }
    for (final e in list('extensionsUsed')) {
      if (!supported.contains(e)) {
        warnings.add('Extensao opcional nao aplicada: $e');
      }
    }
    buffers = [
      for (var i = 0; i < list('buffers').length; i++)
        list('buffers')[i]['uri'] == null
            ? (i == 0 && bin != null
                  ? bin!
                  : modelFail('Buffer binario ausente.'))
            : resource(list('buffers')[i]['uri'] as String),
    ];
    for (var i = 0; i < buffers.length; i++) {
      if (buffers[i].length < (list('buffers')[i]['byteLength'] as int)) {
        modelFail('Buffer incompleto.');
      }
    }
    final rawNodes = list('nodes');
    if (rawNodes.length > 4096) modelFail('Modelo acima de 4096 nos.');
    final parents = <int, int>{};
    for (var i = 0; i < rawNodes.length; i++) {
      for (final child in rawNodes[i]['children'] as List? ?? []) {
        if (child is! int ||
            child < 0 ||
            child >= rawNodes.length ||
            parents.containsKey(child)) {
          modelFail('Hierarquia com filho invalido ou dois pais.');
        }
        parents[child] = i;
      }
    }
    for (var i = 0; i < rawNodes.length; i++) {
      var p = i;
      final visited = <int>{};
      while (parents.containsKey(p)) {
        if (!visited.add(p) || visited.length > 256) {
          modelFail('Hierarquia ciclica ou profunda demais.');
        }
        p = parents[p]!;
      }
    }
    final active = <int>{};
    void visit(int i) {
      if (!active.add(i)) return;
      for (final c in rawNodes[i]['children'] as List? ?? []) {
        visit(c as int);
      }
    }

    final scenes = list('scenes');
    final roots = scenes.isEmpty
        ? [
            for (var i = 0; i < rawNodes.length; i++)
              if (!parents.containsKey(i)) i,
          ]
        : (scenes[doc['scene'] as int? ?? 0]['nodes'] as List? ?? []);
    for (final r in roots) {
      visit(r as int);
    }
    final nodes = <Map<String, dynamic>>[];
    for (var i = 0; i < rawNodes.length; i++) {
      final n = rawNodes[i];
      List<double> vec(String key, List<double> fallback) {
        final value = modelDoubles(n[key] ?? fallback);
        if (value.length != fallback.length || value.any((v) => !v.isFinite)) {
          modelFail('Transformacao de no invalida.');
        }
        return value;
      }

      nodes.add({
        'name': n['name'] as String? ?? 'No ${i + 1}',
        if (parents.containsKey(i)) 'parent': parents[i],
        if (n['skin'] != null) 'skin': n['skin'],
        if (n['matrix'] != null) 'matrix': vec('matrix', List.filled(16, 0)),
        'translation': vec('translation', [0, 0, 0]),
        'rotation': vec('rotation', [0, 0, 0, 1]),
        'scale': vec('scale', [1, 1, 1]),
        'weights':
            n['weights'] ??
            (n['mesh'] == null
                ? []
                : list('meshes')[n['mesh'] as int]['weights'] ?? []),
      });
    }
    final skins = <Map<String, dynamic>>[];
    for (final s in list('skins')) {
      final joints = (s['joints'] as List).cast<int>();
      if (joints.isEmpty ||
          joints.length > 256 ||
          joints.toSet().length != joints.length ||
          joints.any((j) => j < 0 || j >= nodes.length)) {
        modelFail('Skin invalido ou acima de 256 ossos.');
      }
      final inverse = s['inverseBindMatrices'] == null
          ? [
              for (final _ in joints)
                [
                  1.0,
                  0.0,
                  0.0,
                  0.0,
                  0.0,
                  1.0,
                  0.0,
                  0.0,
                  0.0,
                  0.0,
                  1.0,
                  0.0,
                  0.0,
                  0.0,
                  0.0,
                  1.0,
                ],
            ]
          : accessor(s['inverseBindMatrices'] as int, type: 'MAT4');
      if (inverse.length != joints.length) {
        modelFail('Inverse bind matrices nao correspondem aos ossos.');
      }
      skins.add({'joints': joints, 'inverseBind': inverse});
    }
    final materials = <Map<String, dynamic>>[];
    for (final m in list('materials')) {
      final pbr = m['pbrMetallicRoughness'] ?? {};
      final tex = pbr['baseColorTexture'];
      String? image;
      var wrapS = 10497, wrapT = 10497;
      if (tex != null) {
        final td = list('textures')[tex['index'] as int];
        if (td['sampler'] != null) {
          final sampler = list('samplers')[td['sampler'] as int];
          wrapS = sampler['wrapS'] as int? ?? 10497;
          wrapT = sampler['wrapT'] as int? ?? 10497;
        }
        final im = list('images')[td['source'] as int];
        final pixels = im['uri'] != null
            ? resource(im['uri'] as String)
            : view(im['bufferView'] as int);
        image =
            'data:${im['mimeType'] ?? 'application/octet-stream'};base64,${base64Encode(pixels)}';
      }
      if (m['normalTexture'] != null ||
          m['occlusionTexture'] != null ||
          pbr['metallicRoughnessTexture'] != null ||
          m['emissiveTexture'] != null) {
        warnings.add(
          'Mapas normal/oclusao/metal-rugosidade/emissivo nao sao aplicados; fatores e textura de cor preservados.',
        );
      }
      final emissive = modelDoubles(m['emissiveFactor'] ?? [0, 0, 0]);
      materials.add({
        'name': m['name'] ?? 'Material ${materials.length + 1}',
        'color': pbr['baseColorFactor'] ?? [1, 1, 1, 1],
        'metallic': pbr['metallicFactor'] ?? 1,
        'roughness': pbr['roughnessFactor'] ?? 1,
        'emissive': emissive.reduce(math.max),
        'alpha': m['alphaMode'] ?? 'OPAQUE',
        'cutoff': m['alphaCutoff'] ?? .5,
        'doubleSided': m['doubleSided'] ?? false,
        'unlit': m['extensions']?['KHR_materials_unlit'] != null,
        'image': ?image,
        'wrapS': wrapS,
        'wrapT': wrapT,
      });
    }
    final primitives = <Map<String, dynamic>>[];
    var triangleCount = 0;
    for (final ni in active) {
      final n = rawNodes[ni];
      if (n['mesh'] == null) continue;
      for (final p in list('meshes')[n['mesh'] as int]['primitives'] as List) {
        final mode = p['mode'] as int? ?? 4;
        if (mode < 4 || mode > 6) {
          warnings.add('Primitivas de linhas/pontos foram ignoradas.');
          continue;
        }
        final attributes = p['attributes'];
        if (attributes['COLOR_0'] != null) {
          warnings.add(
            'Cores por vertice nao aplicadas nesta versao; material e textura de cor preservados.',
          );
        }
        final positions = accessor(attributes['POSITION'] as int, type: 'VEC3');
        final normals = attributes['NORMAL'] == null
            ? null
            : accessor(attributes['NORMAL'] as int, type: 'VEC3');
        final mi = p['material'] as int? ?? -1;
        final texInfo = mi < 0
            ? null
            : list(
                'materials',
              )[mi]['pbrMetallicRoughness']?['baseColorTexture'];
        final transform = texInfo?['extensions']?['KHR_texture_transform'];
        final uvSet = transform?['texCoord'] ?? texInfo?['texCoord'] ?? 0;
        var uv = attributes['TEXCOORD_$uvSet'] == null
            ? null
            : accessor(attributes['TEXCOORD_$uvSet'] as int, type: 'VEC2');
        if (texInfo != null && uv == null) {
          modelFail('Textura exige TEXCOORD_$uvSet ausente.');
        }
        if (uv != null && transform != null) {
          final offset = modelDoubles(transform['offset'] ?? [0, 0]),
              scale = modelDoubles(transform['scale'] ?? [1, 1]);
          final r = (transform['rotation'] as num? ?? 0).toDouble();
          uv = [
            for (final v in uv)
              [
                offset[0] +
                    math.cos(r) * v[0] * scale[0] -
                    math.sin(r) * v[1] * scale[1],
                offset[1] +
                    math.sin(r) * v[0] * scale[0] +
                    math.cos(r) * v[1] * scale[1],
              ],
          ];
        }
        List<List<int>>? joints;
        List<List<double>>? weights;
        final si = n['skin'] as int?;
        if (si != null) {
          if (si < 0 || si >= skins.length) modelFail('Skin inexistente.');
          if (attributes['JOINTS_0'] == null ||
              attributes['WEIGHTS_0'] == null) {
            modelFail('Malha com skin sem pesos/ossos.');
          }
          joints = List.generate(positions.length, (_) => <int>[]);
          weights = List.generate(positions.length, (_) => <double>[]);
          for (var set = 0; set < 2; set++) {
            if (attributes['JOINTS_$set'] == null) break;
            final js = accessor(attributes['JOINTS_$set'] as int, type: 'VEC4');
            final ws = accessor(
              attributes['WEIGHTS_$set'] as int,
              type: 'VEC4',
            );
            if (js.length != positions.length ||
                ws.length != positions.length) {
              modelFail('Pesos nao correspondem aos vertices.');
            }
            for (var i = 0; i < positions.length; i++) {
              for (var j = 0; j < 4; j++) {
                final joint = js[i][j];
                if (joint != joint.roundToDouble() ||
                    joint < 0 ||
                    joint >= (skins[si]['joints'] as List).length ||
                    ws[i][j] < 0) {
                  modelFail('Peso ou indice de osso invalido.');
                }
                joints[i].add(joint.toInt());
                weights[i].add(ws[i][j]);
              }
            }
          }
          if (attributes['JOINTS_2'] != null) {
            modelFail(
              'Mais de 8 influencias por vertice. Reduza para 8 ao exportar.',
            );
          }
        }
        if ((normals != null && normals.length != positions.length) ||
            (uv != null && uv.length != positions.length)) {
          modelFail('Atributos com contagens diferentes.');
        }
        if (p['indices'] != null) {
          final description = list('accessors')[p['indices'] as int];
          if (![5121, 5123, 5125].contains(description['componentType']) ||
              description['normalized'] == true) {
            modelFail('Indices devem usar inteiros sem normalizacao.');
          }
        }
        final raw = p['indices'] == null
            ? [for (var i = 0; i < positions.length; i++) i]
            : [
                for (final v in accessor(p['indices'] as int, type: 'SCALAR'))
                  v.single.toInt(),
              ];
        if (raw.any((v) => v < 0 || v >= positions.length)) {
          modelFail('Indice de triangulo inexistente.');
        }
        final indices = <int>[];
        if (mode == 4) {
          if (raw.length % 3 != 0) {
            modelFail('Indices de triangulo incompletos.');
          }
          indices.addAll(raw);
        } else {
          for (var i = 2; i < raw.length; i++) {
            final a = mode == 6 ? raw[0] : raw[i - 2],
                b = raw[i - 1],
                c = raw[i];
            if (a == b || b == c || a == c) continue;
            indices.addAll(mode == 5 && i.isOdd ? [b, a, c] : [a, b, c]);
          }
        }
        triangleCount += indices.length ~/ 3;
        if (triangleCount > 150000) {
          modelFail(
            'Acima de 150 mil triangulos: simplifique o modelo antes de importar.',
          );
        }
        final targets = <Map<String, dynamic>>[];
        for (final target in p['targets'] as List? ?? []) {
          if (targets.length >= 64) modelFail('Mais de 64 morph targets.');
          final tm = <String, dynamic>{};
          for (final entry in {
            'POSITION': 'positions',
            'NORMAL': 'normals',
          }.entries) {
            if (target[entry.key] == null) continue;
            final a = accessor(target[entry.key] as int, type: 'VEC3');
            if (a.length != positions.length) {
              modelFail('Morph target com contagem invalida.');
            }
            tm[entry.value] = a;
          }
          targets.add(tm);
        }
        if ((nodes[ni]['weights'] as List).isEmpty && targets.isNotEmpty) {
          nodes[ni]['weights'] = List.filled(targets.length, 0.0);
        }
        primitives.add({
          'node': ni,
          'positions': positions,
          'indices': indices,
          'material': mi,
          'normals': ?normals,
          'uv': ?uv,
          'joints': ?joints,
          'weights': ?weights,
          if (targets.isNotEmpty) 'targets': targets,
        });
      }
    }
    if (primitives.isEmpty || triangleCount == 0) {
      modelFail('A cena ativa nao contem triangulos.');
    }
    if (triangleCount > 30000) {
      warnings.add(
        'Modelo pesado: $triangleCount triangulos; reproducao pode perder fluidez.',
      );
    }
    final clips = <Map<String, dynamic>>[];
    for (final clip in list('animations')) {
      final channels = <Map<String, dynamic>>[];
      var duration = 0.0;
      for (final channel in clip['channels'] as List) {
        final target = channel['target'], ni = target['node'] as int?;
        final path = target['path'] as String;
        if (ni == null ||
            !['translation', 'rotation', 'scale', 'weights'].contains(path)) {
          warnings.add('Canal de animacao nao suportado.');
          continue;
        }
        if (nodes[ni]['matrix'] != null) {
          modelFail('No animado deve usar TRS, nao matrix.');
        }
        final sampler = clip['samplers'][channel['sampler'] as int];
        final interpolation = sampler['interpolation'] as String? ?? 'LINEAR';
        if (!['LINEAR', 'STEP', 'CUBICSPLINE'].contains(interpolation)) {
          modelFail('Interpolacao desconhecida: $interpolation.');
        }
        final times = [
          for (final v in accessor(sampler['input'] as int, type: 'SCALAR'))
            v.single,
        ];
        for (var j = 0; j < times.length; j++) {
          if (times[j] < 0 || (j > 0 && times[j] <= times[j - 1])) {
            modelFail('Tempos de animacao invalidos.');
          }
        }
        var values = accessor(
          sampler['output'] as int,
          type: path == 'weights'
              ? 'SCALAR'
              : path == 'rotation'
              ? 'VEC4'
              : 'VEC3',
        );
        final multiplier = interpolation == 'CUBICSPLINE' ? 3 : 1;
        if (path == 'weights') {
          final size = (nodes[ni]['weights'] as List).length;
          if (size == 0 || values.length != times.length * multiplier * size) {
            modelFail('Animacao de morph invalida.');
          }
          values = [
            for (var j = 0; j < values.length; j += size)
              [for (var k = 0; k < size; k++) values[j + k].single],
          ];
        }
        if (values.length != times.length * multiplier) {
          modelFail('Keyframes com contagem invalida.');
        }
        duration = math.max(duration, times.last);
        channels.add({
          'node': ni,
          'path': path,
          'times': times,
          'values': values,
          'interpolation': interpolation,
        });
      }
      if (channels.isNotEmpty) {
        clips.add({
          'name': clip['name'] ?? 'Clipe ${clips.length + 1}',
          'duration': duration,
          'channels': channels,
        });
      }
    }
    return ModelAsset3D({
      'version': 1,
      'format': 'gltf2',
      'name': scenes.isNotEmpty
          ? scenes[doc['scene'] as int? ?? 0]['name'] ?? 'Modelo glTF'
          : 'Modelo glTF',
      'nodes': nodes,
      'primitives': primitives,
      'skins': skins,
      'clips': clips,
      'materials': materials,
      'warnings': warnings.toList(),
    });
  }
}
