import 'dart:async';

import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/scene3d.dart';
import 'package:aurea/src/features/editor/domain/texto3d.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/editor_shell.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/adicionar.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/barra_contextual.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/barra_do_lote.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/menu_da_camada.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// A lista de projetos em memoria: o salvamento automatico escreve aqui.
class _ProjetosEmMemoria extends ProjectsController {
  @override
  List<VideoProject> build() => [];

  @override
  void upsert(VideoProject project) {
    state = [project];
  }
}

/// Abre o editor num 390x844, com as camadas que [preparar] criar.
Future<ProviderContainer> _abrirEditor(
  WidgetTester tester, {
  void Function(EditorController c)? preparar,
}) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final container = ProviderContainer(
    overrides: [
      projectsControllerProvider.overrideWith(_ProjetosEmMemoria.new),
    ],
  );
  addTearDown(container.dispose);
  preparar?.call(container.read(editorControllerProvider.notifier));
  container.read(selectedLayerProvider.notifier).state = null;
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: const MaterialApp(home: EditorScreen()),
    ),
  );
  await tester.pumpAndSettle();
  return container;
}

/// O `ref` da casca (o elemento dela E um WidgetRef).
WidgetRef _refDaCasca(WidgetTester tester) =>
    tester.element(find.byType(EditorShell)) as WidgetRef;

/// O relogio do editor, lido de dentro da casca.
EscopoDoEditor _escopo(WidgetTester tester) => tester
    .element(find.byType(BarraContextual).first)
    .getInheritedWidgetOfExactType<EscopoDoEditor>()!;

T _ultima<T extends Layer>(ProviderContainer c) =>
    c.read(editorControllerProvider).layers.whereType<T>().first;

/// O "Mais" e o ultimo da barra: numa forma, a 390, ele esta alem da
/// borda — rola a barra ate ele e toca.
Future<void> _tocarNoMais(WidgetTester tester) async {
  final mais = find.byKey(const ValueKey('ferramenta-mais'), skipOffstage: false);
  await tester.ensureVisible(mais);
  await tester.pumpAndSettle();
  await tester.tap(find.byKey(const ValueKey('ferramenta-mais')));
  await tester.pumpAndSettle();
}

List<String> _ids(ProviderContainer c) => [
  for (final l in c.read(editorControllerProvider).layers) l.id,
];

