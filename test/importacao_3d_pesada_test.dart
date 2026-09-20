// O MODELO PESADO: a ficha, a reducao e o credito.
//
// Tres coisas ficam presas aqui, porque as tres mudam o que o dono ve
// quando importa um modelo grande:
//
//   * o CREDITO atravessa a importacao e o arquivo do projeto (as licencas
//     Creative Commons pedem titulo, autor, origem e licenca junto da obra
//     em todo lugar onde ela aparece);
//   * a REDUCAO da malha chega perto do alvo SEM quebrar o modelo: nenhum
//     indice fora da lista de vertices, e ossos, pesos e morphs continuam
//     com um valor por vertice — se isso escorregar, o rig deforma errado
//     e so se descobre no aparelho;
//   * a FICHA (`AnaliseDoModelo`) conta os cinco mapas de cada material, e
//     cada imagem uma vez so, porque e ela que decide se o aviso aparece.
import 'dart:convert';
import 'dart:math' as math;

import 'package:aurea/src/features/editor/domain/analise_do_modelo.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/malha_importada.dart';
import 'package:aurea/src/features/editor/domain/model_asset3d.dart';
import 'package:aurea/src/features/editor/domain/packed_model_vectors.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/texturas_importadas.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image/image.dart' as img;

/// Uma grade [lado] x [lado] com relevo suave: o simplificador precisa de
/// superficie, nao de um plano (plano desce a quatro triangulos e o teste
/// nao provaria nada).
({List<double> posicoes, List<int> indices}) _grade(int lado) {
  final p = <double>[];
  for (var y = 0; y < lado; y++) {
    for (var x = 0; x < lado; x++) {
      p.addAll([
        x / (lado - 1),
        y / (lado - 1),
        .09 * math.sin(x / 3) * math.cos(y / 4),
      ]);
    }
  }
  final idx = <int>[];
  for (var y = 0; y < lado - 1; y++) {
    for (var x = 0; x < lado - 1; x++) {
      final a = y * lado + x;
      idx.addAll([a, a + 1, a + lado, a + 1, a + lado + 1, a + lado]);
    }
  }
  return (posicoes: p, indices: idx);
}

/// A primitiva como o importador a entrega: posicoes/normais/UV em listas
/// de listas, mais o rig (ossos + pesos) e um morph target.
Map<String, dynamic> _primitiva(int lado, {bool comRig = true}) {
  final g = _grade(lado);
  final n = g.posicoes.length ~/ 3;
  return {
    'node': 0,
    'material': 0,
    'positions': [
      for (var i = 0; i < n; i++) g.posicoes.sublist(i * 3, i * 3 + 3),
    ],
    'normals': [for (var i = 0; i < n; i++) const [0.0, 0.0, 1.0]],
    'uv': [
      for (var i = 0; i < n; i++)
        [g.posicoes[i * 3], g.posicoes[i * 3 + 1]],
    ],
    if (comRig) ...{
      'joints': [
        for (var i = 0; i < n; i++) [i % 3, (i + 1) % 3, 0, 0],
      ],
      'weights': [
        for (var i = 0; i < n; i++) const [0.6, 0.4, 0.0, 0.0],
      ],
      'targets': [
        {
          'positions': [
            for (var i = 0; i < n; i++)
              [g.posicoes[i * 3] * .1, 0.0, 0.0],
          ],
        },
      ],
    },
    'indices': g.indices,
  };
}

/// Quantos vertices uma lista de atributo descreve, seja ela empacotada
/// (`PackedModelVectors`) ou lista de listas.
int _quantosVertices(Object? lista) => switch (lista) {
  PackedModelVectors p => p.data.length ~/ p.components,
  List<dynamic> l => l.length,
  _ => -1,
};

int _triangulos(Map<String, dynamic> p) => (p['indices'] as List).length ~/ 3;

/// Confere que a primitiva continua uma malha valida DEPOIS de mexida.
void _malhaValida(Map<String, dynamic> p, {required bool comRig}) {
  final vertices = _quantosVertices(p['positions']);
  expect(vertices, greaterThan(0));
  for (final i in p['indices'] as List) {
    expect(i, lessThan(vertices), reason: 'indice fora da lista de vertices');
    expect(i, greaterThanOrEqualTo(0));
  }
  expect(_quantosVertices(p['normals']), vertices);
  expect(_quantosVertices(p['uv']), vertices);
  if (!comRig) return;
  // UM VALOR POR VERTICE em cada atributo do rig: e o que o motor assume
  // ao montar a pose. Um a menos e leitura fora da lista no aparelho.
  expect(_quantosVertices(p['joints']), vertices);
  expect(_quantosVertices(p['weights']), vertices);
  expect(
    _quantosVertices(((p['targets'] as List).first as Map)['positions']),
    vertices,
  );
}

