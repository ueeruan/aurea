import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/perfil3d.dart';
import 'package:aurea/src/features/editor/domain/am_sections.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/element3d.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/ui/paineis/registro.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/editor_shell.dart';
import 'package:aurea/src/features/editor/presentation/ui/timeline/timeline.dart';
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

/// Abre o editor num aparelho de [tamanho] (390x844 = iPhone 14), com as
/// camadas que [preparar] criar ANTES de a tela nascer.
Future<ProviderContainer> abrirEditor(
  WidgetTester tester, {
  Size tamanho = const Size(390, 844),
  void Function(EditorController c)? preparar,
}) async {
  tester.view.physicalSize = tamanho;
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  final container = ProviderContainer(
    overrides: [
      projectsControllerProvider.overrideWith(_ProjetosEmMemoria.new),
    ],
  );
  addTearDown(container.dispose);
  final c = container.read(editorControllerProvider.notifier);
  preparar?.call(c);
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

/// Toca no CLIPE da camada [id] (o cabecote fica no centro, no tempo 0, e
/// o clipe comeca nele — entao o clipe esta a direita do centro).
Future<void> tocarNoClipe(WidgetTester tester, String id) async {
  final linha = find.byKey(ValueKey('linha-$id'));
  await tester.tapAt(tester.getCenter(linha) + const Offset(40, 0));
  await tester.pumpAndSettle();
}

void main() {
  group('casca do editor', () {
    testWidgets('monta com projeto vazio: zonas, "+" e aviso da timeline', (
      tester,
    ) async {
      await abrirEditor(tester);
      expect(find.byType(EditorShell), findsOneWidget);
      expect(find.byKey(const ValueKey('barra-do-topo')), findsOneWidget);
      expect(find.byKey(const ValueKey('barra-de-transporte')), findsOneWidget);
      expect(find.byType(TimelineDoEditor), findsOneWidget);
      expect(find.byKey(const ValueKey('editor-adicionar')), findsOneWidget);
      expect(find.byKey(const ValueKey('barra-contextual')), findsNothing);
      expect(
        find.text('Toque em + para adicionar a primeira camada'),
        findsOneWidget,
      );
      expect(tester.takeException(), isNull);
    });

    testWidgets('monta com camadas: uma linha por camada', (tester) async {
      final c = await abrirEditor(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'Fundo');
          c.addTextLayer(Duration.zero);
          c.addNullLayer(Duration.zero);
        },
      );
      for (final l in c.read(editorControllerProvider).layers) {
        expect(find.byKey(ValueKey('linha-${l.id}')), findsOneWidget);
      }
      expect(tester.takeException(), isNull);
    });

    testWidgets('390x844: topo 42, previa o resto, transporte 46, '
        'timeline 280, "+" de 73 a 6 da borda', (tester) async {
      await abrirEditor(tester);
      expect(
        tester.getSize(find.byKey(const ValueKey('barra-do-topo'))).height,
        42,
      );
      expect(
        tester
            .getSize(find.byKey(const ValueKey('barra-de-transporte')))
            .height,
        46,
      );
      expect(
        tester.getSize(find.byKey(const ValueKey('zona-timeline'))).height,
        280,
      );
      expect(
        tester.getSize(find.byKey(const ValueKey('zona-previa'))).height,
        844 - 42 - 46 - 280,
      );
      final mais = tester.getRect(
        find.byKey(const ValueKey('editor-adicionar')),
      );
      expect(mais.size, const Size(73, 73));
      expect(mais.right, 390 - 6);
      expect(mais.bottom, 844 - 6);
      // A regua da timeline tem 42.
      expect(
        tester.getSize(find.byKey(const ValueKey('timeline-regua'))).height,
        42,
      );
    });

    testWidgets('selecionar camada troca a barra de ferramentas pelo tipo', (
      tester,
    ) async {
      final c = await abrirEditor(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'Forma');
          c.addTextLayer(Duration.zero);
        },
      );
      final camadas = c.read(editorControllerProvider).layers;
      final forma = camadas.whereType<ShapeLayer>().single;
      final texto = camadas.whereType<TextLayer>().single;

      await tocarNoClipe(tester, forma.id);
      expect(c.read(selectedLayerProvider), forma.id);
      expect(find.byKey(const ValueKey('barra-contextual')), findsOneWidget);
      expect(find.byKey(const ValueKey('ferramenta-forma')), findsOneWidget);
      expect(find.byKey(const ValueKey('ferramenta-fonte')), findsNothing);
      // O "+" sobe para cima da barra contextual.
      expect(
        tester.getRect(find.byKey(const ValueKey('editor-adicionar'))).bottom,
        844 - 6 - 57,
      );

      await tocarNoClipe(tester, texto.id);
      expect(c.read(selectedLayerProvider), texto.id);
      expect(find.byKey(const ValueKey('ferramenta-texto')), findsOneWidget);
      expect(find.byKey(const ValueKey('ferramenta-fonte')), findsOneWidget);
      expect(find.byKey(const ValueKey('ferramenta-forma')), findsNothing);

      // Toque no vazio da timeline solta a selecao e a barra some.
      await tester.tapAt(
        tester.getCenter(find.byKey(ValueKey('linha-${texto.id}'))) -
            const Offset(150, 0),
      );
      await tester.pumpAndSettle();
      expect(c.read(selectedLayerProvider), isNull);
      expect(find.byKey(const ValueKey('barra-contextual')), findsNothing);
    });

    testWidgets('tocar numa ferramenta abre o painel dela (200, por cima da '
        'timeline); o ✓ fecha', (tester) async {
      final c = await abrirEditor(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero, name: 'Forma'),
      );
      final id = c.read(editorControllerProvider).layers.single.id;
      await tocarNoClipe(tester, id);

      await tester.tap(find.byKey(const ValueKey('ferramenta-transformar')));
      await tester.pumpAndSettle();
      expect(c.read(painelAbertoProvider), PainelId.transformar);
      final painel = find.byKey(const ValueKey('painel-transformar'));
      expect(painel, findsOneWidget);
      final r = tester.getRect(painel);
      expect(r.height, 200);
      // SOBE POR CIMA da parte de baixo da timeline: a base do painel e a
      // base da tela, e a regua continua a vista acima dele.
      expect(r.bottom, 844);
      final regua = tester.getRect(find.byKey(const ValueKey('timeline-regua')));
      expect(regua.bottom, lessThanOrEqualTo(r.top));
      // Com painel aberto o "+" sai.
      expect(find.byKey(const ValueKey('editor-adicionar')), findsNothing);

      // O ✓ fecha.
      await tester.tap(find.byKey(const ValueKey('painel-transformar-fechar')));
      await tester.pumpAndSettle();
      expect(c.read(painelAbertoProvider), isNull);
      expect(painel, findsNothing);

      // Outro painel pela barra; o painel cobre a barra, e o ✓ dele fecha.
      await tester.tap(find.byKey(const ValueKey('ferramenta-efeitos')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('painel-efeitos')), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('painel-efeitos-fechar')));
      await tester.pumpAndSettle();
      expect(c.read(painelAbertoProvider), isNull);
      expect(find.byKey(const ValueKey('editor-adicionar')), findsOneWidget);
      expect(tester.takeException(), isNull);
    });

    testWidgets('o painel Transformar edita pela API do controlador', (
      tester,
    ) async {
      final c = await abrirEditor(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero, name: 'Forma'),
      );
      final id = c.read(editorControllerProvider).layers.single.id;
      await tocarNoClipe(tester, id);
      await tester.tap(find.byKey(const ValueKey('ferramenta-transformar')));
      await tester.pumpAndSettle();
      // Aba Opacidade: arrastar a linha para a ESQUERDA baixa a opacidade.
      await tester.tap(find.byKey(const ValueKey('painel-transformar-aba-3')));
      await tester.pumpAndSettle();
      await tester.drag(
        find.byKey(const ValueKey('prop-opacidade')),
        const Offset(-80, 0),
      );
      await tester.pumpAndSettle();
      final l = c.read(editorControllerProvider).layerById(id)!;
      expect(l.opacity.valueAt(Duration.zero), lessThan(1));
      // O losango crava a marca no cabecote.
      await tester.tap(find.byKey(const ValueKey('kf-opacidade')));
      await tester.pumpAndSettle();
      expect(
        c.read(editorControllerProvider).layerById(id)!.opacity.isAnimated,
        isTrue,
      );
    });

    testWidgets('o painel Efeitos mostra a pilha: abre o cartao, desliga', (
      tester,
    ) async {
      final tipo = effectsInCategory('Color').first;
      final c = await abrirEditor(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'Forma');
        },
      );
      final id = c.read(editorControllerProvider).layers.single.id;
      c.read(editorControllerProvider.notifier).addEffect(id, tipo);
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('ferramenta-efeitos')));
      await tester.pumpAndSettle();
      final efeito = c.read(editorControllerProvider).layerById(id)!.effects.single;
      final k = 'efeito-${efeito.id}';
      expect(find.byKey(ValueKey('$k-cabecalho')), findsOneWidget);
      expect(find.byKey(ValueKey('$k-corpo')), findsNothing);
      await tester.tap(find.byKey(ValueKey('$k-seta')));
      await tester.pumpAndSettle();
      expect(find.byKey(ValueKey('$k-corpo')), findsOneWidget);
      await tester.tap(find.byKey(ValueKey('$k-olho')));
      await tester.pumpAndSettle();
      expect(
        c.read(editorControllerProvider).layerById(id)!.effects.single.enabled,
        isFalse,
      );
      expect(tester.takeException(), isNull);
    });

    testWidgets('um arrasto de slider nao reconstroi editor, casca nem '
        'timeline — e vira UM passo de desfazer', (tester) async {
      final c = await abrirEditor(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero, name: 'Forma'),
      );
      final id = c.read(editorControllerProvider).layers.single.id;
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      c.read(painelAbertoProvider.notifier).state = PainelId.transformar;
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('painel-transformar-aba-3')));
      await tester.pumpAndSettle();
      final ctrl = c.read(editorControllerProvider.notifier);
      final antes = c.read(editorControllerProvider);

      Perfil3D.zerar();
      Perfil3D.ligado = true;
      try {
        final g = await tester.startGesture(
          tester.getCenter(find.byKey(const ValueKey('prop-opacidade'))),
        );
        for (var i = 0; i < 12; i++) {
          await g.moveBy(const Offset(-8, 0));
          await tester.pump(const Duration(milliseconds: 16));
        }
        await g.up();
        await tester.pump();
      } finally {
        Perfil3D.ligado = false;
      }
      final r = Perfil3D.relatorio();
      final opacidade = c
          .read(editorControllerProvider)
          .layerById(id)!
          .opacity
          .valueAt(Duration.zero);
      expect(opacidade, lessThan(1), reason: 'o arrasto mudou o valor');
      expect(r.contas['build.editor'] ?? 0, 0);
      expect(r.contas['build.casca'] ?? 0, 0);
      expect(r.contas['build.timeline'] ?? 0, 0);
      // UM passo: desfazer uma vez volta ao projeto de antes do arrasto.
      ctrl.undo();
      expect(
        c.read(editorControllerProvider).layerById(id)!.opacity.valueAt(
          Duration.zero,
        ),
        antes.layerById(id)!.opacity.valueAt(Duration.zero),
      );
    });

    testWidgets('trocar de camada fecha o painel aberto', (tester) async {
      final c = await abrirEditor(
        tester,
        preparar: (c) {
          c.addShapeLayer(Duration.zero, name: 'A');
          c.addShapeLayer(Duration.zero, name: 'B');
        },
      );
      final ids = [
        for (final l in c.read(editorControllerProvider).layers) l.id,
      ];
      await tocarNoClipe(tester, ids.first);
      await tester.tap(find.byKey(const ValueKey('ferramenta-transformar')));
      await tester.pumpAndSettle();
      expect(c.read(painelAbertoProvider), PainelId.transformar);
      c.read(selectedLayerProvider.notifier).state = ids.last;
      await tester.pumpAndSettle();
      expect(c.read(painelAbertoProvider), isNull);
    });

    testWidgets('TODO painel registrado abre sem quebrar', (tester) async {
      final c = await abrirEditor(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero, name: 'Forma'),
      );
      final id = c.read(editorControllerProvider).layers.single.id;
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      expect(paineisRegistrados.keys.toSet(), PainelId.values.toSet());
      for (final p in PainelId.values) {
        c.read(painelAbertoProvider.notifier).state = p;
        await tester.pumpAndSettle();
        expect(
          find.byKey(ValueKey('painel-${p.name}')),
          findsOneWidget,
          reason: 'painel ${p.name}',
        );
        expect(tester.takeException(), isNull, reason: 'painel ${p.name}');
      }
    });

    testWidgets('o "+" abre a folha de adicionar, e Texto cria a camada', (
      tester,
    ) async {
      final c = await abrirEditor(tester);
      await tester.tap(find.byKey(const ValueKey('editor-adicionar')));
      await tester.pumpAndSettle();
      // As categorias da folha nova (test/ui/toolbar cobre o resto).
      expect(find.byKey(const ValueKey('adicionar-aba-midia')), findsOneWidget);
      expect(find.byKey(const ValueKey('adicionar-aba-3d')), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('adicionar-aba-texto')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('adicionar-texto')), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('adicionar-texto')));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('adicionar-texto')), findsNothing);
      final camadas = c.read(editorControllerProvider).layers;
      expect(camadas.whereType<TextLayer>(), hasLength(1));
    });

    testWidgets('desfazer e refazer respondem (e acendem so quando podem)', (
      tester,
    ) async {
      final c = await abrirEditor(tester);
      final desfazer = find.byKey(const ValueKey('transporte-desfazer'));
      final refazer = find.byKey(const ValueKey('transporte-refazer'));
      c.read(editorControllerProvider.notifier).addShapeLayer(
        Duration.zero,
        name: 'Forma',
      );
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).layers, hasLength(1));
      await tester.tap(desfazer);
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).layers, isEmpty);
      await tester.tap(refazer);
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).layers, hasLength(1));
    });

    testWidgets('o salvamento automatico continua: mutacao vai a lista', (
      tester,
    ) async {
      final c = await abrirEditor(tester);
      c.read(editorControllerProvider.notifier).addTextLayer(Duration.zero);
      await tester.pumpAndSettle();
      final salvos = c.read(projectsControllerProvider);
      expect(salvos, hasLength(1));
      expect(salvos.single.layers.whereType<TextLayer>(), hasLength(1));
    });

    for (final tamanho in const [Size(360, 640), Size(360, 800)]) {
      testWidgets('tela pequena $tamanho monta e abre paineis sem estourar', (
        tester,
      ) async {
        final c = await abrirEditor(
          tester,
          tamanho: tamanho,
          preparar: (c) => c.addTextLayer(Duration.zero),
        );
        final id = c.read(editorControllerProvider).layers.single.id;
        await tocarNoClipe(tester, id);
        expect(c.read(selectedLayerProvider), id);
        for (final p in [
          PainelId.transformar,
          PainelId.texto,
          PainelId.efeitos,
          PainelId.propriedades,
        ]) {
          c.read(painelAbertoProvider.notifier).state = p;
          await tester.pumpAndSettle();
          expect(tester.takeException(), isNull, reason: '$tamanho $p');
        }
        // A previa nunca fica com menos de 160.
        expect(
          tester.getSize(find.byKey(const ValueKey('zona-previa'))).height,
          greaterThanOrEqualTo(160),
        );
      });
    }

    testWidgets('play/pausa pelo transporte', (tester) async {
      final c = await abrirEditor(
        tester,
        preparar: (c) => c.addShapeLayer(Duration.zero, name: 'Forma'),
      );
      expect(c.read(editorControllerProvider).layers, hasLength(1));
      await tester.tap(find.byKey(const ValueKey('transporte-play')));
      await tester.pump(const Duration(milliseconds: 200));
      await tester.tap(find.byKey(const ValueKey('transporte-play')));
      await tester.pumpAndSettle();
      expect(tester.takeException(), isNull);
    });
  });

  group('contrato: ferramentasDa', () {
    test('sem camada, a barra e vazia', () {
      expect(ferramentasDa(null), isEmpty);
    });

    test('toda secao de cada tipo tem porta na barra (nada fica de fora)', () {
      final container = ProviderContainer(
        overrides: [
          projectsControllerProvider.overrideWith(_ProjetosEmMemoria.new),
        ],
      );
      addTearDown(container.dispose);
      final c = container.read(editorControllerProvider.notifier)
        ..addTextLayer(Duration.zero)
        ..addShapeLayer(Duration.zero)
        ..addNullLayer(Duration.zero)
        ..addCameraLayer(Duration.zero)
        ..addEmptyGroup(Duration.zero)
        ..addAdjustmentLayer(Duration.zero)
        ..addParticulasLayer(Duration.zero)
        ..addElement3DLayer(Duration.zero, Element3DKind.cube);
      final camadas = <Layer>[
        ...container.read(editorControllerProvider).layers,
        VideoLayer(
          name: 'v',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          sourcePath: 'v.mp4',
        ),
        ImageLayer(
          name: 'i',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          sourcePath: 'i.png',
        ),
        AudioLayer(
          name: 'a',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
          sourcePath: 'a.m4a',
        ),
        Scene3DLayer(
          name: 's',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
        ),
        CaptionLayer(
          name: 'l',
          startTime: Duration.zero,
          duration: const Duration(seconds: 2),
        ),
      ];
      expect(c, isNotNull);

      // A PORTA de cada secao da grade antiga, por tipo.
      Set<String> portasDa(AmSecao s, Layer l) => switch (s) {
        AmSecao.moverTransformar => {PainelId.transformar.name},
        AmSecao.corPreenchimento => l is Element3DLayer
            ? {PainelId.material.name, PainelId.cor.name}
            : {PainelId.cor.name},
        AmSecao.bordaSombra => {PainelId.bordaSombra.name},
        AmSecao.mesclarOpacidade => {PainelId.mascara.name},
        AmSecao.volume || AmSecao.fade => {PainelId.audio.name},
        AmSecao.tempo => {PainelId.tempo.name},
        AmSecao.editarForma => {PainelId.forma.name},
        AmSecao.clonar => {PainelId.clonar.name},
        AmSecao.editarTexto => {PainelId.texto.name},
        AmSecao.ativar3d => {AcaoDaFerramenta.ativar3d},
        AmSecao.editarLegendas => {PainelId.legendas.name},
        AmSecao.particulas => {PainelId.particulas.name},
        AmSecao.cena3d => {PainelId.cena3d.name, PainelId.material.name},
        AmSecao.texto3d => {PainelId.texto3d.name},
        AmSecao.rastrear => {PainelId.rastrear.name},
        AmSecao.camera => {PainelId.camera.name},
        AmSecao.efeitos => {PainelId.efeitos.name},
      };

      for (final l in camadas) {
        final ids = {for (final f in ferramentasDa(l)) f.id};
        for (final s in secoesDe(l)) {
          expect(
            portasDa(s, l).intersection(ids),
            isNotEmpty,
            reason: '${l.runtimeType}: a secao ${s.name} ficou sem porta',
          );
        }
        // O menu da camada e as propriedades existem para todo tipo.
        expect(ids, contains(AcaoDaFerramenta.mais), reason: '${l.runtimeType}');
        expect(ids, contains(PainelId.propriedades.name));
        // Toda ferramenta ou abre painel registrado ou e acao.
        for (final f in ferramentasDa(l)) {
          if (f.abre != null) {
            expect(paineisRegistrados.containsKey(f.abre), isTrue);
          }
        }
      }
      // As listas do plano (§3.2).
      List<String> de(Layer l) => [for (final f in ferramentasDa(l)) f.id];
      final video = camadas.whereType<VideoLayer>().single;
      expect(
        de(video).take(6),
        ['transformar', 'efeitos', 'cor', 'tempo', 'audio', 'mascara'],
      );
      final texto = camadas.whereType<TextLayer>().single;
      expect(
        de(texto).take(5),
        ['texto', 'fonte', 'estilo', 'animar', 'efeitos'],
      );
      expect(de(texto), contains('transformar'));
      final cena = camadas.whereType<Scene3DLayer>().single;
      expect(
        de(cena),
        containsAll(['transformar', 'material', 'luz', 'ambiente', 'animacao3d']),
      );
    });

    test('acao direta so nasce ligada com quem a execute', () {
      final texto = TextLayer(
        name: 't',
        startTime: Duration.zero,
        duration: const Duration(seconds: 1),
        text: 't',
      );
      final soltas = ferramentasDa(texto).where((f) => f.abre == null);
      expect(soltas.every((f) => f.acao == null), isTrue);
      final chamadas = <(String, String)>[];
      final ligadas = ferramentasDa(
        texto,
        aoAcionar: (a, id) => chamadas.add((a, id)),
      ).where((f) => f.abre == null);
      for (final f in ligadas) {
        f.acao!();
      }
      expect(
        chamadas.map((c) => c.$1),
        containsAll([AcaoDaFerramenta.mais, AcaoDaFerramenta.dividir]),
      );
      expect(chamadas.every((c) => c.$2 == texto.id), isTrue);
    });
  });
}
