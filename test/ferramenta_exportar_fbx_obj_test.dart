import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/fbx_import3d.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';

/// FERRAMENTA DE MESA (nao e teste): decodifica um FBX pesado pelo
/// importador do app, sem o teto de triangulos, e escreve a malha como
/// OBJ com UV em build/ — para ser decimada fora e voltar como asset
/// leve. So roda com a variavel AUREA_FBX apontando para o arquivo.
///
///   AUREA_FBX=assets/models/monolito/astronauta.fbx flutter test test/ferramenta_exportar_fbx_obj_test.dart
void main() {
  test('exporta a malha de um FBX para OBJ', () {
    final caminho = Platform.environment['AUREA_FBX'];
    if (caminho == null) return;
    final bytes = File(caminho).readAsBytesSync();
    var asset = importFbx3D(bytes, name: 'exportar', maxTriangles: 4000000);
    // AUREA_FBX_SEM_SKIN=1: ignora o esqueleto e usa so as transformacoes
    // dos nos (a pose crua da malha).
    if (Platform.environment['AUREA_FBX_SEM_SKIN'] == '1') {
      asset = ModelAsset3D({
        ...asset.data,
        'skins': const [],
        'nodes': [
          for (final n in asset.data['nodes'] as List)
            {...(n as Map), 'skin': null},
        ],
        'primitives': [
          for (final p in asset.data['primitives'] as List)
            {...(p as Map), 'joints': null, 'weights': null},
        ],
      });
    }
    final frame = asset.evaluate(Duration.zero, const ModelMotion3D(clip: -1));
    final malha = frame.mesh;
    final lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
    for (final v in malha.verts) {
      for (var i = 0; i < 3; i++) {
        if (v[i] < lo[i]) lo[i] = v[i];
        if (v[i] > hi[i]) hi[i] = v[i];
      }
    }
    final saida = File('build/fbx_exportado.obj');
    final b = StringBuffer('mtllib fbx_exportado.mtl\n');
    for (final v in malha.verts) {
      b.writeln('v ${v[0]} ${v[1]} ${v[2]}');
    }
    var comUv = 0;
    for (var i = 0; i < malha.verts.length; i++) {
      final uv = i < frame.uvs.length ? frame.uvs[i] : null;
      if (uv != null) comUv++;
      b.writeln('vt ${uv?.dx ?? 0} ${1 - (uv?.dy ?? 0)}');
    }
    b.writeln('usemtl base');
    var faces = 0;
    for (final f in malha.faces) {
      for (var i = 1; i < f.length - 1; i++) {
        final a = f[0] + 1, c = f[i] + 1, d = f[i + 1] + 1;
        b.writeln('f $a/$a $c/$c $d/$d');
        faces++;
      }
    }
    saida.writeAsStringSync(b.toString());
    // ignore: avoid_print
    print('exportado: ${malha.verts.length} vertices ($comUv com uv), $faces faces, '
        'caixa min=$lo max=$hi, materiais=${(asset.data['materials'] as List).length}, '
        'avisos=${asset.warnings}');
  });
}