void main() {
  group('barra contextual por tipo', () {
    Layer video() => VideoLayer(
      name: 'v',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      sourcePath: 'v.mp4',
    );
    Layer texto() => TextLayer(
      name: 't',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      text: 't',
    );
    Layer texto3d() => Scene3DLayer(
      name: 'T3D',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      scene: Scene3D(nodes: [SceneNode(texto3d: const Texto3D())]),
    );
    Layer modelo3d() => Scene3DLayer(
      name: 'M3D',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      scene: Scene3D(nodes: [SceneNode()]),
    );
    Layer forma() => ShapeLayer(
      name: 'f',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
    );
    Layer audio() => AudioLayer(
      name: 'a',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      sourcePath: 'a.m4a',
    );

    // O comeco da barra de cada tipo, na ordem em que se procura (§3.2 do
    // plano e docs/alight-motion-abas-e-paineis.md, nivel 1).
    final casos = <(String, Layer Function(), List<String>)>[
      ('vídeo', video, [
        'transformar', 'efeitos', 'cor', 'tempo', 'audio', 'mascara', //
      ]),
      ('texto', texto, ['texto', 'fonte', 'estilo', 'animar', 'efeitos']),
      ('texto 3D', texto3d, ['texto', 'texto3d', 'transformar', 'efeitos']),
      ('modelo 3D', modelo3d, [
        'transformar', 'material', 'luz', 'ambiente', 'animacao3d', //
      ]),
      ('forma', forma, ['transformar', 'forma', 'cor']),
      ('áudio', audio, ['audio', 'velocidade', 'efeitos']),
    ];

    for (final (nome, fazer, comeco) in casos) {
      testWidgets('$nome: as ferramentas na ordem, todas a vista', (
        tester,
      ) async {
        tester.view.physicalSize = const Size(1200, 200);
        tester.view.devicePixelRatio = 1;
        addTearDown(tester.view.resetPhysicalSize);
        addTearDown(tester.view.resetDevicePixelRatio);
        final camada = fazer();
        final lista = ferramentasDa(camada, aoAcionar: (_, _) {});
        final tocadas = <String>[];
        await tester.pumpWidget(
          MaterialApp(
            home: Scaffold(
              body: Align(
                alignment: Alignment.bottomCenter,
                child: BarraContextual(
                  ferramentas: lista,
                  ativo: lista.first.abre,
                  aoTocar: (f) => tocadas.add(f.id),
                ),
              ),
            ),
          ),
        );
        final ids = [for (final f in lista) f.id];
        expect(ids.take(comeco.length), comeco, reason: nome);
        // Na tela, da esquerda para a direita, na mesma ordem.
        final xs = [
          for (final id in ids)
            tester.getCenter(find.byKey(ValueKey('ferramenta-$id'))).dx,
        ];
        for (var i = 1; i < xs.length; i++) {
          expect(xs[i], greaterThan(xs[i - 1]), reason: '$nome ${ids[i]}');
        }
        // Bloco de 57, e a primeira (o painel aberto) acesa.
        expect(
          tester.getSize(find.byKey(const ValueKey('barra-contextual'))).height,
          57,
        );
        final primeira = tester.widget<AureaToolbarButton>(
          find.byKey(ValueKey('ferramenta-${ids.first}')),
        );
        expect(primeira.ativo, isTrue);
        expect(
          tester
              .widget<AureaToolbarButton>(
                find.byKey(ValueKey('ferramenta-${ids[1]}')),
              )
              .ativo,
          isFalse,
        );
        // O "Mais" (o menu da camada) existe em todo tipo e responde.
        await tester.tap(find.byKey(const ValueKey('ferramenta-mais')));
        expect(tocadas, [AcaoDaFerramenta.mais]);
      });
    }

    testWidgets('mais ferramentas do que tela: rola de lado, 64 cada', (
      tester,
    ) async {
      tester.view.physicalSize = const Size(360, 200);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final lista = ferramentasDa(texto());
      await tester.pumpWidget(
        MaterialApp(
          home: Scaffold(
            body: BarraContextual(ferramentas: lista, aoTocar: (_) {}),
          ),
        ),
      );
      expect(
        tester.getSize(find.byKey(const ValueKey('ferramenta-texto'))).width,
        64,
      );
      await tester.drag(
        find.byKey(const ValueKey('barra-contextual')),
        const Offset(-600, 0),
      );
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('ferramenta-mais')), findsOneWidget);
      expect(
        tester.getRect(find.byKey(const ValueKey('ferramenta-mais'))).right,
        lessThanOrEqualTo(360),
      );
    });
  });

  group('barra no editor', () {
    testWidgets('tocar numa ferramenta abre o painel certo e a acende', (
      tester,
    ) async {
      final c = await _abrirEditor(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'Forma');
          c.addTextLayer(Duration.zero);
        },
      );
      final forma = _ultima<ShapeLayer>(c);
      final texto = _ultima<TextLayer>(c);

      c.read(selectedLayerProvider.notifier).state = forma.id;
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('ferramenta-forma')));
      await tester.pumpAndSettle();
      expect(c.read(painelAbertoProvider), PainelId.forma);
      expect(find.byKey(const ValueKey('painel-forma')), findsOneWidget);
      c.read(painelAbertoProvider.notifier).state = null;

      c.read(selectedLayerProvider.notifier).state = texto.id;
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('ferramenta-fonte')));
      await tester.pumpAndSettle();
      expect(c.read(painelAbertoProvider), PainelId.fonte);
      expect(find.byKey(const ValueKey('painel-fonte')), findsOneWidget);
      expect(tester.takeException(), isNull);
    });

    testWidgets('nada escolhido: a barra do projeto, e o "+" no canto dela', (
      tester,
    ) async {
      final c = await _abrirEditor(tester);
      expect(find.byKey(const ValueKey('barra-do-projeto')), findsOneWidget);
      expect(find.byKey(const ValueKey('barra-contextual')), findsNothing);
      for (final id in ['projeto-adicionar', 'projeto-configuracoes']) {
        expect(find.byKey(ValueKey('ferramenta-$id')), findsOneWidget);
      }
      // Sem som nao ha legendas; com uma camada so, nada de "Selecionar".
      expect(
        find.byKey(const ValueKey('ferramenta-projeto-legendas')),
        findsNothing,
      );
      expect(
        find.byKey(const ValueKey('ferramenta-projeto-selecionar')),
        findsNothing,
      );
      // O "+" continua a 6 da borda, por cima do canto da barra.
      expect(
        tester.getRect(find.byKey(const ValueKey('editor-adicionar'))).bottom,
        844 - 6,
      );
      await tester.tap(
        find.byKey(const ValueKey('ferramenta-projeto-adicionar')),
      );
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('folha-de-adicionar')), findsOneWidget);
      expect(c.read(editorControllerProvider).layers, isEmpty);
    });
  });

  group('folha de adicionar', () {
    testWidgets('o "+" abre em 200 ms a folha de 251 com as 5 categorias', (
      tester,
    ) async {
      await _abrirEditor(tester);
      await tester.tap(find.byKey(const ValueKey('editor-adicionar')));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 100));
      final meio = tester.getRect(find.byType(BottomSheet)).top;
      await tester.pump(const Duration(milliseconds: 100));
      final fim = tester.getRect(find.byType(BottomSheet));
      // Aos 100 ms ainda subia; aos 200 ms chegou.
      expect(meio, greaterThan(fim.top));
      await tester.pumpAndSettle();
      expect(tester.getRect(find.byType(BottomSheet)), fim);
      expect(fim.height, 251);
      expect(fim.bottom, 844);
      for (final cat in CategoriaDeAdicionar.values) {
        final aba = find.byKey(ValueKey('adicionar-aba-${cat.id}'));
        expect(aba, findsOneWidget, reason: cat.id);
        expect(tester.getSize(aba).height, lessThanOrEqualTo(58));
      }
      expect(
        [for (final c in CategoriaDeAdicionar.values) c.rotulo],
        ['Mídia', 'Texto', 'Formas', '3D', 'Objetos'],
      );
      // Midia abre com o trilho de fontes: Recentes, Galeria, Audio.
      for (final f in ['recentes', 'galeria', 'audio']) {
        expect(find.byKey(ValueKey('adicionar-midia-$f')), findsOneWidget);
      }
      await tester.tap(find.byKey(const ValueKey('adicionar-fechar')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('folha-de-adicionar')), findsNothing);
    });

    testWidgets('Texto: cria a camada no cabecote, escolhe e fecha', (
      tester,
    ) async {
      final c = await _abrirEditor(tester);
      await tester.tap(find.byKey(const ValueKey('editor-adicionar')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('adicionar-aba-texto')));
      await tester.pumpAndSettle();
      for (final id in ['texto', 'legenda', 'texto3d']) {
        expect(find.byKey(ValueKey('adicionar-$id')), findsOneWidget);
      }
      await tester.tap(find.byKey(const ValueKey('adicionar-texto')));
      await tester.pumpAndSettle();
      final textos = c
          .read(editorControllerProvider)
          .layers
          .whereType<TextLayer>();
      expect(textos, hasLength(1));
      expect(c.read(selectedLayerProvider), textos.single.id);
      expect(find.byKey(const ValueKey('folha-de-adicionar')), findsNothing);
      // A barra da camada nova aparece.
      expect(find.byKey(const ValueKey('ferramenta-texto')), findsOneWidget);
    });

    testWidgets('3D: Do aparelho e Sketchfab moram na categoria 3D', (
      tester,
    ) async {
      await _abrirEditor(tester);
      await tester.tap(find.byKey(const ValueKey('editor-adicionar')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('adicionar-3d-aparelho')), findsNothing);
      await tester.tap(find.byKey(const ValueKey('adicionar-aba-3d')));
      await tester.pumpAndSettle();
      expect(
        find.byKey(const ValueKey('adicionar-3d-aparelho')),
        findsOneWidget,
      );
      expect(
        find.byKey(const ValueKey('adicionar-3d-sketchfab')),
        findsOneWidget,
      );
      expect(find.byKey(const ValueKey('adicionar-3d-texto3d')), findsOneWidget);
    });

    testWidgets('Objetos: nulo, camera, ajuste, particulas, grupo', (
      tester,
    ) async {
      final c = await _abrirEditor(tester);
      await tester.tap(find.byKey(const ValueKey('editor-adicionar')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('adicionar-aba-objetos')));
      await tester.pumpAndSettle();
      for (final id in ['nulo', 'camera', 'ajuste', 'particulas', 'grupo']) {
        expect(find.byKey(ValueKey('adicionar-$id')), findsOneWidget);
      }
      await tester.tap(find.byKey(const ValueKey('adicionar-nulo')));
      await tester.pumpAndSettle();
      final nulo = _ultima<NullLayer>(c);
      expect(c.read(selectedLayerProvider), nulo.id);
    });

    testWidgets('Formas: uma forma da biblioteca entra e fica escolhida', (
      tester,
    ) async {
      final c = await _abrirEditor(tester);
      await tester.tap(find.byKey(const ValueKey('editor-adicionar')));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('adicionar-aba-formas')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('adicionar-desenho-livre')), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('adicionar-forma-0')));
      await tester.pumpAndSettle();
      final forma = _ultima<ShapeLayer>(c);
      expect(c.read(selectedLayerProvider), forma.id);
      expect(tester.takeException(), isNull);
    });
  });

  group('menu da camada', () {
    test('tem os itens pedidos (forma e grupo)', () {
      final forma = ShapeLayer(
        name: 'f',
        startTime: Duration.zero,
        duration: const Duration(seconds: 2),
      );
      final grupo = GroupLayer(
        name: 'g',
        startTime: Duration.zero,
        duration: const Duration(seconds: 2),
      );
      final projeto = VideoProject.empty('p').copyWith(layers: [forma, grupo]);
      List<String> chaves(Layer l) => [
        for (final i in itensDoMenuDaCamada(
          projeto,
          l,
          const Duration(seconds: 1),
          temCamadaCopiada: false,
          temKeyframesCopiados: false,
          podeColarEstilo: false,
        ))
          i.chave!,
      ];
      expect(
        chaves(forma),
        containsAll([
          'camada-duplicar',
          'camada-dividir',
          'camada-apagar',
          'camada-renomear',
          'camada-ocultar',
          'camada-travar',
          'camada-agrupar',
          'camada-vincular',
          'camada-copiar',
          'camada-colar',
          'camada-copiar-keyframes',
          'camada-colar-keyframes',
          'camada-subir',
          'camada-descer',
          'camada-solo',
          'camada-alinhar',
          'camada-selecionar',
          'camada-mais-acoes',
        ]),
      );
      expect(
        chaves(grupo),
        containsAll([
          'camada-desagrupar',
          'camada-entrar-no-grupo',
          'camada-tempo-do-grupo',
        ]),
      );
      // Sem nada copiado, colar fica apagado (mas existe).
      final colar = itensDoMenuDaCamada(
        projeto,
        forma,
        Duration.zero,
        temCamadaCopiada: false,
        temKeyframesCopiados: false,
        podeColarEstilo: false,
      ).firstWhere((i) => i.chave == 'camada-colar');
      expect(colar.habilitado, isFalse);
      // "Mais ações" guarda o resto do menu antigo.
      final mais = [
        for (final i in itensDeMaisAcoes(
          projeto,
          forma,
          Duration.zero,
          temEfeitosCopiados: false,
          mudo: false,
          temBaseDeRecorte: false,
        ))
          i.chave!,
      ];
      expect(
        mais,
        containsAll([
          'camada-aparar-inicio',
          'camada-aparar-fim',
          'camada-copiar-efeitos',
          'camada-colar-efeitos',
          'camada-caber',
          'camada-espelhar-h',
          'camada-motion-blur',
          'camada-fechar-buracos',
        ]),
      );
    });

    testWidgets('"Mais" da barra abre o menu; Duplicar = um passo', (
      tester,
    ) async {
      final c = await _abrirEditor(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero, name: 'Forma'),
      );
      final id = _ultima<ShapeLayer>(c).id;
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      final antes = _ids(c);
      await _tocarNoMais(tester);
      final menu = find.byType(AureaMenu<String>);
      expect(menu, findsOneWidget);
      expect(tester.getSize(menu).width, 250);
      expect(
        tester.getSize(find.byKey(const ValueKey('menu-camada-duplicar'))),
        const Size(250, 40),
      );
      await tester.tap(find.byKey(const ValueKey('menu-camada-duplicar')));
      await tester.pumpAndSettle();
      expect(_ids(c), hasLength(2));
      // A copia vira a selecao.
      expect(c.read(selectedLayerProvider), isNot(id));
      c.read(editorControllerProvider.notifier).undo();
      await tester.pumpAndSettle();
      expect(_ids(c), antes);
    });

    testWidgets('toque longo (posicao): Apagar e Dividir, cada um um passo', (
      tester,
    ) async {
      final c = await _abrirEditor(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero, name: 'Forma'),
      );
      final ctrl = c.read(editorControllerProvider.notifier);
      final id = _ultima<ShapeLayer>(c).id;
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      final escopo = _escopo(tester);
      final ref = _refDaCasca(tester);
      final contexto = tester.element(find.byType(EditorShell));

      // DIVIDIR no cabecote (1 s, dentro da camada de 3 s).
      escopo.playback.seek(const Duration(seconds: 1));
      await tester.pumpAndSettle();
      unawaited(
        mostrarMenuDaCamada(
          contexto,
          ref,
          id,
          posicao: const Offset(200, 600),
          playback: escopo.playback,
        ),
      );
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('menu-camada-dividir')));
      await tester.pumpAndSettle();
      expect(_ids(c), hasLength(2));
      ctrl.undo();
      await tester.pumpAndSettle();
      expect(_ids(c), [id]);

      // APAGAR.
      unawaited(
        mostrarMenuDaCamada(
          contexto,
          ref,
          id,
          posicao: const Offset(200, 600),
          playback: escopo.playback,
        ),
      );
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('menu-camada-apagar')));
      await tester.pumpAndSettle();
      expect(_ids(c), isEmpty);
      ctrl.undo();
      await tester.pumpAndSettle();
      expect(_ids(c), [id]);
      expect(tester.takeException(), isNull);
    });

    testWidgets('toque longo num clipe nao escolhido passa a escolher ele', (
      tester,
    ) async {
      final c = await _abrirEditor(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'A');
          c.addShapeLayer(Duration.zero, name: 'B');
        },
      );
      final ids = _ids(c);
      c.read(selectedLayerProvider.notifier).state = ids.first;
      await tester.pumpAndSettle();
      final escopo = _escopo(tester);
      unawaited(
        mostrarMenuDaCamada(
          tester.element(find.byType(EditorShell)),
          _refDaCasca(tester),
          ids.last,
          posicao: const Offset(100, 700),
          playback: escopo.playback,
        ),
      );
      await tester.pumpAndSettle();
      expect(c.read(selectedLayerProvider), ids.last);
      // Fecha tocando fora.
      await tester.tapAt(const Offset(380, 20));
      await tester.pumpAndSettle();
      expect(find.byType(AureaMenu<String>), findsNothing);
    });
  });

  group('selecao multipla', () {
    testWidgets('"Selecionar várias" liga o modo e a barra do lote agrupa', (
      tester,
    ) async {
      final c = await _abrirEditor(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'A');
          c.addShapeLayer(Duration.zero, name: 'B');
        },
      );
      final ids = _ids(c);
      c.read(selectedLayerProvider.notifier).state = ids.first;
      await tester.pumpAndSettle();
      await _tocarNoMais(tester);
      await tester.tap(find.byKey(const ValueKey('menu-camada-selecionar')));
      await tester.pumpAndSettle();

      expect(c.read(modoSelecionarProvider), isTrue);
      expect(find.byKey(const ValueKey('barra-do-lote')), findsOneWidget);
      expect(find.byKey(const ValueKey('barra-contextual')), findsNothing);
      expect(
        find.descendant(
          of: find.byKey(const ValueKey('lote-contagem')),
          matching: find.text('1'),
        ),
        findsOneWidget,
      );
      // Com uma so, Agrupar existe mas nao age.
      AureaToolbarButton botao(String id) => tester.widget<AureaToolbarButton>(
        find.byKey(ValueKey('ferramenta-$id')),
      );
      expect(botao(AcaoDoLote.agrupar).aoTocar, isNull);
      for (final id in [
        AcaoDoLote.alinhar,
        AcaoDoLote.cascata,
        AcaoDoLote.vincular,
        AcaoDoLote.apagar,
      ]) {
        expect(find.byKey(ValueKey('ferramenta-$id')), findsOneWidget);
      }

      // O toque na segunda camada (timeline ou palco) marca.
      expect(alternarCamadaNoLote(_refDaCasca(tester), ids.last), isTrue);
      await tester.pumpAndSettle();
      expect(
        find.descendant(
          of: find.byKey(const ValueKey('lote-contagem')),
          matching: find.text('2'),
        ),
        findsOneWidget,
      );
      expect(botao(AcaoDoLote.agrupar).aoTocar, isNotNull);

      await tester.tap(find.byKey(const ValueKey('ferramenta-lote-agrupar')));
      await tester.pumpAndSettle();
      final grupos = c
          .read(editorControllerProvider)
          .layers
          .whereType<GroupLayer>();
      expect(grupos, hasLength(1));
      expect(grupos.single.children, hasLength(2));
      expect(c.read(modoSelecionarProvider), isFalse);
      expect(find.byKey(const ValueKey('barra-do-lote')), findsNothing);
      // Um passo: desfazer devolve as duas soltas.
      c.read(editorControllerProvider.notifier).undo();
      await tester.pumpAndSettle();
      expect(_ids(c), ids);
    });

    testWidgets('"Selecionar" da barra do projeto liga; Soltar desliga', (
      tester,
    ) async {
      final c = await _abrirEditor(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'A');
          c.addShapeLayer(Duration.zero, name: 'B');
        },
      );
      await tester.tap(
        find.byKey(const ValueKey('ferramenta-projeto-selecionar')),
      );
      await tester.pumpAndSettle();
      expect(c.read(modoSelecionarProvider), isTrue);
      expect(find.byKey(const ValueKey('barra-do-lote')), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('ferramenta-lote-soltar')));
      await tester.pumpAndSettle();
      expect(c.read(modoSelecionarProvider), isFalse);
      expect(find.byKey(const ValueKey('barra-do-lote')), findsNothing);
      expect(find.byKey(const ValueKey('barra-do-projeto')), findsOneWidget);
    });
  });
}
