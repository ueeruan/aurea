// RELATOS DE 13/09 (noite): "a camada Z ainda nao ta funcionando, nao ta
// dando pra mexer um por um" e "copiar efeitos igual a do AM".
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/video_project.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

({ProviderContainer c, EditorController e}) _motor() {
  final container = ProviderContainer();
  addTearDown(container.dispose);
  return (c: container, e: container.read(editorControllerProvider.notifier));
}

void main() {
  test('Z um por um: cada camada ganha profundidade propria e a pintura segue o Z', () {
    final m = _motor();
    m.e.addTextLayer(Duration.zero, text: 'A');
    m.e.addTextLayer(Duration.zero, text: 'B');
    final ls = m.c.read(editorControllerProvider).layers;
    final a = ls.firstWhere((l) => l is TextLayer && l.text == 'A').id;
    final b = ls.firstWhere((l) => l is TextLayer && l.text == 'B').id;
    // Camadas comuns (sem 3D ligado) aceitam Z direto e viram 3D.
    m.e.editPositionZ(a, Duration.zero, 900);
    m.e.editPositionZ(b, Duration.zero, -300);
    final p = m.c.read(editorControllerProvider);
    expect(p.layerById(a)!.is3D, isTrue);
    expect(p.layerById(a)!.positionZ.valueAt(Duration.zero), 900);
    expect(p.layerById(b)!.positionZ.valueAt(Duration.zero), -300);
    // Pintura: a mais longe (A, Z 900) vem antes da mais perto (B).
    final ordem = depthSortPaintOrder(p.layers.reversed.toList(), Duration.zero)
        .where((l) => l.id == a || l.id == b)
        .map((l) => l.id)
        .toList();
    expect(ordem, [a, b]);
    // Trocar so uma inverte a ordem.
    m.e.editPositionZ(a, Duration.zero, -800);
    final p2 = m.c.read(editorControllerProvider);
    final ordem2 = depthSortPaintOrder(p2.layers.reversed.toList(), Duration.zero)
        .where((l) => l.id == a || l.id == b)
        .map((l) => l.id)
        .toList();
    expect(ordem2, [b, a]);
  });

  test('copiar e colar efeitos entre camadas, com ids novos e sem Time Remap', () {
    final m = _motor();
    m.e.addTextLayer(Duration.zero, text: 'Origem');
    m.e.addTextLayer(Duration.zero, text: 'Destino');
    final ls = m.c.read(editorControllerProvider).layers;
    final origem = ls.firstWhere((l) => l is TextLayer && l.text == 'Origem').id;
    final destino = ls.firstWhere((l) => l is TextLayer && l.text == 'Destino').id;
    m.e.addEffect(origem, EffectType.values.firstWhere((t) => t.name == 'gaussianBlur'));
    final antes = m.c.read(editorControllerProvider).layerById(origem)!.effects;
    expect(antes, isNotEmpty);

    expect(m.e.pasteEffects(destino), 0, reason: 'nada copiado ainda');
    expect(m.e.copyEffects(origem), antes.length);
    expect(m.e.pasteEffects(destino), antes.length);
    final colados = m.c.read(editorControllerProvider).layerById(destino)!.effects;
    expect(colados.map((e) => e.type), antes.map((e) => e.type));
    expect(colados.first.id, isNot(antes.first.id), reason: 'copia independente');
    // Colar de novo anexa outra copia (como no AM).
    m.e.pasteEffects(destino);
    expect(m.c.read(editorControllerProvider).layerById(destino)!.effects.length, antes.length * 2);
  });
}
