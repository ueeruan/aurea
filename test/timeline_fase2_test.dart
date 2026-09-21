
import 'dart:convert';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/ui/shell/contrato.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/abrir_editor.dart' show openEditor;
import 'ui/timeline/montagem.dart';

/// FASE 2 DO REDESIGN — A TIMELINE (docs/UI_REDESIGN_PLAN.md, secao 3.2).
///
/// - Olho (some do preview e da exportacao) no cabecalho de cada linha;
///   cadeado (nao move, nao apara).
/// - Grupos: toque duplo entra; a saida mora na regua; os filhos editam
///   com tudo que o editor tem; sair grava num undo so.
///
/// Reordenar, mover, aparar e a virtualizacao da timeline nova estao em
/// test/ui/timeline/timeline_test.dart.
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

  testWidgets('olho esconde e mostra; a camada bloqueada nao anda no tempo', (
    tester,
  ) async {
    final b = await montarTimeline(
      tester,
      preparar: (c) => c.addShapeLayer(Duration.zero),
    );
    final id = b.projeto.layers.single.id;

    // O OLHO mora no cabecalho da linha (os 44 da direita).
    await tester.tap(find.byKey(ValueKey('olho-$id')));
    await tester.pumpAndSettle();
    expect(b.projeto.isHidden(id), isTrue);
    await tester.tap(find.byKey(ValueKey('olho-$id')));
    await tester.pumpAndSettle();
    expect(b.projeto.isHidden(id), isFalse);

    // Escolhida e bloqueada: arrastar o clipe nao move no tempo (o arrasto
    // vira scrub).
    b.c.toggleLocked(id);
    b.container.read(selectedLayerProvider.notifier).state = id;
    await tester.pumpAndSettle();
    Future<void> arrastarOClipe() async {
      final g = await tester.startGesture(pontoNoTempo(tester, b, id, 0.5));
      for (var i = 0; i < 5; i++) {
        await g.moveBy(const Offset(20, 0));
        await tester.pump(const Duration(milliseconds: 600));
      }
      await g.up();
      await tester.pumpAndSettle();
    }

    await arrastarOClipe();
    expect(
      b.projeto.layerById(id)!.startTime,
      Duration.zero,
      reason: 'cadeado segura',
    );

    b.c.toggleLocked(id);
    await tester.pumpAndSettle();
    await arrastarOClipe();
    expect(
      b.projeto.layerById(id)!.startTime,
      isNot(Duration.zero),
      reason: 'destravada move',
    );
  });

  testWidgets('grupo: toque duplo entra; a saida na regua volta', (
    tester,
  ) async {
    final b = await montarTimeline(
      tester,
      preparar: (c) {
        c.addShapeLayer(Duration.zero, name: 'A');
        c.addShapeLayer(Duration.zero, name: 'B');
      },
    );
    final ids = [for (final l in b.projeto.layers) l.id];
    b.c.groupLayers(ids);
    b.container.read(selectedLayerProvider.notifier).state = null;
    await tester.pumpAndSettle();
    final gid = b.projeto.layers.single.id;
    expect(find.byKey(const ValueKey('timeline-sair-do-grupo')), findsNothing);

    final ponto = pontoNoTempo(tester, b, gid, 0.5);
    await tester.tapAt(ponto);
    await tester.pump(const Duration(milliseconds: 60));
    await tester.tapAt(ponto);
    await tester.pumpAndSettle();
    expect(b.c.dentroDeGrupo, isTrue, reason: 'toque duplo entrou');
    expect(
      find.byKey(const ValueKey('timeline-sair-do-grupo')),
      findsOneWidget,
    );
    // Os filhos estao na timeline.
    for (final id in ids) {
      expect(
        find.byKey(ValueKey('linha-$id')),
        findsOneWidget,
        reason: 'filho $id visivel',
      );
    }

    await tester.tap(find.byKey(const ValueKey('timeline-sair-do-grupo')));
    await tester.pumpAndSettle();
    expect(b.c.dentroDeGrupo, isFalse);
    expect(find.byKey(const ValueKey('timeline-sair-do-grupo')), findsNothing);
    expect(b.container.read(selectedLayerProvider), gid);
  });

  testWidgets('o painel do grupo tem Entrar; Voltar sai do grupo', (
    tester,
  ) async {
    final c = await openEditor(tester);
    final e = c.read(editorControllerProvider.notifier);
    e.groupLayers(
      c.read(editorControllerProvider).layers.map((l) => l.id).toList(),
    );
    final gid = c.read(editorControllerProvider).layers.single.id;
    c.read(selectedLayerProvider.notifier).state = gid;
    await tester.pumpAndSettle();
    c.read(painelAbertoProvider.notifier).state = PainelId.grupo;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('grupo-entrar')), findsOneWidget);
    expect(find.byKey(const ValueKey('grupo-desagrupar')), findsOneWidget);
    await tester.ensureVisible(find.byKey(const ValueKey('grupo-entrar')));
    await tester.tap(find.byKey(const ValueKey('grupo-entrar')));
    await tester.pumpAndSettle();
    expect(e.dentroDeGrupo, isTrue);
    // Sem painel nem selecao, o Voltar da barra do topo sai um nivel.
    await tester.tap(find.byKey(const ValueKey('topo-voltar')));
    await tester.pumpAndSettle();
    expect(e.dentroDeGrupo, isFalse);
    expect(
      find.byKey(const ValueKey('editor-capture')),
      findsOneWidget,
      reason: 'ainda no editor',
    );
  });
}
