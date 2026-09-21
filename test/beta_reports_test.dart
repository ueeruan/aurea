import 'dart:io';
import 'dart:convert';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/application/font_service.dart';
import 'package:aurea/src/features/editor/application/ui/editor_session.dart';
import 'package:aurea/src/features/editor/application/ui/pro_mode.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/lottie_export.dart';
import 'package:aurea/src/features/editor/domain/svg_document.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:aurea/src/features/editor/presentation/am/am_timeline.dart';

import 'editor_hierarchy_test.dart' show openEditor;

class _FontsPicker extends FilePicker {
  _FontsPicker(this.paths);
  final List<String> paths;
  bool multiple = false;
  @override
  Future<FilePickerResult?> pickFiles({
    String? dialogTitle,
    String? initialDirectory,
    FileType type = FileType.any,
    List<String>? allowedExtensions,
    Function(FilePickerStatus)? onFileLoading,
    bool allowCompression = false,
    int compressionQuality = 0,
    bool allowMultiple = false,
    bool withData = false,
    bool withReadStream = false,
    bool lockParentWindow = false,
    bool readSequential = false,
  }) async {
    multiple = allowMultiple;
    return FilePickerResult([
      for (final p in paths)
        PlatformFile(name: p.split('/').last, size: 0, path: p),
    ]);
  }
}

class _SavePicker extends _FontsPicker {
  _SavePicker(this.destination) : super([]);
  String? destination;
  String? savedName;
  Uint8List? savedBytes;
  @override
  Future<String?> saveFile({
    String? dialogTitle,
    String? fileName,
    String? initialDirectory,
    FileType type = FileType.any,
    List<String>? allowedExtensions,
    Uint8List? bytes,
    bool lockParentWindow = false,
  }) async {
    savedName = fileName;
    savedBytes = bytes;
    return destination;
  }
}

