// ACEITACAO (spec §44): quadrado arredondado -> circulo -> pilula ->
// card, mudando tamanho, arredondamento e cor com bezier + overshoot e
// motion blur ligado; icone, titulo e subtitulo entrando em cascata de
// 2 quadros. Tudo pelas MESMAS portas que a interface chama — nenhuma
// animacao codificada a mao, nenhum expression.
import 'package:aurea/src/features/editor/application/editor_controller.dart';
import 'package:aurea/src/features/editor/domain/apple_motion.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/presets_de_movimento.dart';
import 'package:aurea/src/features/editor/domain/project_store.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';
import 'package:flutter/material.dart' hide Easing;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('a animacao Apple-style sai inteira pelas portas do app', () {
    final c = ProviderContainer();
    addTearDown(c.dispose);
    final e = c.read(editorControllerProvider.notifier);

    // ------------------------------------------------ o card que morfa
    e.addShapeLayer(Duration.zero, name: 'Card');
    final card = c
        .read(editorControllerProvider)
        .layers
        .whereType<ShapeLayer>()
        .first
        .id;
    e.addCompoundShapeGeometry(card, ParamShapeKind.rect);

    // Quadrado -> circulo (0s), -> pilula (1s), -> card (2s): cada passo
    // e o preset de morph rapido, cravando keyframes com overshoot na
    // MESMA trilha — a sequencia inteira fica editavel.
    e.morphRapidoDeForma(card, Duration.zero, FormaRapida.circulo);
    e.morphRapidoDeForma(
      card,
      const Duration(seconds: 1),
      FormaRapida.pilula,
    );
    e.morphRapidoDeForma(card, const Duration(seconds: 2), FormaRapida.card);

    ShapeParametric rect() => c
        .read(editorControllerProvider)
        .layerById(card)!
        .let((l) => (l as ShapeLayer).contents)
        .whereType<ShapeParametric>()
        .firstWhere((i) => i.kind == ParamShapeKind.rect);

    // No fim do primeiro passo e um CIRCULO de verdade: lados iguais e
    // arredondamento cheio — e o canto nao deformou, porque quem anima e
    // a conta da forma, nunca a escala da camada.
    final aposCirculo = rect();
    expect(
      aposCirculo.sizeX.valueAt(const Duration(milliseconds: 500)),
      aposCirculo.sizeY.valueAt(const Duration(milliseconds: 500)),
    );
    expect(
      aposCirculo.roundness.valueAt(const Duration(milliseconds: 500)),
      100,
    );
    // Pilula: mais larga que alta, ainda capsula.
    expect(
      aposCirculo.sizeX.valueAt(const Duration(milliseconds: 1500)),
      greaterThan(
        aposCirculo.sizeY.valueAt(const Duration(milliseconds: 1500)) * 2,
      ),
    );
    // Card: arredondamento parcial (nem reto, nem capsula).
    final rCard = aposCirculo.roundness.valueAt(
      const Duration(milliseconds: 2500),
    );
    expect(rCard, greaterThan(0));
    expect(rCard, lessThan(100));
    // O overshoot pedido esta nas curvas (y1 da bezier passa do teto).
    expect(
      aposCirculo.sizeX.keyframes.first.ease.y1,
      greaterThan(1),
      reason: 'bezier com overshoot',
    );

    // ------------------------------------------------ o morph de cor
    final fillId = (c.read(editorControllerProvider).layerById(card)!
            as ShapeLayer)
        .contents
        .whereType<ShapeFill>()
        .first
        .id;
    e.toggleShapeFillColorKeyframe(card, fillId, Duration.zero);
    e.toggleShapeFillColorKeyframe(
      card,
      fillId,
      const Duration(milliseconds: 2500),
    );
    e.setShapeFillColorAt(
      card,
      fillId,
      const Duration(milliseconds: 2500),
      const Color(0xFF7C62FF),
    );
    final fill = (c.read(editorControllerProvider).layerById(card)!
            as ShapeLayer)
        .contents
        .whereType<ShapeFill>()
        .first;
    final corMeio = fill.colorAt(const Duration(milliseconds: 1250));
    expect(corMeio, isNot(fill.colorAt(Duration.zero)));
    expect(
      fill.colorAt(const Duration(milliseconds: 2500)).b,
      closeTo(1, .01),
    );

    // ------------------------------------------------ motion blur
    e.toggleLayerMotionBlur(card);
    expect(
      c.read(editorControllerProvider).metaOf(card).motionBlur,
      isTrue,
    );

    // ------------------------------ icone, titulo e subtitulo em cascata
    e.addIconLayer(
      Duration.zero,
      'M0 0 L24 0 L24 24 L0 24 Z',
      'Ícone',
    );
    e.addTextLayer(Duration.zero, text: 'Título');
    e.addTextLayer(Duration.zero, text: 'Subtítulo');
    final projeto = c.read(editorControllerProvider);
    final icone = projeto.layers.firstWhere((l) => l.name == 'Ícone').id;
    final titulo = projeto.layers
        .firstWhere((l) => l is TextLayer && l.text == 'Título')
        .id;
    final sub = projeto.layers
        .firstWhere((l) => l is TextLayer && l.text == 'Subtítulo')
        .id;
    for (final id in [icone, titulo, sub]) {
      e.aplicarPresetDeMovimento(id, Duration.zero, PresetDeMovimento.subir);
    }
    // Cascata de 2 quadros (30 fps do projeto = 67 ms por passo). A pilha
    // guarda o mais novo em cima; "Fim" percorre de baixo para cima, que
    // e a ordem icone -> titulo -> subtitulo em que foram criados.
    e.cascadeSelection(
      {icone, titulo, sub},
      interval: const Duration(milliseconds: 67),
      order: CascadeOrder.end,
    );
    Duration entrada(String id) => c
        .read(editorControllerProvider)
        .layerById(id)!
        .opacity
        .keyframes
        .first
        .time;
    expect(entrada(titulo) - entrada(icone),
        const Duration(milliseconds: 67));
    expect(entrada(sub) - entrada(titulo),
        const Duration(milliseconds: 67));

    // ------------------------------------------- salvar, abrir, desfazer
    final volta = projectFromJson(
      projectToJson(c.read(editorControllerProvider)),
    );
    final rectVolta = (volta.layerById(card)! as ShapeLayer)
        .contents
        .whereType<ShapeParametric>()
        .firstWhere((i) => i.kind == ParamShapeKind.rect);
    expect(
      rectVolta.roundness.valueAt(const Duration(milliseconds: 500)),
      100,
      reason: 'o arquivo carrega a animacao inteira',
    );
    expect(volta.metaOf(card).motionBlur, isTrue);

    // Desfazer, passo a passo, volta ao projeto vazio sem sobrar nada.
    for (var i = 0; i < 40; i++) {
      e.undo();
    }
    expect(
      c.read(editorControllerProvider).layers.whereType<ShapeLayer>(),
      isEmpty,
    );
  });
}

extension _Let<T> on T {
  R let<R>(R Function(T) f) => f(this);
}
