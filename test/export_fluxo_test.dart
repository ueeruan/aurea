import 'dart:io';

import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/export/presentation/export_video_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

/// A EXPORTACAO EM FLUXO — o caminho novo, sem PNG e sem disco.
///
/// A correcao central da exportacao foi tirar o PNG do meio: cada quadro
/// ia da GPU para a CPU, era comprimido com zlib, gravado no disco, e so
/// depois — numa segunda passada — lido de volta, descomprimido e
/// entregue ao codificador. O PNG nao servia a nenhum proposito de
/// imagem; era so o jeito de os pixels chegarem ao lado nativo. Custava
/// a maior parte do tempo e obrigava a guardar o filme inteiro
/// descomprimido em disco.
///
/// Estes testes prendem as duas propriedades que a correcao criou: o
/// codificador recebe BYTES, na ordem certa, e o disco nao ve quadro
/// nenhum.
void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  late Directory temp;
  late List<String> chamadas;
  late List<int> tamanhosDeQuadro;

  setUp(() {
    temp = Directory.systemTemp.createTempSync('aurea_export_fluxo');
    chamadas = [];
    tamanhosDeQuadro = [];

    // path_provider: as pastas de trabalho e de saida vivem aqui.
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
          const MethodChannel('plugins.flutter.io/path_provider'),
          (call) async => temp.path,
        );

    // O codificador da plataforma, de mentira: ele so anota o que
    // recebeu. E o que se quer verificar.
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(const MethodChannel('aurea/encoder'), (
          call,
        ) async {
          chamadas.add(call.method);
          switch (call.method) {
            case 'available':
              return true;
            case 'freeBytes':
              return 64 * 1024 * 1024 * 1024; // 64 GB livres
            case 'frameRgba':
              final bytes = call.arguments['bytes'] as Uint8List;
              tamanhosDeQuadro.add(bytes.length);
              return true;
            case 'finish':
              // O codificador "produz" o arquivo mudo que o motor espera.
              for (final f
                  in temp.listSync(recursive: true).whereType<Directory>()) {
                final mudo = File('${f.path}/mudo.mp4');
                if (f.path.endsWith('aurea_export')) {
                  mudo.writeAsBytesSync(List.filled(4096, 7));
                }
              }
              return true;
            default:
              return true;
          }
        });
  });

  tearDown(() {
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(
          const MethodChannel('plugins.flutter.io/path_provider'),
          null,
        );
    TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger
        .setMockMethodCallHandler(const MethodChannel('aurea/encoder'), null);
    try {
      temp.deleteSync(recursive: true);
    } catch (_) {}
  });

  Future<ProviderContainer> exportar(WidgetTester tester) async {
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.resetPhysicalSize);
    addTearDown(tester.view.resetDevicePixelRatio);

    final container = ProviderContainer();
    addTearDown(container.dispose);
    final c = container.read(editorControllerProvider.notifier);
    c.addShapeLayer(Duration.zero);
    // Meio segundo a 30 fps: quinze quadros, o bastante para o laco
    // rodar de verdade sem o teste virar bancada.
    c.openProject(
      c.state.copyWith(
        layers: [
          for (final l in c.state.layers)
            l.copyLayer(duration: const Duration(milliseconds: 500)),
        ],
      ),
    );

    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: container,
        child: const MaterialApp(home: ExportVideoScreen()),
      ),
    );
    await tester.tap(find.text('Exportar'));
    await tester.pump();
    // O laco de quadros e assincrono e espera `endOfFrame` a cada passo.
    for (var i = 0; i < 240; i++) {
      await tester.pump(const Duration(milliseconds: 16));
      if (chamadas.contains('finish')) break;
    }
    return container;
  }

  // O QUE ESTE ARQUIVO NAO CONSEGUE TESTAR, e por que esta dito aqui:
  // o laco de quadros chama `RenderRepaintBoundary.toImage`, que e
  // assincrono de VERDADE (espera a thread de raster). Sob o relogio
  // falso do teste de widget ele nunca completa, e `pump` dentro de
  // `runAsync` nao e permitido. Entao o laco para no primeiro quadro.
  //
  // Um teste que passasse assim passaria por nao ter produzido quadro
  // nenhum — pior que nao existir. O que sobra e verificavel e vale:
  // a ESCOLHA do caminho e a ORDEM das etapas antes do laco, que e onde
  // moravam dois defeitos reais (o caminho por arquivo era o unico, e o
  // espaco em disco so era descoberto no fim).
  testWidgets('escolhe o fluxo e confere o espaco ANTES de abrir', (
    tester,
  ) async {
    await exportar(tester);

    expect(chamadas, contains('available'));
    expect(chamadas, contains('freeBytes'));
    expect(chamadas, contains('start'));
    expect(
      chamadas,
      isNot(contains('frame')),
      reason:
          'o caminho por arquivo PNG e so a reserva; nao devia rodar '
          'num aparelho com codificador',
    );
    // A ordem importa: conferir espaco depois de comecar a gravar nao
    // serve de nada, e era assim que uma exportacao de meia hora
    // descobria no fim que nao cabia.
    expect(
      chamadas.indexOf('freeBytes'),
      lessThan(chamadas.indexOf('start')),
      reason: 'o espaco foi conferido depois de abrir o codificador',
    );
    expect(chamadas.indexOf('available'), lessThan(chamadas.indexOf('start')));
  });

  testWidgets('nenhum quadro em PNG e gravado antes do laco', (tester) async {
    await exportar(tester);
    final quadros = temp
        .listSync(recursive: true)
        .whereType<File>()
        .map((f) => f.path)
        .where((p) => p.endsWith('.png') || p.endsWith('.jpg'))
        .toList();
    expect(
      quadros,
      isEmpty,
      reason:
          'a exportacao voltou a gravar quadros no disco: '
          '${quadros.take(3)}',
    );
  });
}
