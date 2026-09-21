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
//     e a faixa especial da folha de velocidade e a entrada avulsa da
//     galeria nao existem mais;
//   * o CARTAO — Speed, Time, Frame, Reverse, Freeze e Interpolacao nas
//     mesmas linhas dos outros efeitos, com o mesmo losango e o MESMO
//     editor de curva de qualquer propriedade.
//
// O Estudio do tempo continua vivo, a um toque no menu ••• do cartao: e
// onde se desenha a curva com o dedo. O que ele deixou de ser e a porta.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/am_sections.dart';
import 'package:aurea/src/features/editor/domain/cut_ops.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';
import 'package:aurea/src/features/editor/presentation/am/effects_panel.dart';
import 'package:aurea/src/features/editor/presentation/am/estudio_do_tempo.dart';
import 'package:aurea/src/features/editor/presentation/am/layer_menu.dart';
import 'package:aurea/src/features/editor/presentation/am/speed_sheet.dart';
import 'package:aurea/src/features/editor/presentation/context/effects/effect_gallery.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

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

EffectInstance _cartao(ProviderContainer c) =>
    _video(c).effects.firstWhere((e) => e.type == EffectType.timeRemap);

/// Um host com Scaffold (a folha de parametro procura um) e um botao que
/// abre o que o teste quiser.
Widget _host(
  ProviderContainer c,
  void Function(BuildContext, WidgetRef) aoTocar,
) => UncontrolledProviderScope(
  container: c,
  child: MaterialApp(
    home: Scaffold(
      key: paramSheetHostKey,
      body: Consumer(
        builder: (context, ref, _) => TextButton(
          onPressed: () => aoTocar(context, ref),
          child: const Text('abrir'),
        ),
      ),
    ),
  ),
);

