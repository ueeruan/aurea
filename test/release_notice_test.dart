import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/core/theme/app_theme.dart';
import 'package:aurea/src/features/projects/presentation/home_shell.dart';
import 'package:aurea/src/features/projects/presentation/release_notice.dart';
import 'package:aurea/src/features/projects/presentation/boas_vindas.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  testWidgets(
    'release summary is shown once, and can be opened again manually',
    (tester) async {
      SharedPreferences.setMockInitialValues({
        releaseNoticeSeenKey: 'older-release',
      // A primeira abertura ja passou: estes testes olham o resto.
      chaveDoAceite: 'ja',
      });
      final prefs = await SharedPreferences.getInstance();
      Widget app() => ProviderScope(
        overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
        child: MaterialApp(theme: AppTheme.dark, home: const HomeShell()),
      );
      await tester.pumpWidget(app());
      await tester.pump(const Duration(milliseconds: 350));
      expect(find.byKey(const Key('release-notice')), findsOneWidget);
      expect(prefs.getString(releaseNoticeSeenKey), 'older-release');
      await tester.tap(find.byKey(const Key('release-notice-dismiss')));
      await tester.pump(const Duration(milliseconds: 350));
      expect(prefs.getString(releaseNoticeSeenKey), releaseNoticeRevision);
      expect(find.text('Novo projeto').hitTestable(), findsOneWidget);
      await tester.pumpWidget(const SizedBox.shrink());
      await tester.pumpWidget(app());
      await tester.pump(const Duration(milliseconds: 350));
      expect(find.byKey(const Key('release-notice')), findsNothing);
      final future = showReleaseNotice(tester.element(find.byType(HomeShell)));
      await tester.pump(const Duration(milliseconds: 350));
      expect(find.byKey(const Key('release-notice')), findsOneWidget);
      await tester.tap(find.byKey(const Key('release-notice-dismiss')));
      await tester.pump(const Duration(milliseconds: 350));
      await future;
      expect(tester.takeException(), isNull);
    },
  );

  testWidgets(
    'summary scrolls on small screens with large text and keeps close reachable',
    (tester) async {
      tester.view.physicalSize = const Size(320, 568);
      tester.view.devicePixelRatio = 1;
      addTearDown(tester.view.reset);
      await tester.pumpWidget(
        MaterialApp(
          theme: AppTheme.dark,
          builder: (context, child) => MediaQuery(
            data: MediaQuery.of(context)
                .copyWith(textScaler: const TextScaler.linear(1.6)),
            child: child!,
          ),
          home: Builder(
            builder: (context) => Scaffold(
              body: TextButton(
                onPressed: () => showReleaseNotice(context),
                child: const Text('Abrir'),
              ),
            ),
          ),
        ),
      );
      await tester.tap(find.text('Abrir'));
      await tester.pumpAndSettle();
      expect(
        find.byKey(const Key('release-notice-dismiss')).hitTestable(),
        findsOneWidget,
      );
      expect(tester.takeException(), isNull);
      await tester.tap(find.byKey(const Key('release-notice-dismiss')));
      await tester.pumpAndSettle();
      expect(find.byKey(const Key('release-notice')), findsNothing);
    },
  );
}
