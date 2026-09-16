import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/settings/presentation/estresse3d_screen.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

/// A TELA DE ESTRESSE ABRE, E OS EFEITOS DAS CENAS EXISTEM.
///
/// As cenas com efeito usavam Light Glow, Glow Volumetrico e Film Grain,
/// que sairam do catalogo: o EffectInstance lancava no meio da cena e o
/// teste parava sem relatorio. E a tela ganhou a bancada A-E do nucleo.
void main() {
  testWidgets('a tela abre com a bancada A-E e os nove testes', (tester) async {
    SharedPreferences.setMockInitialValues({});
    // A tela e de celular em pe (o palco 16:9 ocupa a largura).
    tester.view.physicalSize = const Size(390, 844);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    await tester.pumpWidget(
      const ProviderScope(child: MaterialApp(home: Estresse3DScreen())),
    );
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 50));
    expect(find.text('Bancada A-E'), findsOneWidget);
    expect(find.text('Rodar os nove'), findsOneWidget);
  });

  test('os efeitos que as cenas de estresse usam estao no catalogo', () {
    for (final t in [
      EffectType.unsharpMask,
      EffectType.vignette,
      EffectType.vhsDamage,
    ]) {
      expect(effectSpecs[t], isNotNull, reason: '$t');
      EffectInstance(type: t);
    }
  });
}
