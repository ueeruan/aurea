import 'dart:io';

import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/presentation/widgets/add_layer_sheet.dart';
import 'package:aurea/src/features/media/application/sons_recentes.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();
  late Directory temp;

  setUp(() {
    temp = Directory.systemTemp.createTempSync('sons');
  });

  tearDown(() {
    try {
      temp.deleteSync(recursive: true);
    } catch (_) {}
  });

  Future<ProviderContainer> _com([
    Map<String, Object> inicial = const {},
  ]) async {
    SharedPreferences.setMockInitialValues(inicial);
    final prefs = await SharedPreferences.getInstance();
    return ProviderContainer(
      overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
    );
  }

  String _arquivo(String nome) {
    final f = File('${temp.path}/$nome')..writeAsBytesSync(const [1, 2, 3]);
    return f.path;
  }

  test('o último som usado vai para a frente, sem repetir', () async {
    final c = await _com();
    addTearDown(c.dispose);
    final lista = c.read(sonsRecentesProvider.notifier);
    final a = _arquivo('a.mp3');
    final b = _arquivo('b.mp3');
    lista
      ..registrar(a, 'Trilha A')
      ..registrar(b, 'Trilha B')
      ..registrar(a, 'Trilha A');
    expect(c.read(sonsRecentesProvider).map((s) => s.nome), [
      'Trilha A',
      'Trilha B',
    ]);
  });

  test('sobrevive a reabrir, e o que sumiu do disco sai da lista', () async {
    final vivo = _arquivo('vivo.mp3');
    final c = await _com();
    addTearDown(c.dispose);
    c.read(sonsRecentesProvider.notifier)
      ..registrar(_arquivo('morto.mp3'), 'Morto')
      ..registrar(vivo, 'Vivo');
    final gravado = c
        .read(sharedPreferencesProvider)
        .getString(SonsRecentesNotifier.kChave)!;
    File('${temp.path}/morto.mp3').deleteSync();

    final outro = await _com({SonsRecentesNotifier.kChave: gravado});
    addTearDown(outro.dispose);
    expect(outro.read(sonsRecentesProvider).map((s) => s.nome), ['Vivo']);
  });

  test('a lista tem teto de dez', () async {
    final c = await _com();
    addTearDown(c.dispose);
    final lista = c.read(sonsRecentesProvider.notifier);
    for (var i = 0; i < 13; i++) {
      lista.registrar(_arquivo('s$i.mp3'), 'Som $i');
    }
    expect(c.read(sonsRecentesProvider), hasLength(10));
    expect(c.read(sonsRecentesProvider).first.nome, 'Som 12');
  });

  test('tirar remove só aquele', () async {
    final c = await _com();
    addTearDown(c.dispose);
    final a = _arquivo('a.mp3');
    final b = _arquivo('b.mp3');
    c.read(sonsRecentesProvider.notifier)
      ..registrar(a, 'A')
      ..registrar(b, 'B')
      ..tirar(a);
    expect(c.read(sonsRecentesProvider).single.nome, 'B');
  });

  test('lixo nas prefs vira lista vazia', () async {
    final c = await _com({SonsRecentesNotifier.kChave: '{nada'});
    addTearDown(c.dispose);
    expect(c.read(sonsRecentesProvider), isEmpty);
  });

  testWidgets('a aba de som com recentes cabe na folha sem estourar', (
    tester,
  ) async {
    // REGRESSAO (16/09): a lista de recentes usa Expanded, e a aba de
    // som vivia dentro do SingleChildScrollView da folha — altura sem
    // fim + Expanded = folha quebrada no aparelho. A aba de som agora
    // tem altura propria, como a de midia.
    final c = await _com();
    addTearDown(c.dispose);
    final beat = _arquivo('beat.mp3');
    c.read(sonsRecentesProvider.notifier).registrar(beat, 'Beat da intro');
    await tester.pumpWidget(
      UncontrolledProviderScope(
        container: c,
        child: MaterialApp(
          theme: ThemeData(brightness: Brightness.dark),
          home: Scaffold(
            body: SizedBox(
              height: 270,
              child: AddLayerPanel(onClose: () {}, playhead: Duration.zero),
            ),
          ),
        ),
      ),
    );
    await tester.pump();
    await tester.tap(find.byKey(const ValueKey('add-tab-audio')));
    await tester.pump();
    expect(tester.takeException(), isNull);
    expect(find.byKey(ValueKey('som-recente-$beat')), findsOneWidget);
  });
}