void main() {
  setUpAll(() async {
    TestWidgetsFlutterBinding.ensureInitialized();
    FilePicker.platform = _FontsPicker([]);
    for (final f in [
      'Aurea Motion Sans',
      'Roboto',
      'CupertinoSystemText',
      'CupertinoSystemDisplay',
    ]) {
      await (FontLoader(f)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });
  testWidgets(
    'layer reorder and time moves remain usable in tools and compact timeline',
    (tester) async {
      final c = await openEditor(tester);
      final e = c.read(editorControllerProvider.notifier);
      final id = c.read(editorControllerProvider).layers.last.id;
      c.read(selectedLayerProvider.notifier).state = id;
      await tester.pumpAndSettle();
      expect(
        tester.widget<AmTimeline>(find.byType(AmTimeline)).singleLayerId,
        isNull,
      );
      await tester.tap(find.byKey(const ValueKey('camada-subir')));
      await tester.pumpAndSettle();
      expect(c.read(editorControllerProvider).layers.first.id, id);
      final clock = tester.widget<AmTimeline>(find.byType(AmTimeline)).playback;
      clock.seek(const Duration(seconds: 1));
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('camada-inicio-aqui')));
      await tester.pumpAndSettle();
      expect(
        c.read(editorControllerProvider).layerById(id)!.startTime,
        const Duration(seconds: 1),
      );
      c.read(editorSessionProvider.notifier).openPanel(EditorPanel.transform);
      await tester.pumpAndSettle();
      final before = c.read(editorControllerProvider).layerById(id)!.startTime;
      final label = find
          .text(c.read(editorControllerProvider).layerById(id)!.name)
          .last;
      await tester.drag(label, const Offset(75, 0));
      await tester.pumpAndSettle();
      expect(
        c.read(editorControllerProvider).layerById(id)!.startTime,
        greaterThan(before),
      );
      expect(tester.takeException(), isNull);
      e.undo();
    },
  );
  testWidgets(
    'text keyboard closes and a batch of fonts previews the authored text',
    (tester) async {
      final c = await openEditor(tester);
      final e = c.read(editorControllerProvider.notifier);
      e.addTextLayer(Duration.zero);
      await tester.pumpAndSettle();
      final id = c.read(selectedLayerProvider)!;
      c.read(editorSessionProvider.notifier).openPanel(EditorPanel.editText);
      await tester.pumpAndSettle();
      final field = find.byKey(const ValueKey('texto-conteudo'));
      await tester.enterText(field, 'Minha prévia');
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('texto-fechar-teclado')));
      await tester.pumpAndSettle();
      expect(
        tester
            .widget<EditableText>(find.byType(EditableText).first)
            .focusNode
            .hasFocus,
        isFalse,
      );
      final dir = Directory.systemTemp.createTempSync('aurea-font-batch-');
      const channel = MethodChannel('plugins.flutter.io/path_provider');
      final messenger =
          TestDefaultBinaryMessengerBinding.instance.defaultBinaryMessenger;
      messenger.setMockMethodCallHandler(channel, (_) async => dir.path);
      final original = FilePicker.platform;
      addTearDown(() async {
        FilePicker.platform = original;
        for (final f in ['BetaFontA', 'BetaFontB']) {
          await FontService.instance.remove(f);
        }
        messenger.setMockMethodCallHandler(channel, null);
        await dir.delete(recursive: true);
      });
      final paths = <String>[];
      final bytes = (await rootBundle.load(
        'assets/templates/dnyx/AureaMotionSans.ttf',
      )).buffer.asUint8List();
      for (final name in ['BetaFontA', 'BetaFontB']) {
        final file = File('${dir.path}/$name.ttf');
        file.writeAsBytesSync(bytes);
        paths.add(file.path);
      }
      paths.add('${dir.path}/broken.txt');
      final picker = _FontsPicker(paths);
      FilePicker.platform = picker;
      await tester.tap(find.byKey(const ValueKey('texto-fonte')));
      await tester.pumpAndSettle();
      await tester.ensureVisible(find.byKey(const ValueKey('fontes-importar')));
      await tester.runAsync(() async {
        await tester.tap(find.byKey(const ValueKey('fontes-importar')));
        final deadline = DateTime.now().add(const Duration(seconds: 5));
        // Registration finishes before the picker applies the first font.
        // Wait for both asynchronous steps, including the layer update.
        while ((!FontService.instance.has('BetaFontB') ||
                (c.read(editorControllerProvider).layerById(id) as TextLayer)
                        .fontFamily !=
                    'BetaFontA') &&
            DateTime.now().isBefore(deadline)) {
          await Future<void>.delayed(const Duration(milliseconds: 20));
        }
      });
      await tester.pumpAndSettle();
      expect(picker.multiple, isTrue);
      expect(
        FontService.instance.families,
        containsAll(['BetaFontA', 'BetaFontB']),
      );
      expect(
        (c.read(editorControllerProvider).layerById(id) as TextLayer)
            .fontFamily,
        'BetaFontA',
      );
      expect(find.text('Minha prévia'), findsWidgets);
      expect(tester.takeException(), isNull);
      // O aviso das fontes importadas fecha sozinho depois de uns
      // segundos; sem esperar, o relogio dele dispara com a arvore ja
      // desmontada e o teste falha "depois de ter terminado".
      await tester.pump(const Duration(seconds: 6));
    },
  );
  testWidgets(
    'SVG preserves scale rotation opacity and valid animation endpoints',
    (tester) async {
      final c = await openEditor(tester);
      final e = c.read(editorControllerProvider.notifier);
      final shape = c.read(editorControllerProvider).layers.first as ShapeLayer;
      final l = shape.copyLayer(
        scaleX: AnimatedDouble(.5),
        scaleY: AnimatedDouble(2),
        rotation: AnimatedDouble(30),
        opacity: AnimatedDouble(.4),
        position: AnimatedOffset(const Offset(200, 300))
            .withKeyframe(const Duration(seconds: 1), const Offset(200, 300))
            .withKeyframe(const Duration(seconds: 2), const Offset(400, 300)),
      );
      e.openProject(c.read(editorControllerProvider).copyWith(layers: [l]));
      final svg = exportAnimatedSvg(c.read(editorControllerProvider));
      expect(svg, contains('scale(0.5 2.0)'));
      expect(svg, contains('rotate(30.0)'));
      expect(svg, contains('opacity="0.4"'));
      for (final match in RegExp('keyTimes="([^"]+)"').allMatches(svg)) {
        final times = match.group(1)!.split(';').map(double.parse).toList();
        expect(times.first, 0);
        expect(times.last, 1);
      }
      expect(lerSvg(svg).formas, isNotEmpty);
      final openStroke = l.copyLayer(
        contents: [
          ShapeSvgPath(pathData: 'M0,0 L100,0 L100,100'),
          ShapeStroke(),
        ],
      );
      final strokeSvg = exportAnimatedSvg(
        c.read(editorControllerProvider).copyWith(layers: [openStroke]),
      );
      expect(
        RegExp('d="([^"]+)"').firstMatch(strokeSvg)!.group(1),
        isNot(contains(' Z')),
      );
      expect(strokeSvg, contains('stroke-linecap="round"'));
      e.toggleHidden(l.id);
      expect(
        exportAnimatedSvg(c.read(editorControllerProvider)),
        isNot(contains('<path')),
      );
    },
  );

  testWidgets('SVG export uses a chosen file and handles cancellation', (
    tester,
  ) async {
    final c = await openEditor(tester);
    c.read(proModeProvider.notifier).set(true);
    final e = c.read(editorControllerProvider.notifier);
    e.openProject(
      c.read(editorControllerProvider).copyWith(name: 'Meu/logo:beta'),
    );
    final dir = Directory.systemTemp.createTempSync('aurea-svg-save-');
    final output = File('${dir.path}/logo.svg');
    final picker = _SavePicker(output.path);
    final original = FilePicker.platform;
    FilePicker.platform = picker;
    addTearDown(() {
      FilePicker.platform = original;
      dir.deleteSync(recursive: true);
    });
    await tester.pumpAndSettle();
    // "Exportar" abre a TELA de exportar video (20/09: a folha do meio,
    // que repetia os controles da tela, deixou de existir). O que nao e
    // video — Lottie, SVG, template, pacote — fica atras de "Outros
    // formatos", la dentro.
    await tester.tap(find.byKey(const ValueKey('editor-export')));
    await tester.pumpAndSettle();
    await tester.tap(find.byKey(const ValueKey('export-outros-formatos')));
    await tester.pumpAndSettle();
    final button = find.text('Exportar SVG animado');
    await tester.ensureVisible(button);
    await tester.pumpAndSettle();
    await tester.runAsync(() async {
      await tester.tap(button);
      final deadline = DateTime.now().add(const Duration(seconds: 5));
      while (find.text('SVG animado salvo').evaluate().isEmpty &&
          DateTime.now().isBefore(deadline)) {
        await Future<void>.delayed(const Duration(milliseconds: 20));
        await tester.pump();
      }
    });
    await tester.pumpAndSettle();
    expect(picker.savedName, 'Meu_logo_beta.svg');
    expect(find.text('SVG animado salvo'), findsOneWidget);
    expect(lerSvg(output.readAsStringSync()).formas.length, 2);
    expect(output.readAsStringSync(), utf8.decode(picker.savedBytes!));
    picker.destination = null;
    await tester.ensureVisible(button);
    await tester.tap(button);
    await tester.pumpAndSettle();
    expect(find.text('Exportação cancelada'), findsOneWidget);
    expect(tester.takeException(), isNull);
  });

  testWidgets('empty project footer fits a small phone and opens add tools', (
    tester,
  ) async {
    final c = await openEditor(tester, size: const Size(320, 568));
    c
        .read(editorControllerProvider.notifier)
        .removeLayers(
          c.read(editorControllerProvider).layers.map((l) => l.id).toList(),
        );
    await tester.pumpAndSettle();
    final cta = find.byKey(const ValueKey('estado-vazio-cta'));
    final message = find.byKey(const ValueKey('estado-vazio'));
    expect(tester.getRect(cta).bottom, lessThanOrEqualTo(568));
    expect(tester.getRect(message).bottom, lessThanOrEqualTo(568));
    expect(cta.hitTestable(), findsOneWidget);
    await tester.tap(cta);
    await tester.pumpAndSettle();
    expect(c.read(editorSessionProvider).adding, isTrue);
    expect(tester.takeException(), isNull);
  });

  testWidgets('selecionar nao acende a linha vermelha de apoio', (
    tester,
  ) async {
    // ESTE TESTE JA COBRIU O CONTRARIO. Ate o beta 57 a cruz vermelha
    // aparecia sempre que havia camada selecionada, e o teste exigia
    // isso. O relato do beta 57 desfez a regra: "a linha vermelha de
    // apoio ta bugando, os user passam o dedo por cima e para de mexer
    // (...) essa linha deve aparecer quando tiver mexendo na posicao do
    // objeto somente".
    //
    // Uma marca permanente nao ajudava ninguem a mirar e, quando o
    // objeto realmente parava nela (pelo encaixe, invisivel), a linha
    // ja tinha virado paisagem. Agora ela e o sinal do encaixe: aparece
    // no eixo que pegou, enquanto o dedo esta movendo, e some ao soltar.
    // O outro lado da regra esta em test/linha_de_apoio_test.dart.
    final c = await openEditor(tester);
    c.read(selectedLayerProvider.notifier).state = c
        .read(editorControllerProvider)
        .layers
        .first
        .id;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('composition-guides')), paintsNothing);
    c.read(selectedLayerProvider.notifier).state = null;
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('composition-guides')), paintsNothing);
  });
}
