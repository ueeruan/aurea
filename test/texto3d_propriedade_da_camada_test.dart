// TEXTO 3D E PROPRIEDADE DA CAMADA, e nao um botao de criar.
//
// O QUE ESTES TESTES PRENDEM:
//
//   * o painel Texto 3D SO aparece para a camada que TEM um texto 3D
//     dentro — video, imagem, modelo 3D importado e texto comum nao o veem
//     (um painel que edita uma letra inexistente e pior que painel nenhum);
//   * "Ativar 3D" so existe na camada de TEXTO, e converte de verdade: o
//     texto vira uma Scene3DLayer com o MESMO conteudo, a mesma fonte, a
//     mesma cor e a mesma transformacao, no MESMO lugar da pilha;
//   * mexer em Profundidade e em Metal muda a malha e o material do no.
import 'dart:ui';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/am_sections.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/modelo_do_texto3d.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

const _d = Duration(seconds: 4);

void main() {
  // A FONTE EMPACOTADA VEM DO `rootBundle`: sem o binding, `addTexto3D`
  // devolve nulo e o teste acusaria a conversao por um erro de bancada.
  TestWidgetsFlutterBinding.ensureInitialized();

  late ProviderContainer container;
  late EditorController controller;

  setUp(() {
    container = ProviderContainer();
    addTearDown(container.dispose);
    controller = container.read(editorControllerProvider.notifier);
    controller.openProject(VideoProject.empty('texto 3d'));
  });

  VideoProject projeto() => container.read(editorControllerProvider);

  Scene3DLayer cena() => projeto().layers.whereType<Scene3DLayer>().single;

  group('O painel Texto 3D so aparece para texto 3D', () {
    test('quatro tipos de camada, so um ve a secao', () async {
      final noId = await controller.addTexto3D(
        Duration.zero,
        'AUREA',
        EstiloDoTexto3D.ouro,
      );
      expect(noId, isNotNull, reason: 'a fonte do app tem de abrir');
      final texto3d = cena();

      // Uma cena 3D SEM texto: e o caso do modelo importado.
      final modelo3d = Scene3DLayer(
        name: 'modelo',
        startTime: Duration.zero,
        duration: _d,
        scene: Scene3D(
          nodes: [SceneNode(name: 'cubo', kind: Element3DKind.cube)],
        ),
      );
      final video = VideoLayer(
        name: 'v',
        startTime: Duration.zero,
        duration: _d,
        sourcePath: '/tmp/v.mp4',
      );
      final imagem = ImageLayer(
        name: 'i',
        startTime: Duration.zero,
        duration: _d,
        sourcePath: '/tmp/i.png',
      );
      final textoComum = TextLayer(
        name: 't',
        startTime: Duration.zero,
        duration: _d,
        text: 'oi',
      );

      expect(secoesDe(texto3d), contains(AmSecao.texto3d));
      for (final outra in [modelo3d, video, imagem, textoComum]) {
        expect(
          secoesDe(outra),
          isNot(contains(AmSecao.texto3d)),
          reason: '${outra.runtimeType} nao tem texto 3D para editar',
        );
      }
    });

    test('Ativar 3D so existe no texto comum', () async {
      await controller.addTexto3D(Duration.zero, 'AUREA', EstiloDoTexto3D.ouro);
      final camadas = <Layer>[
        cena(),
        VideoLayer(
          name: 'v',
          startTime: Duration.zero,
          duration: _d,
          sourcePath: '/tmp/v.mp4',
        ),
        ImageLayer(
          name: 'i',
          startTime: Duration.zero,
          duration: _d,
          sourcePath: '/tmp/i.png',
        ),
        TextLayer(
          name: 't',
          startTime: Duration.zero,
          duration: _d,
          text: 'oi',
        ),
      ];
      for (final c in camadas) {
        expect(
          secoesDe(c).contains(AmSecao.ativar3d),
          c is TextLayer,
          reason: '${c.runtimeType}',
        );
      }
    });

    test('o texto comum nao passa do teto de sete fichas', () {
      final t = TextLayer(
        name: 't',
        startTime: Duration.zero,
        duration: _d,
        text: 'oi',
      );
      expect(secoesDe(t).length, lessThanOrEqualTo(kAmMaximoSecoes));
    });
  });

  group('Ativar 3D converte o texto', () {
    test('mesmo conteudo, mesma cor, mesma transformacao, mesmo lugar', () async {
      final texto = TextLayer(
        id: 'titulo',
        name: 'titulo',
        startTime: const Duration(seconds: 1),
        duration: const Duration(seconds: 7),
        text: 'AUREA',
        color: const Color(0xFFFF3B30),
        position: AnimatedOffset(const Offset(120, 340)),
        scaleX: AnimatedDouble(1.4),
        rotation: AnimatedDouble(12),
      );
      // UMA CAMADA POR CIMA, para provar que a ordem da pilha sobrevive: o
      // texto que estava embaixo continua embaixo depois de virar 3D.
      controller.openProject(
        VideoProject.empty('conversao').copyWith(
          layers: [
            ImageLayer(
              name: 'logo',
              startTime: Duration.zero,
              duration: _d,
              sourcePath: '/tmp/i.png',
            ),
            texto,
          ],
        ),
      );
      final indiceAntes = projeto().layers.indexWhere((l) => l.id == texto.id);
      final quantasAntes = projeto().layers.length;

      final noId = await controller.ativarTexto3D(texto.id);
      expect(noId, isNotNull, reason: 'a conversao tem de dar certo');

      final layers = projeto().layers;
      expect(layers.length, quantasAntes, reason: 'troca, nao soma');
      expect(
        layers.whereType<TextLayer>(),
        isEmpty,
        reason: 'o texto comum virou 3D, nao ficou os dois',
      );
      final nova = layers[indiceAntes];
      expect(nova, isA<Scene3DLayer>(), reason: 'no lugar exato da pilha');

      final no = (nova as Scene3DLayer).scene.nodeById(noId!)!;
      expect(no.texto3d!.texto, 'AUREA');
      expect(
        no.texto3d!.cor,
        const Color(0xFFFF3B30).toARGB32(),
        reason: 'a cor escolhida tem de migrar',
      );
      expect(nova.position.valueAt(Duration.zero), const Offset(120, 340));
      expect(nova.scaleX.valueAt(Duration.zero), 1.4);
      expect(nova.rotation.valueAt(Duration.zero), 12);
      expect(nova.startTime, const Duration(seconds: 1));
      expect(nova.duration, const Duration(seconds: 7));

      // A COR NO MATERIAL, e nao so no parametro: o vermelho tem de estar
      // na frente da letra.
      final frente = no.modelAsset!.data['materials'][0] as Map;
      final canais = (frente['color'] as List).cast<num>();
      expect(canais[0], greaterThan(0.9), reason: 'vermelho forte');
      expect(canais[1], lessThan(0.4));

      // E a secao do painel passa a existir para a camada nova.
      expect(secoesDe(nova), contains(AmSecao.texto3d));
      expect(secoesDe(nova), isNot(contains(AmSecao.ativar3d)));
    });

    test('texto vazio e camada de outro tipo nao convertem', () async {
      controller.openProject(
        VideoProject.empty('nada').copyWith(
          layers: [
            TextLayer(
              id: 'vazio',
              name: 't',
              startTime: Duration.zero,
              duration: _d,
              text: '   ',
            ),
            ImageLayer(
              id: 'img',
              name: 'i',
              startTime: Duration.zero,
              duration: _d,
              sourcePath: '/tmp/i.png',
            ),
          ],
        ),
      );
      expect(await controller.ativarTexto3D('vazio'), isNull);
      expect(await controller.ativarTexto3D('img'), isNull);
      expect(await controller.ativarTexto3D('nao-existe'), isNull);
      expect(projeto().layers.whereType<Scene3DLayer>(), isEmpty);
    });
  });

  group('Os controles mudam o modelo', () {
    test('Profundidade refaz a malha, Metal troca o material', () async {
      final noId = (await controller.addTexto3D(
        Duration.zero,
        'AUREA',
        EstiloDoTexto3D.ouro,
      ))!;
      final antes = cena().scene.nodeById(noId)!;
      final trianguloAntes = antes.modelAsset!.triangleCount;
      final metalAntes =
          (antes.modelAsset!.data['materials'][0] as Map)['metallic'] as num;

      // PROFUNDIDADE: mais extrusao, mais triangulo lateral.
      expect(
        await controller.editarTexto3D(
          cena().id,
          noId,
          antes.texto3d!.copyWith(espessura: 90),
          EstiloDoTexto3D.ouro,
        ),
        isTrue,
      );
      final grosso = cena().scene.nodeById(noId)!;
      expect(grosso.texto3d!.espessura, 90);
      expect(grosso.modelAsset, isNot(same(antes.modelAsset)));
      expect(
        grosso.modelAsset!.triangleCount,
        greaterThanOrEqualTo(trianguloAntes),
      );

      // METAL a mao: o material sai do valor da predefinicao.
      expect(
        await controller.editarTexto3D(
          cena().id,
          noId,
          grosso.texto3d!.copyWith(metalico: 0.1, rugosidade: 0.8),
          EstiloDoTexto3D.ouro,
        ),
        isTrue,
      );
      final fosco = cena().scene.nodeById(noId)!;
      final mat = fosco.modelAsset!.data['materials'][0] as Map;
      expect(mat['metallic'], 0.1);
      expect(mat['roughness'], 0.8);
      expect(metalAntes, isNot(0.1), reason: 'o ouro nasce metalico');
      expect(
        fosco.texto3d!.espessura,
        90,
        reason: 'mexer no material nao desfaz a forma',
      );

      // E a predefinicao LIMPA o ajuste a mao.
      final semAjuste = fosco.texto3d!.copyWith(semAcabamentoProprio: true);
      expect(semAjuste.metalico, isNull);
      expect(semAjuste.rugosidade, isNull);
      expect(semAjuste.cor, isNull);
    });

    test('Reflexo vai para a cena', () async {
      await controller.addTexto3D(Duration.zero, 'A', EstiloDoTexto3D.cromo);
      final id = cena().id;
      controller.ajustarReflexoDoTexto3D(id, 0.25);
      expect(cena().scene.envReflect, closeTo(0.25, 1e-9));
      // Fora da faixa entra na faixa; camada que nao e cena nao quebra.
      controller.ajustarReflexoDoTexto3D(id, 5);
      expect(cena().scene.envReflect, 1.0);
    });

    test('o acabamento sobrevive a gravacao do projeto', () {
      const t = Texto3D(
        texto: 'A',
        cor: 0xFF102030,
        metalico: 0.3,
        rugosidade: 0.7,
        emissivo: 1.5,
      );
      final lido = Texto3D.fromJson(t.toJson());
      expect(lido, t);
      // Sem acabamento, o JSON nao ganha chave nenhuma a toa.
      const limpo = Texto3D(texto: 'A');
      expect(limpo.toJson().containsKey('cor'), isFalse);
      expect(Texto3D.fromJson(limpo.toJson()), limpo);
    });
  });
}
