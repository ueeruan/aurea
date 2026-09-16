import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/context/effects/effect_detail_sheet.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

Future<EscolhaDoDetalhe?> _abrir(WidgetTester tester, EffectType tipo) async {
  EscolhaDoDetalhe? escolha;
  await tester.pumpWidget(
    ProviderScope(
      child: MaterialApp(
        home: Scaffold(
          body: Builder(
            builder: (context) => Center(
              child: TextButton(
                onPressed: () async =>
                    escolha = await showEffectDetail(context, tipo),
                child: const Text('abrir'),
              ),
            ),
          ),
        ),
      ),
    ),
  );
  await tester.tap(find.text('abrir'));
  await tester.pumpAndSettle();
  return escolha;
}

void main() {
  testWidgets('o detalhe mostra o que o efeito faz, os prontos e as palavras', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(400, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await _abrir(tester, EffectType.glitch);
    final spec = effectSpecs[EffectType.glitch]!;
    expect(find.text(spec.name), findsWidgets);
    expect(find.byKey(const ValueKey('detalhe-aplicar')), findsOneWidget);
    for (final pronto in spec.presets) {
      expect(
        find.byKey(ValueKey('detalhe-pronto-${pronto.nome}')),
        findsOneWidget,
        reason: pronto.nome,
      );
    }
    expect(
      find.byKey(ValueKey('detalhe-tag-${spec.synonyms.first}')),
      findsOneWidget,
    );
    expect(find.text('Prontos'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('Aplicar devolve o efeito cru', (tester) async {
    tester.view.physicalSize = const Size(400, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    EscolhaDoDetalhe? escolha;
    await tester.pumpWidget(
      ProviderScope(
        child: MaterialApp(
          home: Scaffold(
            body: Builder(
              builder: (context) => Center(
                child: TextButton(
                  onPressed: () async => escolha = await showEffectDetail(
                    context,
                    EffectType.glitch,
                  ),
                  child: const Text('abrir'),
                ),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('detalhe-aplicar')));
    await tester.pumpAndSettle();
    expect(escolha, isA<AplicarEfeito>());
    expect((escolha! as AplicarEfeito).pronto, isNull);
  });

  testWidgets('tocar num pronto devolve aquele pronto', (tester) async {
    tester.view.physicalSize = const Size(400, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final spec = effectSpecs[EffectType.glitch]!;
    EscolhaDoDetalhe? escolha;
    await tester.pumpWidget(
      ProviderScope(
        child: MaterialApp(
          home: Scaffold(
            body: Builder(
              builder: (context) => Center(
                child: TextButton(
                  onPressed: () async => escolha = await showEffectDetail(
                    context,
                    EffectType.glitch,
                  ),
                  child: const Text('abrir'),
                ),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();
    final alvo = spec.presets.first;
    await tester.tap(find.byKey(ValueKey('detalhe-pronto-${alvo.nome}')));
    await tester.pumpAndSettle();
    expect((escolha! as AplicarEfeito).pronto?.nome, alvo.nome);
  });

  testWidgets('tocar numa palavra vira busca', (tester) async {
    tester.view.physicalSize = const Size(400, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final spec = effectSpecs[EffectType.glitch]!;
    EscolhaDoDetalhe? escolha;
    await tester.pumpWidget(
      ProviderScope(
        child: MaterialApp(
          home: Scaffold(
            body: Builder(
              builder: (context) => Center(
                child: TextButton(
                  onPressed: () async => escolha = await showEffectDetail(
                    context,
                    EffectType.glitch,
                  ),
                  child: const Text('abrir'),
                ),
              ),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();
    final palavra = spec.synonyms.first;
    await tester.tap(find.byKey(ValueKey('detalhe-tag-$palavra')));
    await tester.pumpAndSettle();
    expect((escolha! as ProcurarPor).palavra, palavra);
    // A palavra acha o proprio efeito de volta.
    expect(searchEffects(palavra), contains(EffectType.glitch));
  });
}
