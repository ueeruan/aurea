import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/effect_preset_store.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/application/ui/pro_mode.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';
import 'package:aurea/src/features/editor/presentation/am/layer_menu.dart';
import 'package:aurea/src/features/editor/presentation/am/param_sheet_shell.dart'
    show ParamSheetShell;
import 'package:aurea/src/features/editor/presentation/widgets/add_layer_sheet.dart';
import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  setUp(() {
    RecentSheets.instance.clear();
    EffectPresetStore.semArquivo = true;
  });
  tearDown(() {
    RecentSheets.instance.clear();
    EffectPresetStore.semArquivo = false;
  });

  Future<(_NavigationObserver, ProviderContainer)> openEditor(
    WidgetTester tester,
    void Function(EditorController) seed,
  ) async {
    tester.view.physicalSize = const Size(430, 932);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);
    final container = ProviderContainer();
    addTearDown(container.dispose);
    seed(container.read(editorControllerProvider.notifier));
    // Pro: as acoes rapidas Caminho e Tempo so existem no Pro.
    container.read(proModeProvider.notifier).set(true);
    final observer = _NavigationObserver();
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          theme: ThemeData(platform: TargetPlatform.iOS),
          // IDIOMA FIXO, e nao o da maquina.
          //
          // Parte dos rotulos da doca passa por `translate()`: 'Câmera' e
          // 'Partículas' saem em ingles quando o locale resolvido nao e
          // pt ('Camera', 'Particles'), enquanto os rotulos que ainda nao
          // foram para o dicionario continuam em portugues. Sem fixar o
          // idioma, a mesma expectativa passava ou falhava conforme o
          // locale do computador que rodou a suite. O aplicativo e pt por
          // padrao (`appLanguageProvider`), entao e pt que se testa.
          locale: const Locale('pt'),
          supportedLocales: const [Locale('pt')],
          localizationsDelegates: GlobalMaterialLocalizations.delegates,
          navigatorObservers: [observer],
          home: Builder(
            builder: (context) => Scaffold(
              body: TextButton(
                onPressed: () => Navigator.of(context).push(
                  MaterialPageRoute<void>(builder: (_) => const _MenuHost()),
                ),
                child: const Text('inicio'),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('inicio'));
    await tester.pumpAndSettle();
    return (observer, container);
  }

  void camera(EditorController c) => c.addCameraLayer(Duration.zero);
  void text(EditorController c) => c.addTextLayer(Duration.zero);
  void grid(EditorController c) {
    c.addNullLayer(Duration.zero);
  }

  void element(EditorController c) =>
      c.addElement3DLayer(Duration.zero, Element3DKind.cube);
  void audio(EditorController c) => c.addAudioLayer(
    Duration.zero,
    'audio.wav',
    'Som',
    const Duration(seconds: 5),
  );

  // A secao da cena 3D nao entra neste laco: ela abre o ESTUDIO (uma
  // rota), nao uma ficha de parametros. O teste dela vem logo abaixo.
  // OS ROTULOS AQUI SAO OS DA GRADE DE HOJE.
  //
  // Eles mudaram desde que este laco foi escrito e o laco ficou vermelho
  // por isso: 'Cor e\npreench.' e abreviacao de uma grade antiga, 'Fonte'
  // e 'Caminho' viraram abas de dentro de "Editar texto" (a grade nao tem
  // mais secao para elas), 'Particulas' ganhou acento quando a interface
  // passou a ser traduzida, e a secao da cena 3D deixou de oferecer cor —
  // `secoesDe` da cor a forma, ao texto e ao elemento, e a cor da cena
  // mora no Estudio.
  final cases = <(String, void Function(EditorController), String)>[
    ('camera da composicao', camera, 'Câmera'),
    ('Grid / Clonar', grid, 'Clonar'),
    ('Elemento 3D', element, 'Elemento 3D'),
    ('cor do elemento 3D', element, 'Cor e preenchimento'),
    ('borda e sombra', text, 'Borda e sombra'),
    ('volume', audio, 'Volume'),
    ('particulas', (c) => c.addParticulasLayer(Duration.zero), 'Partículas'),
    (
      'legendas',
      (c) =>
          c.addCaptionLayerFromSrt('1\n00:00:00,000 --> 00:00:02,000\nTeste\n'),
      'Editar legendas',
    ),
    (
      'tempo da precomp',
      (c) => c.openProject(
        VideoProject.empty('Teste').copyWith(
          layers: [
            GroupLayer(
              name: 'Grupo',
              startTime: Duration.zero,
              duration: const Duration(seconds: 5),
            ),
          ],
        ),
      ),
      'Tempo',
    ),
  ];
  for (final (name, seed, button) in cases) {
    testWidgets('$name abre e fecha pelo menu real sem retirar o editor', (
      tester,
    ) async {
      final (observer, container) = await openEditor(tester, seed);
      final project = container.read(editorControllerProvider);
      // Reabrir detecta tambem callbacks que continuam fechando o menu antigo.
      for (var repeat = 0; repeat < 2; repeat++) {
        await tester.tap(find.text('abrir menu'));
        await tester.pumpAndSettle();
        // Acao rapida e tile podem ter o mesmo rotulo (Volume): o primeiro
        // serve — os dois abrem a mesma ficha.
        // 'Tempo' saiu da lista: a porta do grupo mora na doca (14/09).
        //
        // O DESVIO PELO "Mais" SAIU (16/09): as tres secoes que moravam
        // atras dele — Câmeras, Fonte e Caminho — viraram a secao 'Câmera'
        // da grade, as abas de dentro de "Editar texto" e a ficha do
        // Estudio. Nenhum caso deste laco precisa mais de dois toques.
        final target = find.text(button).evaluate().isNotEmpty
            ? find.text(button).first
            : find.byTooltip(button).first;
        await tester.ensureVisible(target);
        await tester.tap(target);
        await tester.pumpAndSettle();
        expect(observer.pagesPopped, 0, reason: '$name retirou o editor');
        expect(find.text('editor aberto'), findsOneWidget);
        expect(find.byType(ParamSheetShell), findsOneWidget);
        expect(tester.takeException(), isNull);
        closeParamSheet(tester.element(find.byType(ParamSheetShell)));
        await tester.pumpAndSettle();
        expect(observer.pagesPopped, 0);
        expect(find.byType(ParamSheetShell), findsNothing);
      }
      expect(container.read(editorControllerProvider), same(project));
    });
  }

  // OS DOIS TESTES DE "CENA 3D ABRE O ESTUDIO" SAIRAM COM O ESTUDIO:
  // o motor 3D antigo foi apagado inteiro (docs/3d-diligent.md) e nao ha
  // mais estudio para abrir. O que ficou no lugar e uma camada que nao
  // desenha, ate o backend novo entrar.

  // O "Grid" DA GRADE DE ADICIONAR VIROU O "NULO".
  //
  // A aba chama-se "Objeto / Elemento" (nao "Objeto") e a grade de clones
  // deixou de ser um tipo de camada proprio: quem carrega o modulo e o
  // Nulo, e a grade se liga depois, na secao "Clonar" da doca.
  for (final name in ['Nulo']) {
    testWidgets('adicionar $name mantem a composicao aberta', (tester) async {
      final (observer, container) = await openEditor(tester, (_) {});
      await tester.tap(find.text('adicionar'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Objeto / Elemento'));
      await tester.pumpAndSettle();
      await tester.tap(find.text(name));
      await tester.pumpAndSettle();
      expect(observer.pagesPopped, 0);
      final layers = container.read(editorControllerProvider).layers;
      expect(layers, hasLength(1));
      expect(layers.single, isA<NullLayer>());
      expect((layers.single as NullLayer).is3D, isTrue);
      expect(find.text('editor aberto'), findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  }

  testWidgets('grade oferece Importar 3D e nao oferece cena vazia', (
    tester,
  ) async {
    await openEditor(tester, (_) {});
    await tester.tap(find.text('adicionar'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Objeto / Elemento'));
    await tester.pumpAndSettle();
    expect(find.text('Importar 3D'), findsOneWidget);
    expect(find.text('Scene 3D'), findsNothing);
  });

  testWidgets('tocar em Particulas cria a camada antes de fechar a folha', (
    tester,
  ) async {
    final (observer, container) = await openEditor(tester, (_) {});
    await tester.tap(find.text('adicionar'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('Objeto / Elemento'));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('add-particulas')));
    await tester.pumpAndSettle();
    expect(observer.pagesPopped, 0);
    final layers = container.read(editorControllerProvider).layers;
    expect(layers, hasLength(1));
    expect(layers.single, isA<ParticulasLayer>());
    expect(layers.single.name, startsWith('Partículas 3D'));
    expect(find.byType(AddLayerPanel), findsNothing);
    expect(find.text('editor aberto'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });
}

class _NavigationObserver extends NavigatorObserver {
  int pagesPopped = 0;

  @override
  void didPop(Route<dynamic> route, Route<dynamic>? previousRoute) {
    if (route is PageRoute) pagesPopped++;
  }
}

class _MenuHost extends ConsumerStatefulWidget {
  const _MenuHost();

  @override
  ConsumerState<_MenuHost> createState() => _MenuHostState();
}

class _MenuHostState extends ConsumerState<_MenuHost>
    with SingleTickerProviderStateMixin {
  late final PlaybackController playback;
  bool _dock = false;

  @override
  void initState() {
    super.initState();
    playback = PlaybackController(
      vsync: this,
      durationOf: () => ref.read(editorControllerProvider).duration,
    );
  }

  @override
  void dispose() {
    playback.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    key: paramSheetHostKey,
    body: Column(
      children: [
        const Text('editor aberto'),
        TextButton(
          onPressed: () => showAddLayerSheet(context, ref, Duration.zero),
          child: const Text('adicionar'),
        ),
        TextButton(
          onPressed: () => setState(() => _dock = true),
          child: const Text('abrir menu'),
        ),
        // O E2 real (LayerToolsDock), no lugar do menu modal antigo.
        if (_dock)
          Expanded(
            child: LayerToolsDock(
              layer: ref.watch(editorControllerProvider).layers.first,
              playback: playback,
              onAction: (_) {},
            ),
          ),
      ],
    ),
  );
}
