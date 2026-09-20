import 'dart:convert';

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/core/theme/app_theme.dart';
import 'package:aurea/src/features/community/application/conta_da_comunidade.dart';
import 'package:aurea/src/features/community/application/social_service.dart';
import 'package:aurea/src/features/community/presentation/social_pages.dart';
import 'package:aurea/src/features/community/presentation/social_widgets.dart';
import 'package:aurea/src/features/projects/presentation/home_shell.dart';
import 'package:aurea/src/features/projects/presentation/release_notice.dart';
import 'package:aurea/src/features/projects/application/projects_controller.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter/material.dart';
import 'package:flutter_localizations/flutter_localizations.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'apoio/print_da_ui.dart';

final _code = 'ab' * 24;

class _Projects extends ProjectsController {
  @override
  List<VideoProject> build() => [
    VideoProject.empty('Meu primeiro motion'),
    VideoProject.empty('Projeto vertical'),
  ];
}

final _official = <String, dynamic>{
  'id': 'official',
  'apelido': 'aurea',
  'nome': 'Aurea',
  'bio': 'Novidades, tutoriais e comunidade.',
  'verificado': true,
  'oficial': true,
  'seguidores': 123,
  'seguindo': 2,
  'euSigo': false,
  'bloqueado': false,
};

class FakeSocial extends SocialService {
  FakeSocial() : super('https://test.local');
  final calls = <String>[];
  bool following = false;
  String? sent;
  @override
  Future<Map<String, dynamic>> request(
    String path,
    String code, {
    String method = 'GET',
    Map<String, dynamic>? data,
  }) async {
    calls.add('$method $path');
    if (path.endsWith('/follow')) {
      following = method == 'POST';
      return {'ok': true};
    }
    if (path.startsWith('/social/profiles/')) {
      return {
        ..._official,
        'euSigo': following,
        'seguidores': following ? 124 : 123,
      };
    }
    if (path == '/social/me' && method == 'PATCH') {
      return {'id': 'mine', ...data!};
    }
    if (path.startsWith('/social/feed')) return {'posts': []};
    if (path == '/social/chats') {
      return {
        'conversas': [
          {
            'perfil': _official,
            'ultima': 'Bem-vindo!',
            'quando': '2026-09-19T12:00:00Z',
            'naoLidas': 1,
          },
        ],
      };
    }
    if (path.contains('/read')) return {'ok': true};
    if (path.startsWith('/social/chats/')) {
      if (method == 'POST') {
        sent = data!['texto'] as String;
        return {'ok': true};
      }
      return {
        'mensagens': [
          {
            'id': 1,
            'sender': 'official',
            'recipient': 'mine',
            'text': 'Bem-vindo!',
            'created_at': '2026-09-19T12:00:00Z',
          },
          if (sent != null)
            {
              'id': 2,
              'sender': 'mine',
              'recipient': 'official',
              'text': sent,
              'created_at': '2026-09-19T12:01:00Z',
            },
        ],
      };
    }
    throw StateError('Unexpected $method $path');
  }
}

Future<void> mount(
  WidgetTester tester,
  Widget child,
  FakeSocial social, {
  double width = 375,
  GlobalKey? capture,
}) async {
  tester.view.physicalSize = Size(width, 812);
  tester.view.devicePixelRatio = 1;
  addTearDown(tester.view.reset);
  SharedPreferences.setMockInitialValues({
    'abertura.aceite': 'yes',
    releaseNoticeSeenKey: releaseNoticeRevision,
    'comunidade.conta': jsonEncode(
      ContaDaComunidade(
        id: 'mine',
        apelido: 'editor',
        codigo: _code,
        criadaEm: DateTime(2026),
      ).toJson(),
    ),
  });
  final prefs = await SharedPreferences.getInstance();
  final theme = AppTheme.dark;
  await tester.pumpWidget(
    ProviderScope(
      overrides: [
        sharedPreferencesProvider.overrideWithValue(prefs),
        socialServiceProvider.overrideWithValue(social),
        projectsControllerProvider.overrideWith(_Projects.new),
      ],
      child: MaterialApp(
        locale: const Locale('pt', 'BR'),
        supportedLocales: const [Locale('pt', 'BR')],
        localizationsDelegates: GlobalMaterialLocalizations.delegates,
        theme: theme.copyWith(
          textTheme: theme.textTheme.apply(fontFamily: 'Roboto'),
          appBarTheme: theme.appBarTheme.copyWith(
            titleTextStyle: theme.appBarTheme.titleTextStyle?.copyWith(
              fontFamily: 'Roboto',
            ),
          ),
          filledButtonTheme: FilledButtonThemeData(
            style: theme.filledButtonTheme.style?.copyWith(
              textStyle: WidgetStatePropertyAll(
                theme.filledButtonTheme.style?.textStyle
                    ?.resolve({})
                    ?.copyWith(fontFamily: 'Roboto'),
              ),
            ),
          ),
        ),
        home: RepaintBoundary(key: capture, child: child),
      ),
    ),
  );
  await tester.pump();
  await tester.pump(const Duration(milliseconds: 350));
}

