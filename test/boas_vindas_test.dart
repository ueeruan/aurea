// A PRIMEIRA ABERTURA (v1.1.1): boas-vindas com o combinado, uma vez
// so, antes de qualquer novidade de versao.
import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/core/theme/app_theme.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/projects/presentation/boas_vindas.dart';
import 'package:aurea/src/features/projects/presentation/home_shell.dart';
import 'package:aurea/src/features/projects/presentation/release_notice.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

class _SemProjetos extends ProjectsController {
  @override
  List<VideoProject> build() => const [];
}

void main() {
  Future<SharedPreferences> _prefs(Map<String, Object> inicial) async {
    SharedPreferences.setMockInitialValues(inicial);
    return SharedPreferences.getInstance();
  }

  Widget _app(SharedPreferences prefs) => ProviderScope(
    overrides: [
      sharedPreferencesProvider.overrideWithValue(prefs),
      projectsControllerProvider.overrideWith(_SemProjetos.new),
    ],
    child: MaterialApp(theme: AppTheme.dark, home: const HomeShell()),
  );

  testWidgets('na primeira vez, as boas-vindas aparecem e ficam gravadas', (
    tester,
  ) async {
    tester.view.physicalSize = const Size(400, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final prefs = await _prefs({});
    await tester.pumpWidget(_app(prefs));
    await tester.pump(const Duration(milliseconds: 400));
    expect(find.byKey(const ValueKey('abertura-comecar')), findsOneWidget);
    // As novidades da versao NAO aparecem por cima das boas-vindas.
    expect(find.byKey(const Key('release-notice')), findsNothing);

    await tester.ensureVisible(find.byKey(const ValueKey('abertura-comecar')));
    await tester.pump(const Duration(milliseconds: 200));
    await tester.tap(find.byKey(const ValueKey('abertura-comecar')));
    await tester.pump(const Duration(milliseconds: 400));
    expect(prefs.getString(chaveDoAceite), isNotNull);
    expect(
      prefs.getString(releaseNoticeSeenKey),
      releaseNoticeRevision,
      reason: 'quem acabou de chegar nao precisa das novidades da versao',
    );
    expect(tester.takeException(), isNull);
  });

  testWidgets('quem já aceitou nunca mais vê', (tester) async {
    tester.view.physicalSize = const Size(400, 900);
    tester.view.devicePixelRatio = 1;
    addTearDown(tester.view.reset);
    final prefs = await _prefs({
      chaveDoAceite: '2026-09-15',
      releaseNoticeSeenKey: releaseNoticeRevision,
    });
    await tester.pumpWidget(_app(prefs));
    await tester.pump(const Duration(milliseconds: 400));
    expect(find.byKey(const ValueKey('abertura-comecar')), findsNothing);
    expect(tester.takeException(), isNull);
  });
}
