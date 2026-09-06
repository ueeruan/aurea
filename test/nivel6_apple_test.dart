import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/editor/domain/apple_motion.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/keyframe.dart';
import 'package:aurea/src/features/editor/domain/layer.dart';
import 'package:aurea/src/features/editor/domain/layer_meta.dart';
import 'package:aurea/src/features/editor/domain/shape.dart';

void main() {
  group('presets Apple', () {
    test('beziers usam os valores documentados', () {
      expect(Easing.appleStandard.x1, 0.25);
      expect(Easing.appleStandard.y1, 0.1);
      expect(Easing.appleStandard.x2, 0.25);
      expect(Easing.appleStandard.y2, 1);
      expect(Easing.appleExit.x1, 0.4);
      expect(Easing.appleExit.y1, 0);
      expect(Easing.appleExit.x2, 1);
      expect(Easing.appleExit.y2, 1);
    });

    test('mola de interface nao cruza o alvo em mais de 1%', () {
      var maximum = 0.0;
      for (var i = 0; i <= 1000; i++) {
        maximum = maximum < Easing.interfaceSpring.transform(i / 1000)
            ? Easing.interfaceSpring.transform(i / 1000)
            : maximum;
      }
      expect(maximum, lessThan(1.01));
      expect(Easing.interfaceSpring.transform(1), 1);
    });
  });

  group('cascata', () {
    ShapeLayer layer(int i) => ShapeLayer(
      id: 'layer-$i',
      name: 'Layer $i',
      startTime: Duration.zero,
      duration: const Duration(seconds: 2),
      opacity: AnimatedDouble(0, const [
        Keyframe(time: Duration.zero, value: 0),
        Keyframe(time: Duration(milliseconds: 100), value: 1),
      ]),
    );

    test('cinco camadas recebem exatamente i vezes 40 ms', () {
      final layers = [for (var i = 0; i < 5; i++) layer(i)];
      final result = cascadeLayerKeyframes(layers, {
        for (final value in layers) value.id,
      }, ease: Easing.interfaceSpring);
      for (var i = 0; i < result.length; i++) {
        expect(
          result[i].opacity.keyframes.first.time,
          Duration(milliseconds: i * 40),
        );
        expect(result[i].opacity.keyframes.first.ease.type, EasingType.spring);
      }
    });

    test('intervalo zero e estritamente neutro', () {
      final layers = [layer(0), layer(1)];
      final result = cascadeLayerKeyframes(
        layers,
        {for (final value in layers) value.id},
        interval: Duration.zero,
        ease: Easing.interfaceSpring,
      );
      expect(identical(result, layers), isTrue);
    });
  });

  test('retangulo aceita raio independente por canto', () {
    final shape = ShapeParametric(
      sizeX: AnimatedDouble(100),
      sizeY: AnimatedDouble(100),
      roundnessPercent: false,
      cornerTopLeft: AnimatedDouble(40),
      cornerTopRight: AnimatedDouble(0),
      cornerBottomRight: AnimatedDouble(0),
      cornerBottomLeft: AnimatedDouble(0),
    ).buildAt(Duration.zero);
    expect(shape.contains(const Offset(-49, -49)), isFalse);
    expect(shape.contains(const Offset(49, -49)), isTrue);
  });

  test('vidro fosco e sombra expõem controles do nivel 6', () {
    final glass = effectSpecs[EffectType.liquidGlass]!;
    expect(glass.montar, ['blur', 'saturation', 'brightness']);
    expect(glass.presets, isNotEmpty);
    expect(glass.params.keys, containsAll(['tint', 'rim', 'grain']));
    expect(ShadowStyle().spread.base, 0);
  });
}