/// Um PNG solido de [lado] x [lado] como `data:` URI, do jeito que o
/// importador guarda as texturas.
String _png(int lado, {int r = 200, int g = 120, int b = 40, int? alfa}) {
  final imagem = img.Image(width: lado, height: lado, numChannels: alfa == null ? 3 : 4);
  for (final p in imagem) {
    p.setRgba(r, g, b, alfa ?? 255);
  }
  return 'data:image/png;base64,${base64Encode(img.encodePng(imagem))}';
}

ModelAsset3D _modelo({
  required List<Map<String, dynamic>> primitivas,
  List<Map<String, dynamic>> materiais = const [],
  int clips = 0,
  List<int> ossos = const [],
}) => ModelAsset3D({
  'version': 1,
  'format': 'gltf2',
  'name': 'Sintetico',
  'nodes': [
    {'name': 'raiz'},
  ],
  'primitives': primitivas,
  'materials': materiais,
  'warnings': <String>[],
  if (ossos.isNotEmpty)
    'skins': [
      {'joints': ossos},
    ],
  'clips': [
    for (var i = 0; i < clips; i++)
      {
        'name': 'anim$i',
        'channels': [
          {
            'times': [0.0, 1.0],
            'values': [
              [0.0, 0.0, 0.0],
              [1.0, 0.0, 0.0],
            ],
          },
        ],
      },
  ],
});

