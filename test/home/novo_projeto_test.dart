// NOVO PROJETO NUMA FOLHA SO: nome, proporcao, resolucao, fps, fundo e
// CRIAR — que poe o projeto na lista e abre o editor direto. Dois toques
// da Inicio ate o editor.
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/core/ds/aurea_teclado_numerico.dart'
    show TecladoNumerico;
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/projects/presentation/new_project_sheet.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio_home.dart';

void main() {
  testWidgets('"+ Novo projeto" abre a folha com todas as escolhas', (
    tester,
  ) async {
    await montarInicio(tester);
    await tester.tap(find.byKey(const ValueKey('novo-projeto')));
    await assentar(tester);

    expect(find.byKey(const ValueKey('projeto-nome')), findsOneWidget);
    for (final k in ['9:16', '16:9', '1:1', '4:5', '4:3', 'livre']) {
      expect(find.byKey(ValueKey('formato-$k')), findsOneWidget, reason: k);
    }
    for (final r in [720, 1080, 1440, 2160]) {
      expect(find.byKey(ValueKey('resolucao-$r')), findsOneWidget);
    }
    for (final f in [24, 25, 30, 60]) {
      expect(find.byKey(ValueKey('fps-$f')), findsOneWidget);
    }
    for (var i = 0; i < fundosDoProjeto.length; i++) {
      expect(find.byKey(ValueKey('fundo-$i')), findsOneWidget);
    }
    expect(find.byKey(const ValueKey('criar-projeto')), findsOneWidget);
    // O nome sugerido e a ficha viva do padrao dos Ajustes nunca mexidos
    // (9:16 — app de celular —, 1080, 30).
    expect(find.text('Projeto 1'), findsOneWidget);
    expect(find.text('1080 × 1920 · 30 fps'), findsOneWidget);
    // A ficha muda com o toque.
    await tester.tap(find.byKey(const ValueKey('formato-16:9')));
    await tester.pump();
    expect(find.text('1920 × 1080 · 30 fps'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('CRIAR com 1:1, 1080p e 30 fps cria o projeto certo e abre o '
      'editor', (tester) async {
    final (c, registro) = await montarInicio(
      tester,
      projetos: [VideoProject.empty('Antigo')],
    );
    // Toque 1: criar.
    await tester.tap(find.byKey(const ValueKey('novo-projeto')));
    await assentar(tester);
    await tester.tap(find.byKey(const ValueKey('formato-1:1')));
    await tester.tap(find.byKey(const ValueKey('resolucao-1080')));
    await tester.tap(find.byKey(const ValueKey('fps-30')));
    await tester.pump();
    expect(find.text('1080 × 1080 · 30 fps'), findsOneWidget);
    // Toque 2: CRIAR.
    await tester.tap(find.byKey(const ValueKey('criar-projeto')));
    await assentar(tester);

    final lista = c.read(projectsControllerProvider);
    expect(lista, hasLength(2));
    final novo = lista.first;
    expect(novo.name, 'Projeto 2');
    expect(novo.aspectRatio, 1.0);
    expect(novo.resolutionHeight, 1080);
    expect(novo.fps, 30);
    expect((novo.outputWidth, novo.outputHeight), (1080, 1080));
    expect(novo.backgroundColor, fundosDoProjeto.first.cor);
    // O MESMO projeto foi para o editor, e a tela do editor esta na frente.
    expect(registro.carregados.single.id, novo.id);
    expect(find.byKey(chaveDoEditorFalso), findsOneWidget);
    await c.read(projectsControllerProvider.notifier).flush();
  });

  testWidgets('nome digitado, 25 fps e fundo branco vao para o projeto', (
    tester,
  ) async {
    final (c, _) = await montarInicio(tester);
    await tester.tap(find.byKey(const ValueKey('novo-projeto')));
    await assentar(tester);
    await tester.enterText(
      find.byKey(const ValueKey('projeto-nome')),
      'Vinheta do canal',
    );
    await tester.tap(find.byKey(const ValueKey('formato-4:5')));
    await tester.tap(find.byKey(const ValueKey('resolucao-2160')));
    await tester.tap(find.byKey(const ValueKey('fps-25')));
    await tester.ensureVisible(find.byKey(const ValueKey('fundo-1')));
    await tester.tap(find.byKey(const ValueKey('fundo-1')));
    await tester.pump();
    await tester.ensureVisible(find.byKey(const ValueKey('criar-projeto')));
    await tester.tap(find.byKey(const ValueKey('criar-projeto')));
    await assentar(tester);

    final novo = c.read(projectsControllerProvider).single;
    expect(novo.name, 'Vinheta do canal');
    expect(novo.aspectRatio, closeTo(4 / 5, 1e-9));
    expect(novo.fps, 25);
    expect((novo.outputWidth, novo.outputHeight), (2160, 2700));
    expect(novo.backgroundColor, const Color(0xFFFFFFFF));
    await c.read(projectsControllerProvider.notifier).flush();
  });

  testWidgets('medida livre: os numeros viram o quadro, sem regua de '
      'resolucao', (tester) async {
    final (c, _) = await montarInicio(tester);
    await tester.tap(find.byKey(const ValueKey('novo-projeto')));
    await assentar(tester);
    expect(find.byKey(const ValueKey('livre-largura')), findsNothing);
    await tester.tap(find.byKey(const ValueKey('formato-livre')));
    await tester.pump();
    expect(find.byKey(const ValueKey('livre-largura')), findsOneWidget);
    expect(find.byKey(const ValueKey('livre-altura')), findsOneWidget);
    expect(find.byKey(const ValueKey('resolucao-1080')), findsNothing);

    // Largura 1920 pelo teclado do app.
    await tester.tap(find.byKey(const ValueKey('livre-largura')));
    await assentar(tester);
    expect(find.byType(TecladoNumerico), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('tecla-apagar')));
    for (final d in ['1', '9', '2', '0']) {
      await tester.tap(find.byKey(ValueKey('tecla-$d')));
    }
    await tester.pump();
    await tester.tap(find.text('OK'));
    await assentar(tester);
    expect(find.text('1920 × 1350 · 30 fps'), findsOneWidget);

    await tester.ensureVisible(find.byKey(const ValueKey('criar-projeto')));
    await tester.tap(find.byKey(const ValueKey('criar-projeto')));
    await assentar(tester);
    final novo = c.read(projectsControllerProvider).single;
    expect((novo.outputWidth, novo.outputHeight), (1920, 1350));
    await c.read(projectsControllerProvider.notifier).flush();
  });

  testWidgets('fechar a folha nao cria nada', (tester) async {
    final (c, registro) = await montarInicio(tester);
    await tester.tap(find.byKey(const ValueKey('novo-projeto')));
    await assentar(tester);
    await tester.tap(find.byKey(const ValueKey('folha-fechar')));
    await assentar(tester);
    expect(c.read(projectsControllerProvider), isEmpty);
    expect(registro.carregados, isEmpty);
    expect(find.byKey(chaveDoEditorFalso), findsNothing);
  });

  test('o quadro sai da proporcao e da resolucao pela regra do projeto', () {
    expect(quadroDoFormato(9 / 16, 1080), (largura: 1080, altura: 1920));
    expect(quadroDoFormato(16 / 9, 720), (largura: 1280, altura: 720));
    expect(quadroDoFormato(1, 2160), (largura: 2160, altura: 2160));
  });
}
