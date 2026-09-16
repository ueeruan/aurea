import 'dart:math' as math;

import 'effect.dart';

/// Explicit ABI, independent of enum indices and project serialization.
class PixelKernel {
  const PixelKernel(this.mode, this.keys);
  final int mode;
  final List<String> keys;
}

const pixelKernels = <EffectType, PixelKernel>{
  // RECORTE (keying). Os tres mexem no alfa: e o que faz o fundo sumir
  // de verdade, e nao virar preto.
  EffectType.chromaKey: PixelKernel(33, [
    'tolerancia',
    'suavidade',
    'difusao',
    'supressao',
  ]),
  EffectType.lumaKey: PixelKernel(34, [
    'limiar',
    'tolerancia',
    'difusao',
    'inverter',
  ]),
  EffectType.colorKey: PixelKernel(35, ['tolerancia', 'suavidade']),
  EffectType.findEdges: PixelKernel(36, ['inverter', 'mistura']),
  // OPACIDADE (v1.1.1): dissolver por chuvisco e borda de pena. Os dois
  // mexem no ALFA, como o recorte — por isso moram aqui.
  EffectType.dissolver: PixelKernel(48, [
    'quantidade',
    'grao',
    'suavidade',
    'semente',
  ]),
  EffectType.pena: PixelKernel(49, ['tamanho', 'suavidade']),
  // GERADORES (v1.1.1): desenham por conta propria e so leem a camada
  // para misturar (ou para recortar).
  EffectType.nuvens: PixelKernel(50, [
    'escala',
    'detalhe',
    'evolucao',
    'contraste',
    'semente',
    'mistura',
    'recortar',
  ]),
  EffectType.xadrez: PixelKernel(51, [
    'quantidade',
    'esticar',
    'angulo',
    'mistura',
    'recortar',
  ]),
  EffectType.listras: PixelKernel(52, [
    'quantidade',
    'angulo',
    'proporcao',
    'suavidade',
    'deslocamento',
    'mistura',
    'recortar',
  ]),
  EffectType.pontos: PixelKernel(53, [
    'quantidade',
    'tamanho',
    'suavidade',
    'angulo',
    'mistura',
    'recortar',
  ]),
  EffectType.estrelas: PixelKernel(54, [
    'quantidade',
    'tamanho',
    'cintilar',
    'velocidade',
    'semente',
    'mistura',
    'recortar',
  ]),
  // CORTINA, RECORTE E BORDA (v1.1.1): todos mexem no alfa por pixel.
  EffectType.cortina: PixelKernel(56, [
    'progresso',
    'angulo',
    'suavidade',
    'inverter',
  ]),
  EffectType.cortinaRadial: PixelKernel(57, [
    'progresso',
    'comeco',
    'suavidade',
    'sentido',
    'centro_x',
    'centro_y',
  ]),
  EffectType.apertarRecorte: PixelKernel(58, ['aperto', 'suavidade']),
  EffectType.meioTom: PixelKernel(59, [
    'quantidade',
    'angulo',
    'suavidade',
    'mistura',
  ]),
  EffectType.contorno: PixelKernel(60, [
    'espessura',
    'suavidade',
    'so_contorno',
  ]),
  EffectType.brilhoPorDentro: PixelKernel(61, ['tamanho', 'forca']),
  EffectType.bordasAsperas: PixelKernel(62, [
    'tamanho',
    'escala',
    'evolucao',
    'semente',
  ]),
  EffectType.raios: PixelKernel(55, [
    'quantidade',
    'centro_x',
    'centro_y',
    'largura',
    'suavidade',
    'giro',
    'alcance',
    'mistura',
    'recortar',
  ]),
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
    // S_FilmDamage 2: sempre DEPOIS dos sete de cima (slots 7 a 13).
    'fios',
    'balanco',
    'desfoque',
    'vinheta',
    'saturacao',
    'sepia',
    'tamanho_poeira',
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
  // COLORING (modos 37 a 43). A ordem das chaves e a ordem dos slots do
  // shader: p0.x, p0.y, p0.z, p0.w, p1.x...
  EffectType.colorBalance: PixelKernel(37, [
    'shadow_red',
    'shadow_green',
    'shadow_blue',
    'midtone_red',
    'midtone_green',
    'midtone_blue',
    'highlight_red',
    'highlight_green',
    'highlight_blue',
    'preserve_luminosity',
  ]),
  EffectType.selectiveColor: PixelKernel(38, [
    'colors',
    'cyan',
    'magenta',
    'yellow',
    'black',
    'method',
  ]),
  EffectType.channelMixer: PixelKernel(39, [
    'red_red',
    'red_green',
    'red_blue',
    'red_const',
    'green_red',
    'green_green',
    'green_blue',
    'green_const',
    'blue_red',
    'blue_green',
    'blue_blue',
    'blue_const',
    'monochrome',
  ]),
  EffectType.photoFilter: PixelKernel(40, [
    'mode',
    'density',
    'temperature',
    'preserve_luminosity',
  ]),
  EffectType.gradientMap: PixelKernel(41, [
    'blend_mode',
    'opacity',
    'midtones',
    'balance',
  ]),
  EffectType.brightnessContrast: PixelKernel(42, ['brightness', 'contrast']),
  EffectType.colorTune: PixelKernel(43, [
    'lift_hue',
    'lift_saturation',
    'lift_luminance',
    'gamma_hue',
    'gamma_saturation',
    'gamma_luminance',
    'gain_hue',
    'gain_saturation',
    'gain_luminance',
    'offset_hue',
    'offset_saturation',
    'offset_luminance',
  ]),
  // 44 - fatias de glitch dos one framers.
  EffectType.sliceGlitch: PixelKernel(44, [
    'slices',
    'probability',
    'offset',
    'rgb_offset',
    'block_size',
    'block_strength',
    'desaturate',
    'posterize',
    'speed',
    'seed',
  ]),
  // 45 em diante - a camada de ajuste do After: a mesma conta em Dart esta
  // em domain/efeitos_do_after.dart.
  EffectType.hueSaturation: PixelKernel(45, [
    'master_hue',
    'master_saturation',
    'master_lightness',
    'colorize',
    'colorize_hue',
    'colorize_saturation',
  ]),
  EffectType.sSharpen: PixelKernel(47, [
    'amount',
    'width',
    'threshold',
    'luma',
    'chroma',
  ]),
  EffectType.mathOps: PixelKernel(46, [
    'operation',
    'source_b',
    'b_blur',
    'a_lights',
    'a_darks',
    'a_saturation',
    'b_lights',
    'b_darks',
    'b_saturation',
    'dest_lights',
    'dest_darks',
    'dest_saturation',
    'mask',
    'mask_blur',
    'invert_mask',
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
