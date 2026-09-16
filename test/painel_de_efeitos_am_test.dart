// "A ABA DE CONFIGURACAO DOS EFEITOS DEVE SER ASSIM IGUAL A DO AM" (dono,
// 16/09/2026, com a print do Alight Motion).
//
// A planta: rail com voltar / keyframe / curva; um cartao por efeito com
// ▼ nome ••• lixeira; cada parametro com chip, fita e caixa de valor. Um
// efeito aberto por vez, e o recem-adicionado abre sozinho.
//
// Com AUREA_PRINT_DIR apontando uma pasta, grava as prints da ficha.
import 'dart:io';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/am/effects_panel.dart';
import 'package:aurea/src/features/editor/presentation/widgets/fita_de_ajuste.dart';
import 'package:aurea/src/features/editor/presentation/widgets/linha_de_parametro.dart';
import 'package:aurea/src/features/editor/presentation/widgets/rails_do_painel.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/print_da_ui.dart';
import 'editor_hierarchy_test.dart' show openEditor;

Future<void> _print(WidgetTester tester, String nome) async {
  final pasta = pastaDePrint;
  if (pasta == null) return;
  await tester.pumpAndSettle();
  await tester.runAsync(() async {
    final boundary = tester.renderObject<RenderRepaintBoundary>(
      find.byKey(const ValueKey('editor-capture')),
    );
    final image = await boundary.toImage(pixelRatio: 2);
    final bytes = (await image.toByteData(
      format: ui.ImageByteFormat.png,
    ))!.buffer.asUint8List();
    image.dispose();
    await File('$pasta/$nome.png').writeAsBytes(bytes);
  });
}

Future<(ProviderContainer, String)> _abrirEfeitos(
  WidgetTester tester,
  Size tamanho,
  List<EffectType> tipos,
) async {
  final c = await openEditor(tester, size: tamanho);
  final controller = c.read(editorControllerProvider.notifier);
  final id = c.read(editorControllerProvider).layers.first.id;
  for (final t in tipos) {
    controller.addEffect(id, t);
  }
  c.read(selectedLayerProvider.notifier).state = id;
  await tester.pumpAndSettle();
  c.read(editorSessionProvider.notifier).openPanel(EditorPanel.effects);
  await tester.pumpAndSettle();
  return (c, id);
}

/// A lista e preguicosa: cartao fora da vista nem existe. Rola ate ele.
Future<void> _rolarAte(WidgetTester tester, Finder alvo) async {
  await tester.scrollUntilVisible(
    alvo,
    80,
    scrollable: find
        .descendant(
          of: find.byType(EffectsPanel),
          matching: find.byType(Scrollable),
        )
        .first,
  );
  await tester.pumpAndSettle();
}

Finder _noCartao(String effectId, Finder alvo) =>
    find.descendant(of: find.byKey(ValueKey(effectId)), matching: alvo);

