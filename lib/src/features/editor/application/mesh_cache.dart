import 'dart:io';

import 'package:flutter/foundation.dart';

import '../domain/element3d.dart';
import '../domain/mesh_import.dart';

/// MODELOS 3D IMPORTADOS (OBJ/FBX), lidos uma vez e guardados.
///
/// Mesmo contrato do TextureCache: o pintor pede a malha de forma
/// sincrona; se ainda nao chegou, pinta o solido nativo e a leitura
/// dispara. Quando termina, [revision] sobe e quem escuta repinta. A
/// leitura e o parse rodam fora da thread de UI.
class MeshCache {
  MeshCache._();

  static final MeshCache instance = MeshCache._();

  final ValueNotifier<int> revision = ValueNotifier<int>(0);

  final Map<String, Element3DMesh> _meshes = {};
  final Map<String, MeshImportResult> _results = {};
  final Set<String> _loading = {};
  final Map<String, String> _failed = {};

  Element3DMesh? meshFor(String path) {
    final m = _meshes[path];
    if (m != null) return m;
    if (!_loading.contains(path) && !_failed.containsKey(path)) {
      load(path);
    }
    return null;
  }

  /// Resultado completo (contagem de faces etc.), se ja carregado.
  MeshImportResult? resultFor(String path) => _results[path];

  /// Motivo da falha, se falhou.
  String? errorFor(String path) => _failed[path];

  void put(String path, MeshImportResult r) {
    _meshes[path] = r.mesh;
    _results[path] = r;
    _failed.remove(path);
    revision.value++;
  }

  /// Le e converte o arquivo; lanca [MeshImportException] se nao der.
  Future<MeshImportResult> load(String path) async {
    final pronto = _results[path];
    if (pronto != null) return pronto;
    _loading.add(path);
    try {
      final bytes = await File(path).readAsBytes();
      final ext = path.split('.').last;
      if (ext.toLowerCase() == 'fbx' && looksLikeBinaryFbx(bytes)) {
        throw const MeshImportException(
            'FBX binario nao e suportado: exporte como FBX ASCII ou OBJ.');
      }
      final r = await compute(_parse, (String.fromCharCodes(bytes), ext));
      _meshes[path] = r.mesh;
      _results[path] = r;
      _failed.remove(path);
      revision.value++;
      return r;
    } on MeshImportException catch (e) {
      _failed[path] = e.message;
      rethrow;
    } catch (e) {
      _failed[path] = 'Nao deu para ler o modelo: $e';
      throw MeshImportException(_failed[path]!);
    } finally {
      _loading.remove(path);
    }
  }
}

MeshImportResult _parse((String, String) args) =>
    importMeshText(args.$1, extension: args.$2);
