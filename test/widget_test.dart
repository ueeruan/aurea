import 'package:aurea/src/features/projects/presentation/release_notice.dart';
import 'package:aurea/src/features/projects/presentation/boas_vindas.dart';

import 'dart:convert';

import 'package:aurea/src/features/community/application/conta_da_comunidade.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:aurea/src/app.dart';
import 'package:aurea/src/core/storage/prefs.dart';

void main() {
  testWidgets('home shell renders brand, tabs and new project action', (
    tester,
  ) async {
    SharedPreferences.setMockInitialValues({
      releaseNoticeSeenKey: releaseNoticeRevision,
      // A primeira abertura ja passou: estes testes olham o resto.
      chaveDoAceite: 'ja',
      'comunidade.conta': jsonEncode(
        ContaDaComunidade(
          id: 'teste',
          apelido: 'Tester',
          codigo: 'a' * 48,
          criadaEm: DateTime(2026),
        ).toJson(),
      ),
    });
    final prefs = await SharedPreferences.getInstance();

    await tester.pumpWidget(
      ProviderScope(
        overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
        child: const AureaApp(),
      ),
    );

    expect(find.text('AUREA'), findsOneWidget);
    expect(find.text('Novo projeto'), findsOneWidget);
    expect(find.text('Ajustes'), findsOneWidget);
    expect(find.text('Perfil'), findsOneWidget);
    // A aba Sobre nao esta na barra: a porta dela e o menu da Inicio.
    await tester.tap(find.byKey(const ValueKey('home-menu')));
    await tester.pump();
    await tester.pump(const Duration(milliseconds: 300));
    expect(find.text('Sobre'), findsOneWidget);
  });
}
