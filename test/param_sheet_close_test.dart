import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/presentation/am/am_widgets.dart';

/// O PAINEL DE PARAMETRO NAO E UMA ROTA.
///
/// Ele e a folha persistente do Scaffold do editor. Fechamentos devem
/// atingir somente essa folha, inclusive em trocas e chamadas repetidas,
/// sem consumir a rota do editor. Estes testes protegem essa navegacao.
void main() {
  setUp(() => RecentSheets.instance.clear());
  tearDown(() => RecentSheets.instance.clear());

  Future<void> openEditor(WidgetTester tester) async {
    await tester.pumpWidget(
      MaterialApp(
        theme: ThemeData(platform: TargetPlatform.iOS),
        home: Builder(
          builder: (context) => Scaffold(
            body: TextButton(
              onPressed: () => Navigator.of(context).push(
                MaterialPageRoute<void>(builder: (_) => const _EditorFalso()),
              ),
              child: const Text('tela inicial'),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('tela inicial'));
    await tester.pumpAndSettle();
  }

  testWidgets('contexto do editor nao pode fechar sua rota como painel', (
    tester,
  ) async {
    await openEditor(tester);
    closeParamSheet(tester.element(find.text('editor')));
    await tester.pumpAndSettle();
    expect(find.text('editor'), findsOneWidget);
    expect(find.text('tela inicial'), findsNothing);
  });

  testWidgets('fechamento repetido nao fecha o editor nem painel substituto', (
    tester,
  ) async {
    await openEditor(tester);
    await tester.tap(find.text('abrir painel'));
    await tester.pumpAndSettle();
    final close = ParamSheetScope.maybeOf(
      tester.element(find.text('conteudo do painel')),
    )!;
    close();
    close();
    await tester.pumpAndSettle();
    await tester.tap(find.text('abrir painel'));
    await tester.pumpAndSettle();
    close();
    await tester.pumpAndSettle();
    expect(find.text('conteudo do painel'), findsOneWidget);
    expect(find.text('editor'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('atalho recente troca de ferramenta sem sair do editor', (
    tester,
  ) async {
    await openEditor(tester);
    final context = tester.element(find.text('editor'));
    showParamSheet(
      context,
      title: 'Primeiro',
      builder: (_) => const Text('primeira ferramenta'),
    );
    await tester.pumpAndSettle();
    showParamSheet(
      context,
      title: 'Segundo',
      builder: (_) => const Text('segunda ferramenta'),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.text('Primeiro'));
    await tester.pumpAndSettle();
    expect(find.text('primeira ferramenta'), findsOneWidget);
    expect(find.text('segunda ferramenta'), findsNothing);
    expect(find.text('editor'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('ferramenta da rota 3D usa o Scaffold dessa rota', (
    tester,
  ) async {
    await openEditor(tester);
    final context = tester.element(find.text('editor'));
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => Scaffold(
          body: Builder(
            builder: (studioContext) => TextButton(
              onPressed: () => showParamSheet(
                studioContext,
                builder: (_) => const Text('ferramenta 3D'),
              ),
              child: const Text('estudio 3D'),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    await tester.tap(find.text('estudio 3D'));
    await tester.pumpAndSettle();
    expect(find.text('ferramenta 3D'), findsOneWidget);
    await tester.tap(find.byIcon(CupertinoIcons.chevron_back));
    await tester.pumpAndSettle();
    expect(find.text('estudio 3D'), findsOneWidget);
    expect(find.text('ferramenta 3D'), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('fechar o painel nao derruba o editor', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: Builder(
          builder: (context) => Scaffold(
            body: Center(
              child: TextButton(
                onPressed: () => Navigator.of(context).push(
                  MaterialPageRoute<void>(builder: (_) => const _EditorFalso()),
                ),
                child: const Text('tela inicial'),
              ),
            ),
          ),
        ),
      ),
    );

    await tester.tap(find.text('tela inicial'));
    await tester.pumpAndSettle();
    expect(find.text('editor'), findsOneWidget);

    await tester.tap(find.text('abrir painel'));
    await tester.pumpAndSettle();
    expect(find.text('conteudo do painel'), findsOneWidget);

    // O voltar do rodape da casca — o caminho comum de fechar.
    await tester.tap(find.byIcon(CupertinoIcons.chevron_back));
    await tester.pumpAndSettle();

    expect(
      find.text('conteudo do painel'),
      findsNothing,
      reason: 'o painel tinha de fechar',
    );
    expect(
      find.text('editor'),
      findsOneWidget,
      reason: 'o editor caiu junto com o painel',
    );
    expect(
      find.text('tela inicial'),
      findsNothing,
      reason: 'voltou para a tela inicial',
    );
  });

  testWidgets('sem Scaffold hospedeiro o painel vira rota e fecha a rota', (
    tester,
  ) async {
    await tester.pumpWidget(
      MaterialApp(
        home: Material(
          child: Builder(
            builder: (context) => Center(
              child: TextButton(
                onPressed: () => showParamSheet(
                  context,
                  title: 'Painel',
                  builder: (_) => const Center(child: Text('painel modal')),
                ),
                child: const Text('abrir'),
              ),
            ),
          ),
        ),
      ),
    );

    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();
    expect(find.text('painel modal'), findsOneWidget);

    await tester.tap(find.byIcon(CupertinoIcons.chevron_back));
    await tester.pumpAndSettle();

    expect(find.text('painel modal'), findsNothing);
    expect(find.text('abrir'), findsOneWidget);
  });
}

class _EditorFalso extends StatelessWidget {
  const _EditorFalso();

  @override
  Widget build(BuildContext context) => Scaffold(
    key: paramSheetHostKey,
    body: Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          const Text('editor'),
          TextButton(
            onPressed: () => showParamSheet(
              context,
              title: 'Painel',
              builder: (_) => const Center(child: Text('conteudo do painel')),
            ),
            child: const Text('abrir painel'),
          ),
        ],
      ),
    ),
  );
}
