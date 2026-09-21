// O CAMINHO INTEIRO, NA DOCA DE VERDADE:
//
//   texto comum -> ficha "Ativar 3D" -> vira texto 3D -> a folha do
//   Texto 3D abre nessa camada, com os controles na ordem do dono.
//
// Era a metade que faltava do "Texto 3D como propriedade da camada": o
// painel ja sabia editar, mas so existia para quem tinha criado a camada
// pelo botao de texto 3D. Quem ja tinha escrito e posicionado um texto
// nao tinha como extrudar sem apagar tudo.
//
// A DOCA E A REAL (`LayerToolsDock`), e nao uma lista de rotulos montada
// no teste: a ficha so vale se ela aparece para o texto e NAO aparece
// para o resto.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/playback_controller.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/am/layer_menu.dart';
import 'package:aurea/src/features/editor/presentation/context/parameter_row.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  late ProviderContainer container;

  VideoProject projeto() => container.read(editorControllerProvider);

  Future<void> abrirDoca(
    WidgetTester tester,
    void Function(EditorController) semear,
  ) async {
    tester.view.physicalSize = const Size(430, 932);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.openProject(VideoProject.empty('doca'));
    semear(c);
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: MaterialApp(
          theme: ThemeData(platform: TargetPlatform.iOS),
          home: const _DocaHost(),
        ),
      ),
    );
    await tester.pumpAndSettle();
  }

  testWidgets('a ficha Ativar 3D so aparece no texto', (tester) async {
    await abrirDoca(tester, (c) => c.addTextLayer(Duration.zero));
    expect(find.text('Ativar 3D'), findsOneWidget);

    await abrirDoca(
      tester,
      (c) => c.addImageLayer(Duration.zero, '/tmp/i.png', 'imagem'),
    );
    expect(
      find.text('Ativar 3D'),
      findsNothing,
      reason: 'imagem nao tem letra para extrudar',
    );
  });

  testWidgets('tocar em Ativar 3D converte e abre a folha do Texto 3D', (
    tester,
  ) async {
    await abrirDoca(tester, (c) => c.addTextLayer(Duration.zero, text: 'AUREA'));
    expect(projeto().layers.whereType<TextLayer>(), isNotEmpty);

    // MONTAR A LETRA LE A FONTE DE VERDADE: dentro do `testWidgets` o
    // relogio e falso, entao o toque corre em `runAsync` — sem isso o
    // Future de I/O nunca completa e o teste fica pendurado.
    await tester.runAsync(() async {
      await tester.tap(find.text('Ativar 3D'));
      await tester.pump();
      // Tempo de verdade para a fonte abrir e a malha ser montada.
      await Future<void>.delayed(const Duration(milliseconds: 600));
    });
    await tester.pumpAndSettle();

    // VIROU 3D, e nao ficou os dois.
    expect(projeto().layers.whereType<TextLayer>(), isEmpty);
    final cena = projeto().layers.whereType<Scene3DLayer>().single;
    final no = cena.scene.nodes.firstWhere((n) => n.texto3d != null);
    expect(no.texto3d!.texto, 'AUREA');

    // E A FOLHA ABRIU NESSA CAMADA, com os controles prioritarios.
    expect(find.byKey(const ValueKey('texto3d-folha')), findsOneWidget);
    expect(
      find.byKey(const ValueKey('texto3d-controle-Profundidade')),
      findsOneWidget,
    );
    expect(find.byKey(const ValueKey('texto3d-estilo-cromo')), findsOneWidget);

    // NENHUMA PREVIA PROPRIA: o unico preview e o palco.
    expect(find.byType(RawImage), findsNothing);
    expect(find.byType(ParameterRow), findsWidgets);
  });
}

/// A doca real, sobre a primeira camada do projeto.
class _DocaHost extends ConsumerStatefulWidget {
  const _DocaHost();

  @override
  ConsumerState<_DocaHost> createState() => _DocaHostState();
}

class _DocaHostState extends ConsumerState<_DocaHost>
    with SingleTickerProviderStateMixin {
  late final PlaybackController playback;

  @override
  void initState() {
    super.initState();
    playback = PlaybackController(
      vsync: this,
      durationOf: () => ref.read(editorControllerProvider).duration,
    );
  }

  @override
  void dispose() {
    playback.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final camadas = ref.watch(editorControllerProvider).layers;
    return Scaffold(
      body: camadas.isEmpty
          ? const SizedBox.shrink()
          : LayerToolsDock(
              layer: camadas.first,
              playback: playback,
              onAction: (_) {},
            ),
    );
  }
}
