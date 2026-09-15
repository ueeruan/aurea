// CURVA E MESCLA (v1.1.1): as familias da curva com os tipos novos, o
// inverter, e a mesclagem em sete categorias com todos os modos.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/domain/blend_extra.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/layer_menu.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

Future<ProviderContainer> _editor(WidgetTester tester) async {
  tester.view.physicalSize = const Size(390, 844);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  final c = ProviderContainer(
    overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
  );
  addTearDown(c.dispose);
  final e = c.read(editorControllerProvider.notifier);
  e.addShapeLayer(Duration.zero, name: 'A');
  c.read(selectedLayerProvider.notifier).state = null;
  await tester.pumpWidget(
    UncontrolledProviderScope(
      container: c,
      child: const MaterialApp(home: EditorScreen()),
    ),
  );
  await tester.pumpAndSettle();
  return c;
}

void main() {
  test('os tipos novos saem de 0 e chegam a 1 sem numero impossivel', () {
    const novos = [
      Easing.bounceIn,
      Easing.elasticIn,
      Easing.stepsRandom,
      Easing.oscillate,
      Easing.repeat,
      Easing.sawtooth,
    ];
    for (final e in novos) {
      expect(e.transform(0), 0, reason: e.label);
      expect(e.transform(1), 1, reason: e.label);
      for (var i = 1; i < 100; i++) {
        final v = e.transform(i / 100);
        expect(v.isFinite, isTrue, reason: '${e.label} em ${i / 100}');
      }
    }
    // O quique de entrada e o de saida ao contrario.
    for (final t in [0.1, 0.33, 0.7]) {
      expect(
        Easing.bounceIn.transform(t),
        closeTo(1 - Easing.bounce.transform(1 - t), 1e-9),
      );
    }
    // Oscilar 3 vezes passa pelo valor final no meio do caminho.
    expect(Easing.oscillate.transform(1 / 5), closeTo(1, 1e-9));
    // Dente de serra: rampa reta repetida.
    expect(Easing.sawtooth.transform(1 / 6), closeTo(0.5, 1e-9));
  });

  test('inverter: bezier espelha as alcas, quique e elastico trocam de ponta', () {
    final inv = Easing.easeIn.invertida!;
    expect(inv.x1, closeTo(0, 1e-9));
    expect(inv.x2, closeTo(0.58, 1e-9));
    expect(Easing.bounce.invertida!.type, EasingType.bounceIn);
    expect(Easing.elasticIn.invertida!.type, EasingType.elastic);
    expect(Easing.oscillate.invertida, isNull);
  });

  test('os tipos novos vao e voltam do arquivo pelo indice', () {
    final p = VideoProject(name: 'p', createdAt: DateTime(2026, 9, 15));
    final json = projectToJson(p);
    // O indice dos antigos nao mudou (arquivos velhos continuam lendo).
    expect(EasingType.hold.index, 8);
    expect(EasingType.bounceIn.index, 9);
    expect(projectFromJson(json).name, 'p');
  });

  test('as sete categorias cobrem todos os modos do motor, sem repetir', () {
    expect(categoriasDeMescla, hasLength(7));
    final nativos = <BlendMode>{};
    final proprios = <AureaBlend>{};
    for (final cat in categoriasDeMescla) {
      for (final m in cat.modos) {
        if (m.nativo != null) expect(nativos.add(m.nativo!), isTrue);
        if (m.aurea != null) expect(proprios.add(m.aurea!), isTrue);
      }
    }
    expect(proprios, AureaBlend.values.toSet());
    expect(nativos, containsAll([BlendMode.dstIn, BlendMode.dstOut, BlendMode.plus]));
  });

  testWidgets('mesclagem: categoria abre, modo proprio liga e o cabecalho mostra', (
    tester,
  ) async {
    final c = await _editor(tester);
    final id = c.read(editorControllerProvider).layers.first.id;
    c.read(selectedLayerProvider.notifier).state = id;
    c.read(editorSessionProvider.notifier).openPanel(EditorPanel.blending);
    await tester.pumpAndSettle();
    await tester.tap(find.text('Mesclagem').last);
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('mescla-categorias')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('categoria-mescla-Contraste')));
    await tester.pumpAndSettle();
    await tester.ensureVisible(find.byKey(const ValueKey('mescla-vividLight')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('mescla-vividLight')));
    await tester.pumpAndSettle();
    expect(
      c.read(editorControllerProvider).layerById(id)!.customBlend,
      AureaBlend.vividLight,
    );
    expect(find.text('Luz viva'), findsWidgets);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });

  testWidgets('curva: familia Quique, preset de entrada e o inverter', (
    tester,
  ) async {
    final c = await _editor(tester);
    final e = c.read(editorControllerProvider.notifier);
    final id = c.read(editorControllerProvider).layers.first.id;
    e.toggleKeyframe(id, Duration.zero, LayerProp.opacity);
    e.editOpacity(id, const Duration(seconds: 2), .2);
    expect(
      c.read(editorControllerProvider).layerById(id)!.opacity.keyframes,
      hasLength(2),
    );
    c.read(selectedLayerProvider.notifier).state = id;
    final playback = tester.widget<PreviewStage>(find.byType(PreviewStage)).playback;
    playback.seek(const Duration(seconds: 1));
    c.read(editorSessionProvider.notifier).openCurve(LayerProp.opacity);
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('curva-familia-1')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('curva-preset-Quique na entrada')));
    await tester.pumpAndSettle();
    Easing ease() =>
        c.read(editorControllerProvider).layerById(id)!.opacity.keyframes.first.ease;
    expect(ease().type, EasingType.bounceIn);
    expect(find.text('Quique na entrada'), findsWidgets);
    await tester.tap(find.byKey(const ValueKey('curve-inverter')));
    await tester.pumpAndSettle();
    expect(ease().type, EasingType.bounce);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });
}
