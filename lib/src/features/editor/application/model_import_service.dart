import 'dart:convert';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';

import '../domain/malha_importada.dart';
import '../domain/model_asset3d.dart';
import '../domain/model_import3d.dart';
import '../domain/fbx_import3d.dart';
import '../domain/obj_import3d.dart';

// Parsing and file access run off the UI isolate. Do not run the native
// analyzer here: it blocks the UI and its recommendations are not consumed
// by the active renderer.
Future<ModelAsset3D> readModel3DFiles(List<String> paths) =>
    Isolate.run(() async {
      final asset = await _read(paths);
      // Solda, cache de vertices, busca e niveis de detalhe em C++ — aqui,
      // no isolate da importacao, uma vez so (ver malha_importada.dart).
      otimizarMalhasImportadas(asset.data);
      return asset;
    });

Future<ModelAsset3D> _read(List<String> paths) async {
  final models = paths
      .where(
        (p) =>
            RegExp(r'\.(glb|gltf|obj|fbx)$', caseSensitive: false).hasMatch(p),
      )
      .toList();
  if (models.length != 1) {
    modelFail(
      'Selecione um modelo GLB, glTF, OBJ ou FBX e seus arquivos complementares.',
    );
  }
  final file = File(models.single);
  final root = await file.parent.resolveSymbolicLinks();
  final resources = <String, Uint8List>{};
  Future<Uint8List> read(File f) async {
    return f.readAsBytes();
  }

  final bytes = await read(file);
  Future<Uint8List> resolve(String uri, {Directory? relativeTo}) async {
    if (resources.containsKey(uri)) return resources[uri]!;
    final normalized = Uri.decodeComponent(uri).replaceAll('\\', '/');
    final parsed = Uri.tryParse(normalized);
    if (parsed == null ||
        parsed.hasScheme ||
        normalized.startsWith('/') ||
        normalized.split('/').contains('..')) {
      modelFail(
        'Caminho externo recusado: $uri. Coloque os recursos na pasta do modelo.',
      );
    }
    File candidate = File.fromUri(
      (relativeTo ?? file.parent).uri.resolve(normalized),
    );
    if (!await candidate.exists()) {
      final matching = paths
          .where(
            (p) =>
                p.replaceAll('\\', '/').split('/').last ==
                normalized.split('/').last,
          )
          .toList();
      if (matching.length != 1) {
        modelFail(
          'Arquivo complementar ausente: $uri. Selecione o recurso junto com o modelo.',
        );
      }
      candidate = File(matching.single);
    }
    final resolved = await candidate.resolveSymbolicLinks();
    final isSelected = paths.any(
      (p) => File(p).absolute.path == candidate.absolute.path,
    );
    if (!isSelected &&
        !resolved.toLowerCase().startsWith(
          '${root.toLowerCase()}${Platform.pathSeparator}',
        )) {
      modelFail(
        'Recurso fora da pasta do modelo. Selecione esse arquivo explicitamente.',
      );
    }
    return resources[uri] = await read(candidate);
  }

  final lower = file.path.toLowerCase();
  if (lower.endsWith('.fbx')) {
    // As imagens selecionadas junto entram pelo nome: e assim que o FBX
    // acha a textura base.
    for (final p in paths) {
      if (!RegExp(
        r'.(png|jpe?g|webp|bmp)$',
        caseSensitive: false,
      ).hasMatch(p)) {
        continue;
      }
      final f = File(p);
      resources[f.uri.pathSegments.last] = await read(f);
    }
    return importFbx3D(
      bytes,
      name: file.uri.pathSegments.last,
      resources: resources,
    );
  }
  if (lower.endsWith('.obj')) {
    final source = utf8.decode(bytes);
    // MTL E TEXTURA SAO OPCIONAIS: o que faltar vira aviso no importador,
    // e a geometria entra com material padrao.
    Future<Uint8List?> talvez(String uri, {Directory? relativeTo}) async {
      try {
        return await resolve(uri, relativeTo: relativeTo);
      } on ModelImportException {
        return null;
      }
    }

    for (final line in const LineSplitter().convert(source)) {
      if (!line.trimLeft().startsWith('mtllib ')) continue;
      final mtlName = line.trim().substring(7).trim();
      final mtlBytes = await talvez(mtlName);
      if (mtlBytes == null) continue;
      final mtl = utf8.decode(mtlBytes, allowMalformed: true);
      for (final entry in const LineSplitter().convert(mtl)) {
        if (!entry.trimLeft().startsWith('map_Kd ')) continue;
        final name = texturaDoMapKd(
          entry.trim().substring(7).trim().split(RegExp(r'\s+')),
        );
        if (name == null) continue;
        final mtlDirectory = Directory.fromUri(
          file.parent.uri.resolve(mtlName).resolve('.'),
        );
        await talvez(name, relativeTo: mtlDirectory);
      }
    }
    return importObj3D(
      source,
      name: file.uri.pathSegments.last,
      resources: resources,
    );
  }
  Map<String, dynamic>? doc;
  if (lower.endsWith('.gltf')) {
    doc = (jsonDecode(utf8.decode(bytes)) as Map).cast<String, dynamic>();
  } else if (bytes.length >= 20) {
    final data = ByteData.sublistView(bytes);
    final length = data.getUint32(12, Endian.little);
    if (20 + length <= bytes.length &&
        data.getUint32(16, Endian.little) == 0x4e4f534a) {
      doc = (jsonDecode(utf8.decode(bytes.sublist(20, 20 + length))) as Map)
          .cast<String, dynamic>();
    }
  }
  if (doc != null) {
    for (final entry in [
      ...doc['buffers'] as List? ?? [],
      ...doc['images'] as List? ?? [],
    ]) {
      final uri = entry['uri'] as String?;
      if (uri != null && !uri.startsWith('data:')) await resolve(uri);
    }
  }
  final asset = importGltf3D(
    bytes,
    binary: lower.endsWith('.glb'),
    resources: resources,
  );
  // O NOME DO ARQUIVO quando a cena nao tem nome de verdade: quase todo
  // exportador chama a cena de "Scene", e a camada nascia com esse nome.
  final nome = (asset.data['name'] as String? ?? '').trim();
  if (nome.isEmpty || nome == 'Scene' || nome == 'Modelo glTF') {
    final arquivo = file.uri.pathSegments.last;
    final ponto = arquivo.lastIndexOf('.');
    asset.data['name'] = ponto > 0 ? arquivo.substring(0, ponto) : arquivo;
  }
  return asset;
}
