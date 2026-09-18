// PRINT DA UI POR TESTE (sem emulador).
//
// Carrega fontes reais no flutter_tester (senao o texto vira retangulo) e
// grava o que estiver dentro de um RepaintBoundary em PNG. So grava quando
// AUREA_PRINT_DIR aponta uma pasta — os testes normais nao escrevem nada.
import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';

Future<void> carregarFontesReais() async {
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
  await (FontLoader('MaterialIcons')
        ..addFont(rootBundle.load('fonts/MaterialIcons-Regular.otf')))
      .load();
}

String? get pastaDePrint {
  final p = Platform.environment['AUREA_PRINT_DIR'];
  return (p == null || p.isEmpty) ? null : p;
}

Future<void> gravarPrint(WidgetTester tester, GlobalKey chave, String nome) async {
  final pasta = pastaDePrint;
  if (pasta == null) return;
  // A PASTA PODE NAO EXISTIR AINDA: apontar AUREA_PRINT_DIR para uma pasta
  // nova e o uso normal da ferramenta, e sem isto a primeira gravacao
  // morria com PathNotFoundException em vez de criar o destino.
  Directory(pasta).createSync(recursive: true);
  await tester.pump();
  await tester.runAsync(() async {
    final boundary = chave.currentContext!.findRenderObject()! as RenderRepaintBoundary;
    final image = await boundary.toImage(pixelRatio: 2);
    final bytes = (await image.toByteData(format: ui.ImageByteFormat.png))!.buffer.asUint8List();
    await File('$pasta/$nome.png').writeAsBytes(bytes);
    image.dispose();
  });
}
