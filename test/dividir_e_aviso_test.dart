import 'package:aurea/src/core/avisos/avisos_service.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/projects/presentation/aviso_ao_vivo.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'editor_hierarchy_test.dart' show openEditor;

/// Dois pedidos do beta que se provam do mesmo jeito: uma coisa que tem
/// de estar SEMPRE na tela.
///
///   - "DEIXE A OPCAO DE DIVIDIR CAMADAS SEMPRE VISIVEL": a tesoura mora
///     no transporte, que nunca sai da tela, e corta no cabecote.
///   - o aviso ao vivo: um recado escrito no servidor aparece na Inicio
///     de todo aparelho, e o X esconde so aquele recado.
void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });

  testWidgets('a tesoura esta na barra da selecao e divide no cabecote', (
    tester,
  ) async {
    // NA PLANTA DO AM (v1.1.1): a tesoura mora na barra flutuante que
    // aparece com a camada selecionada — um toque no clipe e ela esta
    // na tela, sempre no mesmo lugar.
    final c = await openEditor(tester);
    final tesoura = find.byKey(const ValueKey('camada-dividir'));
    final antes = c.read(editorControllerProvider).layers.length;
    expect(
      tesoura,
      findsNothing,
      reason: 'sem selecao nao ha o que cortar — a barra nem existe',
    );

    // Seleciona tocando no palco e leva o cabecote para o meio do clipe.
    await tester.tapAt(tester.getRect(find.byType(PreviewStage)).center);
    await tester.pumpAndSettle();
    expect(tesoura, findsOneWidget, reason: 'selecionou, a tesoura chegou');
    final id = c.read(selectedLayerProvider)!;
    final camada = c.read(editorControllerProvider).layerById(id)!;
    final playback = tester
        .widget<PreviewStage>(find.byType(PreviewStage))
        .playback;
    playback.seek(camada.startTime + camada.duration ~/ 2);
    await tester.pumpAndSettle();

    await tester.tap(tesoura);
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).layers.length,
      antes + 1,
      reason: 'dividir no cabecote faz duas camadas de uma',
    );
  });

  testWidgets('os avisos empilham, e o X esconde so aquele', (tester) async {
    SharedPreferences.setMockInitialValues({});
    final servico = AvisosService();
    addTearDown(servico.parar);
    servico.todos.value = const [
      Aviso(
        id: 'bug-export-1',
        texto: 'Estamos resolvendo um bug na exportação.',
        nivel: NivelDoAviso.problema,
      ),
      Aviso(
        id: 'grupo-1',
        texto: 'Entre no grupo do WhatsApp.',
        link: 'https://chat.whatsapp.com/exemplo',
      ),
    ];
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: Column(children: [AvisoAoVivo(servico: servico)])),
      ),
    );
    await tester.pump();
    expect(find.byKey(const ValueKey('aviso-bug-export-1')), findsOneWidget);
    expect(find.byKey(const ValueKey('aviso-grupo-1')), findsOneWidget);
    expect(find.textContaining('Saiba mais'), findsOneWidget);
    // O de cima e o de cima: a ordem da lista e a ordem na tela.
    expect(
      tester.getRect(find.byKey(const ValueKey('aviso-bug-export-1'))).top,
      lessThan(tester.getRect(find.byKey(const ValueKey('aviso-grupo-1'))).top),
    );

    // Fechar um deixa o outro.
    await tester.tap(find.byKey(const ValueKey('aviso-fechar-bug-export-1')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('aviso-bug-export-1')), findsNothing);
    expect(find.byKey(const ValueKey('aviso-grupo-1')), findsOneWidget);
  });

  testWidgets('um aviso de popup abre a janela, e so uma vez', (tester) async {
    SharedPreferences.setMockInitialValues({});
    final servico = AvisosService();
    addTearDown(servico.parar);
    const grupo = Aviso(
      id: 'grupo-1',
      texto: 'Entre no grupo do WhatsApp.',
      link: 'https://chat.whatsapp.com/exemplo',
      popup: true,
    );
    servico.todos.value = const [grupo];
    servico.emJanela.value = grupo;
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(body: Column(children: [AvisoAoVivo(servico: servico)])),
      ),
    );
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('aviso-janela-grupo-1')), findsOneWidget);
    expect(find.text('Entre no grupo do WhatsApp.'), findsWidgets);
    expect(find.byKey(const ValueKey('aviso-janela-abrir')), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('aviso-janela-fechar')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('aviso-janela-grupo-1')), findsNothing);
    // A faixa continua: quem quiser o link depois, acha la.
    expect(find.byKey(const ValueKey('aviso-grupo-1')), findsOneWidget);
    expect(servico.emJanela.value, isNull);
  });

  test('vencido nao aparece; dispensado tambem nao; e o popup e opcional', () async {
    SharedPreferences.setMockInitialValues({
      'aviso.dispensados': ['velho'],
    });
    final s = AvisosService();
    addTearDown(s.parar);
    // Sem rede o iniciar so le o guardado; aqui nao ha guardado.
    await s.iniciar();
    expect(s.todos.value, isEmpty);
    final vencido = Aviso(
      id: 'x',
      texto: 'ja passou',
      ate: DateTime.now().subtract(const Duration(days: 1)),
    );
    expect(vencido.vencido, isTrue);
    expect(Aviso.deJson({'id': '', 'texto': 'sem id'}), isNull);
    expect(Aviso.deJson({'id': 'a', 'texto': '   '}), isNull);
    expect(Aviso.deJson({'id': 'a', 'texto': 'ok', 'nivel': 'atencao'})!.nivel,
        NivelDoAviso.atencao);
    expect(Aviso.deJson({'id': 'a', 'texto': 'ok'})!.popup, isFalse);
    expect(Aviso.deJson({'id': 'a', 'texto': 'ok', 'popup': true})!.popup, isTrue);
  });
}
