import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/application/ui/effect_recents.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

Future<ProviderContainer> _comPrefs([
  Map<String, Object> inicial = const {},
]) async {
  SharedPreferences.setMockInitialValues(inicial);
  final prefs = await SharedPreferences.getInstance();
  return ProviderContainer(
    overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
  );
}

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test('o mais usado agora vai para a frente, sem repetir', () async {
    final c = await _comPrefs();
    addTearDown(c.dispose);
    final lista = c.read(effectRecentsProvider.notifier);
    lista
      ..registrar(EffectType.glitch)
      ..registrar(EffectType.tint)
      ..registrar(EffectType.glitch);
    expect(lista.tipos, [EffectType.glitch, EffectType.tint]);
  });

  test('a lista tem teto: o mais velho sai', () async {
    final c = await _comPrefs();
    addTearDown(c.dispose);
    final lista = c.read(effectRecentsProvider.notifier);
    final tipos = EffectType.values.take(EffectRecentsNotifier.maximo + 3);
    for (final t in tipos) {
      lista.registrar(t);
    }
    expect(lista.tipos.length, EffectRecentsNotifier.maximo);
    expect(lista.tipos.first, tipos.last);
    expect(lista.tipos.contains(tipos.first), isFalse);
  });

  test('o que foi usado sobrevive a fechar o aplicativo', () async {
    final c = await _comPrefs();
    addTearDown(c.dispose);
    c.read(effectRecentsProvider.notifier).registrar(EffectType.vhs);
    final gravado = c
        .read(sharedPreferencesProvider)
        .getStringList(EffectRecentsNotifier.kChave);
    expect(gravado, ['vhs']);

    final outro = await _comPrefs({
      EffectRecentsNotifier.kChave: ['vhs'],
    });
    addTearDown(outro.dispose);
    expect(outro.read(effectRecentsProvider.notifier).tipos, [EffectType.vhs]);
  });

  test('efeito que saiu do catálogo não volta como recente', () async {
    final c = await _comPrefs({
      EffectRecentsNotifier.kChave: ['nao_existe_mais', 'vhs'],
    });
    addTearDown(c.dispose);
    expect(c.read(effectRecentsProvider.notifier).tipos, [EffectType.vhs]);
  });

  test('sem prefs, a lista existe vazia em vez de estourar', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    expect(c.read(effectRecentsProvider), isEmpty);
    c.read(effectRecentsProvider.notifier).registrar(EffectType.glitch);
    expect(c.read(effectRecentsProvider.notifier).tipos, [EffectType.glitch]);
  });

  group('sugeridos', () {
    test('mídia e forma recebem listas diferentes', () {
      final midia = efeitosRecomendados(ehMidia: true);
      final forma = efeitosRecomendados(ehMidia: false);
      expect(midia, isNot(equals(forma)));
      expect(midia, contains(EffectType.curves));
      expect(forma, contains(EffectType.repetirEmCirculo));
      // O que resolve para os dois entra nos dois.
      expect(midia, contains(EffectType.aparecerSumir));
      expect(forma, contains(EffectType.aparecerSumir));
    });

    test('o que a pessoa acabou de usar vem primeiro, uma vez só', () {
      final lista = efeitosRecomendados(
        ehMidia: true,
        recentes: [EffectType.vhs, EffectType.curves],
      );
      expect(lista.first, EffectType.vhs);
      expect(lista.where((t) => t == EffectType.curves).length, 1);
      expect(lista.toSet().length, lista.length);
    });

    test('a lista tem tamanho pedido e só efeitos que existem', () {
      final lista = efeitosRecomendados(ehMidia: false, quantos: 5);
      expect(lista.length, 5);
      for (final t in lista) {
        expect(effectSpecs.containsKey(t), isTrue, reason: t.name);
      }
    });
  });
}
