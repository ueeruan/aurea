// O MODO DESENVOLVEDOR ESCONDIDO (v1.1.1): sete toques na versao, na
// aba Sobre, ligam as ferramentas — e mais sete desligam.
import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/core/theme/app_theme.dart';
import 'package:aurea/src/features/about/presentation/about_tab.dart';
import 'package:aurea/src/features/projects/presentation/release_notice.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  Future<SharedPreferences> _prefs(Map<String, Object> inicial) async {
    SharedPreferences.setMockInitialValues(inicial);
    return SharedPreferences.getInstance();
  }

  Widget _app(SharedPreferences prefs) => ProviderScope(
    overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
    child: MaterialApp(
      theme: AppTheme.dark,
      home: const Scaffold(body: AboutTab()),
    ),
  );

  testWidgets('sete toques na versão ligam; menos que isso, nada', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(400, 1200);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final prefs = await _prefs({});
    await tester.pumpWidget(_app(prefs));
    await tester.pump();

    for (var i = 0; i < 6; i++) {
      await tester.tap(find.byKey(const ValueKey('sobre-versao')));
    }
    await tester.pump();
    expect(find.byKey(const ValueKey('dev-build')), findsNothing);
    expect(prefs.getBool(chaveDoModoDev), isNull);

    await tester.tap(find.byKey(const ValueKey('sobre-versao')));
    await tester.pump();
    expect(prefs.getBool(chaveDoModoDev), isTrue);
    expect(find.byKey(const ValueKey('dev-build')), findsOneWidget);
    expect(find.byKey(const ValueKey('dev-limpar-avisos')), findsOneWidget);
    // A snack do aviso tem relogio proprio: deixa ela morrer antes de
    // desmontar a arvore.
    await tester.pump(const Duration(seconds: 6));
    expect(tester.takeException(), isNull);
  });

  testWidgets('mais sete desligam, e "rever avisos" limpa a novidade', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(400, 1200);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final prefs = await _prefs({
      chaveDoModoDev: true,
      releaseNoticeSeenKey: 'algum',
      'abertura.aceite': 'ja',
    });
    await tester.pumpWidget(_app(prefs));
    await tester.pump();
    expect(find.byKey(const ValueKey('dev-build')), findsOneWidget);

    await tester.tap(find.byKey(const ValueKey('dev-limpar-avisos')));
    await tester.pump();
    expect(prefs.getString(releaseNoticeSeenKey), isNull);
    expect(
      prefs.getString('abertura.aceite'),
      'ja',
      reason: 'o aceite da primeira abertura nao e um aviso',
    );

    for (var i = 0; i < 7; i++) {
      await tester.tap(find.byKey(const ValueKey('sobre-versao')));
    }
    await tester.pump();
    expect(prefs.getBool(chaveDoModoDev), isFalse);
    expect(find.byKey(const ValueKey('dev-build')), findsNothing);
    // A snack do aviso tem relogio proprio: deixa ela morrer antes de
    // desmontar a arvore.
    await tester.pump(const Duration(seconds: 6));
    expect(tester.takeException(), isNull);
  });
}
