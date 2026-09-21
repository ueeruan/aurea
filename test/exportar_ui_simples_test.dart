import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/export/domain/export_settings.dart';
import 'package:aurea/src/features/export/presentation/export_video_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// A TELA DE EXPORTAR DEPOIS DA SIMPLIFICACAO (20/09/2026).
///
/// A tela pedia cinco decisoes em fileiras de pastilhas (Formato,
/// Tamanho, Quadros, Codec, Qualidade) e a folha do editor pedia as
/// mesmas cinco mais a taxa, em outra aparencia. Agora ha UMA decisao em
/// cima — a predefinicao — e o resto mora num cartao "Ajustes" que nasce
/// fechado.
///
/// O que este arquivo prende:
///   1. a predefinicao muda de verdade a resolucao e os quadros;
///   2. o cartao "Ajustes" nasce RECOLHIDO (a razao de ser da tela);
///   3. a duracao anunciada e a do CONTEUDO, nao o piso da linha do
///      tempo — a conta de peso e de tempo sai dela.
///
/// Projeto de proposito vertical: 1080x1920 a 24 fps com dois segundos de
/// conteudo. Vertical porque e onde o piso de cinco segundos e a conta de
/// "1080p" mais enganam. Desde 21/09 "Np" nesta tela e o LADO MENOR = N
/// (1080p num 1080x1920 e 1080x1920; antes era uma ALTURA e dava
/// 608x1080 — menor que o projeto).
VideoProject _projeto() => VideoProject(
  name: 'p',
  createdAt: DateTime(2026, 9, 20),
  aspectRatio: 9 / 16,
  resolutionHeight: 1080,
  fps: 24,
  layers: [
    ShapeLayer(
      name: 'forma',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
    ),
  ],
);

Future<ProviderContainer> _abrirTela(WidgetTester tester) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);

  final container = ProviderContainer();
  addTearDown(container.dispose);
  container.read(editorControllerProvider.notifier).openProject(_projeto());

  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: const MaterialApp(home: ExportVideoScreen()),
    ),
  );
  await tester.pump();
  return container;
}

/// O texto da linha de resumo ("1080 x 1920 · 24 fps · ~13 MB · 0:02").
String _resumo(WidgetTester tester) =>
    tester.widget<Text>(find.byKey(const ValueKey('export-resumo'))).data!;

void main() {
  testWidgets('a predefinicao muda a resolucao e os quadros', (tester) async {
    await _abrirTela(tester);

    // Nasce em "Maxima qualidade"? Nao: nasce no que veio dos ajustes
    // padrao (tamanho do projeto, fps do projeto, qualidade media), que
    // nao e predefinicao nenhuma — entao "Personalizado" e o aceso. O
    // que importa aqui e o resumo: o tamanho e o fps do projeto.
    expect(_resumo(tester), startsWith('1080 x 1920 · 24 fps'));

    // YOUTUBE 1080p: "1080p" e o LADO MENOR. Num projeto vertical de
    // 1080x1920 o quadro fica igual — e nao encolhe para 608x1080.
    await tester.tap(
      find.byKey(const ValueKey('export-predefinicao-youtube')),
    );
    await tester.pump();
    expect(_resumo(tester), startsWith('1080 x 1920 · 24 fps'));

    // REELS / TIKTOK: tamanho do projeto e 30 quadros — os dois mudam
    // com um toque so.
    await tester.tap(find.byKey(const ValueKey('export-predefinicao-reels')));
    await tester.pump();
    expect(_resumo(tester), startsWith('1080 x 1920 · 30 fps'));
  });

  testWidgets('o cartao Ajustes nasce recolhido e abre no toque', (
    tester,
  ) async {
    await _abrirTela(tester);

    expect(
      find.byKey(const ValueKey('export-cabeca-ajustes')),
      findsOneWidget,
      reason: 'o cartao existe, so esta fechado',
    );
    expect(
      find.byKey(const ValueKey('export-ajustes-corpo')),
      findsNothing,
      reason: 'quem nao abrir nunca ve resolucao, codec nem qualidade',
    );
    // Nem os controles de dentro sao construidos.
    for (final chave in const [
      'export-ajuste-formato',
      'export-ajuste-tamanho',
      'export-ajuste-quadros',
      'export-ajuste-codec',
      'export-ajuste-qualidade',
    ]) {
      expect(find.byKey(ValueKey(chave)), findsNothing, reason: chave);
    }

    await tester.tap(find.byKey(const ValueKey('export-cabeca-ajustes')));
    await tester.pumpAndSettle();

    expect(find.byKey(const ValueKey('export-ajustes-corpo')), findsOneWidget);
    for (final chave in const [
      'export-ajuste-formato',
      'export-ajuste-tamanho',
      'export-ajuste-quadros',
      'export-ajuste-codec',
      'export-ajuste-qualidade',
    ]) {
      expect(find.byKey(ValueKey(chave)), findsOneWidget, reason: chave);
    }

    // ABERTO, O CARTAO NAO ESTOURA A COLUNA. O rodape inteiro e uma
    // lista que rola e para em 62% da tela; o palco encolhe, e nada
    // passa da borda — nem num aparelho baixo.
    expect(tester.takeException(), isNull);
    tester.view.physicalSize = const Size(320, 568);
    await tester.pumpAndSettle();
    expect(tester.takeException(), isNull);
    tester.view.physicalSize = const Size(390, 844);
    await tester.pumpAndSettle();

    // E fecha de novo no mesmo toque.
    await tester.tap(find.byKey(const ValueKey('export-cabeca-ajustes')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('export-ajustes-corpo')), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('a duracao anunciada e a do conteudo, nao o piso de 5 s', (
    tester,
  ) async {
    final container = await _abrirTela(tester);
    final p = container.read(editorControllerProvider);

    // O projeto do teste: dois segundos de conteudo, cinco de linha do
    // tempo. Sao numeros diferentes de proposito.
    expect(p.duracaoDoConteudo, const Duration(seconds: 2));
    expect(p.duration, const Duration(seconds: 5));

    expect(
      _resumo(tester),
      endsWith('0:02'),
      reason:
          'a tela anunciava 0:05 e um arquivo mais que duas vezes maior '
          'do que o que o motor grava',
    );

    // E o peso segue a mesma duracao: dois segundos, nao cinco.
    final texto = _resumo(tester);
    final mb = int.parse(
      RegExp(r'~(\d+) MB').firstMatch(texto)!.group(1)!,
    );
    final esperado = const ExportSettings()
        .estimatedMegabytes(1080, 1920, 24, const Duration(seconds: 2))
        .round();
    expect(mb, esperado);
  });
}