void main() {
  testWidgets('home central creation button opens the real project sheet', (
    tester,
  ) async {
    final key = GlobalKey();
    if (pastaDePrint != null) await carregarFontesReais();
    await mount(tester, const HomeShell(), FakeSocial(), capture: key);
    expect(tester.takeException(), isNull);
    await gravarPrint(tester, key, 'home-social');
    await tester.tap(find.byKey(const ValueKey('home-criar')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 400));
    expect(find.byKey(const ValueKey('criar-projeto')), findsOneWidget);
    expect(tester.takeException(), isNull);
    await tester.pumpWidget(const SizedBox());
  });
  test('old local nickname without a real credential must register', () {
    expect(
      ContaDaComunidade.deJson(
        jsonEncode({'id': 'old', 'apelido': 'Nick', 'codigo': ''}),
      ),
      isNull,
    );
  });
  for (final width in [320.0, 375.0]) {
    testWidgets(
      'public verified profile fits $width and keeps credentials private',
      (tester) async {
        final api = FakeSocial();
        final key = GlobalKey();
        if (pastaDePrint != null) await carregarFontesReais();
        await mount(
          tester,
          const SocialProfilePage(userId: 'official'),
          api,
          width: width,
          capture: key,
        );
        expect(find.byType(VerifiedBadge), findsOneWidget);
        expect(find.text(_code), findsNothing);
        expect(find.text('123'), findsOneWidget);
        await tester.tap(find.text('Seguir'));
        await tester.pump();
        await tester.pump(const Duration(milliseconds: 100));
        expect(find.text('Seguindo'), findsWidgets);
        expect(find.text('124'), findsOneWidget);
        expect(api.calls, contains('POST /social/profiles/official/follow'));
        expect(tester.takeException(), isNull);
        await gravarPrint(tester, key, 'perfil-${width.toInt()}');
        await tester.pumpWidget(const SizedBox());
      },
    );
  }
  testWidgets(
    'chat sends authenticated messages and removes polling on disposal',
    (tester) async {
      final api = FakeSocial();
      final key = GlobalKey();
      if (pastaDePrint != null) await carregarFontesReais();
      await mount(tester, SocialChatPage(peer: _official), api, capture: key);
      expect(find.text('Bem-vindo!'), findsOneWidget);
      await tester.enterText(find.byType(TextField), 'Meu primeiro motion!');
      await tester.tap(find.byTooltip('Enviar'));
      await tester.pump();
      await tester.pump(const Duration(milliseconds: 100));
      expect(api.sent, 'Meu primeiro motion!');
      expect(find.text('Meu primeiro motion!'), findsOneWidget);
      expect(tester.takeException(), isNull);
      await gravarPrint(tester, key, 'chat');
      await tester.pumpWidget(const SizedBox());
      final count = api.calls.length;
      await tester.pump(const Duration(seconds: 5));
      expect(api.calls.length, count);
    },
  );
  testWidgets('profile editor saves display name and biography to server', (
    tester,
  ) async {
    final api = FakeSocial();
    await mount(
      tester,
      EditSocialProfile(
        profile: {
          ..._official,
          'id': 'mine',
          'apelido': 'editor',
          'nome': 'Editor',
        },
      ),
      api,
    );
    final fields = find.byType(TextField);
    expect(fields, findsNWidgets(3));
    await tester.enterText(fields.at(0), 'Meu nome');
    await tester.enterText(fields.at(2), 'Minha biografia');
    await tester.ensureVisible(find.text('Salvar perfil'));
    await tester.tap(find.text('Salvar perfil'));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 350));
    expect(api.calls, contains('PATCH /social/me'));
    expect(tester.takeException(), isNull);
  });
}
