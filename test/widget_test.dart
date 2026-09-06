import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'package:aurea/src/app.dart';
import 'package:aurea/src/core/storage/prefs.dart';

void main() {
  testWidgets('home shell renders brand, tabs and new project action',
      (tester) async {
    SharedPreferences.setMockInitialValues({});
    final prefs = await SharedPreferences.getInstance();

    await tester.pumpWidget(
      ProviderScope(
        overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
        child: const AureaApp(),
      ),
    );

    expect(find.text('Aurea'), findsOneWidget);
    expect(find.text('Novo projeto'), findsOneWidget);
    expect(find.text('Ajustes'), findsOneWidget);
    expect(find.text('Usuario'), findsOneWidget);
    expect(find.text('Sobre'), findsOneWidget);
  });
}
