// O TIME REMAP E UM EFEITO. E SO ISSO.
//
// 20/09, recado do dono: "nao quero tela especial, nem secao escondida,
// nem editor separado. JA EXISTIA um Time Remap simples e poderoso, e e
// esse que eu quero, como EFEITO COMUM em EFEITOS -> TEMPO, junto de
// Time Warp e Posterize Time".
//
// Este arquivo guarda as duas metades disso:
//
//   * a PORTA — o efeito esta no catalogo, na categoria Tempo, ao lado do
//     RGB Time Warp e do Posterize Time; aplicar cria a trilha do motor;
//   * o CARTAO — Speed, Time, Frame, Reverse, Freeze e Interpolacao nas
//     mesmas linhas dos outros efeitos, com o mesmo losango e o MESMO
//     editor de curva de qualquer propriedade.
//
// A aplicacao pelo catalogo e o "fora de video" moram em
// test/ui/paineis/efeitos_catalogo_test.dart.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/am_sections.dart';
import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/curva/curva.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/efeitos.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/tempo.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'ui/paineis/apoio_paineis.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

VideoLayer _clipe() => VideoLayer(
  id: 'v',
  name: 'video',
  startTime: Duration.zero,
  duration: const Duration(seconds: 4),
  sourcePath: '/video.mp4',
  sourceDuration: const Duration(seconds: 8),
  position: AnimatedOffset(Offset.zero),
);

ProviderContainer _projeto(Layer clipe) {
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  c
      .read(editorControllerProvider.notifier)
      .openProject(
        VideoProject(
          name: 'p',
          createdAt: DateTime(2026, 9, 20),
          layers: [clipe],
        ),
      );
  return c;
}

VideoLayer _video(ProviderContainer c) =>
    c.read(editorControllerProvider).layerById('v')! as VideoLayer;

EffectInstance _efeito(VideoProject p) => (p.layerById('v')! as VideoLayer)
    .effects
    .firstWhere((e) => e.type == EffectType.timeRemap);

/// O painel de efeitos montado sozinho (a bancada da UI nova), com o
/// Time Remap aplicado DEPOIS de montar: o recem-aplicado abre sozinho.
Future<BancadaDoPainel> _painelDeEfeitos(WidgetTester tester) async {
  final (b, id) = await montarPainel(
    tester,
    preparar: (c) {
      abrirProjetoCom(c, [_clipe()]);
      return 'v';
    },
    painel: (id) => PainelEfeitos(layerId: id),
    tamanho: const Size(430, 932),
    altura: 900,
  );
  b.c.addEffect(id, EffectType.timeRemap);
  await tester.pumpAndSettle();
  return b;
}

