// O PREVIEW REAL USANDO O MOTOR C++ — O TESTE QUE FECHA A FASE 2.
//
// O que os testes do `nucleo_vulkan` provam e que a TUBULACAO funciona e
// que o quadro composto chega a swapchain. O que faltava era a pergunta
// que o dono escreveu com todas as letras: "nao declare o renderer pronto
// sem demonstrar que ele realmente esta sendo utilizado pelo preview".
//
// Aqui a arvore de verdade sobe: `CompositionView` com um projeto de
// verdade, o interruptor do motor ligado, e o numero que decide — quantos
// quadros o MOTOR apresentou enquanto o preview esteve na tela.
//
// O QUE ELE NAO PROVA: desempenho. O emulador usa SwiftShader (software) e
// o compositor de referencia e CPU; um numero de tempo de quadro daqui nao
// diz nada sobre um celular. Isso exige aparelho.
//
// Rodar:
//   flutter test integration_test/preview_do_motor_test.dart -d emulator-5554
import 'package:aurea_render/aurea_render.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/video_layer_manager.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_vulkan.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';

void main() {
  IntegrationTestWidgetsFlutterBinding.ensureInitialized();

  /// Um projeto de verdade: duas formas visiveis, com tempo e animacao.
  Widget palco(ProviderContainer c) {
    final time = ValueNotifier<Duration>(const Duration(milliseconds: 500));
    return UncontrolledProviderScope(
      container: c,
      child: MaterialApp(
        home: Scaffold(
          body: Center(
            child: SizedBox(
              width: 320,
              height: 240,
              child: ColoredBox(
                color: Colors.black,
                child: CompositionView(
                  time: time,
                  videos: VideoLayerManager(),
                  selectedId: null,
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }

  ProviderContainer containerComProjeto() {
    final c = ProviderContainer();
    final e = c.read(editorControllerProvider.notifier);
    e.addShapeLayer(
      Duration.zero,
      name: 'Fundo',
      contents: [
        ShapePath(primitive: ShapePrimitive.rectangle),
        ShapeFill(color: const Color(0xFF204060)),
      ],
    );
    c.read(editorControllerProvider.notifier).editPosition(
      c.read(editorControllerProvider).layers.first.id,
      Duration.zero,
      const Offset(160, 120),
    );
    e.addShapeLayer(
      Duration.zero,
      name: 'Frente',
      contents: [
        ShapeParametric(
          kind: ParamShapeKind.ellipse,
          sizeX: AnimatedDouble(80),
          sizeY: AnimatedDouble(80),
        ),
        ShapeFill(color: const Color(0xFFFF8020)),
      ],
    );
    c.read(editorControllerProvider.notifier).editPosition(
      c.read(editorControllerProvider).layers.first.id,
      Duration.zero,
      const Offset(160, 120),
    );
    return c;
  }

  testWidgets('o preview monta o motor C++ e os quadros CHEGAM na tela', (
    tester,
  ) async {
    final c = containerComProjeto();
    addTearDown(c.dispose);
    c.read(motorDoPreviewProvider.notifier).state = true;

    final antes = PreviewVulkan.estatisticas();
    await tester.pumpWidget(palco(c));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 100));

    // O PREVIEW DESENHADO PELO MOTOR ESTA NA ARVORE. Sem isto o teste
    // mediria o caminho antigo e chamaria de sucesso.
    expect(
      find.byKey(const ValueKey('preview-motor-cpp')),
      findsOneWidget,
      reason: 'o interruptor nao levou o preview para o motor',
    );

    // E OS QUADROS SAO APRESENTADOS DE VERDADE. Um ticker a 60 Hz num
    // emulador de software nao entrega um quadro por `pump`; o que importa
    // e que a contagem SUBA.
    for (var i = 0; i < 40; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }
    final depois = PreviewVulkan.estatisticas();
    // ignore: avoid_print
    print('== PREVIEW DO MOTOR ==\nantes: $antes\ndepois: $depois');

    expect(
      depois.apresentados,
      greaterThan(antes.apresentados),
      reason: 'o motor nao apresentou nenhum quadro',
    );
    expect(depois.falhas, antes.falhas, reason: 'nenhuma falha ao apresentar');
  });

  testWidgets('o interruptor DESLIGADO mantem o caminho do Flutter', (
    tester,
  ) async {
    final c = containerComProjeto();
    addTearDown(c.dispose);
    c.read(motorDoPreviewProvider.notifier).state = false;
    await tester.pumpWidget(palco(c));
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.byKey(const ValueKey('preview-motor-cpp')), findsNothing);
    // A composicao do Flutter voltou a ser montada: as camadas estao la.
    expect(find.byType(CompositionView), findsOneWidget);
  });

  testWidgets('o projeto continua inteiro depois de passar pelo motor', (
    tester,
  ) async {
    final c = containerComProjeto();
    addTearDown(c.dispose);
    c.read(motorDoPreviewProvider.notifier).state = true;
    final quantas = c.read(editorControllerProvider).layers.length;
    await tester.pumpWidget(palco(c));
    for (var i = 0; i < 10; i++) {
      await tester.pump(const Duration(milliseconds: 16));
    }
    // O MOTOR NAO ESCREVE NO PROJETO. Ele le a cena. Um renderizador que
    // mexe no estado e um renderizador que perde o trabalho de quem edita.
    expect(c.read(editorControllerProvider).layers.length, quantas);
    expect(
      c.read(editorControllerProvider).layers.every((l) => l.effects.isEmpty),
      isTrue,
    );
  });
}
