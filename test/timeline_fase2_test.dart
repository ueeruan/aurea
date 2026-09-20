
import 'dart:convert';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// FASE 2 DO REDESIGN — A TIMELINE (docs/UI_REDESIGN_PLAN.md, secao 3.2).
///
/// - Coluna esquerda fixa: olho (some do preview e da exportacao),
///   cadeado (nao move, nao apara), ◆ (vai ao keyframe).
/// - Reordenar por toque longo + arrastar; toque longo parado continua
///   alternando a selecao multipla.
/// - Grupos: toque duplo entra; Projeto › Grupo na regua; os filhos
///   editam com tudo que o editor tem; sair grava num undo so.
/// - Expandir timeline pelo canto da regua.
/// - Pedacos do mesmo arquivo, sem sobreposicao, na mesma linha.
/// - Regua sem os controles removidos a pedido dos betas.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  ProviderContainer controlador() {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    return c;
  }

  group('olho fechado (hidden)', () {
    test('some do preview e da exportacao, e sobrevive ao disco', () {
      final c = controlador();
      final e = c.read(editorControllerProvider.notifier);
      e.addShapeLayer(Duration.zero, name: 'A');
      e.addShapeLayer(Duration.zero, name: 'B');
      final a = c.read(editorControllerProvider).layers.last.id;
      expect(c.read(editorControllerProvider).rendersInPreview(a), isTrue);

      e.toggleHidden(a);
      final p = c.read(editorControllerProvider);
      expect(p.isHidden(a), isTrue);
      expect(p.rendersInPreview(a), isFalse);
      expect(e.projetoParaExportar.layers.map((l) => l.id), isNot(contains(a)));
      expect(
        p.layers.map((l) => l.id),
        contains(a),
        reason: 'continua no projeto',
      );

      final volta = projectFromJson(
        jsonDecode(jsonEncode(projectToJson(p))) as Map<String, dynamic>,
      );
      expect(volta.isHidden(a), isTrue);

      e.undo();
      expect(c.read(editorControllerProvider).isHidden(a), isFalse);
    });

    test('LayerMeta.hidden entra no isEmpty e no copyWith', () {
      const m = LayerMeta(hidden: true);
      expect(m.isEmpty, isFalse);
      expect(m.copyWith(hidden: false).isEmpty, isTrue);
    });
  });

  group('entrar no grupo', () {
    test('os filhos viram as camadas de trabalho; sair grava num undo so', () {
      final c = controlador();
      final e = c.read(editorControllerProvider.notifier);
      e.addShapeLayer(const Duration(seconds: 1), name: 'A');
      e.addShapeLayer(const Duration(seconds: 2), name: 'B');
      final ids = c
          .read(editorControllerProvider)
          .layers
          .map((l) => l.id)
          .toList();
      e.groupLayers(ids);
      final grupo =
          c.read(editorControllerProvider).layers.single as GroupLayer;
      expect(grupo.children.length, 2);
      final passosAntes = e.canUndo;

      e.enterGroup(grupo.id);
      expect(e.dentroDeGrupo, isTrue);
      // Numerado como no Alight Motion (14/09/2026).
      expect(e.caminhoDoGrupo, ['Grupo 1']);
      final dentro = c.read(editorControllerProvider);
      expect(dentro.layers.length, 2, reason: 'os filhos sao a timeline');
      expect(
        dentro.layers.map((l) => l.startTime),
        contains(Duration.zero),
        reason: 'tempo local ao grupo',
      );
      expect(c.read(selectedLayerProvider), isNull);
      expect(e.canUndo, isFalse, reason: 'pilha propria dentro do grupo');

      // Editar um filho com uma operacao comum do editor.
      final filho = dentro.layers.first.id;
      e.renameLayer(filho, 'Filho editado');
      e.moveLayer(filho, const Duration(milliseconds: 500));
      expect(e.canUndo, isTrue);

      // O projeto completo ja reflete a edicao (e o que se salva).
      final completo = e.projetoCompleto;
      final g = completo.layers.single as GroupLayer;
      expect(g.children.map((l) => l.name), contains('Filho editado'));

      e.exitGroup();
      expect(e.dentroDeGrupo, isFalse);
      final fora = c.read(editorControllerProvider);
      final g2 = fora.layers.single as GroupLayer;
      expect(g2.children.map((l) => l.name), contains('Filho editado'));
      expect(
        g2.children.firstWhere((l) => l.id == filho).startTime,
        const Duration(milliseconds: 500),
      );
      expect(
        c.read(selectedLayerProvider),
        grupo.id,
        reason: 'volta selecionando o grupo',
      );

      // Um undo so desfaz a edicao inteira feita la dentro.
      expect(e.canUndo, isTrue);
      e.undo();
      final g3 = c.read(editorControllerProvider).layers.single as GroupLayer;
      expect(g3.children.map((l) => l.name), isNot(contains('Filho editado')));
      expect(e.canUndo, passosAntes);
    });

    test('sair sem editar nao empilha undo; openProject fecha grupos', () {
      final c = controlador();
      final e = c.read(editorControllerProvider.notifier);
      e.addShapeLayer(Duration.zero, name: 'A');
      e.groupLayer(c.read(editorControllerProvider).layers.single.id);
      final gid = c.read(editorControllerProvider).layers.single.id;
      final n = e.canUndo;
      e.enterGroup(gid);
      e.exitGroup();
      expect(e.canUndo, n);
      e.enterGroup(gid);
      e.openProject(VideoProject.empty('Outro'));
      expect(e.dentroDeGrupo, isFalse);
    });

    test('a meta dos filhos (olho, cadeado) acompanha o grupo', () {
      final c = controlador();
      final e = c.read(editorControllerProvider.notifier);
      e.addShapeLayer(Duration.zero, name: 'A');
      e.groupLayer(c.read(editorControllerProvider).layers.single.id);
      final gid = c.read(editorControllerProvider).layers.single.id;
      e.enterGroup(gid);
      final filho = c.read(editorControllerProvider).layers.single.id;
      e.toggleLocked(filho);
      e.exitGroup();
      expect(c.read(editorControllerProvider).metaOf(filho).locked, isTrue);
    });
  });

  group('empacotamento visual', () {
    test('pedacos do mesmo arquivo sem sobreposicao dividem a linha', () {
      VideoLayer v(String nome, int s0, int dur) => VideoLayer(
        name: nome,
        startTime: Duration(seconds: s0),
        duration: Duration(seconds: dur),
        sourcePath: '/a.mp4',
      );
      final trilhas = empacotarTrilhas([
        v('b', 2, 2),
        v('a', 0, 2),
        v('c', 1, 2), // sobrepoe: linha nova
        VideoLayer(
          name: 'outro',
          startTime: Duration.zero,
          duration: const Duration(seconds: 1),
          sourcePath: '/b.mp4',
        ),
        ShapeLayer(
          name: 'forma',
          startTime: Duration.zero,
          duration: const Duration(seconds: 1),
        ),
        ShapeLayer(
          name: 'forma 2',
          startTime: Duration.zero,
          duration: const Duration(seconds: 1),
        ),
      ]);
      expect(trilhas.map((t) => t.length).toList(), [2, 1, 1, 1, 1]);
      expect(trilhas.first.map((l) => l.name), ['b', 'a']);
    });
  });

  testWidgets(
    'coluna esquerda: olho e cadeado por linha; bloqueada nao arrasta',
    (tester) async {
      final c = await openEditor(tester);
      final e = c.read(editorControllerProvider.notifier);
      final id = c.read(editorControllerProvider).layers.first.id;
      expect(find.byKey(ValueKey('olho-$id')), findsOneWidget);
      expect(find.byKey(ValueKey('kf-$id')), findsOneWidget);

      await tester.tap(find.byKey(ValueKey('olho-$id')));
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).isHidden(id), isTrue);
      expect(find.byTooltip('Mostrar camada'), findsOneWidget);

      await tester.longPress(find.byKey(ValueKey('kf-$id')));
      await tester.pumpAndSettle();
      expect(e.isLocked(id), isTrue);

      // Selecionada e bloqueada: arrastar a barra nao move no tempo.
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      final antes = c.read(editorControllerProvider).layerById(id)!.startTime;
      // Drag the clip body, not the left eye/lock column of its row.
      final linha = tester.getRect(find.byKey(ValueKey('clip-content-$id')));
      Future<void> moverBarra() async {
        final gesture = await tester.startGesture(Offset(linha.left + 90, linha.center.dy));
        // The editor uses hold-and-drag for clips; a short drag scrubs time.
        await tester.pump(const Duration(milliseconds: 600));
        await gesture.moveBy(const Offset(120, 0));
        await tester.pump();
        await gesture.up();
      }
      await moverBarra();
      await tester.pumpAndSettle();
      expect(
        c.read(editorControllerProvider).layerById(id)!.startTime,
        antes,
        reason: 'cadeado segura',
      );

      await tester.longPress(find.byKey(ValueKey('kf-$id')));
      await tester.pumpAndSettle();
      await moverBarra();
      await tester.pumpAndSettle();
      expect(
        c.read(editorControllerProvider).layerById(id)!.startTime,
        isNot(antes),
        reason: 'destravada move',
      );
    },
  );

  testWidgets(
    'toque longo + arrastar reordena; toque longo parado alterna a selecao multipla',
    (tester) async {
      final c = await openEditor(tester);
      final camadas = c.read(editorControllerProvider).layers;
      final deBaixo = camadas[1].id;
      final linha = tester.getRect(find.byKey(ValueKey(deBaixo)));
      final pegada = Offset(linha.left + 30, linha.top + kAmBarHeight / 2);

      // Toque longo e sobe uma linha, sem selecionar antes.
      final dedo = await tester.startGesture(pegada);
      await tester.pump(const Duration(milliseconds: 600));
      for (var i = 1; i <= 6; i++) {
        await dedo.moveBy(const Offset(0, -kAmRowHeight / 6));
        await tester.pump(const Duration(milliseconds: 16));
      }
      await dedo.up();
      await tester.pumpAndSettle();
      expect(
        c.read(editorControllerProvider).layers.first.id,
        deBaixo,
        reason: 'subiu na pilha',
      );
      expect(
        c.read(multiSelectProvider),
        isEmpty,
        reason: 'mover nao e selecionar',
      );

      // Toque longo parado: entra na selecao multipla.
      final linha2 = tester.getRect(find.byKey(ValueKey(deBaixo)));
      await tester.longPressAt(
        Offset(linha2.left + 30, linha2.top + kAmBarHeight / 2),
      );
      await tester.pumpAndSettle();
      expect(c.read(multiSelectProvider), contains(deBaixo));
    },
  );

  testWidgets(
    'grupo: toque duplo entra, Projeto › Grupo na regua, Projeto sai',
    (tester) async {
      final c = await openEditor(tester);
      final e = c.read(editorControllerProvider.notifier);
      final ids = c
          .read(editorControllerProvider)
          .layers
          .map((l) => l.id)
          .toList();
      e.groupLayers(ids);
      c.read(selectedLayerProvider.notifier).state = null;
      await tester.pumpAndSettle();
      final gid = c.read(editorControllerProvider).layers.single.id;
      expect(find.byKey(ValueKey('grupo-contagem-$gid')), findsOneWidget);
      expect(find.byKey(const ValueKey('timeline-breadcrumb')), findsNothing);

      final linha = tester.getRect(find.byKey(ValueKey(gid)));
      final ponto = Offset(linha.left + 30, linha.top + kAmBarHeight / 2);
      await tester.tapAt(ponto);
      await tester.pump(const Duration(milliseconds: 60));
      await tester.tapAt(ponto);
      await tester.pumpAndSettle();
      expect(e.dentroDeGrupo, isTrue, reason: 'toque duplo entrou');
      expect(find.byKey(const ValueKey('timeline-breadcrumb')), findsOneWidget);
      expect(find.text('Projeto'), findsOneWidget);
      expect(find.text('Grupo 1'), findsWidgets);
      // Os filhos estao na timeline.
      for (final id in ids) {
        expect(
          find.byKey(ValueKey(id)),
          findsOneWidget,
          reason: 'filho $id visivel',
        );
      }

      await tester.tap(find.byKey(const ValueKey('timeline-breadcrumb-0')));
      await tester.pumpAndSettle();
      expect(e.dentroDeGrupo, isFalse);
      expect(find.byKey(const ValueKey('timeline-breadcrumb')), findsNothing);
      expect(c.read(selectedLayerProvider), gid);
    },
  );

  testWidgets('E2 do grupo tem Entrar; Voltar sai do grupo', (tester) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    e.groupLayers(
      c.read(editorControllerProvider).layers.map((l) => l.id).toList(),
    );
    await tester.pumpAndSettle();
    // 14/09/2026: o "Mais" saiu na refacao do menu e levou o Entrar junto.
    // As portas do grupo agora moram na doca da camada.
    expect(find.byKey(const ValueKey('grupo-entrar')), findsOneWidget);
    expect(find.byKey(const ValueKey('grupo-desagrupar')), findsOneWidget);
    expect(find.byKey(const ValueKey('grupo-tempo')), findsOneWidget);
    await tester.ensureVisible(find.byKey(const ValueKey('grupo-entrar')));
    await tester.tap(find.byKey(const ValueKey('grupo-entrar')));
    await tester.pumpAndSettle();
    expect(e.dentroDeGrupo, isTrue);
    await tester.tap(find.byKey(const ValueKey('editor-back')));
    await tester.pumpAndSettle();
    expect(e.dentroDeGrupo, isFalse);
    expect(
      find.byKey(const ValueKey('editor-capture')),
      findsOneWidget,
      reason: 'ainda no editor',
    );
  });

  testWidgets('estado expandido ainda encolhe o preview sem icone na regua', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final preview = tester
        .getRect(find.byKey(const ValueKey('preview-resize-handle')))
        .top;
    expect(find.byKey(const ValueKey('timeline-expandir')), findsNothing);
    c.read(editorSessionProvider.notifier).toggleTimelineExpanded();
    await tester.pumpAndSettle();
    expect(c.read(editorSessionProvider).timelineExpanded, isTrue);
    expect(
      tester.getRect(find.byKey(const ValueKey('preview-resize-handle'))).top,
      lessThan(preview),
    );
    expect(find.byTooltip('Recolher timeline'), findsNothing);
    c.read(editorSessionProvider.notifier).toggleTimelineExpanded();
    await tester.pumpAndSettle();
    expect(c.read(editorSessionProvider).timelineExpanded, isFalse);
  });

  testWidgets('regua sem os controles removidos a pedido dos betas', (
    tester,
  ) async {
    final c = await openEditor(tester);
    expect(c.read(magneticProvider), isTrue);
    for (final key in [
      'timeline-ima',
      'timeline-buscar',
      'timeline-entrada',
      'timeline-saida',
    ]) {
      expect(find.byKey(ValueKey(key)), findsNothing);
    }
    expect(find.byKey(const ValueKey('timeline-expandir')), findsNothing);
    expect(find.byKey(const ValueKey('timeline-selecionar')), findsNothing);
  });
}
