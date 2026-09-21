// PRIMEIRO USO DO "+" COM A GALERIA NEGADA: a unica saida para o seletor
// do sistema eram dois icones sem rotulo no canto. No lugar da grade vem
// um bloco claro: "Permitir acesso" e "Escolher arquivos", com nome.
import 'dart:io';
import 'dart:typed_data';

import 'package:aurea/src/core/ds/ds.dart';
import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/presentation/ui/toolbar/galeria.dart';
import 'package:aurea/src/features/media/application/gallery_service.dart';
import 'package:aurea/src/features/media/application/media_import_service.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:image_picker/image_picker.dart';
import 'package:shared_preferences/shared_preferences.dart';

class _GaleriaNegada extends GalleryService {
  _GaleriaNegada(this.acesso);

  GalleryAccess acesso;
  int pedidos = 0, ajustes = 0, maisFotos = 0;

  @override
  Future<GalleryAccess> requestAccess() async {
    pedidos++;
    return acesso;
  }

  @override
  Future<List<GalleryAlbum>> albums() async => const [
    GalleryAlbum('all', 'Todos'),
  ];

  @override
  Future<List<GalleryAsset>> page(String album, int page, int size) async =>
      const [];

  @override
  Future<Uint8List?> thumbnail(GalleryAsset asset) async => null;

  @override
  Future<File?> file(GalleryAsset asset) async => null;

  @override
  Future<void> settings() async => ajustes++;

  @override
  Future<void> selectMore() async => maisFotos++;
}

class _SeletorDeArquivos extends MediaImportService {
  int escolhas = 0;

  @override
  Future<XFile?> pickMediaFile() async {
    escolhas++;
    return XFile('/app/midia/clipe.mp4', name: 'clipe.mp4');
  }
}

Future<(_GaleriaNegada, _SeletorDeArquivos, List<(String, bool)>)> _montar(
  WidgetTester tester,
  GalleryAccess acesso,
) async {
  SharedPreferences.setMockInitialValues(const {});
  final prefs = await SharedPreferences.getInstance();
  final galeria = _GaleriaNegada(acesso);
  final seletor = _SeletorDeArquivos();
  final entraram = <(String, bool)>[];
  await tester.pumpWidget(
    ProviderScope(
      overrides: [
        sharedPreferencesProvider.overrideWithValue(prefs),
        galleryServiceProvider.overrideWith((ref) => galeria),
        mediaImportServiceProvider.overrideWithValue(seletor),
      ],
      child: MaterialApp(
        home: Scaffold(
          body: Align(
            alignment: Alignment.bottomCenter,
            // A area da galeria na folha de 251 (menos as abas e o trilho).
            child: SizedBox(
              width: 411 - AureaDims.abasDaFolhaDeAdicionar,
              height: 183,
              child: GalleryPanel(
                onImport: (arquivo, video, _) async =>
                    entraram.add((arquivo.path.split('/').last, video)),
              ),
            ),
          ),
        ),
      ),
    ),
  );
  await tester.pumpAndSettle();
  return (galeria, seletor, entraram);
}

void main() {
  testWidgets('negado: bloco com "Permitir acesso" e "Escolher arquivos" no '
      'lugar da grade; escolher arquivo importa o video', (tester) async {
    final (galeria, seletor, entraram) = await _montar(
      tester,
      GalleryAccess.denied,
    );
    expect(find.byKey(const ValueKey('galeria-sem-acesso')), findsOneWidget);
    expect(find.byKey(const ValueKey('gallery-grid')), findsNothing);
    // Botoes do DS, com o rotulo escrito.
    for (final (chave, rotulo) in [
      ('galeria-permitir-acesso', 'Permitir acesso'),
      ('galeria-escolher-arquivos', 'Escolher arquivos'),
    ]) {
      final botao = find.byKey(ValueKey(chave));
      expect(tester.widget<AureaToolbarButton>(botao).rotulo, rotulo);
      expect(
        find.descendant(of: botao, matching: find.text(rotulo)),
        findsOneWidget,
      );
      // Inteiro dentro da area (nada escondido, nada a rolar).
      final r = tester.getRect(botao);
      final area = tester.getRect(find.byType(GalleryPanel));
      expect(r.left, greaterThanOrEqualTo(area.left));
      expect(r.top, greaterThanOrEqualTo(area.top));
      expect(r.right, lessThanOrEqualTo(area.right));
      expect(r.bottom, lessThanOrEqualTo(area.bottom));
    }

    await tester.tap(find.byKey(const ValueKey('galeria-escolher-arquivos')));
    await tester.pumpAndSettle();
    expect(seletor.escolhas, 1);
    // O seletor devolveu um .mp4: entra como VIDEO.
    expect(entraram, [('clipe.mp4', true)]);

    // PERMITIR: pede de novo; negado de vez, abre os ajustes do app.
    final pedidosAntes = galeria.pedidos;
    await tester.tap(find.byKey(const ValueKey('galeria-permitir-acesso')));
    await tester.pumpAndSettle();
    expect(galeria.pedidos, greaterThan(pedidosAntes));
    expect(galeria.ajustes, 1);
    // Liberado nos ajustes: a volta ao app troca o bloco pela grade.
    galeria.acesso = GalleryAccess.full;
    tester.binding.handleAppLifecycleStateChanged(AppLifecycleState.resumed);
    await tester.pumpAndSettle();
    expect(find.byKey(const ValueKey('galeria-sem-acesso')), findsNothing);
    expect(tester.takeException(), isNull);
  });

  testWidgets('limitado e sem nada liberado: o bloco oferece liberar fotos', (
    tester,
  ) async {
    final (galeria, _, _) = await _montar(tester, GalleryAccess.limited);
    expect(find.byKey(const ValueKey('galeria-sem-acesso')), findsOneWidget);
    await tester.tap(find.byKey(const ValueKey('galeria-permitir-acesso')));
    await tester.pumpAndSettle();
    expect(galeria.maisFotos, 1);
    expect(galeria.ajustes, 0);
  });
}
