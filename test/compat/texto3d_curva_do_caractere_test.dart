// A CURVA DO CARACTERE DO TEXTO 3D SOBREVIVE A SALVAR E REABRIR.
//
// A pendencia: `AjusteDeCaracteres._trilhaParaJson` gravava so o tempo e
// o valor de cada marca, e a curva editada num caractere (painel da curva
// da UI nova) voltava a linear ao reabrir o projeto — e no render tambem,
// que le a pose do mesmo JSON guardado no modelo.
//
// A correcao e ADITIVA: a curva e um terceiro item OPCIONAL da marca, na
// codificacao das outras trilhas do projeto (`easingToJson`). Estes
// testes prendem os dois lados:
//
//   * curva editada num caractere volta igual (bezier, mola, quique), no
//     parametro E na pose que o motor desenha;
//   * arquivo ANTIGO (marca de dois itens) abre igual ao que abria —
//     linear —, e regravar um ajuste sem curva produz o mesmo bloco;
//   * lixo no lugar da curva vira linear, sem derrubar o ajuste.
import 'dart:convert';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d_animado.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

const _meio = Duration(milliseconds: 500);
const _fim = Duration(seconds: 1);

/// Uma trilha 0 -> 80 em um segundo, com [curva] no trecho.
AnimatedDouble _trilha(Easing curva) =>
    AnimatedDouble(0)
        .comMarcaInserida(Duration.zero, 0)
        .comMarcaInserida(_fim, 80)
        .withEase(Duration.zero, curva);

VideoProject _projetoCom(List<AjusteDeCaracteres> ajustes) => VideoProject(
  name: 'curva do caractere',
  createdAt: DateTime(2026, 9, 21),
  layers: [
    Scene3DLayer(
      id: 'cena',
      name: 'Texto 3D',
      startTime: Duration.zero,
      duration: const Duration(seconds: 5),
      scene: Scene3D(
        nodes: [
          SceneNode(
            id: 'no',
            name: 'ABCDE',
            texto3d: Texto3D(texto: 'ABCDE', ajustes: ajustes),
            estiloTexto3d: EstiloDoTexto3D.cromo,
          ),
        ],
      ),
    ),
  ],
);

/// Salvar e reabrir de verdade: JSON em texto e de volta.
VideoProject _salvarEReabrir(VideoProject p) => projectFromJson(
  (jsonDecode(jsonEncode(projectToJson(p))) as Map).cast<String, dynamic>(),
);

Texto3D _texto(VideoProject p) =>
    (p.layers.single as Scene3DLayer).scene.nodeById('no')!.texto3d!;

