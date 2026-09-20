// A PORTA DO TIME REMAP.
//
// O Estudio do tempo nunca foi apagado — o que sumiu foram os CHAMADORES
// dele, duas vezes seguidas: a porta da folha Velocidade morreu em
// `aba36bb` e a da galeria de efeitos em `7e7c294`. O recurso inteiro
// (curva, keyframes de tempo, congelar, reverso, rampas) ficou no
// aplicativo sem nenhum caminho ate ele, e ninguem percebeu porque
// nenhum teste cobria a PORTA — so o editor montado direto.
//
// Este arquivo cobre o caminho: a ficha "Tempo" na grade da camada de
// video, a faixa do Time Remap no topo da folha, o grafico do estudio do
// outro lado dela, a entrada da galeria de efeitos e o seletor de
// interpolacao de quadros (que sem a folha so existia depois de a curva
// existir).
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/am_sections.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';
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

void main() {
  group('a grade da camada de video tem a secao Tempo', () {
    test('secoesDe(VideoLayer) inclui o tempo e nao passa do teto', () {
      final secoes = secoesDe(_clipe());
      expect(secoes, contains(AmSecao.tempo));
      expect(secoes.length, lessThanOrEqualTo(kAmMaximoSecoes));
      // A grade do video bate EXATAMENTE no teto: uma oitava secao aqui
      // significa que alguma outra tem de sair.
      expect(secoes.length, kAmMaximoSecoes);
    });

    test('a secao Tempo respeita a ordem do enum', () {
      final indices = [
        for (final s in secoesDe(_clipe())) AmSecao.values.indexOf(s),
      ];
      expect(indices, [...indices]..sort());
    });

    test('so o video ganha a secao Tempo', () {
      // A curva de tempo e da FONTE do clipe: um texto ou uma forma nao
      // tem fonte para remapear.
      for (final camada in [
        TextLayer(
          name: 't',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          text: 'oi',
        ),
        ShapeLayer(
          name: 's',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
        ),
        AudioLayer(
          name: 'a',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          sourcePath: '/a.m4a',
        ),
      ]) {
        expect(
          secoesDe(camada),
          isNot(contains(AmSecao.tempo)),
          reason: '${camada.runtimeType}',
        );
      }
    });
  });

  group('Tempo -> Time Remap', () {
    testWidgets('a ficha "Tempo" da grade abre a folha com a porta', (
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

      expect(
        find.byKey(const ValueKey('abrir-estudio-do-tempo')),
        findsOneWidget,
      );
      expect(tester.takeException(), isNull);
    });

    testWidgets('a porta abre o Estudio do tempo, com o grafico', (
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

      await tester.tap(find.byKey(const ValueKey('abrir-estudio-do-tempo')));
      await tester.pumpAndSettle();

      expect(find.byKey(const ValueKey('estudio-tempo-grafico')), findsOneWidget);
      // As acoes que o dono procura no Time Remap, todas na mesma casa.
      expect(find.byKey(const ValueKey('estudio-tempo-keyframe')), findsOneWidget);
      expect(find.byKey(const ValueKey('estudio-tempo-congelar')), findsOneWidget);
      expect(find.byKey(const ValueKey('estudio-tempo-reverso')), findsOneWidget);
      expect(tester.takeException(), isNull);
    });

    testWidgets('a entrada da galeria de efeitos abre o MESMO estudio', (
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
        _host(
          c,
          (context, ref) => showEffectGallery(context, ref, 'v', pb),
        ),
      );
      await tester.tap(find.text('abrir'));
      await tester.pumpAndSettle();

      final entrada = find.byKey(const ValueKey('efeito-time_remap'));
      expect(entrada, findsOneWidget, reason: 'a aba Tempo perdeu a entrada');
      await tester.tap(entrada);
      await tester.pumpAndSettle();

      expect(find.byKey(const ValueKey('estudio-tempo-grafico')), findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  });

  group('interpolacao de quadros', () {
    testWidgets('o chip muda o campo da camada SEM curva de tempo', (
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
