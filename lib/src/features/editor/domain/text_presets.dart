import 'keyframe.dart';
import 'text_animator.dart';

/// Presets de animacao de texto: cada um constroi uma pilha de animadores
/// com keyframes RELATIVOS ao inicio da camada (spec §11).
class TextPreset {
  const TextPreset({required this.name, required this.build});

  final String name;
  final List<TextAnimator> Function() build;
}

AnimatedDouble _sweep01(Duration duration, [Easing ease = Easing.linear]) {
  // start varre 0 -> 1: a cobertura encolhe e revela unidade a unidade.
  return AnimatedDouble(0)
      .withKeyframe(Duration.zero, 0, ease)
      .withKeyframe(duration, 1);
}

final textPresets = <TextPreset>[
  TextPreset(
    name: 'Fade por letra',
    build: () => [
      TextAnimator(
        name: 'Fade por letra',
        selectors: [
          RangeSelector(
            shape: SelectorShape.rampUp,
            start: _sweep01(const Duration(milliseconds: 1200)),
          ),
        ],
        properties: [
          AnimatorProperty(
              type: TextAnimProp.opacity, value: AnimatedDouble(0)),
        ],
      ),
    ],
  ),
  TextPreset(
    name: 'Subir por letra',
    build: () => [
      TextAnimator(
        name: 'Subir por letra',
        selectors: [
          RangeSelector(
            shape: SelectorShape.rampUp,
            start: _sweep01(
                const Duration(milliseconds: 1100), Easing.easeInOut),
          ),
        ],
        properties: [
          AnimatorProperty(
              type: TextAnimProp.positionY, value: AnimatedDouble(90)),
          AnimatorProperty(
              type: TextAnimProp.opacity, value: AnimatedDouble(0)),
        ],
      ),
    ],
  ),
  TextPreset(
    name: 'Maquina de escrever',
    build: () => [
      TextAnimator(
        name: 'Maquina de escrever',
        selectors: [
          RangeSelector(
            shape: SelectorShape.square,
            smoothness: AnimatedDouble(0),
            start: _sweep01(const Duration(milliseconds: 1600)),
          ),
        ],
        properties: [
          AnimatorProperty(
              type: TextAnimProp.opacity, value: AnimatedDouble(0)),
        ],
      ),
    ],
  ),
  TextPreset(
    name: 'Escala pop',
    build: () => [
      TextAnimator(
        name: 'Escala pop',
        selectors: [
          RangeSelector(
            shape: SelectorShape.rampUp,
            start: _sweep01(
                const Duration(milliseconds: 1000), Easing.overshoot),
          ),
        ],
        properties: [
          AnimatorProperty(
              type: TextAnimProp.scale, value: AnimatedDouble(0)),
          AnimatorProperty(
              type: TextAnimProp.opacity, value: AnimatedDouble(0)),
        ],
      ),
    ],
  ),
  TextPreset(
    name: 'Cascata',
    build: () => [
      TextAnimator(
        name: 'Cascata',
        selectors: [
          RangeSelector(
            shape: SelectorShape.rampUp,
            start: _sweep01(
                const Duration(milliseconds: 1400), Easing.easeOut),
          ),
        ],
        properties: [
          AnimatorProperty(
              type: TextAnimProp.positionY, value: AnimatedDouble(-140)),
          AnimatorProperty(
              type: TextAnimProp.rotation, value: AnimatedDouble(-18)),
          AnimatorProperty(
              type: TextAnimProp.opacity, value: AnimatedDouble(0)),
        ],
      ),
    ],
  ),
  TextPreset(
    name: 'Wiggle',
    build: () => [
      TextAnimator(
        name: 'Wiggle',
        selectors: [
          WigglySelector(
            wigglesPerSecond: AnimatedDouble(3),
            correlation: AnimatedDouble(20),
          ),
        ],
        properties: [
          AnimatorProperty(
              type: TextAnimProp.positionY, value: AnimatedDouble(16)),
          AnimatorProperty(
              type: TextAnimProp.rotation, value: AnimatedDouble(10)),
        ],
      ),
    ],
  ),
  TextPreset(
    name: 'Onda',
    build: () => [
      TextAnimator(
        name: 'Onda',
        selectors: [
          RangeSelector(
            shape: SelectorShape.smooth,
            start: AnimatedDouble(-0.4),
            end: AnimatedDouble(0),
            offset: AnimatedDouble(0)
                .withKeyframe(Duration.zero, 0, Easing.linear)
                .withKeyframe(const Duration(milliseconds: 2500), 1.4),
          ),
        ],
        properties: [
          AnimatorProperty(
              type: TextAnimProp.positionY, value: AnimatedDouble(-46)),
        ],
      ),
    ],
  ),
];
