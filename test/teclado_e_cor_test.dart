// TECLADO NUMERICO E SELETOR DE COR (v1.1.1): o valor exato se digita num
// teclado do app (contas, sinal, tempo com dois-pontos, "=" que resolve);
// a cor tem quadro, roda e RGB, original ao lado da nova, codigo para
// copiar e colar e cores guardadas que se apagam arrastando.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/codigo_de_cor.dart';
import 'package:aurea/src/features/editor/domain/expr.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/core/ds/aurea_seletor_de_cor.dart';
import 'package:aurea/src/core/ds/conta_gotas.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/core/ds/aurea_teclado_numerico.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

import 'apoio/print_da_ui.dart';

class _Projetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

void main() {
  setUpAll(carregarFontesReais);

  test('o que o teclado entrega vira numero: contas, simbolos e tempo', () {
    expect(lerValorDigitado('1:30'), 90);
    expect(lerValorDigitado('1:02:03.5'), 3723.5);
    expect(lerValorDigitado('-0:15'), -15);
    expect(lerValorDigitado('2×3'), 6);
    expect(lerValorDigitado('10÷4'), 2.5);
    expect(lerValorDigitado('−5+1'), -4);
    expect(lerValorDigitado('12,5'), 12.5);
    expect(lerValorDigitado('50%', percentOf: 200), 100);
    expect(lerValorDigitado('3×'), isNull);
    expect(formatarValorDigitado(12.50, 2), '12.5');
    expect(formatarValorDigitado(3, 1), '3');
  });

  testWidgets('teclado: conta resolvida, sinal e o teto da faixa', (tester) async {
    double? recebido;
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: Builder(
            builder: (context) => TextButton(
              onPressed: () async {
                recebido = await showNumberInput(
                  context,
                  value: 40,
                  min: -100,
                  max: 100,
                  decimals: 1,
                );
              },
              child: const Text('abrir'),
            ),
          ),
        ),
      ),
    );
    await tester.tap(find.text('abrir'));
    await tester.pumpAndSettle();
    String texto() =>
        tester.widget<EditableText>(find.byType(EditableText)).controller.text;

    for (final t in ['1', '2', 'apagar', '5', 'vezes', '4']) {
      await tester.tap(find.byKey(ValueKey('tecla-$t')));
      await tester.pump();
    }
    expect(texto(), '15×4', reason: 'o primeiro digito troca o valor selecionado');
    expect(find.text('= 60'), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('tecla-igual')));
    await tester.pump();
    expect(texto(), '60');

    await tester.tap(find.byKey(const ValueKey('tecla-sinal')));
    await tester.pump();
    expect(texto(), '-60');

    await tester.tap(find.byKey(const ValueKey('tecla-0')));
    await tester.pump();
    expect(find.text('Fica em -100'), findsOneWidget);

    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(recebido, -100);
  });

  test('codigos de cor entram e saem', () {
    expect(corDoCodigo('#3366FF'), const Color(0xFF3366FF));
    expect(corDoCodigo('36f'), const Color(0xFF3366FF));
    expect(corDoCodigo('#3366FF80'), const Color(0x803366FF));
    expect(corDoCodigo('0x803366FF'), const Color(0x803366FF));
    expect(corDoCodigo('rgb(51, 102, 255)'), const Color(0xFF3366FF));
    final rgba = corDoCodigo('rgba(51,102,255,0.5)')!;
    expect(rgba.a, closeTo(.5, 1e-9));
    expect(corDoCodigo('rgb(100%, 0%, 0%)'), const Color(0xFFFF0000));
    expect(corDoCodigo('azul'), isNull);
    expect(corDoCodigo('rgb(300, 0, 0)'), isNull);
    expect(codigoHexDaCor(const Color(0xFF3366FF)), '#3366FF');
    expect(codigoHexDaCor(const Color(0x803366FF)), '#3366FF80');
    expect(codigoRgbaDaCor(const Color(0xFF3366FF)), 'rgba(51, 102, 255, 1)');
  });

  testWidgets('seletor: abas, original x nova, RGB digitado, guardar e apagar cor', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(400, 1000);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final vistas = <Color>[];
    String? colado;
    tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
      SystemChannels.platform,
      (call) async {
        if (call.method == 'Clipboard.getData') return {'text': colado};
        return null;
      },
    );
    addTearDown(
      () => tester.binding.defaultBinaryMessenger.setMockMethodCallHandler(
        SystemChannels.platform,
        null,
      ),
    );
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: SeletorDeCor(
            initial: const Color(0xFF3366FF),
            onChanged: vistas.add,
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('cor-quadro')), findsOneWidget);
    // Fora do editor nao ha palco: o conta-gotas nao aparece.
    expect(find.byKey(const ValueKey('cor-conta-gotas')), findsNothing);

    await tester.tap(find.byKey(const ValueKey('cor-aba-roda')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('cor-roda')), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('cor-aba-rgb')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('cor-canal-R-valor')));
    await tester.pumpAndSettle();
    await tester.enterText(find.byKey(const ValueKey('valor-campo')), '255');
    await tester.tap(find.text('OK'));
    await tester.pumpAndSettle();
    expect(vistas.last.toARGB32(), 0xFFFF66FF);
    expect(
      find.descendant(
        of: find.byKey(const ValueKey('cor-canal-R-valor')),
        matching: find.text('255'),
      ),
      findsOneWidget,
    );

    // A original volta com um toque.
    await tester.tap(find.byKey(const ValueKey('cor-original')));
    await tester.pumpAndSettle();
    expect(vistas.last.toARGB32(), 0xFF3366FF);

    // Colar um codigo valido aplica; um invalido avisa.
    colado = 'rgb(0, 255, 0)';
    await tester.tap(find.byKey(const ValueKey('cor-colar')));
    await tester.pumpAndSettle();
    expect(vistas.last.toARGB32(), 0xFF00FF00);
    colado = 'banana';
    await tester.tap(find.byKey(const ValueKey('cor-colar')));
    await tester.pumpAndSettle();
    expect(find.text('Não é um código de cor'), findsOneWidget);

    // Guardar e apagar arrastando para a lixeira.
    await tester.tap(find.byKey(const ValueKey('cor-salvar')));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('cor-amostra-0')), findsOneWidget);
    final gesto = await tester.startGesture(
      tester.getCenter(find.byKey(const ValueKey('cor-amostra-0'))),
    );
    await tester.pump(const Duration(milliseconds: 700));
    expect(find.byKey(const ValueKey('cor-lixeira')), findsOneWidget);
    await gesto.moveTo(tester.getCenter(find.byKey(const ValueKey('cor-lixeira'))));
    await tester.pump();
    await gesto.up();
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('cor-amostra-0')), findsNothing);
    expect(find.byKey(const ValueKey('cor-salvar')), findsOneWidget);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 5));
  });
  test('o pixel certo sai da foto do palco', () {
    final dados = ByteData(2 * 2 * 4);
    // (1, 0) vermelho opaco; (0, 1) azul meio transparente.
    dados
      ..setUint8(4, 255)
      ..setUint8(7, 255)
      ..setUint8(10, 255)
      ..setUint8(11, 128);
    expect(corNoPixel(dados, 2, 2, const Offset(1.4, .2)), const Color(0xFFFF0000));
    expect(corNoPixel(dados, 2, 2, const Offset(0, 1)), const Color(0x800000FF));
    expect(corNoPixel(dados, 2, 2, const Offset(2, 0)), isNull);
  });

  testWidgets('no editor: o seletor ganha conta-gotas e pega a cor do palco', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final c = ProviderContainer(
      overrides: [projectsControllerProvider.overrideWith(_Projetos.new)],
    );
    addTearDown(c.dispose);
    c.read(editorControllerProvider.notifier).addShapeLayer(Duration.zero, name: 'F');
    final chave = GlobalKey();
    Color? escolhida;
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: RepaintBoundary(
          key: chave,
          child: MaterialApp(
            theme: ThemeData(
              platform: TargetPlatform.iOS,
              fontFamily: 'Aurea Motion Sans',
              brightness: Brightness.dark,
            ),
            home: const EditorScreen(),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    final contexto = tester.element(find.byType(EditorScreen));
    showColorPicker(contexto, initial: const Color(0xFF3366FF)).then(
      (v) => escolhida = v,
    );
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('cor-conta-gotas')), findsOneWidget);
    await gravarPrint(tester, chave, 'seletor-de-cor');
    await tester.tap(find.byKey(const ValueKey('cor-aba-roda')));
    await tester.pumpAndSettle();
    await gravarPrint(tester, chave, 'seletor-de-cor-roda');

    await tester.tap(find.byKey(const ValueKey('cor-conta-gotas')));
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 300)));
    await tester.pumpAndSettle();
    await tester.runAsync(() => Future<void>.delayed(const Duration(milliseconds: 300)));
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('conta-gotas-palco')), findsOneWidget);
    final palco = tester.getRect(find.byKey(const ValueKey('conta-gotas-palco')));
    final gesto = await tester.startGesture(palco.center);
    await tester.pump();
    await gesto.moveBy(const Offset(4, 4));
    await tester.pump();
    expect(find.byKey(const ValueKey('conta-gotas-lupa')), findsOneWidget);
    await gravarPrint(tester, chave, 'conta-gotas');
    await gesto.up();
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('conta-gotas-palco')), findsNothing);
    await tester.tap(find.byKey(const ValueKey('cor-pronto')));
    await tester.pumpAndSettle();
    expect(escolhida, isNotNull);
    expect(escolhida!.a, 1);
    expect(tester.takeException(), isNull);
    await tester.pump(const Duration(seconds: 1));
  });

  testWidgets('print: teclado numerico', (tester) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final chave = GlobalKey();
    await tester.pumpWidget(
      RepaintBoundary(
        key: chave,
        child: MaterialApp(
          theme: ThemeData(
            platform: TargetPlatform.iOS,
            fontFamily: 'Aurea Motion Sans',
            brightness: Brightness.dark,
          ),
          home: Scaffold(
            body: Builder(
              builder: (context) => TextButton(
                onPressed: () => showNumberInput(
                  context,
                  value: 1.5,
                  unit: 's',
                  min: 0,
                  max: 60,
                  decimals: 2,
                  title: 'Duração',
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
    for (final t in ['1', 'dois-pontos', '3', '0']) {
      await tester.tap(find.byKey(ValueKey('tecla-$t')));
      await tester.pump();
    }
    await tester.pumpAndSettle();
    expect(find.text('Fica em 60s'), findsOneWidget);
    await gravarPrint(tester, chave, 'teclado-numerico');
  });
}