void main() {
  group('credito atravessa o projeto', () {
    VideoProject projetoCom(ModelCredit3D credito) => VideoProject(
      name: 'credito',
      createdAt: DateTime(2026, 9, 20),
      layers: [
        Scene3DLayer(
          id: 'cena',
          name: 'Cena 3D',
          startTime: Duration.zero,
          duration: const Duration(seconds: 5),
          scene: Scene3D(
            nodes: [
              SceneNode(id: 'no', name: 'Modelo', size: 200, credit: credito),
            ],
            lights: Scene3D.tresPontos,
          ),
        ),
      ],
    );

    ModelCredit3D creditoDoProjeto(Map<String, dynamic> json) {
      final cena = projectFromJson(json).layers.single as Scene3DLayer;
      return cena.scene.nodes.single.credit;
    }

    test('os seis campos voltam do arquivo iguais', () {
      const original = ModelCredit3D(
        author: 'Fulano de Tal',
        license: 'CC Attribution-NonCommercial',
        url: 'https://sketchfab.com/3d-models/abc',
        title: 'Estátua da praça',
        source: 'Sketchfab',
        authorUrl: 'https://sketchfab.com/fulano',
      );
      final volta = creditoDoProjeto(projectToJson(projetoCom(original)));
      expect(volta.author, original.author);
      expect(volta.license, original.license);
      expect(volta.url, original.url);
      expect(volta.title, original.title);
      expect(volta.source, original.source);
      expect(volta.authorUrl, original.authorUrl);
      expect(volta.porExtenso, original.porExtenso);
      expect(volta.isEmpty, isFalse);
    });

    test('credito vazio nao escreve chave nenhuma', () {
      final json = projectToJson(projetoCom(const ModelCredit3D()));
      expect(jsonEncode(json).contains('"authorUrl"'), isFalse);
      expect(creditoDoProjeto(json).isEmpty, isTrue);
    });

    test('chave com tipo errado nao derruba o projeto', () {
      // Arquivo editado a mao, ou salvo por uma versao futura: o credito e
      // enfeite perto da cena, e nao pode ser o motivo de o projeto nao
      // abrir.
      final json = projectToJson(
        projetoCom(const ModelCredit3D(author: 'Fulano', title: 'Obra')),
      );
      final texto = jsonEncode(json)
          .replaceAll('"author":"Fulano"', '"author":42')
          .replaceAll('"title":"Obra"', '"title":{"a":1}');
      final volta = creditoDoProjeto(
        jsonDecode(texto) as Map<String, dynamic>,
      );
      expect(volta.author, isNull);
      expect(volta.title, isNull);
    });

    test('a linha TASL sai por extenso, so com o que existe', () {
      const c = ModelCredit3D(
        author: 'Fulano',
        license: 'CC Attribution',
        url: 'https://exemplo/abc',
        title: 'Obra',
        source: 'Sketchfab',
      );
      expect(
        c.porExtenso,
        '"Obra" (https://exemplo/abc) por Fulano, licença CC Attribution, '
        'via Sketchfab',
      );
      expect(const ModelCredit3D(author: 'Só o autor').porExtenso,
          'por Só o autor');
      expect(const ModelCredit3D().porExtenso, isEmpty);
    });
  });

  group('reducao da malha principal', () {
    test('os triangulos descem perto do alvo e a malha continua valida', () {
      final p = _primitiva(90); // ~15,8 mil triangulos
      final antes = _triangulos(p);
      expect(antes, greaterThan(15000));
      final data = <String, dynamic>{
        'materials': <dynamic>[],
        'primitives': [p],
        'warnings': <String>[],
      };
      otimizarMalhasImportadas(data, alvoDeTriangulos: 4000);
      final depois = _triangulos(p);
      // Perto, e nao exato: o simplificador para onde a topologia manda.
      expect(depois, lessThan(antes ~/ 2));
      expect(depois, lessThanOrEqualTo(4000 * 2));
      expect(depois, greaterThan(500));
      _malhaValida(p, comRig: true);
    });

    test('sem alvo, a otimizacao nao tira um triangulo', () {
      final p = _primitiva(60);
      final antes = _triangulos(p);
      final data = <String, dynamic>{
        'materials': <dynamic>[],
        'primitives': [p],
        'warnings': <String>[],
      };
      otimizarMalhasImportadas(data);
      // A solda e a busca renumeram, mas a contagem de faces e a mesma.
      expect(_triangulos(p), antes);
      _malhaValida(p, comRig: true);
    });

    test('primitiva pequena nao paga a conta das grandes', () {
      // Botoes, olhos e parafusos somem inteiros quando o simplificador
      // tira deles a mesma fracao que tira do corpo.
      final alvos = alvosPorPrimitiva([200000, 400, 300], 50000);
      expect(alvos[1], isNull);
      expect(alvos[2], isNull);
      expect(alvos[0], isNotNull);
      expect(alvos[0]!, lessThan(200000));
    });

    test('modelo que ja cabe no alvo fica intocado', () {
      expect(alvosPorPrimitiva([1000, 2000], 50000), [null, null]);
      expect(alvosPorPrimitiva([1000, 2000], null), [null, null]);
    });

    test('o alvo se divide entre as primitivas grandes', () {
      final alvos = alvosPorPrimitiva([60000, 20000], 40000);
      final soma = alvos.fold<int>(0, (a, b) => a + (b ?? 0));
      expect(soma, closeTo(40000, 40000 * .1));
      expect(alvos[0]!, greaterThan(alvos[1]!));
    });

    test('duas primitivas descem juntas e nenhuma quebra', () {
      final a = _primitiva(70), b = _primitiva(70, comRig: false);
      final data = <String, dynamic>{
        'materials': <dynamic>[],
        'primitives': [a, b],
        'warnings': <String>[],
      };
      final antes = _triangulos(a) + _triangulos(b);
      otimizarMalhasImportadas(data, alvoDeTriangulos: 3000);
      expect(_triangulos(a) + _triangulos(b), lessThan(antes ~/ 2));
      _malhaValida(a, comRig: true);
      _malhaValida(b, comRig: false);
    });
  });

  group('reducao das texturas', () {
    test('a textura grande cai para o lado pedido e continua legivel', () {
      final data = <String, dynamic>{
        'materials': [
          {'name': 'm', 'image': _png(2048)},
        ],
      };
      expect(reduzirTexturasDoModelo(data, lado: 512), 1);
      final uri = (data['materials'] as List).first['image'] as String;
      final bytes = bytesDoDataUri(uri)!;
      final lida = img.decodeImage(bytes);
      expect(lida, isNotNull);
      expect(math.max(lida!.width, lida.height), 512);
    });

    test('a textura que ja cabe fica exatamente como veio', () {
      final original = _png(256);
      final data = <String, dynamic>{
        'materials': [
          {'image': original},
        ],
      };
      expect(reduzirTexturasDoModelo(data, lado: 1024), 0);
      expect((data['materials'] as List).first['image'], same(original));
    });

    test('a mesma imagem em dois materiais converte uma vez so', () {
      final original = _png(1400);
      final data = <String, dynamic>{
        'materials': [
          {'image': original},
          {'image': original, 'normalImage': original},
        ],
      };
      // Uma conversao, tres lugares apontando para o mesmo texto novo.
      expect(reduzirTexturasDoModelo(data, lado: 512), 1);
      final m = (data['materials'] as List).cast<Map>();
      expect(m[0]['image'], same(m[1]['image']));
      expect(m[1]['normalImage'], same(m[1]['image']));
      expect(m[0]['image'], isNot(original));
    });

    test('a transparencia sobrevive (sai PNG, nao JPEG)', () {
      final data = <String, dynamic>{
        'materials': [
          {'image': _png(1200, alfa: 90)},
        ],
      };
      expect(reduzirTexturasDoModelo(data, lado: 300), 1);
      final uri = (data['materials'] as List).first['image'] as String;
      expect(uri.startsWith('data:image/png;'), isTrue);
      final lida = img.decodeImage(bytesDoDataUri(uri)!)!;
      expect(lida.getPixel(10, 10).a, closeTo(90, 8));
    });

    test('lixo no lugar da imagem nao derruba a importacao', () {
      final data = <String, dynamic>{
        'materials': [
          {'image': 'data:image/png;base64,###', 'normalImage': 'arquivo.png'},
        ],
      };
      expect(reduzirTexturasDoModelo(data, lado: 256), 0);
    });
  });

  group('ficha do modelo', () {
    test('conta malha, texturas distintas, materiais, animacoes e ossos', () {
      final cor = _png(2048), relevo = _png(1024, r: 128, g: 128, b: 255);
      final asset = _modelo(
        primitivas: [_primitiva(40)],
        materiais: [
          {'name': 'a', 'image': cor, 'normalImage': relevo},
          // O MESMO mapa de cor de novo: uma textura, nao duas.
          {'name': 'b', 'image': cor},
        ],
        clips: 2,
        ossos: [0, 1, 2],
      );
      final f = AnaliseDoModelo.doModelo(asset, arquivoBytes: 12345);
      expect(f.triangulos, _triangulos(_primitiva(40)));
      expect(f.vertices, 40 * 40);
      expect(f.texturas, 2);
      expect(f.maiorTextura, 2048);
      expect(f.pixelsDasTexturas, 2048 * 2048 + 1024 * 1024);
      expect(f.materiais, 2);
      expect(f.animacoes, 2);
      expect(f.ossos, 3);
      expect(f.arquivoBytes, 12345);
      expect(f.memoriaDasTexturas, cor.length + relevo.length);
      expect(f.memoriaDaMalha, greaterThan(0));
    });

    test('modelo simples nao e pesado; o aviso nao aparece para um cubo', () {
      final f = AnaliseDoModelo.doModelo(
        _modelo(
          primitivas: [_primitiva(20)],
          materiais: [
            {'image': _png(512)},
          ],
        ),
      );
      expect(f.pesado(), isFalse);
      expect(f.motivos(), isEmpty);
    });

    test('cada motivo de peso aparece sozinho', () {
      expect(
        const AnaliseDoModelo(triangulos: 200000).motivos(),
        {MotivoDoPeso.triangulos},
      );
      expect(const AnaliseDoModelo(maiorTextura: 4096).motivos(), {
        MotivoDoPeso.textura,
      });
      expect(
        const AnaliseDoModelo(arquivoBytes: 200 * 1024 * 1024).motivos(),
        {MotivoDoPeso.arquivo},
      );
      expect(
        const AnaliseDoModelo(memoriaDasTexturas: 200 * 1024 * 1024).motivos(),
        {MotivoDoPeso.memoria},
      );
    });

    test('o "depois" do aviso reduz malha e textura, e nao o resto', () {
      const antes = AnaliseDoModelo(
        triangulos: 600000,
        vertices: 300000,
        texturas: 3,
        maiorTextura: 4096,
        pixelsDasTexturas: 4096 * 4096 * 3,
        materiais: 5,
        animacoes: 2,
        ossos: 40,
        memoriaDaMalha: 40 * 1024 * 1024,
        memoriaDasTexturas: 120 * 1024 * 1024,
      );
      final depois = antes.estimativaOtimizada(
        alvoDeTriangulos: 150000,
        ladoDaTextura: 1024,
      );
      expect(depois.triangulos, 150000);
      expect(depois.vertices, 75000);
      expect(depois.maiorTextura, 1024);
      expect(depois.memoriaDasTexturas, lessThan(antes.memoriaDasTexturas));
      // O que a otimizacao NAO toca continua igual na estimativa.
      expect(depois.materiais, antes.materiais);
      expect(depois.animacoes, antes.animacoes);
      expect(depois.ossos, antes.ossos);
      expect(depois.texturas, antes.texturas);
    });

    test('numero legivel de relance', () {
      expect(contagemLegivel(980), '980');
      expect(contagemLegivel(1200), '1,2 mil');
      expect(contagemLegivel(46261), '46 mil');
      expect(contagemLegivel(1200000), '1,2 milhão');
      expect(contagemLegivel(1960000), '2 milhões');
      expect(memoriaLegivel(900), '900 B');
      expect(memoriaLegivel(12 * 1024 * 1024), '12 MB');
      expect(memoriaLegivel((1.5 * 1024 * 1024 * 1024).round()), '1,5 GB');
    });
  });
}
