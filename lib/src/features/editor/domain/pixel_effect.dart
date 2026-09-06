import 'dart:math' as math;

import 'effect.dart';

/// Explicit ABI, independent of enum indices and project serialization.
class PixelKernel {
  const PixelKernel(this.mode, this.keys);
  final int mode;
  final List<String> keys;
}

const pixelKernels = <EffectType, PixelKernel>{
  EffectType.levels: PixelKernel(1, [
    'entradaMin',
    'entradaMax',
    'gama',
    'saidaMin',
    'saidaMax',
    'canal',
  ]),
  EffectType.posterize: PixelKernel(2, ['niveis']),
  EffectType.curves: PixelKernel(3, [
    'contraste',
    'brilho',
    'sombras',
    'altas',
  ]),
  EffectType.vibrance: PixelKernel(4, [
    'vibracao',
    'saturacao',
    'protecaoPele',
  ]),
  EffectType.corrections: PixelKernel(5, [
    'exposicao',
    'contraste',
    'altas',
    'sombras',
    'temperatura',
    'matiz',
    'saturacao',
    'gama',
  ]),
  EffectType.whiteBalance: PixelKernel(6, ['temperatura', 'matiz']),
  EffectType.colorWheels: PixelKernel(7, [
    'sombrasR',
    'sombrasG',
    'sombrasB',
    'altasR',
    'altasG',
    'altasB',
  ]),
  EffectType.unmult: PixelKernel(8, ['limiar', 'suavidade']),
  EffectType.tint: PixelKernel(9, ['strength']),
  EffectType.vignette: PixelKernel(10, [
    'quantidade',
    'raio',
    'suavidade',
    'forma',
    'centroX',
    'centroY',
  ]),
  EffectType.mosaic: PixelKernel(11, ['blocos']),
  EffectType.filmGrain: PixelKernel(12, ['intensidade', 'tamanho', 'semente']),
  EffectType.fractalNoise: PixelKernel(13, [
    'escala',
    'complexidade',
    'contraste',
    'evolucao',
    'opacidade',
    'semente',
  ]),
  EffectType.turbulentDisplace: PixelKernel(14, [
    'quantidade',
    'tamanho',
    'complexidade',
    'evolucao',
    'semente',
  ]),
  EffectType.radialBlur: PixelKernel(15, ['quantidade', 'modo', 'amostras']),
  EffectType.directionalBlur: PixelKernel(16, ['comprimento', 'angulo']),
  EffectType.unsharpMask: PixelKernel(17, ['quantidade', 'raio', 'limiar']),
  EffectType.lightRays: PixelKernel(18, [
    'comprimento',
    'intensidade',
    'amostras',
    'centroX',
    'centroY',
  ]),
  EffectType.zoomWarp: PixelKernel(19, ['quantidade', 'rastro', 'amostras']),
  EffectType.gradient4: PixelKernel(20, ['opacity', 'blend', 'angle']),
  // 21/22 are extraction and response stages of the multiscale glow.
  EffectType.rgbSplit: PixelKernel(23, [
    'deslocamento',
    'angulo',
    'canais',
    'suavizar',
  ]),
  EffectType.radialAberration: PixelKernel(24, ['quantidade']),
  EffectType.bend: PixelKernel(25, [
    'quantidade',
    'eixo',
    'curvatura',
    'ancora',
  ]),
  EffectType.ccSplit: PixelKernel(26, [
    'divisao',
    'angulo',
    'centro',
    'suavidade',
  ]),
  EffectType.digitalDamage: PixelKernel(27, [
    'blocos',
    'altura',
    'deslocamento',
    'cor',
    'intervalo',
    'semente',
  ]),
  EffectType.glitchify: PixelKernel(28, [
    'intensidade',
    'blocos',
    'deslocamento',
    'cor',
    'velocidade',
    'ruidoLinha',
    'semente',
  ]),
  EffectType.vhs: PixelKernel(29, [
    'intensidade',
    'linhas',
    'sangramento',
    'tremor',
    'ruido',
    'desbotar',
    'semente',
  ]),
  EffectType.filmDamage: PixelKernel(30, [
    'poeira',
    'riscos',
    'cintilacao',
    'granulacao',
    'queimado',
    'salto',
    'semente',
  ]),
  EffectType.flicker: PixelKernel(31, [
    'amount',
    'frequency',
    'style',
    'target',
    'seed',
  ]),
  EffectType.glitch: PixelKernel(32, [
    'quantidade',
    'velocidade',
    'intervalo',
    'deslize',
    'escala',
    'cor',
    'luz',
    'desfoque',
    'rgb',
    'semente',
  ]),
};

/// Sanitized immutable uniforms, evaluated at the current layer-local time.
/// IDs/units/keyframes remain compatible with saved effects and presets.
class PixelEffectFrame {
  PixelEffectFrame(
    this.mode,
    List<double> values, {
    this.time = 0,
    this.color = const [1.0, 1.0, 1.0, 1.0],
    this.extraColors = const [],
    this.pixelScale = 1,
  }) : values = List.unmodifiable(
         List.generate(32, (i) {
           final value = i < values.length ? values[i] : 0.0;
           return value.isFinite ? value : 0.0;
         }),
       );
  final int mode;
  final List<double> values, color, extraColors;
  final double time, pixelScale;

  factory PixelEffectFrame.of(
    EffectInstance effect,
    Duration time, {
    double pixelScale = 1,
  }) {
    final kernel = pixelKernels[effect.type]!;
    final spec = effectSpecs[effect.type]!;
    final values = [
      for (final key in kernel.keys)
        effect.paramAt(key, time).isFinite
            ? effect
                  .paramAt(key, time)
                  .clamp(spec.params[key]!.min, spec.params[key]!.max)
            : spec.params[key]!.initial,
    ];
    final c = effect.color;
    return PixelEffectFrame(
      kernel.mode,
      values,
      time: time.inMicroseconds / 1e6,
      pixelScale: pixelScale,
      color: [c.r, c.g, c.b, c.a],
      extraColors: [
        for (final color in effect.extraColors) ...[
          color.r,
          color.g,
          color.b,
          color.a,
        ],
      ],
    );
  }
}

/// CPU reference for the nonlinear kernels. Used by numerical tests and tools;
/// the render path evaluates the equivalent functions in the fragment shader.
double levelsChannel(
  double value,
  double black,
  double white,
  double gamma,
  double outputBlack,
  double outputWhite,
) {
  final span = white - black;
  final x = span.abs() < 1e-6
      ? (value >= white ? 1.0 : 0.0)
      : ((value - black) / span).clamp(0.0, 1.0);
  return (outputBlack +
          (outputWhite - outputBlack) * math.pow(x, 1 / gamma.clamp(.01, 100)))
      .clamp(0.0, 1.0);
}

double posterizeChannel(double value, int levels) {
  final n = levels.clamp(2, 255) - 1;
  return (value.clamp(0.0, 1.0) * n).round() / n;
}