void main() {
  group('o catalogo', () {
    test('Time Remap esta na categoria Tempo, com os outros dois', () {
      final tempo = effectsInCategory('Time');
      expect(tempo, contains(EffectType.timeRemap));
      // "junto de Time Warp e Posterize Time" (dono, 20/09).
      expect(tempo, contains(EffectType.rgbTimeWarp));
      expect(tempo, contains(EffectType.posterizeTime));
    });

    test('e escolhivel: entra no catalogo e na busca', () {
      expect(efeitosDoCatalogo, contains(EffectType.timeRemap));
      expect(searchEffects('time remap'), contains(EffectType.timeRemap));
      expect(searchEffects('congelar'), contains(EffectType.timeRemap));
      // Nada mais fica escondido do catalogo.
      expect(efeitosForaDoCatalogo, isEmpty);
    });

    test('o cartao tem UMA trilha guardada, a do motor', () {
      // Speed e Frame sao leituras da mesma curva. Dois parametros para o
      // mesmo instante divergiriam no primeiro congelamento.
      expect(effectSpecs[EffectType.timeRemap]!.params.keys, ['tempo']);
      expect(
        effectSpecs[EffectType.timeRemap]!.params['tempo']!.label,
        'Time',
      );
    });
  });

  group('aplicar', () {
    test('aplicar o efeito cria a trilha de tempo (identidade)', () {
      final c = _projeto(_clipe());
      expect(_video(c).timeRemap, isNull);

      c.read(editorControllerProvider.notifier).addEffect(
        'v',
        EffectType.timeRemap,
      );

      final trilha = _video(c).timeRemap;
      expect(trilha, isNotNull, reason: 'aplicar tem de criar a trilha');
      expect(hasTimeRemap(_video(c)), isTrue);
      // Identidade: nenhum quadro sai do lugar ao aplicar.
      expect(
        videoSourceTimeAt(_video(c), const Duration(seconds: 2)).inMilliseconds,
        closeTo(2000, 40),
      );
    });

    test('aplicar duas vezes nao empilha dois Time Remap', () {
      final c = _projeto(_clipe());
      final controller = c.read(editorControllerProvider.notifier);
      controller.addEffect('v', EffectType.timeRemap);
      controller.addEffect('v', EffectType.timeRemap);
      expect(
        _video(c).effects.where((e) => e.type == EffectType.timeRemap).length,
        1,
      );
    });
  });

  group('o cartao no painel de efeitos', () {
    testWidgets('abre com Speed, Time, Frame, Reverse, Freeze e Interpolação', (
      tester,
    ) async {
      final b = await _painelDeEfeitos(tester);
      final efeito = _efeito(b.projeto);
      expect(
        find.byKey(const ValueKey('prop-time-remap-speed')),
        findsOneWidget,
      );
      expect(
        find.byKey(ValueKey('prop-${efeito.id}-tempo')),
        findsOneWidget,
        reason: 'a linha "Time" e o parametro da ficha, como nos outros',
      );
      expect(
        find.byKey(const ValueKey('prop-time-remap-frame')),
        findsOneWidget,
      );
      expect(
        find.byKey(const ValueKey('prop-time-remap-reverse')),
        findsOneWidget,
      );
      expect(
        find.byKey(const ValueKey('prop-time-remap-freeze')),
        findsOneWidget,
      );
      expect(
        find.byKey(const ValueKey('prop-time-remap-interpolacao')),
        findsOneWidget,
      );
      expect(tester.takeException(), isNull);
    });

    testWidgets('a escolha de interpolacao muda a camada e persiste', (
      tester,
    ) async {
      final b = await _painelDeEfeitos(tester);
      VideoLayer video() => b.projeto.layerById('v')! as VideoLayer;

      expect(video().interpolacao, InterpolacaoDeQuadros.nenhuma);
      await tester.tap(
        find.descendant(
          of: find.byKey(const ValueKey('prop-time-remap-interpolacao')),
          matching: find.text(
            rotuloDaInterpolacao(InterpolacaoDeQuadros.nenhuma),
          ),
        ),
      );
      await tester.pumpAndSettle();
      await tester.tap(
        find.text(rotuloDaInterpolacao(InterpolacaoDeQuadros.mesclar)).last,
      );
      await tester.pumpAndSettle();
      expect(video().interpolacao, InterpolacaoDeQuadros.mesclar);

      // ... e sobrevive ao arquivo.
      final volta = projectFromJson(projectToJson(b.projeto));
      final lida = volta.layerById('v')! as VideoLayer;
      expect(lida.interpolacao, InterpolacaoDeQuadros.mesclar);
      expect(tester.takeException(), isNull);
    });

    testWidgets('o botao de curva abre o editor de curva GERAL', (
      tester,
    ) async {
      final b = await _painelDeEfeitos(tester);
      // A identidade ja nasce com duas marcas: a trilha esta animada.
      expect(_efeito(b.projeto).hasAnimation, isTrue);

      await tester.tap(find.byKey(const ValueKey('time-remap-abrir-curva')));
      await tester.pumpAndSettle();

      // O mesmo editor que abre para opacidade, escala ou qualquer
      // parametro de efeito — proibido um segundo editor de curva.
      expect(find.byType(EditorDeCurva), findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  });

  group('Reverse e Freeze mexem no mapeamento de tempo', () {
    test('Reverse troca o quadro que toca em cada instante', () {
      final c = _projeto(_clipe());
      final controller = c.read(editorControllerProvider.notifier);
      controller.addEffect('v', EffectType.timeRemap);
      final antes = videoSourceTimeAt(
        _video(c),
        const Duration(seconds: 1),
      );

      controller.setClipReverse('v', true);

      final depois = videoSourceTimeAt(_video(c), const Duration(seconds: 1));
      expect(_video(c).reverse, isTrue);
      expect(
        depois,
        isNot(antes),
        reason: 'ligar o reverso tem de mudar o mapeamento',
      );
    });

    test('Freeze segura o quadro do cabecote ate o fim da barra', () {
      final c = _projeto(_clipe());
      final controller = c.read(editorControllerProvider.notifier);
      controller.addEffect('v', EffectType.timeRemap);
      const cabecote = Duration(seconds: 2);
      expect(controller.timeRemapCongeladoEm('v', cabecote), isFalse);

      controller.setTimeRemapFreeze('v', cabecote, true);

      expect(controller.timeRemapCongeladoEm('v', cabecote), isTrue);
      final noCabecote = videoSourceTimeAt(_video(c), cabecote);
      final noFim = videoSourceTimeAt(_video(c), const Duration(seconds: 4));
      expect(
        (noFim - noCabecote).inMilliseconds.abs(),
        lessThan(40),
        reason: 'congelado, o clipe fica no mesmo quadro',
      );

      // Soltar volta a andar.
      controller.setTimeRemapFreeze('v', cabecote, false);
      expect(controller.timeRemapCongeladoEm('v', cabecote), isFalse);
      expect(
        videoSourceTimeAt(_video(c), const Duration(seconds: 4)),
        isNot(noFim),
      );
    });

    test('Speed reinclina o trecho seguinte, sem mexer no passado', () {
      final c = _projeto(_clipe());
      final controller = c.read(editorControllerProvider.notifier);
      controller.addEffect('v', EffectType.timeRemap);
      const cabecote = Duration(seconds: 1);
      final passado = videoSourceTimeAt(
        _video(c),
        const Duration(milliseconds: 500),
      );

      controller.setTimeRemapSpeed('v', cabecote, 2);

      expect(
        controller.timeRemapSpeedAt('v', cabecote),
        closeTo(2, 0.15),
        reason: 'a velocidade lida tem de ser a que se pediu',
      );
      expect(
        videoSourceTimeAt(_video(c), const Duration(milliseconds: 500)),
        passado,
        reason: 'o que ja passou nao se mexe',
      );
    });

    test('Resetar volta a identidade, sem tirar o efeito', () {
      final c = _projeto(_clipe());
      final controller = c.read(editorControllerProvider.notifier);
      controller.addEffect('v', EffectType.timeRemap);
      controller.setTimeRemapFreeze('v', const Duration(seconds: 1), true);

      controller.resetarCurvaDeTempo('v');

      expect(hasTimeRemap(_video(c)), isTrue);
      expect(
        videoSourceTimeAt(_video(c), const Duration(seconds: 3)).inMilliseconds,
        closeTo(3000, 40),
      );
    });
  });

  group('a grade de secoes', () {
    test('a secao Tempo da grade ficou, e agora e so a velocidade', () {
      final secoes = secoesDe(_clipe());
      expect(secoes, contains(AmSecao.tempo));
      expect(secoes.length, lessThanOrEqualTo(kAmMaximoSecoes));
      expect(secoes.length, kAmMaximoSecoes);
      final indices = [
        for (final s in secoes) AmSecao.values.indexOf(s),
      ];
      expect(indices, [...indices]..sort());
    });
  });

  group('interpolacao de quadros', () {
    test('o rotulo de cada modo e o vocabulario da tela', () {
      expect(
        [for (final m in InterpolacaoDeQuadros.values) rotuloDaInterpolacao(m)],
        ['Nenhuma', 'Mistura', 'Fluxo óptico', 'Fluxo óptico (IA)'],
      );
    });

    test('o selo avisa que a previa nao e o arquivo', () {
      // Enquanto o palco mostrar o quadro mais proximo, mistura e fluxo
      // optico so existem na exportacao — e a tela precisa dizer isso.
      expect(seloDaInterpolacao(InterpolacaoDeQuadros.nenhuma), isNull);
      expect(
        seloDaInterpolacao(InterpolacaoDeQuadros.movimento),
        contains('exportação: fluxo óptico'),
      );
      expect(
        seloDaInterpolacao(InterpolacaoDeQuadros.mesclar),
        kPreviaMisturaQuadros ? isNull : contains('exportação: mistura'),
      );
    });
  });
}
