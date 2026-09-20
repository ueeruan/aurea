import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/community/application/comunidade_service.dart';
import 'package:aurea/src/features/community/application/conta_da_comunidade.dart';
import 'package:aurea/src/features/community/presentation/cadastro_obrigatorio.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

class _Servidor extends ComunidadeService {
  @override
  Future<RespostaDaConta> criarConta(String apelido) async =>
      RespostaDaConta(id: 'nova-conta', apelido: apelido, codigo: 'b' * 48);
}

void main() {
  testWidgets('o app so abre depois de criar a conta', (tester) async {
    SharedPreferences.setMockInitialValues({});
    final prefs = await SharedPreferences.getInstance();

    await tester.pumpWidget(
      ProviderScope(
        overrides: [
          sharedPreferencesProvider.overrideWithValue(prefs),
          comunidadeServiceProvider.overrideWithValue(_Servidor()),
        ],
        child: const MaterialApp(
          home: CadastroObrigatorioGate(
            child: Scaffold(body: Text('Aplicativo aberto')),
          ),
        ),
      ),
    );

    expect(find.byKey(const ValueKey('cadastro-obrigatorio-titulo')), findsOne);
    expect(find.text('Aplicativo aberto'), findsNothing);

    await tester.enterText(
      find.byKey(const ValueKey('cadastro-nickname')),
      'Ana Motion',
    );
    await tester.tap(find.byKey(const ValueKey('cadastro-criar-conta')));
    await tester.pumpAndSettle();

    expect(find.text('Aplicativo aberto'), findsOneWidget);
    expect(find.text('Crie sua conta'), findsNothing);
  });
}
