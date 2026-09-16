import 'dart:io';
import 'dart:ui' as ui;

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/application/ui/editor_layout.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart';
import 'package:aurea/src/features/editor/presentation/am/layer_menu.dart';
import 'package:aurea/src/features/editor/presentation/am/transform_panel.dart';
import 'package:aurea/src/features/editor/presentation/widgets/add_layer_sheet.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';

import 'editor_hierarchy_test.dart' show openEditor;

void main() {
  setUpAll(() async {
    TestWidgetsFlutterBinding.ensureInitialized();
    for (final family in [
      'Aurea Motion Sans',
      '.SF Pro Text',
      '.SF Pro Display',
      '.SF UI Text',
      '.SF UI Display',
    ]) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
    await (FontLoader('packages/cupertino_icons/CupertinoIcons')..addFont(
          rootBundle.load('packages/cupertino_icons/assets/CupertinoIcons.ttf'),
        ))
        .load();
    await (FontLoader(
      'MaterialIcons',
    )..addFont(rootBundle.load('fonts/MaterialIcons-Regular.otf'))).load();
  });

  for (final size in [const Size(375, 667), const Size(430, 844)]) {
    testWidgets('AM editing flow and visible canvas at $size', (tester) async {
      final c = await openEditor(tester, size: size);
      final controller = c.read(editorControllerProvider.notifier);
      controller.setComposition(aspectRatio: 9 / 16);
      controller.setBackgroundColor(Colors.white);
      for (final layer in c.read(editorControllerProvider).layers) {
        controller.editPosition(
          layer.id,
          Duration.zero,
          const Offset(540, 960),
        );
        controller.setShapePrimaryColor(layer.id, const Color(0xFFFF7A21));
      }
      await tester.pumpAndSettle();
      final preview = tester.getRect(find.byType(PreviewStage));

      Future<void> shot(String name) async {
        if (size.width != 430 ||
            const String.fromEnvironment('AM_CAPTURE') != name) {
          return;
        }
        await (FontLoader('packages/cupertino_icons/CupertinoIcons')..addFont(
              rootBundle.load(
                'packages/cupertino_icons/assets/CupertinoIcons.ttf',
              ),
            ))
            .load();
        await tester.pumpAndSettle();
        void repaint(RenderObject node) {
          node.markNeedsPaint();
          node.visitChildren(repaint);
        }

        repaint(
          tester.renderObject(find.byKey(const ValueKey('editor-capture'))),
        );
        await tester.pump();
        await tester.runAsync(() async {
          final b = tester.renderObject<RenderRepaintBoundary>(
            find.byKey(const ValueKey('editor-capture')),
          );
          final im = await b.toImage(pixelRatio: 2);
          final bytes = await im.toByteData(format: ui.ImageByteFormat.png);
          final file = File('output/am-redesign-56/$name.png');
          await file.parent.create(recursive: true);
          await file.writeAsBytes(bytes!.buffer.asUint8List());
          im.dispose();
        });
      }

      expect(
        tester.getRect(find.byKey(const ValueKey('editor-undo'))).top,
        greaterThanOrEqualTo(preview.bottom),
      );
      await shot('timeline');
      await tester.tap(find.byKey(const ValueKey('editor-fab')));
      await tester.pumpAndSettle();
      expect(find.byType(AddLayerPanel), findsOneWidget);
      // O "+" nao mexe no palco: e um seletor, nao trabalho
      // na camada.
      expect(tester.getRect(find.byType(PreviewStage)), preview);
      expect(find.byTooltip('Circulo').hitTestable(), findsOneWidget);
      expect(
        tester.getRect(find.text('Texto')).left,
        greaterThan(size.width - 60),
      );
      await shot('adicionar');
      await tester.tap(find.byTooltip('Fechar adicionar'));
      await tester.pumpAndSettle();
      expect(find.byType(AddLayerPanel), findsNothing);
      expect(c.read(editorSessionProvider).panel, EditorPanel.none);
      await tester.tap(find.byKey(const ValueKey('editor-fab')));
      await tester.pumpAndSettle();
      await tester.tap(find.byTooltip('Circulo'));
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).layers.length, 3);
      controller.setShapePrimaryColor(
        c.read(selectedLayerProvider)!,
        const Color(0xFFFF7A21),
      );
      await tester.pumpAndSettle();
      expect(find.byType(LayerToolsDock), findsOneWidget);
      // COM UMA CAMADA SELECIONADA O PALCO CEDE (16/09, escolha do
      // dono): a timeline batia no piso e quem perdia altura era o
      // painel — em Borda e sombra o botao de adicionar saia da lista.
      // Agora o palco devolve esses ~40 px enquanto se edita e os
      // retoma quando nada esta selecionado. Continua generoso: nunca
      // abaixo do minimo, e sempre mais alto do que era antes de o
      // palco ganhar tamanho proprio.
      final comCamada = tester.getRect(find.byType(PreviewStage));
      expect(comCamada.height, lessThan(preview.height));
      expect(comCamada.height, greaterThan(preview.height * 0.8));
      expect(
        comCamada.height,
        greaterThanOrEqualTo(EditorLayoutMetrics.previewMin),
      );
      expect(
        tester.widget<AmTimeline>(find.byType(AmTimeline)).singleLayerId,
        isNull,
      );
      await shot('camada');
      await tester.tap(find.text('Movimentação e transformação'));
      await tester.pumpAndSettle();
      expect(find.byType(TransformPanel), findsOneWidget);
      expect(
        tester.getRect(find.byType(PreviewStage)).height,
        greaterThanOrEqualTo(EditorLayoutMetrics.previewMin),
      );
      // 'painel-voltar' e a chave de QUATRO botoes diferentes no app
      // (cabecalho da folha, trilho do painel, cromo, folha de
      // contexto). Com o palco no tamanho da planta, o da folha e o do
      // trilho ficam tocaveis ao mesmo tempo — sao dois botoes de
      // verdade, como na planta. O que importa aqui e que exista pelo
      // menos um caminho de volta ao alcance do dedo.
      expect(
        find.byKey(const ValueKey('painel-voltar')).hitTestable(),
        findsAtLeastNWidgets(1),
      );
      await shot('posicao');
      final id = c.read(selectedLayerProvider)!;
      await tester.tap(find.byWidgetPredicate(
          // O MESMO BOTAO: com auto-key ligado o tooltip ganha um
          // sufixo (" · auto-key ligado"), e o casamento exato perdia
          // o botao.
          (w) =>
              w is Tooltip &&
              (w.message ?? '').startsWith('Opções de transformação'),
        ));
      await tester.pumpAndSettle();
      await tester.tap(
        find.byWidgetPredicate(
          (w) => w is CheckedPopupMenuItem<String> && w.value == '3d',
        ),
      );
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).layerById(id)!.is3D, isTrue);
      await tester.tap(find.byWidgetPredicate(
          // O MESMO BOTAO: com auto-key ligado o tooltip ganha um
          // sufixo (" · auto-key ligado"), e o casamento exato perdia
          // o botao.
          (w) =>
              w is Tooltip &&
              (w.message ?? '').startsWith('Opções de transformação'),
        ));
      await tester.pumpAndSettle();
      await tester.tap(
        find.byWidgetPredicate(
          (w) => w is CheckedPopupMenuItem<String> && w.value == '3d',
        ),
      );
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).layerById(id)!.is3D, isFalse);
      final beforeDrag = c
          .read(editorControllerProvider)
          .layerById(id)!
          .position
          .base;
      await tester.drag(
        find.byKey(const ValueKey('position-drag-pad')),
        const Offset(24, 12),
      );
      await tester.pumpAndSettle();
      expect(
        c.read(editorControllerProvider).layerById(id)!.position.base,
        isNot(beforeDrag),
      );
      controller.toggleKeyframe(id, Duration.zero, LayerProp.position);
      controller.toggleKeyframe(
        id,
        const Duration(seconds: 2),
        LayerProp.position,
      );
      await tester.pumpAndSettle();
      await tester.tap(find.byTooltip('Editar curva da propriedade'));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('curve-edit-area')), findsOneWidget);
      expect(
        tester.getRect(find.byType(PreviewStage)).height,
        greaterThanOrEqualTo(EditorLayoutMetrics.previewMin),
      );
      await shot('curva');
      await tester.tap(find.byKey(const ValueKey('editor-back')));
      await tester.pumpAndSettle();
      expect(find.byType(TransformPanel), findsOneWidget);
      await tester.tap(find.byKey(const ValueKey('painel-voltar')).first);
      await tester.pumpAndSettle();
      expect(find.byType(LayerToolsDock), findsOneWidget);
      expect(c.read(selectedLayerProvider), id);
      await tester.tap(find.byKey(const ValueKey('editor-back')));
      await tester.pumpAndSettle();
      expect(c.read(selectedLayerProvider), isNull);
      expect(find.byKey(const ValueKey('editor-fab')), findsOneWidget);
      expect(tester.takeException(), isNull);
    });
  }

  testWidgets(
    'effect stack opens one control at a time and follows additions',
    (tester) async {
      final c = await openEditor(tester);
      final controller = c.read(editorControllerProvider.notifier);
      final id = c.read(editorControllerProvider).layers.first.id;
      controller.addEffect(id, EffectType.gaussianBlur);
      controller.addEffect(id, EffectType.lightGlow);
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      c.read(editorSessionProvider.notifier).openPanel(EditorPanel.effects);
      await tester.pumpAndSettle();
      expect(find.text('Resetar'), findsOneWidget);
      var effects = c.read(editorControllerProvider).layerById(id)!.effects;
      // Com o palco no tamanho da planta a pilha desceu: traz a ficha
      // para a vista antes de tocar, como o dedo faria.
      await tester.ensureVisible(find.text(effects.last.spec.name));
      await tester.pumpAndSettle();
      await tester.tap(find.text(effects.last.spec.name));
      await tester.pumpAndSettle();
      expect(
        find.descendant(
          of: find.byKey(ValueKey(effects.first.id)),
          matching: find.text('Resetar'),
        ),
        findsNothing,
      );
      expect(
        find.descendant(
          of: find.byKey(ValueKey(effects.last.id)),
          matching: find.text('Resetar'),
        ),
        findsOneWidget,
      );
      controller.addEffect(id, EffectType.vignette);
      await tester.pumpAndSettle();
      effects = c.read(editorControllerProvider).layerById(id)!.effects;
      expect(
        find.descendant(
          of: find.byKey(ValueKey(effects.last.id)),
          matching: find.text('Resetar'),
        ),
        findsOneWidget,
      );
      expect(find.text('Resetar'), findsOneWidget);
      controller.removeEffect(id, effects.last.id);
      await tester.pumpAndSettle();
      expect(find.text('Resetar'), findsNothing);
      expect(tester.takeException(), isNull);
    },
  );
}
