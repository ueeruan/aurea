import 'dart:io';

import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/model_import_service.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';

/// OS MODELOS EMPACOTADOS DO MONOLITO importam pelo mesmo caminho que o
/// usuario usa (readModel3DFiles), abaixo dos limites do app, com as
/// texturas resolvidas. E o teste que impede um asset quebrado de ir
/// para o aparelho.
void main() {
  const pasta = 'assets/models/monolito';

  ({Object? min, Object? max}) caixa(ModelAsset3D m) {
    final f = m.evaluate(Duration.zero, const ModelMotion3D(clip: -1));
    final lo = [1e9, 1e9, 1e9], hi = [-1e9, -1e9, -1e9];
    for (final v in f.mesh.verts) {
      for (var i = 0; i < 3; i++) {
        if (v[i] < lo[i]) lo[i] = v[i];
        if (v[i] > hi[i]) hi[i] = v[i];
      }
    }
    return (min: lo, max: hi);
  }

  void relatorio(String nome, ModelAsset3D m) {
    final c = caixa(m);
    // ignore: avoid_print
    print('$nome: ${m.triangleCount} tri, materiais=${(m.data['materials'] as List).map((x) => '${x['name']}:${x['image'] != null ? 'img' : '-'}').join(',')}, '
        'clips=${m.clipNames} ${[for (final k in m.clips) k['duration']]}, joints=${m.joints.length}, '
        'caixa min=${c.min} max=${c.max}, avisos=${m.warnings}');
  }

  test('portal (OBJ voxel) importa com a paleta', () async {
    final m = await readModel3DFiles(['$pasta/portal.obj', '$pasta/portal.mtl', '$pasta/NetherPortal.png']);
    relatorio('portal', m);
    expect(m.triangleCount, lessThan(150000));
    expect((m.data['materials'] as List).any((x) => x['image'] != null), isTrue);
  });

  test('arvore decimada importa abaixo do limite', () async {
    final m = await readModel3DFiles(['$pasta/arvore.obj', '$pasta/arvore.mtl', '$pasta/arvore.jpg']);
    relatorio('arvore', m);
    expect(m.triangleCount, lessThan(150000));
    expect(m.triangleCount, greaterThan(10000));
  });

  test('astronauta (OBJ decimado do FBX) importa com a textura', () async {
    final m = await readModel3DFiles(['$pasta/astronauta.obj', '$pasta/astronauta.mtl', '$pasta/Astronaut_BaseColornew.jpeg']);
    relatorio('astronauta', m);
    expect(m.triangleCount, lessThan(150000));
    expect((m.data['materials'] as List).any((x) => x['image'] != null), isTrue);
    expect(File('$pasta/astronauta.obj').lengthSync(), lessThan(8 * 1024 * 1024));
  });
}
