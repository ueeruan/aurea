import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import '../../domain/element3d.dart';
import '../../domain/scene3d.dart';

/// Portable bridge for existing procedural/OBJ/GLB project geometry. Called on
/// an asset isolate, never from paint/build. Filament owns glTF GPU resources.
Uint8List encodeNodeGlb(SceneNode node) {
  final frame = node.modelAsset?.evaluate(Duration.zero, node.modelMotion);
  final mesh = frame?.mesh ?? node.mesh ?? element3DMesh(node.kind);
  final bin = BytesBuilder(copy: false);
  final views = <Map<String, Object>>[], accessors = <Map<String, Object>>[];
  int addBytes(Uint8List bytes) {
    final index = views.length;
    views.add({
      'buffer': 0,
      'byteOffset': bin.length,
      'byteLength': bytes.length,
    });
    bin.add(bytes);
    if (bin.length % 4 != 0) bin.add(Uint8List(4 - bin.length % 4));
    return index;
  }

  int floats(
    List<double> values,
    String type,
    int components, {
    bool bounds = false,
  }) {
    final accessor = <String, Object>{
      'bufferView': addBytes(Float32List.fromList(values).buffer.asUint8List()),
      'componentType': 5126,
      'count': values.length ~/ components,
      'type': type,
    };
    if (bounds) {
      final lo = List.filled(components, double.infinity),
          hi = List.filled(components, double.negativeInfinity);
      for (var i = 0; i < values.length; i++) {
        final j = i % components;
        if (values[i] < lo[j]) lo[j] = values[i];
        if (values[i] > hi[j]) hi[j] = values[i];
      }
      accessor['min'] = lo;
      accessor['max'] = hi;
    }
    accessors.add(accessor);
    return accessors.length - 1;
  }

  final groups = <Material3D, List<int>>{};
  for (var f = 0; f < mesh.faces.length; f++) {
    var material = node.useModelMaterials && frame != null
        ? frame.materials[f]
        : node.material;
    final image = material.faceImagePaths[f];
    if (image != null) material = material.copyWith(imagePath: image);
    (groups[material] ??= []).add(f);
  }
  final materials = <Map<String, Object>>[],
      primitives = <Map<String, Object>>[];
  final images = <Map<String, Object>>[], textures = <Map<String, Object>>[];
  final textureByPath = <String, int>{};
  final extensions = <String>{};
  for (final entry in groups.entries) {
    final positions = <double>[], normals = <double>[], uvs = <double>[];
    final indices = <int>[];
    // Preserve hard edges while sharing vertices within a material. Imported
    // smooth normals keep their seams; face normals split procedural edges.
    final vertexIndices = <(int, double, double, double), int>{};
    for (final faceIndex in entry.value) {
      final face = mesh.faces[faceIndex];
      if (face.length < 3) continue;
      Vec3 point(int i) {
        final p = mesh.verts[i];
        return Vec3(p[0], p[1], p[2]);
      }

      final normal = (point(face[1]) - point(face[0]))
          .cross(point(face[2]) - point(face[0]))
          .normalized;
      for (var i = 1; i + 1 < face.length; i++) {
        for (final v in [face[0], face[i], face[i + 1]]) {
          final p = point(v);
          final n = frame?.normals[v] ?? normal;
          final key = (v, n.x, n.y, n.z);
          final existing = vertexIndices[key];
          if (existing != null) {
            indices.add(existing);
            continue;
          }
          final index = positions.length ~/ 3;
          vertexIndices[key] = index;
          indices.add(index);
          positions.addAll([p.x, p.y, p.z]);
          normals.addAll([n.x, n.y, n.z]);
          final uv = frame?.uvs[v];
          uvs.addAll(
            uv == null ? [(p.x + 1) / 2, (1 - p.y) / 2] : [uv.dx, uv.dy],
          );
        }
      }
    }
    if (positions.isEmpty) continue;
    final m = entry.key, c = m.baseColor;
    final pbr = <String, Object>{
      'baseColorFactor': [c.r, c.g, c.b, c.a * m.opacity],
      'metallicFactor': m.metallic,
      'roughnessFactor': m.roughness,
    };
    final path = m.imagePath;
    if (path != null && path.isNotEmpty) {
      var textureIndex = textureByPath[path];
      if (textureIndex == null) {
        final bytes = path.startsWith('data:')
            ? Uri.parse(path).data!.contentAsBytes()
            : File(path).readAsBytesSync();
        final png = bytes.length > 4 && bytes[0] == 137 && bytes[1] == 80;
        final jpg = bytes.length > 2 && bytes[0] == 255 && bytes[1] == 216;
        if (!png && !jpg) {
          throw UnsupportedError('Filament bridge requires PNG/JPEG textures');
        }
        images.add({
          'bufferView': addBytes(bytes),
          'mimeType': png ? 'image/png' : 'image/jpeg',
        });
        textures.add({'source': images.length - 1, 'sampler': 0});
        textureIndex = textures.length - 1;
        textureByPath[path] = textureIndex;
      }
      pbr['baseColorTexture'] = {'index': textureIndex};
    }
    final material = <String, Object>{
      'name': m.name,
      'pbrMetallicRoughness': pbr,
      'doubleSided': m.doubleSided,
      'alphaMode': m.kind == MaterialKind.cutout
          ? 'MASK'
          : (m.isTransparent ? 'BLEND' : 'OPAQUE'),
      'alphaCutoff': m.alphaCutoff,
      'emissiveFactor': [c.r * m.emissive, c.g * m.emissive, c.b * m.emissive],
    };
    if (m.kind == MaterialKind.unlit) {
      extensions.add('KHR_materials_unlit');
      material['extensions'] = {'KHR_materials_unlit': <String, Object>{}};
    }
    materials.add(material);
    final shortIndices = positions.length ~/ 3 <= 65536;
    final indexBytes = shortIndices
        ? Uint16List.fromList(indices).buffer.asUint8List()
        : Uint32List.fromList(indices).buffer.asUint8List();
    final indexAccessor = accessors.length;
    accessors.add({
      'bufferView': addBytes(indexBytes),
      'componentType': shortIndices ? 5123 : 5125,
      'count': indices.length,
      'type': 'SCALAR',
    });
    primitives.add({
      'indices': indexAccessor,
      'attributes': {
        'POSITION': floats(positions, 'VEC3', 3, bounds: true),
        'NORMAL': floats(normals, 'VEC3', 3),
        'TEXCOORD_0': floats(uvs, 'VEC2', 2),
      },
      'material': materials.length - 1,
    });
  }
  if (primitives.isEmpty) throw StateError('Empty mesh');
  final document = {
    'asset': {'version': '2.0', 'generator': 'Aurea scene bridge'},
    'scene': 0,
    'scenes': [
      {
        'nodes': [0],
      },
    ],
    'nodes': [
      {'mesh': 0, 'name': node.id},
    ],
    'meshes': [
      {'primitives': primitives},
    ],
    'materials': materials,
    'buffers': [
      {'byteLength': bin.length},
    ],
    'bufferViews': views,
    'accessors': accessors,
    if (images.isNotEmpty) 'images': images,
    if (textures.isNotEmpty) 'textures': textures,
    if (textures.isNotEmpty)
      'samplers': [
        {'magFilter': 9729, 'minFilter': 9987, 'wrapS': 33071, 'wrapT': 33071},
      ],
    if (extensions.isNotEmpty) 'extensionsUsed': extensions.toList(),
  };
  final json = utf8.encode(jsonEncode(document));
  final paddedJson = (json.length + 3) & ~3;
  final result = Uint8List(12 + 8 + paddedJson + 8 + bin.length);
  final header = ByteData.sublistView(result);
  header.setUint32(0, 0x46546c67, Endian.little);
  header.setUint32(4, 2, Endian.little);
  header.setUint32(8, result.length, Endian.little);
  header.setUint32(12, paddedJson, Endian.little);
  header.setUint32(16, 0x4e4f534a, Endian.little);
  result.fillRange(20, 20 + paddedJson, 32);
  result.setRange(20, 20 + json.length, json);
  header.setUint32(20 + paddedJson, bin.length, Endian.little);
  header.setUint32(24 + paddedJson, 0x004e4942, Endian.little);
  result.setRange(28 + paddedJson, result.length, bin.takeBytes());
  return result;
}
