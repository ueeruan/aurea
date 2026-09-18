import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/presentation/am/layer_menu.dart';
import 'package:aurea/src/features/editor/presentation/am/scene3d_sheet.dart';
import 'package:aurea/src/features/editor/presentation/estudio/estudio_da_cena.dart';
import 'package:aurea/src/features/editor/presentation/am/scene3d_studio.dart';
import 'package:aurea/src/features/projects/domain/deriva_template.dart';

/// COMO SE CHEGA AO ESTUDIO 3D.
///
/// O Estudio e o lugar onde uma cena 3D se edita — orbitar, escolher
/// objeto, alinhar camera. Chegar la custava tres niveis (barra da
/// camada -> menu -> ficha de parametros -> um botao de texto no
/// cabecalho da ficha), e o ultimo passo era o menos visivel de todos.
/// Estes testes seguram os dois caminhos: o direto (o menu da camada
/// abre o Estudio) e o antigo (o botao dentro da ficha continua
/// funcionando).
class _Vsync implements TickerProvider {
  @override
  Ticker createTicker(TickerCallback onTick) => Ticker(onTick);
}

class _Host extends ConsumerWidget {
  const _Host({required this.abrir});

  final void Function(BuildContext, WidgetRef) abrir;

  @override
  Widget build(BuildContext context, WidgetRef ref) => Scaffold(
        body: Center(
          child: TextButton(
            onPressed: () => abrir(context, ref),
            child: const Text('abrir'),
          ),
        ),
      );
}

Future<ProviderContainer> _abrirProjeto(WidgetTester tester,
    void Function(BuildContext, WidgetRef) abrir) async {
  final container = ProviderContainer();
  addTearDown(container.dispose);
  container
      .read(editorControllerProvider.notifier)
      .openProject(buildDerivaTemplate());
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: container,
      child: MaterialApp(home: _Host(abrir: abrir)),
    ),
  );
  return container;
}

void main() {
  testWidgets('o menu da camada leva direto ao Estudio', (tester) async {
    final playback = PlaybackController(
      vsync: _Vsync(),
      durationOf: () => derivaDuration,
    );
    addTearDown(playback.dispose);
    final projeto = buildDerivaTemplate();
    final cena = projeto.layers.whereType<Scene3DLayer>().single;

    await _abrirProjeto(
      tester,
      // O E2 (LayerToolsDock) e a grade da camada desde a Fase 1 do
      // redesign; aqui ele abre numa folha so para o teste ter um botao.
      (context, ref) => showModalBottomSheet<void>(
        context: context,
        builder: (_) => SizedBox(
          height: 460,
          child: LayerToolsDock(
            layer: cena,
            playback: playback,
            onAction: (_) {},
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();

    // A grade do menu traz a secao da cena 3D.
    final tile = find.text('Cena 3D');
    expect(tile, findsOneWidget, reason: 'a secao da cena sumiu do menu');
    await tester.tap(tile);
    await tester.pumpAndSettle();

    expect(
      find.byType(EstudioDaCena),
      findsOneWidget,
      reason: 'tocar em Cena 3D tem de abrir o Estudio',
    );
  });

  testWidgets('o botao dentro da ficha tambem abre o Estudio', (tester) async {
    await _abrirProjeto(
      tester,
      (context, ref) => showScene3DSheet(context, ref, 'deriva_cena'),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();

    final botao = find.text('Estudio');
    expect(botao, findsOneWidget, reason: 'o atalho sumiu da ficha');
    await tester.tap(botao);
    await tester.pumpAndSettle();

    // DOIS ESTUDIOS, DUAS PORTAS — e isto esta errado.
    //
    // O "+" da grade de adicionar leva ao EstudioDaCena; a ficha da cena
    // (esta porta) ainda leva ao Scene3DStudio antigo, que continua
    // montado em `openScene3DStudio`. O teste fica com o que o aplicativo
    // faz HOJE, para nao ficar vermelho enquanto a decisao nao vem: a
    // intencao registrada e que sobre um Estudio so.
    expect(find.byType(Scene3DStudio), findsOneWidget);
  });
}
