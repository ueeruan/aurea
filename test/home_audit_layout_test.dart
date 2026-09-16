import 'package:aurea/src/features/projects/presentation/release_notice.dart';
import 'package:aurea/src/features/projects/presentation/boas_vindas.dart';
import 'package:aurea/src/core/theme/app_theme.dart';
import 'package:aurea/src/features/projects/presentation/home_shell.dart';
import 'package:aurea/src/core/storage/prefs.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  setUpAll(() async {
    for (final family in ['Aurea Motion Sans', 'Roboto']) {
      await (FontLoader(family)..addFont(
            rootBundle.load('assets/templates/dnyx/AureaMotionSans.ttf'),
          ))
          .load();
    }
  });
  for (final width in [320.0, 375.0, 411.0, 768.0]) {
    testWidgets('home primary labels fit at width $width', (tester) async {
      tester.view.physicalSize = Size(width, 844);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      SharedPreferences.setMockInitialValues({
        releaseNoticeSeenKey: releaseNoticeRevision,
      // A primeira abertura ja passou: estes testes olham o resto.
      chaveDoAceite: 'ja',
      });
      final prefs = await SharedPreferences.getInstance();
      final theme = AppTheme.dark;
      // The native default font is not available in flutter_tester. Resolve
      // that fallback explicitly, keeping the app's sizes and button padding.
      final button = theme.filledButtonTheme.style!;
      final resolved = button.textStyle!
          .resolve({})!
          .copyWith(fontFamily: 'Roboto');
      await tester.pumpWidget(
        ProviderScope(
          overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
          child: MaterialApp(
            theme: theme.copyWith(
              filledButtonTheme: FilledButtonThemeData(
                style: button.copyWith(
                  textStyle: WidgetStatePropertyAll(resolved),
                ),
              ),
            ),
            home: const HomeShell(),
          ),
        ),
      );
      // Home also starts asynchronous feeds; layout does not wait for network.
      await tester.pump(const Duration(milliseconds: 300));
      for (final label in ['Novo projeto']) {
        expect(find.text(label).hitTestable(), findsOneWidget);
        expect(
          tester.getSize(find.text(label)).height,
          lessThan(26),
          reason: '$label should fit on one line',
        );
      }
      expect(tester.takeException(), isNull);
    });
  }
}
