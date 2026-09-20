import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:ui' as ui;

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:aurea/src/features/editor/presentation/editor_screen.dart';
import 'package:aurea/src/features/editor/presentation/widgets/add_layer_sheet.dart';
import 'package:aurea/src/features/editor/presentation/widgets/gallery_panel.dart';
import 'package:aurea/src/features/editor/presentation/widgets/preview_stage.dart';
import 'package:aurea/src/features/media/application/gallery_service.dart';
import 'package:aurea/src/features/media/application/media_import_service.dart';
import 'package:aurea/src/features/media/application/midias_recentes.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:flutter/material.dart';
import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image_picker/image_picker.dart';
import 'package:shared_preferences/shared_preferences.dart';

class _Projects extends ProjectsController {
  @override
  List<VideoProject> build() => [];
  @override
  void upsert(VideoProject project) => state = [project];
}

class _Gallery extends GalleryService {
  GalleryAccess access = GalleryAccess.full;
  bool fail = false;
  int thumbnails = 0, selections = 0;
  Completer<List<GalleryAsset>>? delayed;
  @override
  Future<GalleryAccess> requestAccess() async => access;
  @override
  Future<List<GalleryAlbum>> albums() async => const [
    GalleryAlbum('all', 'Todos'),
    GalleryAlbum('photos', 'Fotos'),
  ];
  @override
  Future<List<GalleryAsset>> page(String album, int page, int size) async {
    if (album == 'all' && delayed != null) return delayed!.future;
    if (fail) throw StateError('offline');
    return List.generate(
      page == 0 ? size : 4,
      (i) => GalleryAsset('$album-$page-$i', video: i.isOdd),
    );
  }

  @override
  Future<Uint8List?> thumbnail(GalleryAsset asset) async {
    thumbnails++;
    return (await rootBundle.load('assets/templates/dnyx/gallery-left.png'))
        .buffer
        .asUint8List();
  }

  @override
  Future<File?> file(GalleryAsset asset) async => File('test-photo.png');
  @override
  Future<void> selectMore() async {
    selections++;
  }
}

class _Importer extends MediaImportService {
  bool fail = false;
  int copias = 0;
  @override
  Future<XFile> persist(XFile file, {bool image = false}) async {
    copias++;
    if (fail) throw StateError('unavailable');
    return XFile('saved-photo.png');
  }

  @override
  Future<XFile?> pickImageFromGallery() async => null;
}