/// O painel de efeitos montado sozinho, com o clipe ja selecionado.
Future<PlaybackController> _painelDeEfeitos(
  WidgetTester tester,
  ProviderContainer c,
) async {
  tester.view.physicalSize = const Size(430, 932);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  late final PlaybackController pb;
  pb = PlaybackController(
    vsync: tester,
    durationOf: () => const Duration(seconds: 10),
  );
  addTearDown(pb.dispose);
  c.read(selectedLayerProvider.notifier).state = 'v';
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          key: paramSheetHostKey,
          body: EffectsPanel(playback: pb, onBack: () {}),
        ),
      ),
    ),
  );
  await tester.pumpAndSettle();
  return pb;
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

    testWidgets('a galeria aplica pelo tile, como qualquer efeito', (
      tester,
    ) async {
      tester.view.physicalSize = const Size(430, 932);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final c = _projeto(_clipe());
      late final PlaybackController pb;
      pb = PlaybackController(
        vsync: tester,
        durationOf: () => const Duration(seconds: 10),
      );
      addTearDown(pb.dispose);
      await tester.pumpWidget(
        _host(c, (context, ref) => showEffectGallery(context, ref, 'v', pb)),
      );
      await tester.tap(find.text('abrir'));
      await tester.pumpAndSettle();

      // Filtra pela busca: a grade e preguicosa e o tile pode nascer fora
      // da vista.
      await tester.enterText(
        find.byKey(const ValueKey('galeria-busca')),
        'time remap',
      );
      await tester.pumpAndSettle();

      final tile = find.byKey(const ValueKey('efeito-time_remap'));
      expect(tile, findsOneWidget, reason: 'o Time Remap sumiu da galeria');
      await tester.tap(tile);
      await tester.pumpAndSettle();

      expect(_video(c).timeRemap, isNotNull);
      expect(tester.takeException(), isNull);
    });

    testWidgets('num texto o Time Remap nem aparece', (tester) async {
      tester.view.physicalSize = const Size(430, 932);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final c = _projeto(
        TextLayer(
          id: 'v',
          name: 't',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          text: 'oi',
        ),
      );
      late final PlaybackController pb;
      pb = PlaybackController(
        vsync: tester,
        durationOf: () => const Duration(seconds: 10),
      );
      addTearDown(pb.dispose);
      await tester.pumpWidget(
        _host(c, (context, ref) => showEffectGallery(context, ref, 'v', pb)),
      );
      await tester.tap(find.text('abrir'));
      await tester.pumpAndSettle();
      await tester.enterText(
        find.byKey(const ValueKey('galeria-busca')),
        'time remap',
      );
      await tester.pumpAndSettle();

      expect(find.byKey(const ValueKey('efeito-time_remap')), findsNothing);
      expect(tester.takeException(), isNull);
    });
  });

  group('o cartao no painel de efeitos', () {
    testWidgets('abre com Speed, Time, Frame, Reverse, Freeze e Interpolação', (
      tester,
    ) async {
      final c = _projeto(_clipe());
      c.read(editorControllerProvider.notifier).addEffect(
        'v',
        EffectType.timeRemap,
      );
      await _painelDeEfeitos(tester, c);

      final efeito = _cartao(c);
      expect(find.byKey(const ValueKey('time-remap-speed')), findsOneWidget);
      expect(
        find.byKey(ValueKey('efeito-param-${efeito.id}/tempo')),
        findsOneWidget,
        reason: 'a linha "Time" e o parametro da ficha, como nos outros',
      );
      expect(find.byKey(const ValueKey('time-remap-frame')), findsOneWidget);
      expect(find.byKey(const ValueKey('time-remap-reverse')), findsOneWidget);
      expect(find.byKey(const ValueKey('time-remap-freeze')), findsOneWidget);
      expect(
        find.byKey(const ValueKey('time-remap-interpolacao')),
        findsOneWidget,
      );
      // O botao gordo que abria um editor proprio saiu do cartao.
      expect(
        find.byKey(const ValueKey('efeito-time-remap-abrir')),
        findsNothing,
      );
      expect(tester.takeException(), isNull);
    });

    testWidgets('a escolha de interpolacao muda a camada e persiste', (
      tester,
    ) async {
      final c = _projeto(_clipe());
      c.read(editorControllerProvider.notifier).addEffect(
        'v',
        EffectType.timeRemap,
      );
      await _painelDeEfeitos(tester, c);

      expect(_video(c).interpolacao, InterpolacaoDeQuadros.nenhuma);
      await tester.tap(find.text('Frame Blending'));
      await tester.pumpAndSettle();
      expect(_video(c).interpolacao, InterpolacaoDeQuadros.mesclar);

      // ... e sobrevive ao arquivo.
      final volta = projectFromJson(
        projectToJson(c.read(editorControllerProvider)),
      );
      final lida = volta.layerById('v')! as VideoLayer;
      expect(lida.interpolacao, InterpolacaoDeQuadros.mesclar);
      expect(tester.takeException(), isNull);
    });

    testWidgets('o botao de curva abre o GRAFICO GERAL, nao um segundo', (
      tester,
    ) async {
      final c = _projeto(_clipe());
      final controller = c.read(editorControllerProvider.notifier);
      controller.addEffect('v', EffectType.timeRemap);
      // A identidade ja nasce com duas marcas: a trilha esta animada.
      expect(_cartao(c).hasAnimation, isTrue);
      await _painelDeEfeitos(tester, c);

      await tester.tap(find.byTooltip('Editar curva da propriedade'));
      await tester.pumpAndSettle();

      // `curva-sheet-valor` e o grafico generico — o mesmo que abre
      // para opacidade, escala ou qualquer parametro de efeito.
      expect(
        find.byKey(const ValueKey('curva-sheet-valor')),
        findsOneWidget,
        reason: 'tem de ser o mesmo grafico de qualquer propriedade',
      );
      expect(
        find.byKey(const ValueKey('estudio-tempo-grafico')),
        findsNothing,
        reason: 'proibido um segundo editor de curva',
      );
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

  group('as portas velhas sairam do caminho', () {
    testWidgets('a folha de velocidade nao tem mais a faixa Time Remap', (
      tester,
    ) async {
      tester.view.physicalSize = const Size(430, 932);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final c = _projeto(_clipe());
      await tester.pumpWidget(
        _host(c, (context, ref) => showSpeedSheet(context, ref, 'v')),
      );
      await tester.tap(find.text('abrir'));
      await tester.pumpAndSettle();

      expect(
        find.byKey(const ValueKey('abrir-estudio-do-tempo')),
        findsNothing,
        reason: 'a porta oficial do Time Remap e o efeito',
      );
      // O que a folha continua sendo: a velocidade CONSTANTE do clipe.
      expect(find.byKey(const ValueKey('velocidade-regua')), findsOneWidget);
      expect(tester.takeException(), isNull);
    });

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

    testWidgets('a ficha "Tempo" da grade abre a folha de velocidade', (
      tester,
    ) async {
      tester.view.physicalSize = const Size(430, 932);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final c = _projeto(_clipe());
      late final PlaybackController pb;
      pb = PlaybackController(
        vsync: tester,
        durationOf: () => const Duration(seconds: 10),
      );
      addTearDown(pb.dispose);
      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: c,
          child: MaterialApp(
            home: Scaffold(
              key: paramSheetHostKey,
              body: Consumer(
                builder: (context, ref, _) => LayerToolsDock(
                  layer: ref.watch(editorControllerProvider).layers.first,
                  playback: pb,
                  onAction: (_) {},
                ),
              ),
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();

      final ficha = find.text('Tempo');
      expect(ficha, findsOneWidget, reason: 'a grade perdeu a ficha Tempo');
      await tester.tap(ficha);
      await tester.pumpAndSettle();

      expect(find.byKey(const ValueKey('velocidade-regua')), findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  });

  group('interpolacao de quadros', () {
    testWidgets('o chip da folha muda o campo da camada SEM curva de tempo', (
      tester,
    ) async {
      tester.view.physicalSize = const Size(430, 932);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      final c = _projeto(_clipe());
      expect(_video(c).interpolacao, InterpolacaoDeQuadros.nenhuma);
      await tester.pumpWidget(
        _host(c, (context, ref) => showSpeedSheet(context, ref, 'v')),
      );
      await tester.tap(find.text('abrir'));
      await tester.pumpAndSettle();

      final chip = find.byKey(const ValueKey('interpolacao-mesclar'));
      await tester.scrollUntilVisible(chip, 120);
      await tester.pumpAndSettle();
      await tester.tap(chip);
      await tester.pumpAndSettle();

      // A camera lenta de velocidade CONSTANTE tambem escolhe como os
      // quadros do meio nascem — era isto que so existia dentro da curva.
      expect(_video(c).interpolacao, InterpolacaoDeQuadros.mesclar);
      expect(_video(c).timeRemap, isNull);
      expect(tester.takeException(), isNull);
    });

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
