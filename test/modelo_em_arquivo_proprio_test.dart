// O MODELO 3D MORA FORA DO ARQUIVO DO PROJETO.
//
// A geometria de um modelo importado e enorme perto do resto: posicoes,
// normais, UVs e indices viram listas de milhoes de numeros. Guardada
// dentro do projeto, ela era reescrita a cada salvamento automatico, no
// fio que responde ao toque — 18,8 MB e quase 350 ms para um glTF de 60
// mil triangulos, medidos em `bancada_projeto_com_3d_test.dart`. Era o
// "clica e responde dois segundos depois" do relato.
//
// Agora o modelo vai para um arquivo proprio e o projeto guarda so uma
// referencia. Isto aqui cobra as quatro coisas que nao podem falhar:
//
//   1. O projeto encolhe de verdade, e o modelo volta inteiro.
//   2. Salvar de novo NAO reescreve o modelo (senao nao se ganhou nada).
//   3. Projeto ANTIGO, com o modelo embutido, continua abrindo.
//   4. Se o arquivo do modelo sumir, o projeto abre sem ele — nunca
//      deixa de abrir.
import 'dart:convert';
import 'dart:io';

import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/projects/application/project_repository.dart';
import 'package:flutter_test/flutter_test.dart';

ModelAsset3D _modelo({int triangulos = 3000}) {
  final vertices = triangulos * 3;
  final positions = <double>[];
  final indices = <int>[];
  for (var i = 0; i < vertices; i++) {
    final f = i / vertices;
    positions.addAll([f, f * 2, f * 3]);
    indices.add(i);
  }
  return ModelAsset3D({
    'version': 1,
    'format': 'gltf2',
    'name': 'Modelo',
    'nodes': [
      {'name': 'raiz'},
    ],
    'primitives': [
      {'node': 0, 'positions': positions, 'indices': indices, 'material': 0},
    ],
    'skins': <dynamic>[],
    'clips': <dynamic>[],
    'materials': <dynamic>[],
    'warnings': <dynamic>[],
  });
}

VideoProject _projeto(ModelAsset3D modelo, {String nome = 'com modelo'}) =>
    VideoProject(
      id: 'proj-teste',
      name: nome,
      createdAt: DateTime(2026, 9, 9),
      layers: [
        Scene3DLayer(
          id: 'cena',
          name: 'Cena 3D',
          startTime: Duration.zero,
          duration: const Duration(seconds: 6),
          scene: Scene3D(
            nodes: [
              SceneNode(
                id: 'no',
                name: 'Modelo',
                size: 200,
                modelAsset: modelo,
              ),
            ],
            lights: Scene3D.tresPontos,
          ),
        ),
      ],
    );

int _triangulosDe(VideoProject p) {
  final cena = p.layers.whereType<Scene3DLayer>().single;
  final asset = cena.scene.nodes.single.modelAsset;
  if (asset == null) return -1;
  return (asset.primitives.single as Map)['indices'].length as int;
}

void main() {
  late Directory pasta;
  late ProjectRepository repo;

  setUp(() {
    pasta = Directory.systemTemp.createTempSync('aurea-pesos');
    repo = ProjectRepository(directory: pasta);
  });
  tearDown(() {
    if (pasta.existsSync()) pasta.deleteSync(recursive: true);
  });

  File arquivoDoProjeto() => File('${pasta.path}/proj-teste.json');
  Directory pastaDosModelos() => Directory('${pasta.path}/modelos');

  test('o projeto encolhe e o modelo volta inteiro', () async {
    final modelo = _modelo();
    final antes = _triangulosDe(_projeto(modelo));
    await repo.save(_projeto(modelo));
    await repo.flush();

    expect(arquivoDoProjeto().existsSync(), isTrue);
    final tamanhoDoProjeto = arquivoDoProjeto().lengthSync();
    final modelos = pastaDosModelos().listSync().whereType<File>().toList();
    expect(modelos, hasLength(1), reason: 'o modelo nao foi para fora');
    final tamanhoDoModelo = modelos.single.lengthSync();

    // O projeto tem de ser MUITO menor que o modelo: e disso que sai o
    // ganho, porque so ele e reescrito a cada salvamento.
    expect(
      tamanhoDoProjeto * 20,
      lessThan(tamanhoDoModelo),
      reason:
          'projeto com ${tamanhoDoProjeto}B ao lado de um modelo de '
          '${tamanhoDoModelo}B: o peso nao saiu de dentro',
    );

    final lidos = await repo.loadAll();
    expect(lidos, hasLength(1));
    expect(
      _triangulosDe(lidos.single),
      antes,
      reason: 'o modelo nao voltou igual do arquivo ao lado',
    );
  });

  test('salvar de novo NAO reescreve o modelo', () async {
    final modelo = _modelo();
    await repo.save(_projeto(modelo));
    await repo.flush();
    final arquivo = pastaDosModelos().listSync().whereType<File>().single;
    final primeiraGravacao = arquivo.lastModifiedSync();

    // Reabre como o aplicativo faz, edita o nome e salva de novo: o
    // modelo e o mesmo, e o arquivo dele nao pode ser tocado.
    final reaberto = (await repo.loadAll()).single;
    await repo.save(reaberto.copyWith(name: 'outro nome'));
    await repo.flush();

    final agora = pastaDosModelos().listSync().whereType<File>();
    expect(agora, hasLength(1), reason: 'nasceu um segundo arquivo de modelo');
    expect(
      agora.single.lastModifiedSync(),
      primeiraGravacao,
      reason: 'o modelo foi reescrito num salvamento que nao o mudou',
    );
    expect((await repo.loadAll()).single.name, 'outro nome');
  });

  test('projeto ANTIGO, com o modelo embutido, continua abrindo', () async {
    // O formato de antes: o modelo inteiro dentro do arquivo do projeto,
    // sem nenhuma referencia. Ninguem pode perder um projeto porque o
    // formato mudou.
    final modelo = _modelo(triangulos: 500);
    final antigo = projectToJson(_projeto(modelo, nome: 'antigo'));
    expect(
      jsonEncode(antigo).contains('"positions"'),
      isTrue,
      reason: 'o formato antigo tem de continuar embutindo o modelo',
    );
    arquivoDoProjeto().writeAsStringSync(jsonEncode(antigo));

    final lidos = await repo.loadAll();
    expect(lidos, hasLength(1));
    expect(lidos.single.name, 'antigo');
    expect(
      _triangulosDe(lidos.single),
      1500,
      reason: 'o modelo embutido nao voltou',
    );
  });

  test('se o arquivo do modelo sumir, o projeto ainda abre', () async {
    await repo.save(_projeto(_modelo(triangulos: 400)));
    await repo.flush();
    pastaDosModelos().listSync().whereType<File>().single.deleteSync();

    final lidos = await repo.loadAll();
    expect(
      lidos,
      hasLength(1),
      reason: 'perder o modelo nao pode levar o projeto junto',
    );
    expect(lidos.single.layers.whereType<Scene3DLayer>(), hasLength(1));
    expect(
      _triangulosDe(lidos.single),
      -1,
      reason: 'o modelo tinha de vir vazio, e a cena tinha de abrir',
    );
  });
}