void main() {
  setUpAll(() async {
    for (final family in [
      'Aurea Motion Sans',
      'Roboto',
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
  for (final size in [const Size(360, 640), const Size(430, 844)]) {
    testWidgets('embedded add and media panels preserve preview at $size', (
      tester,
    ) async {
      tester.view.physicalSize = size;
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.resetPhysicalSize);
      addTearDown(tester.view.resetDevicePixelRatio);
      final gallery = _Gallery();
      final container = ProviderContainer(
        overrides: [
          projectsControllerProvider.overrideWith(_Projects.new),
          galleryServiceProvider.overrideWith((ref) => gallery),
          mediaImportServiceProvider.overrideWithValue(_Importer()),
        ],
      );
      addTearDown(container.dispose);
      container
          .read(editorControllerProvider.notifier)
          .openProject(VideoProject.empty('Projeto de teste'));
      final boundary = GlobalKey();
      Future<void> capture(String name) async {
        if (!const bool.fromEnvironment('AUREA_CAPTURE_UI') ||
            size.width != 430) {
          return;
        }
        await tester.pump();
        await tester.runAsync(() async {
          final image =
              await (boundary.currentContext!.findRenderObject()!
                      as RenderRepaintBoundary)
                  .toImage(pixelRatio: 2);
          final bytes = (await image.toByteData(
            format: ui.ImageByteFormat.png,
          ))!.buffer.asUint8List();
          await File('output/am-ui-reference/$name.png').writeAsBytes(bytes);
          image.dispose();
        });
      }

      await tester.pumpWidget(
        UncontrolledProviderScope(
          container: container,
          child: RepaintBoundary(
            key: boundary,
            child: const MaterialApp(
              debugShowCheckedModeBanner: false,
              home: EditorScreen(),
            ),
          ),
        ),
      );
      await tester.pumpAndSettle();
      final preview = tester.getRect(find.byType(PreviewStage));
      final barriers = find.byType(ModalBarrier).evaluate().length;
      await tester.tap(find.byTooltip('Adicionar camada'));
      await tester.pumpAndSettle();
      expect(find.byType(AddLayerPanel), findsOneWidget);
      expect(find.byType(ModalBarrier).evaluate().length, barriers);
      expect(tester.getRect(find.byType(PreviewStage)), preview);
      await capture('aurea-shapes');
      await tester.tap(find.text('Mídia'));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('gallery-grid')), findsOneWidget);
      expect(
        gallery.thumbnails,
        lessThan(30),
        reason: 'Only visible thumbnails are decoded',
      );
      expect(tester.getRect(find.byType(PreviewStage)), preview);
      await tester.tap(find.byTooltip('Selecionar álbum'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Fotos').last);
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('photos-0-0')), findsOneWidget);
      // Widget-test clocks do not wait for native image decoders. Explicitly
      // await the displayed providers so this verifies pixels, not just tiles.
      await tester.runAsync(() async {
        for (final element
            in find
                .descendant(
                  of: find.byType(GalleryPanel),
                  matching: find.byType(Image),
                )
                .evaluate()) {
          await precacheImage((element.widget as Image).image, element);
        }
      });
      await tester.pumpAndSettle();
      expect(
        tester
            .widgetList<RawImage>(
              find.descendant(
                of: find.byType(GalleryPanel),
                matching: find.byType(RawImage),
              ),
            )
            .where((image) => image.image != null),
        isNotEmpty,
      );
      await capture('aurea-gallery');
      await tester.tap(find.byTooltip('Fechar adicionar'));
      await tester.pumpAndSettle();
      expect(find.byType(AddLayerPanel), findsNothing);
      expect(tester.getRect(find.byType(PreviewStage)), preview);
      await tester.tap(find.byKey(const ValueKey('editor-fab')));
      await tester.pumpAndSettle();
      await tester.tap(find.byTooltip('Circulo'));
      await tester.pumpAndSettle();
      expect(container.read(editorControllerProvider).layers.length, 1);
      expect(find.byType(AddLayerPanel), findsNothing);
      expect(tester.getRect(find.byType(PreviewStage)), preview);
      await capture('aurea-layer-tools');
      expect(tester.takeException(), isNull);
    });
  }

  testWidgets('limited gallery, failed import and cancellation remain usable', (
    tester,
  ) async {
    final gallery = _Gallery()..access = GalleryAccess.limited;
    final importer = _Importer()..fail = true;
    var imports = 0;
    await tester.pumpWidget(
      ProviderScope(
        overrides: [
          galleryServiceProvider.overrideWith((ref) => gallery),
          mediaImportServiceProvider.overrideWithValue(importer),
        ],
        child: MaterialApp(
          home: Scaffold(
            body: SizedBox(
              width: 320,
              height: 320,
              child: GalleryPanel(
                onImport: (_, _, _) async {
                  imports++;
                },
              ),
            ),
          ),
        ),
      ),
    );
    await tester.pumpAndSettle();
    expect(
      find.text('Acesso limitado · Selecionar mais fotos'),
      findsOneWidget,
    );
    await tester.tap(find.text('Acesso limitado · Selecionar mais fotos'));
    await tester.pumpAndSettle();
    expect(gallery.selections, 1);
    await tester.tap(find.byKey(const ValueKey('all-0-0')));
    await tester.pumpAndSettle();
    expect(imports, 0);
    expect(find.textContaining('Não foi possível importar'), findsOneWidget);
    expect(find.text('Carregando mídia…'), findsNothing);
    await tester.tap(find.byTooltip('Fotos do sistema'));
    await tester.pumpAndSettle();
    expect(imports, 0, reason: 'Cancellation does not create a layer');
    importer.fail = false;
    await tester.tap(find.byKey(const ValueKey('all-0-0')));
    await tester.pumpAndSettle();
    expect(imports, 1);
    expect(tester.takeException(), isNull);
  });

  testWidgets(
    'late response from previous album cannot replace selected album',
    (tester) async {
      final gallery = _Gallery()..delayed = Completer<List<GalleryAsset>>();
      await tester.pumpWidget(
        ProviderScope(
          overrides: [galleryServiceProvider.overrideWith((ref) => gallery)],
          child: MaterialApp(
            home: Scaffold(
              body: SizedBox(
                width: 320,
                height: 320,
                child: GalleryPanel(onImport: (_, _, _) async {}),
              ),
            ),
          ),
        ),
      );
      await tester.pump();
      await tester.pump();
      await tester.tap(find.byTooltip('Selecionar álbum'));
      await tester.pump(const Duration(milliseconds: 300));
      await tester.pump(const Duration(milliseconds: 300));
      await tester.tap(find.text('Fotos').last);
      await tester.pumpAndSettle();
      gallery.delayed!.complete(const [GalleryAsset('stale', video: false)]);
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('photos-0-0')), findsOneWidget);
      expect(find.byKey(const ValueKey('stale')), findsNothing);
    },
  );

  group('álbum lembrado e Recentes da Aurea', () {
    late Directory temp;
    late SharedPreferences prefs;

    setUp(() => temp = Directory.systemTemp.createTempSync('galeria-ui'));
    tearDown(() {
      try {
        temp.deleteSync(recursive: true);
      } catch (_) {}
    });

    Future<Widget> painel(
      Map<String, Object> valores, {
      _Importer? importer,
      void Function(XFile file)? aoImportar,
    }) async {
      SharedPreferences.setMockInitialValues(valores);
      prefs = await SharedPreferences.getInstance();
      return ProviderScope(
        overrides: [
          sharedPreferencesProvider.overrideWithValue(prefs),
          galleryServiceProvider.overrideWith((ref) => _Gallery()),
          mediaImportServiceProvider.overrideWithValue(
            importer ?? _Importer(),
          ),
        ],
        child: MaterialApp(
          home: Scaffold(
            body: SizedBox(
              width: 320,
              height: 320,
              child: GalleryPanel(
                onImport: (file, _, _) async => aoImportar?.call(file),
              ),
            ),
          ),
        ),
      );
    }

    testWidgets('a galeria reabre no álbum da última vez', (tester) async {
      // REGRESSAO: o estado do painel morre quando a folha fecha, e toda
      // abertura caia em "Todos" — dez fotos da mesma pasta, dez buscas.
      await tester.pumpWidget(await painel({'galeria.ultimo_album': 'photos'}));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('photos-0-0')), findsOneWidget);
      expect(find.text('Fotos'), findsOneWidget);
    });

    testWidgets('escolher no menu grava o álbum', (tester) async {
      await tester.pumpWidget(await painel(const {}));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('all-0-0')), findsOneWidget);
      expect(
        find.byKey(const ValueKey('galeria-recentes')),
        findsNothing,
        reason: 'sem recentes, sem botão do relógio',
      );
      await tester.tap(find.byTooltip('Selecionar álbum'));
      await tester.pumpAndSettle();
      await tester.tap(find.text('Fotos').last);
      await tester.pumpAndSettle();
      expect(prefs.getString('galeria.ultimo_album'), 'photos');
      expect(find.byKey(const ValueKey('photos-0-0')), findsOneWidget);
    });

    testWidgets('álbum que sumiu do aparelho cai no primeiro', (tester) async {
      await tester.pumpWidget(await painel({'galeria.ultimo_album': 'sumiu'}));
      await tester.pumpAndSettle();
      expect(find.byKey(const ValueKey('all-0-0')), findsOneWidget);
      expect(find.text('Todos'), findsOneWidget);
    });

    testWidgets('um recente entra sem copiar o arquivo de novo', (
      tester,
    ) async {
      final png = File('${temp.path}/logo.png')
        ..writeAsBytesSync(
          (await rootBundle.load(
            'assets/templates/dnyx/gallery-left.png',
          )).buffer.asUint8List(),
        );
      final importer = _Importer();
      XFile? importado;
      await tester.pumpWidget(
        await painel(
          {
            MidiasRecentesNotifier.kChave: jsonEncode([
              {'c': png.path, 'n': 'logo.png', 'v': false},
            ]),
          },
          importer: importer,
          aoImportar: (f) => importado = f,
        ),
      );
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('galeria-recentes')));
      await tester.pumpAndSettle();
      expect(
        find.byKey(const ValueKey('galeria-grade-recentes')),
        findsOneWidget,
      );
      await tester.tap(find.byKey(ValueKey('recente-${png.path}')));
      await tester.pumpAndSettle();
      expect(importado?.path, png.path);
      expect(importado?.name, 'logo.png');
      expect(
        importer.copias,
        0,
        reason: 'o arquivo já mora dentro do app: nada de segunda cópia',
      );
      expect(tester.takeException(), isNull);
    });

    testWidgets('recente que sumiu do disco sai da lista com recado', (
      tester,
    ) async {
      final sumido = '${temp.path}/foi-embora.png';
      final vivo = File('${temp.path}/vivo.png')
        ..writeAsBytesSync(
          (await rootBundle.load(
            'assets/templates/dnyx/gallery-left.png',
          )).buffer.asUint8List(),
        );
      await tester.pumpWidget(
        await painel({
          MidiasRecentesNotifier.kChave: jsonEncode([
            {'c': vivo.path, 'n': 'vivo.png', 'v': false},
            {'c': sumido, 'n': 'foi-embora.png', 'v': false},
          ]),
        }),
      );
      await tester.pumpAndSettle();
      await tester.tap(find.byKey(const ValueKey('galeria-recentes')));
      await tester.pumpAndSettle();
      // O que nao esta mais no disco ja e peneirado na leitura das prefs.
      expect(find.byKey(ValueKey('recente-${vivo.path}')), findsOneWidget);
      expect(find.byKey(ValueKey('recente-$sumido')), findsNothing);
      expect(tester.takeException(), isNull);
    });
  });
}