void _mesmaCurva(Easing lida, Easing original, String rotulo) {
  expect(lida.type, original.type, reason: '$rotulo: tipo');
  expect(lida.x1, original.x1, reason: '$rotulo: x1');
  expect(lida.y1, original.y1, reason: '$rotulo: y1');
  expect(lida.x2, original.x2, reason: '$rotulo: x2');
  expect(lida.y2, original.y2, reason: '$rotulo: y2');
  expect(lida.count, original.count, reason: '$rotulo: count');
  expect(lida.intensity, original.intensity, reason: '$rotulo: intensidade');
  if (original.type == EasingType.spring) {
    expect(lida.response, original.response, reason: '$rotulo: resposta');
    expect(lida.damping, original.damping, reason: '$rotulo: amortecimento');
  }
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  group('curva editada num caractere', () {
    final curvas = {
      'bezier (acelerar)': Easing.easeIn,
      'bezier livre': const Easing(x1: 0.7, y1: 0.1, x2: 0.2, y2: 0.95),
      'mola': Easing.softSpring,
      'quique': Easing.bounce,
    };
    for (final caso in curvas.entries) {
      test('${caso.key} volta igual depois de salvar e reabrir', () {
        final original = _trilha(caso.value);
        final lida = _texto(
          _salvarEReabrir(
            _projetoCom([
              AjusteDeCaracteres(
                inicio: 2,
                fim: 2,
                trilhas: {MedidaDoCaractere.z: original},
              ),
            ]),
          ),
        ).ajustes.single.trilha(MedidaDoCaractere.z);
        expect(lida.keyframes, hasLength(2));
        _mesmaCurva(lida.easeAt(Duration.zero), caso.value, caso.key);
        // O valor no meio do trecho e o da CURVA, nao o da reta (40).
        final esperado = original.valueAt(_meio);
        expect(esperado, isNot(closeTo(40, 1e-3)), reason: 'curva de verdade');
        expect(lida.valueAt(_meio), closeTo(esperado, 1e-9));
      });
    }

    test('igualdade por valor ve a curva: sem ela o ajuste e outro', () {
      AjusteDeCaracteres com(Easing e) => AjusteDeCaracteres(
        inicio: 1,
        fim: 3,
        trilhas: {MedidaDoCaractere.y: _trilha(e)},
      );
      expect(com(Easing.easeIn), com(Easing.easeIn));
      expect(com(Easing.easeIn), isNot(com(Easing.linear)));
      final volta = _texto(_salvarEReabrir(_projetoCom([com(Easing.easeOut)])))
          .ajustes;
      expect(volta, [com(Easing.easeOut)]);
    });

    test('o controlador grava a curva no MODELO, e o render a respeita '
        'depois de reabrir', () async {
      final container = ProviderContainer();
      addTearDown(container.dispose);
      final c = container.read(editorControllerProvider.notifier);
      c.openProject(VideoProject.empty('curva no render'));
      final noId = (await c.addTexto3D(
        Duration.zero,
        'ABCDE',
        EstiloDoTexto3D.ouro,
      ))!;
      Scene3DLayer cena(VideoProject p) =>
          p.layers.whereType<Scene3DLayer>().single;
      final cenaId = cena(container.read(editorControllerProvider)).id;
      c.ajustarCaracteresDoTexto3D(cenaId, noId, [
        AjusteDeCaracteres(
          inicio: 2,
          fim: 2,
          trilhas: {MedidaDoCaractere.z: _trilha(Easing.easeIn)},
        ),
      ]);

      double zDoC(VideoProject p, Duration t) {
        final no = cena(p).scene.nodeById(noId)!;
        final data = no.modelAsset!.data;
        final matrizes = matrizesDoTextoAnimado(data, t, null)!;
        final tr = ((data['nodes'] as List)[3] as Map)['translation'] as List;
        return matrizes[3]!.getTranslation().z - (tr[2] as num).toDouble();
      }

      final antes = container.read(editorControllerProvider);
      final esperado = _trilha(Easing.easeIn).valueAt(_meio);
      expect(
        zDoC(antes, _meio),
        closeTo(esperado, 1e-6),
        reason: 'na sessao, o motor ja desenha a curva',
      );
      final depois = _salvarEReabrir(antes);
      expect(
        zDoC(depois, _meio),
        closeTo(esperado, 1e-6),
        reason: 'depois de reabrir, a curva continua no render',
      );
      expect(zDoC(depois, _fim), closeTo(80, 1e-6));
      _mesmaCurva(
        cena(depois).scene
            .nodeById(noId)!
            .texto3d!
            .ajustes
            .single
            .trilha(MedidaDoCaractere.z)
            .easeAt(Duration.zero),
        Easing.easeIn,
        'parametro do no',
      );
    });
  });

  group('arquivo antigo, sem o campo da curva', () {
    // O BLOCO COMO O ARQUIVO SEMPRE FOI GRAVADO (`antes-da-ui-nova`):
    // marca de dois itens. O resto da camada sai do gravador de hoje, que
    // nao mudou desde o checkpoint; so o bloco dos ajustes e o antigo,
    // escrito a mao no formato que o `_trilhaParaJson` antigo produzia.
    List<Object> blocoAntigo() => [
      {
        'i': 2,
        'f': 2,
        'z': {
          'b': 0.0,
          'k': [
            [0, 0.0],
            [1000000, 80.0],
          ],
        },
        'girY': -30.0,
      },
    ];

    Map<String, dynamic> camadaAntiga() {
      final json = (jsonDecode(
        jsonEncode(layerToJson(_projetoCom(const []).layers.single)),
      ) as Map).cast<String, dynamic>();
      final no = ((json['scene'] as Map)['nodes'] as List).single as Map;
      (no['texto3d'] as Map)['ajustesDeCaracteres'] = blocoAntigo();
      return json;
    }

    test('abre linear, como sempre abriu', () {
      final l = layerFromJson(camadaAntiga()) as Scene3DLayer;
      final a = l.scene.nodeById('no')!.texto3d!.ajustes.single;
      final z = a.trilha(MedidaDoCaractere.z);
      expect(z.keyframes, hasLength(2));
      expect(z.keyframes.every((k) => k.ease.isLinear), isTrue);
      expect(z.valueAt(_meio), closeTo(40, 1e-9));
      expect(a.trilha(MedidaDoCaractere.girY).base, -30);
    });

    test('regravar sem curva editada devolve o MESMO bloco (dois itens)', () {
      final l = layerFromJson(camadaAntiga()) as Scene3DLayer;
      final json = jsonDecode(jsonEncode(layerToJson(l))) as Map;
      final bloco =
          ((((json['scene'] as Map)['nodes'] as List).single as Map)['texto3d']
                  as Map)['ajustesDeCaracteres']
              as List;
      expect(jsonEncode(bloco), jsonEncode(blocoAntigo()));
    });

    test('lixo no lugar da curva vira linear e nao derruba o ajuste', () {
      for (final lixo in <Object?>[
        'bezier',
        42,
        <String, Object?>{'t': 0},
        <String, Object?>{'x1': 'a', 'y1': 0, 'x2': 1, 'y2': 1},
        null,
      ]) {
        final a = AjusteDeCaracteres.fromJson({
          'i': 0,
          'f': -1,
          'x': {
            'b': 0.0,
            'k': [
              [0, 0.0, lixo],
              [1000000, 10.0],
            ],
          },
        })!;
        final x = a.trilha(MedidaDoCaractere.x);
        expect(x.keyframes, hasLength(2), reason: '$lixo');
        expect(x.easeAt(Duration.zero).isLinear, isTrue, reason: '$lixo');
      }
    });

    test('a curva vai com a mesma codificacao das outras trilhas', () {
      final a = AjusteDeCaracteres(
        trilhas: {MedidaDoCaractere.escala: _trilha(Easing.easeOut)},
      );
      final marcas = ((a.toJson()['escala'] as Map)['k'] as List);
      expect((marcas.first as List).last, easingToJson(Easing.easeOut));
      // A marca da ponta (sem trecho depois) fica linear: dois itens.
      expect((marcas.last as List), hasLength(2));
    });
  });
}