void main() {
  setUpAll(carregarFontesReais);

  for (final tamanho in [const Size(375, 667), const Size(430, 932)]) {
    testWidgets('a ficha segue a planta em $tamanho', (tester) async {
      final (c, id) = await _abrirEfeitos(tester, tamanho, [
        EffectType.levels,
        EffectType.hueSaturation,
        EffectType.unsharpMask,
      ]);
      expect(find.byType(EffectsPanel), findsOneWidget);
      // O rail de toda ferramenta: voltar, keyframe, curva.
      expect(find.byType(RailEsquerdo), findsOneWidget);
      expect(find.byKey(const ValueKey('painel-voltar')), findsWidgets);

      final efeitos = c.read(editorControllerProvider).layerById(id)!.effects;
      final niveis = efeitos.first;
      final usm = efeitos.last;
      // Ao abrir o painel, o primeiro efeito vem aberto e os outros
      // recolhidos: um controle por vez.
      expect(_noCartao(niveis.id, find.byType(LinhaDeParametro)), findsNWidgets(5));
      expect(_noCartao(usm.id, find.byType(LinhaDeParametro)), findsNothing);
      await _print(tester, 'efeitos-levels-${tamanho.width.round()}');

      // Tocar no nome de outro efeito abre ele e fecha o anterior.
      final cabecalho = find.byKey(ValueKey('efeito-cabecalho-${usm.id}'));
      await _rolarAte(tester, cabecalho);
      await tester.tap(cabecalho);
      await tester.pumpAndSettle();
      expect(_noCartao(niveis.id, find.byType(LinhaDeParametro)), findsNothing);
      expect(_noCartao(usm.id, find.byType(LinhaDeParametro)), findsNWidgets(3));
      expect(_noCartao(usm.id, find.text('Quantidade')), findsOneWidget);
      expect(_noCartao(usm.id, find.text('50%')), findsOneWidget);
      expect(_noCartao(usm.id, find.text('1,0')), findsOneWidget);
      expect(_noCartao(usm.id, find.byType(CupertinoSwitch)), findsOneWidget);
      // Cabecalho da planta: ▼ nome ••• lixeira.
      expect(_noCartao(usm.id, find.text('Unsharp Mask')), findsOneWidget);
      expect(find.byKey(ValueKey('efeito-menu-${usm.id}')), findsOneWidget);
      expect(find.byKey(ValueKey('efeito-remover-${usm.id}')), findsOneWidget);
      await _print(tester, 'efeitos-unsharp-${tamanho.width.round()}');

      // Adicionar com o painel aberto: o novo abre sozinho.
      c.read(editorControllerProvider.notifier).addEffect(id, EffectType.exposure);
      await tester.pumpAndSettle();
      final novo = c.read(editorControllerProvider).layerById(id)!.effects.last;
      expect(_noCartao(novo.id, find.byType(LinhaDeParametro)), findsNWidgets(3));
      expect(_noCartao(usm.id, find.byType(LinhaDeParametro)), findsNothing);
      expect(tester.takeException(), isNull);
      await tester.pump(const Duration(seconds: 1));
    });
  }

  testWidgets('arrastar a fita muda o numero num desfazer so', (tester) async {
    final (c, id) = await _abrirEfeitos(tester, const Size(430, 932), [
      EffectType.unsharpMask,
    ]);
    final controller = c.read(editorControllerProvider.notifier);
    double quantidade() => c
        .read(editorControllerProvider)
        .layerById(id)!
        .effects
        .single
        .paramAt('amount', Duration.zero);
    expect(quantidade(), 50);
    final fita = find.byType(FitaDeAjuste).first;
    await tester.drag(fita, const Offset(-80, 0));
    await tester.pumpAndSettle();
    final depois = quantidade();
    expect(depois, isNot(50));
    // O chip do parametro arrastado fica aceso (e o alvo do rail).
    controller.undo();
    await tester.pumpAndSettle();
    expect(quantidade(), 50, reason: 'o arrasto inteiro volta num desfazer');
    await tester.pump(const Duration(seconds: 1));
  });

  testWidgets('interruptor, menu e lixeira fazem o que dizem', (tester) async {
    final (c, id) = await _abrirEfeitos(tester, const Size(430, 932), [
      EffectType.exposure,
      EffectType.brightnessContrast,
    ]);
    List<EffectInstance> efeitos() =>
        c.read(editorControllerProvider).layerById(id)!.effects;
    final bc = efeitos().last;
    await _rolarAte(tester, find.byKey(ValueKey('efeito-cabecalho-${bc.id}')));
    await tester.tap(find.byKey(ValueKey('efeito-cabecalho-${bc.id}')));
    await tester.pumpAndSettle();

    // Modo legado.
    await _rolarAte(tester, _noCartao(bc.id, find.byType(CupertinoSwitch)));
    await tester.tap(_noCartao(bc.id, find.byType(CupertinoSwitch)));
    await tester.pumpAndSettle();
    expect(efeitos().last.paramAt('use_legacy', Duration.zero), 1);

    // ••• > Mover para cima.
    await tester.tap(find.byKey(ValueKey('efeito-menu-${bc.id}')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('efeito-menu-subir')));
    await tester.pumpAndSettle();
    expect(efeitos().first.id, bc.id);

    // ••• > Desativar efeito. O cartao subiu: rola ate ele de novo.
    await tester.drag(
      find.byType(Scrollable).last,
      const Offset(0, 400),
    );
    await tester.pumpAndSettle();
    await _rolarAte(tester, find.byKey(ValueKey('efeito-menu-${bc.id}')));
    await tester.tap(find.byKey(ValueKey('efeito-menu-${bc.id}')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('efeito-menu-ligar')));
    await tester.pumpAndSettle();
    expect(efeitos().first.enabled, isFalse);

    // O losango do rail marca o keyframe do efeito aberto.
    final losango = find.descendant(
      of: find.byType(RailEsquerdo),
      matching: find.bySemanticsLabel('Marcar keyframe aqui'),
    );
    expect(losango, findsOneWidget);
    await tester.tap(losango);
    await tester.pumpAndSettle();
    expect(efeitos().first.hasAnimation, isTrue);

    // Lixeira.
    await tester.tap(find.byKey(ValueKey('efeito-remover-${bc.id}')));
    await tester.pumpAndSettle();
    expect(efeitos().map((e) => e.id), isNot(contains(bc.id)));
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });
}
