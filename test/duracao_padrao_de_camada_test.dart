import 'package:aurea/src/core/storage/prefs.dart';
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/settings/application/settings_controller.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:shared_preferences/shared_preferences.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  Future<ProviderContainer> _com(Map<String, Object> inicial) async {
    SharedPreferences.setMockInitialValues(inicial);
    final prefs = await SharedPreferences.getInstance();
    final c = ProviderContainer(
      overrides: [sharedPreferencesProvider.overrideWithValue(prefs)],
    );
    return c;
  }

  test('a camada nova dura o que os Ajustes mandarem', () async {
    final c = await _com({'settings.defaultLayerSeconds': 5});
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addTextLayer(Duration.zero);
    e.addShapeLayer(Duration.zero);
    e.addImageLayer(Duration.zero, '/tmp/a.png', 'a.png');
    for (final l in c.read(editorControllerProvider).layers) {
      expect(l.duration, const Duration(seconds: 5), reason: l.name);
    }
  });

  test('sem escolha (e sem prefs), continuam os 3 s de sempre', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addTextLayer(Duration.zero);
    expect(
      c.read(editorControllerProvider).layers.single.duration,
      const Duration(seconds: 3),
    );
  });

  test('a duração pedida na chamada continua mandando', () async {
    final c = await _com({'settings.defaultLayerSeconds': 5});
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);
    e.addImageLayer(
      Duration.zero,
      '/tmp/a.png',
      'a.png',
      duracao: const Duration(seconds: 2),
    );
    expect(
      c.read(editorControllerProvider).layers.single.duration,
      const Duration(seconds: 2),
    );
  });

  test('o controlador dos Ajustes prende o número na faixa', () async {
    final c = await _com({});
    addTearDown(c.dispose);
    final s = c.read(settingsControllerProvider.notifier);
    s.setDefaultLayerSeconds(99);
    expect(c.read(settingsControllerProvider).defaultLayerSeconds, 30);
    s.setDefaultLayerSeconds(0);
    expect(c.read(settingsControllerProvider).defaultLayerSeconds, 1);
  });
}
