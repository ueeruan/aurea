// MODELO SEM NORMAL NAO ENTRA NO MOTOR COM UM BUFFER DE ZEROS.
//
// A ABI diz "normais NULO faz o motor calcular a normal PLANA", e o motor so
// calcula quando o ponteiro e nulo. Um OBJ sem `vn` — que e a maioria do que
// sai de scanner e de conversor, e o caso do `anel_teste.obj` que o dono
// importou — chegava como uma lista de nulos que virava um buffer de ZEROS.
// O motor entendia "tem normal", pulava a conta da plana, e cada vertice
// ficava com a normal (0,0,0).
//
// O `normalize` de um vetor nulo nao da erro nenhum: o modelo sai chapado e
// escuro, com a luz direta valendo zero e so o ambiente aparecendo. Foi
// exatamente o que o dono viu — "a importacao de 3D continua importando TUDO
// preto".
//
// O DEFEITO E DE DADO, NAO DE DESENHO: por isso o teste e aqui, e nao numa
// captura de tela. Se este teste passar e a tela continuar preta, o problema
// esta em outro lugar da cadeia — e nao mais nesta.
import 'package:aurea/src/features/editor/application/fonte_de_malha.dart';
import 'package:aurea/src/features/editor/application/motor3d_nativo.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:flutter_test/flutter_test.dart';

/// Um quadrado de dois triangulos, com ou sem normal por vertice.
ModelAsset3D _quadrado({required bool comNormal}) {
  const posicoes = <List<double>>[
    [-1, -1, 0],
    [1, -1, 0],
    [1, 1, 0],
    [-1, 1, 0],
  ];
  return ModelAsset3D({
    'version': 1,
    'format': 'obj',
    'name': comNormal ? 'com normal' : 'sem normal',
    'nodes': [
      {'name': 'raiz'},
    ],
    'primitives': [
      {
        'node': 0,
        'positions': posicoes,
        'indices': <int>[0, 1, 2, 0, 2, 3],
        'material': 0,
        if (comNormal)
          'normals': <List<double>>[
            [0, 0, 1],
            [0, 0, 1],
            [0, 0, 1],
            [0, 0, 1],
          ],
      },
    ],
    'skins': <dynamic>[],
    'clips': <dynamic>[],
    'materials': <dynamic>[],
    'warnings': <dynamic>[],
  });
}

MalhaDoNo _fonte(ModelAsset3D asset) {
  final no = SceneNode(
    name: asset.name,
    kind: Element3DKind.plane,
    size: 120,
    modelAsset: asset,
  );
  return CacheDeMalhas().doNo(
    no,
    Duration.zero,
    lodDaReceita: (_) => null,
    assinaturaDoMaterial: assinaturaDoMaterial3D,
  )!;
}

void main() {
  test('sem normal, o ponteiro vai NULO e o motor calcula a plana', () {
    final cruas = malhasCruas3DDe(_fonte(_quadrado(comNormal: false)));
    expect(cruas, hasLength(1));
    expect(
      cruas.single.normais,
      isNull,
      reason:
          'um buffer de zeros passa por "tem normal" no motor, que entao '
          'nao calcula a plana: cada vertice fica com a normal (0,0,0) e o '
          'modelo vira uma silhueta sem luz nenhuma',
    );
  });

  test('com normal, o ponteiro leva as normais do arquivo', () {
    final cruas = malhasCruas3DDe(_fonte(_quadrado(comNormal: true)));
    expect(cruas.single.normais, isNotNull);
    final n = cruas.single.normais!;
    expect(n.length, 4 * 3);
    expect(n[2], 1.0, reason: 'a normal do arquivo e (0,0,1)');
  });
}
