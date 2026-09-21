import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';

/// Monta [filho] numa tela de [largura] x [altura], dentro de um
/// `MaterialApp` + `Scaffold` (as folhas do teclado numerico precisam de
/// Navigator e Material).
Future<void> montarDs(
  WidgetTester tester,
  Widget filho, {
  double largura = 390,
  double altura = 844,
  Key? chaveDoApp,
}) async {
  tester.view.physicalSize = Size(largura, altura);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.resetPhysicalSize);
  addTearDown(tester.view.resetDevicePixelRatio);
  await tester.pumpWidget(
    MaterialApp(
      key: chaveDoApp,
      home: Scaffold(
        body: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 22),
          child: Center(child: filho),
        ),
      ),
    ),
  );
}
