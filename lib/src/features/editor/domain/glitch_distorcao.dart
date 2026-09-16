import 'dart:math' as math;
import 'dart:ui';

import 'effect.dart';

/// Glitches da aba Distorcer (16/09): Glitchify (CSpice), Twitch (Video
/// Copilot) e Cross Glitch (BCC Cross Glitch, Boris FX Continuum).
///
/// Glitchify: nomes, faixas e padroes LIDOS do After Effects 2026 do dono.
/// Twitch e Cross Glitch nao estao instalados: parametros da documentacao
/// publica (Video Copilot / Boris FX), padroes escolhidos por nos.
/// Tudo o que e sorteio por quadro (passos de tempo, envelopes dos tiques,
/// deslocamentos do shake) sai daqui pronto; os shaders so desenham.
const _modos = ['Normal', 'Adição', 'Tela', 'Multiplicar', 'Diferença', 'Sobrepor', 'Clarear', 'Escurecer', 'Luz suave'];

const efeitosGlitchDistorcao = <EffectType, EffectSpec>{
  EffectType.glitchify: EffectSpec(
    id: 'glitchify',
    name: 'Glitchify',
    category: 'Distort',
    procedural: true,
    cost: 2,
    synonyms: ['glitchify', 'glitch', 'falha', 'digital', 'cspice', 'pixel streak', 'datamosh', 'rgb split'],
    params: {
      'amount': EffectParam('Intensidade', 50, 0, 100, decimals: 1, dragStep: .2),
      'speed': EffectParam('Velocidade', 50, 0, 100, decimals: 1, dragStep: .2),
      'seed': EffectParam('Semente', 0, 0, 32768, kind: ParamKind.seed, decimals: 0),
      'completion': EffectParam('Conclusão', 0, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'softness': EffectParam('Suavidade', 10, 0, 100, decimals: 1, dragStep: .2),
      'transform_enable': EffectParam('Glitch de transformação', 1, 0, 1, kind: ParamKind.toggle),
      'transform_speed': EffectParam('Velocidade da transformação', 1, 0, 100, decimals: 1, dragStep: .2),
      'position_x': EffectParam('Posição X', 0, -30000, 30000, decimals: 1, dragStep: .5),
      'position_y': EffectParam('Posição Y', 0, -30000, 30000, decimals: 1, dragStep: .5),
      'position_wiggle': EffectParam('Oscilação da posição', 0, 0, 30000, decimals: 1, dragStep: .5),
      'scale': EffectParam('Escala', 100, -30000, 30000, unit: '%', decimals: 1, dragStep: .2),
      'scale_wiggle': EffectParam('Oscilação da escala', 0, 0, 30000, decimals: 1, dragStep: .2),
      'crop_left': EffectParam('Cortar esquerda', 0, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'crop_top': EffectParam('Cortar topo', 0, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'crop_right': EffectParam('Cortar direita', 100, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'crop_bottom': EffectParam('Cortar base', 100, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'crop_wiggle': EffectParam('Oscilação do corte', 0, 0, 30000, decimals: 1, dragStep: .2),
      'transform_opacity': EffectParam('Opacidade da transformação', 100, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'transform_mode': EffectParam('Modo da transformação', 0, 0, 8, kind: ParamKind.choice, options: _modos),
      'composite_over': EffectParam('Compor sobre o original', 0, 0, 1, kind: ParamKind.toggle),
      'channel_enable': EffectParam('Glitch de canal', 1, 0, 1, kind: ParamKind.toggle),
      'split_channel': EffectParam('Canal separado', 0, 0, 4, kind: ParamKind.choice,
          options: ['Vermelho e azul', 'Vermelho', 'Verde', 'Azul', 'Vermelho e verde']),
      'split_horizontal': EffectParam('Separação horizontal', 50, -100, 100, decimals: 1, dragStep: .2),
      'split_vertical': EffectParam('Separação vertical', 0, -100, 100, decimals: 1, dragStep: .2),
      'split_speed': EffectParam('Velocidade da separação', 0, 0, 10, decimals: 2, dragStep: .02),
      'channel_scale': EffectParam('Escala de canal', 30, -800, 800, decimals: 1, dragStep: .5),
      'scale_channel': EffectParam('Canal escalado', 0, 0, 4, kind: ParamKind.choice,
          options: ['Vermelho', 'Verde', 'Azul', 'Vermelho e azul', 'Verde e azul']),
      'scale_vertical': EffectParam('Escala vertical', 0, 0, 1, kind: ParamKind.toggle),
      'scale_offset': EffectParam('Centro da escala', 50, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'scale_speed': EffectParam('Velocidade da escala', 3, 0, 10, decimals: 2, dragStep: .02),
      'channel_mode': EffectParam('Modo do canal', 0, 0, 8, kind: ParamKind.choice, options: _modos),
      'color_enable': EffectParam('Glitch de cor', 1, 0, 1, kind: ParamKind.toggle),
      'color_offset': EffectParam('Deslocar cor', 20, 0, 100, decimals: 1, dragStep: .2),
      'color_amount': EffectParam('Quantidade de cor', 80, 0, 100, decimals: 1, dragStep: .2),
      'color_opacity': EffectParam('Opacidade da cor', 25, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'color_mode': EffectParam('Modo da cor', 0, 0, 8, kind: ParamKind.choice, options: _modos),
      'image_enable': EffectParam('Glitch de imagem', 1, 0, 1, kind: ParamKind.toggle),
      'pixel_streak': EffectParam('Riscos de pixel', 25, 0, 100, decimals: 1, dragStep: .2),
      'vertical_sort': EffectParam('Riscos verticais', 0, 0, 1, kind: ParamKind.toggle),
      'slice_amount': EffectParam('Fatias', 50, 0, 100, decimals: 1, dragStep: .2),
      'slice_position': EffectParam('Posição das fatias', 50, 0, 100, decimals: 1, dragStep: .2),
      'slice_opacity': EffectParam('Opacidade das fatias', 50, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'slice_mode': EffectParam('Modo das fatias', 2, 0, 8, kind: ParamKind.choice, options: _modos),
      'block_amount': EffectParam('Blocos', 50, 0, 100, decimals: 1, dragStep: .2),
      'block_speed': EffectParam('Velocidade dos blocos', 50, 0, 100, decimals: 1, dragStep: .2),
      'block_width': EffectParam('Largura dos blocos', 150, 0, 100000, decimals: 0, dragStep: 1),
      'block_height': EffectParam('Altura dos blocos', 50, 0, 100000, decimals: 0, dragStep: 1),
      'block_direction': EffectParam('Direção dos blocos', 0, 0, 2, kind: ParamKind.choice,
          options: ['Horizontal', 'Vertical', 'Ambas']),
      'block_mode': EffectParam('Modo dos blocos', 8, 0, 8, kind: ParamKind.choice, options: _modos),
      'compression_enable': EffectParam('Glitch de compressão', 0, 0, 1, kind: ParamKind.toggle),
      'compression_multiplier': EffectParam('Multiplicador da compressão', 50, 0, 100, decimals: 1, dragStep: .2),
      'dither_amount': EffectParam('Pontilhado', 50, 0, 100, decimals: 1, dragStep: .2),
      'jpeg_amount': EffectParam('JPEG', 50, 0, 100, decimals: 1, dragStep: .2),
      'noise_gain': EffectParam('Ganho do ruído', 1.5, 0, 5, decimals: 2, dragStep: .01),
      'lacunarity': EffectParam('Lacunaridade', 10, 0, 30, decimals: 2, dragStep: .05),
      'octaves': EffectParam('Oitavas', 2, 0, 6, decimals: 0, dragStep: .05),
      'repeat_edge': EffectParam('Repetir pixels da borda', 1, 0, 1, kind: ParamKind.toggle),
      'scale_quantization': EffectParam('Degraus da escala', 2, 1, 64, decimals: 0, dragStep: .1),
      'fill_gaps': EffectParam('Preencher vãos', 0, 0, 1, kind: ParamKind.toggle),
      'use_noise': EffectParam('Riscos pelo ruído', 1, 0, 1, kind: ParamKind.toggle),
      'noise_cutoff': EffectParam('Corte do ruído', 50, 0, 100, decimals: 1, dragStep: .2),
      'slice_quality': EffectParam('Qualidade das fatias', 10, 0, 100, decimals: 1, dragStep: .2),
      'max_random_shift': EffectParam('Deslocamento máximo', 40, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'slice_quantization': EffectParam('Degraus das fatias', 8, 1, 64, decimals: 0, dragStep: .1),
      'grouping_factor': EffectParam('Agrupamento dos blocos', 5, 0, 1000, decimals: 0, dragStep: .1),
      'wiggle_quantization': EffectParam('Degraus da oscilação', 2, 1, 64, decimals: 0, dragStep: .1),
    },
    montar: ['amount', 'speed', 'pixel_streak'],
    presets: [
      EffectPronto('Glitch padrão', {}),
      EffectPronto('Sinal quebrado', {'amount': 85, 'speed': 70, 'block_amount': 80, 'slice_amount': 80}),
      EffectPronto('Só cor', {'image_enable': 0, 'color_opacity': 60, 'split_horizontal': 100}),
    ],
  ),
  EffectType.twitch: EffectSpec(
    id: 'twitch',
    name: 'Twitch',
    category: 'Distort',
    procedural: true,
    cost: 3,
    synonyms: ['twitch', 'tique', 'tremor', 'glitch', 'video copilot', 'shake', 'rgb split', 'flash'],
    params: {
      'amount': EffectParam('Intensidade', 50, 0, 100, decimals: 1, dragStep: .2),
      'speed': EffectParam('Velocidade', 50, 0, 100, decimals: 1, dragStep: .2),
      'behavior': EffectParam('Suavidade dos tiques', 30, 0, 100, decimals: 1, dragStep: .2),
      'border': EffectParam('Borda', 0, 0, 2, kind: ParamKind.choice, options: ['Repetir borda', 'Espelhar', 'Vazio']),
      'seed': EffectParam('Semente', 0, 0, 10000, kind: ParamKind.seed, decimals: 0),
      'blur_enable': EffectParam('Desfoque', 1, 0, 1, kind: ParamKind.toggle),
      'blur_amount': EffectParam('Quantidade do desfoque', 50, 0, 100, decimals: 1, dragStep: .2),
      'blur_speed': EffectParam('Intervalo do desfoque', 50, 0, 100, decimals: 1, dragStep: .2),
      'blur_opacity': EffectParam('Opacidade do desfoque', 100, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'blur_mode': EffectParam('Modo do desfoque', 0, 0, 8, kind: ParamKind.choice, options: _modos),
      'blur_boost': EffectParam('Reforço do desfoque', 0, 0, 100, decimals: 1, dragStep: .2),
      'light_enable': EffectParam('Luz', 1, 0, 1, kind: ParamKind.toggle),
      'light_amount': EffectParam('Quantidade de luz', 50, 0, 100, decimals: 1, dragStep: .2),
      'light_speed': EffectParam('Intervalo da luz', 50, 0, 100, decimals: 1, dragStep: .2),
      'light_behavior': EffectParam('Comportamento da luz', 2, 0, 2, kind: ParamKind.choice,
          options: ['Mais claro', 'Mais escuro', 'Ambos']),
      'scale_enable': EffectParam('Escala', 1, 0, 1, kind: ParamKind.toggle),
      'scale_amount': EffectParam('Quantidade da escala', 50, 0, 100, decimals: 1, dragStep: .2),
      'scale_speed': EffectParam('Intervalo da escala', 50, 0, 100, decimals: 1, dragStep: .2),
      'scale_origin_x': EffectParam('Origem X', 50, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'scale_origin_y': EffectParam('Origem Y', 50, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'origin_random': EffectParam('Origem aleatória', 50, 0, 100, decimals: 1, dragStep: .2),
      'slide_enable': EffectParam('Deslize', 1, 0, 1, kind: ParamKind.toggle),
      'slide_amount': EffectParam('Quantidade do deslize', 50, 0, 100, decimals: 1, dragStep: .2),
      'slide_speed': EffectParam('Intervalo do deslize', 50, 0, 100, decimals: 1, dragStep: .2),
      'slide_direction': EffectParam('Direção do deslize', 0, 0, 2, kind: ParamKind.choice,
          options: ['Horizontal', 'Vertical', 'Ambas']),
      'slide_spread': EffectParam('Espalhamento', 50, 0, 100, decimals: 1, dragStep: .2),
      'slide_tendency': EffectParam('Tendência', 0, -100, 100, decimals: 1, dragStep: .2),
      'motion_blur': EffectParam('Borrão de movimento', 50, 0, 100, decimals: 1, dragStep: .2),
      'rgb_split': EffectParam('Separação RGB', 20, 0, 100, decimals: 1, dragStep: .2),
      'color_enable': EffectParam('Cor', 0, 0, 1, kind: ParamKind.toggle),
      'color_amount': EffectParam('Quantidade de cor', 50, 0, 100, decimals: 1, dragStep: .2),
      'color_speed': EffectParam('Intervalo da cor', 50, 0, 100, decimals: 1, dragStep: .2),
      'color_random': EffectParam('Aleatoriedade da cor', 100, 0, 100, decimals: 1, dragStep: .2),
    },
    montar: ['amount', 'speed', 'slide_amount'],
    presets: [
      EffectPronto('Tique padrão', {}),
      EffectPronto('Só deslize', {'blur_enable': 0, 'light_enable': 0, 'scale_enable': 0, 'rgb_split': 60}),
      EffectPronto('Pancada', {'amount': 90, 'speed': 30, 'behavior': 0, 'color_enable': 1}),
    ],
  ),
  EffectType.crossGlitch: EffectSpec(
    id: 'cross_glitch',
    name: 'Cross Glitch',
    category: 'Distort',
    procedural: true,
    cost: 1,
    hasColor: true,
    defaultColor: Color(0xFF000000),
    colorLabels: ['Cor de fundo'],
    synonyms: ['cross glitch', 'glitch', 'bcc', 'continuum', 'blocos', 'shake', 'flicker', 'falha digital'],
    params: {
      'intensity': EffectParam('Intensidade do glitch', 100, 0, 200, decimals: 1, dragStep: .2),
      'intensity_random': EffectParam('Aleatoriedade da intensidade', 20, 0, 100, decimals: 1, dragStep: .2),
      'interval': EffectParam('Intervalo do glitch', 15, 1, 300, unit: 'q', decimals: 0, dragStep: .1),
      'interval_random': EffectParam('Aleatoriedade do intervalo', 50, 0, 100, decimals: 1, dragStep: .2),
      'duration': EffectParam('Duração do glitch', 50, 1, 100, unit: '%', decimals: 1, dragStep: .2),
      'duration_random': EffectParam('Aleatoriedade da duração', 20, 0, 100, decimals: 1, dragStep: .2),
      'seed': EffectParam('Semente', 0, 0, 10000, kind: ParamKind.seed, decimals: 0),
      'start_at_zero': EffectParam('Começar no zero', 1, 0, 1, kind: ParamKind.toggle),
      'edge': EffectParam('Bordas', 0, 0, 1, kind: ParamKind.choice, options: ['Ladrilhar', 'Refletir']),
      'block_enable': EffectParam('Dano em blocos', 1, 0, 1, kind: ParamKind.toggle),
      'block_intensity': EffectParam('Intensidade dos blocos', 100, 0, 200, decimals: 1, dragStep: .2),
      'block_peak': EffectParam('Pico dos blocos', 50, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'block_run': EffectParam('Sequência dos blocos', 30, 0, 100, decimals: 1, dragStep: .2),
      'block_size': EffectParam('Tamanho dos blocos', 2, 0, 4, kind: ParamKind.choice,
          options: ['8 px', '16 px', '32 px', '64 px', '128 px']),
      'block_saturation': EffectParam('Saturação dos blocos', 80, 0, 100, decimals: 1, dragStep: .2),
      'pattern_amount': EffectParam('Padrão', 30, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'pattern_complexity': EffectParam('Complexidade do padrão', 50, 0, 100, decimals: 1, dragStep: .2),
      'pattern_opacity': EffectParam('Opacidade do padrão', 70, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'vary_pattern_color': EffectParam('Variar cor do padrão', 50, 0, 100, decimals: 1, dragStep: .2),
      'shift_enable': EffectParam('Deslocamento', 1, 0, 1, kind: ParamKind.toggle),
      'shift_intensity': EffectParam('Intensidade do deslocamento', 100, 0, 200, decimals: 1, dragStep: .2),
      'shift_peak': EffectParam('Pico do deslocamento', 50, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'line_duplication': EffectParam('Duplicar linhas', 20, 0, 100, decimals: 1, dragStep: .2),
      'shift_amount': EffectParam('Quantidade do deslocamento', 15, 0, 100, decimals: 1, dragStep: .2),
      'shift_density': EffectParam('Densidade do deslocamento', 40, 0, 100, decimals: 1, dragStep: .2),
      'shift_run': EffectParam('Altura das faixas', 30, 0, 100, decimals: 1, dragStep: .2),
      'skew': EffectParam('Inclinação', 0, -100, 100, decimals: 1, dragStep: .2),
      'jitter': EffectParam('Tremulação', 10, 0, 100, decimals: 1, dragStep: .2),
      'line_drop': EffectParam('Linhas perdidas', 10, 0, 100, decimals: 1, dragStep: .2),
      'line_drop_density': EffectParam('Densidade das linhas perdidas', 50, 0, 100, decimals: 1, dragStep: .2),
      'shake_enable': EffectParam('Chacoalhar', 1, 0, 1, kind: ParamKind.toggle),
      'shake_intensity': EffectParam('Intensidade do chacoalhar', 100, 0, 200, decimals: 1, dragStep: .2),
      'shake_peak': EffectParam('Pico do chacoalhar', 50, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'shake_x': EffectParam('Chacoalhar X', 20, 0, 500, decimals: 1, dragStep: .2),
      'shake_y': EffectParam('Chacoalhar Y', 5, 0, 500, decimals: 1, dragStep: .2),
      'rgb_split': EffectParam('Separação RGB', 10, 0, 200, decimals: 1, dragStep: .2),
      'shake_skew': EffectParam('Inclinação do chacoalhar', 0, 0, 100, decimals: 1, dragStep: .2),
      'rotate': EffectParam('Giro', 0, 0, 45, unit: '°', decimals: 1, dragStep: .1),
      'flicker_enable': EffectParam('Cintilação', 1, 0, 1, kind: ParamKind.toggle),
      'flicker_intensity': EffectParam('Intensidade da cintilação', 100, 0, 200, decimals: 1, dragStep: .2),
      'brightness': EffectParam('Brilho', 30, 0, 100, decimals: 1, dragStep: .2),
      'brightness_offset': EffectParam('Deslocar brilho', 0, -100, 100, decimals: 1, dragStep: .2),
      'saturation': EffectParam('Saturação', 30, 0, 100, decimals: 1, dragStep: .2),
      'saturation_offset': EffectParam('Deslocar saturação', 0, -100, 100, decimals: 1, dragStep: .2),
      'use_background': EffectParam('Usar cor de fundo', 0, 0, 1, kind: ParamKind.toggle),
    },
    montar: ['intensity', 'interval', 'duration'],
    presets: [
      EffectPronto('Glitch cruzado', {}),
      EffectPronto('Só blocos', {'shift_enable': 0, 'shake_enable': 0, 'flicker_enable': 0, 'block_intensity': 150}),
      EffectPronto('Tremido forte', {'shake_x': 80, 'rgb_split': 40, 'interval': 8}),
    ],
  ),
};

double _hash(double a, double b) {
  final s = math.sin(a * 12.9898 + b * 78.233) * 43758.5453;
  return s - s.floorToDouble();
}

double Function(String) _leitor(EffectInstance e, Duration local) => (String k) {
      final p = e.spec.params[k]!;
      final bruto = e.paramAt(k, local);
      if (!bruto.isFinite) return p.initial;
      return bruto.clamp(p.min, p.max).toDouble();
    };

/// 64 floats do `shaders/glitchify.frag` (ordem p0.x..p15.w). Ver a resposta
/// do lote para a tabela; o shader le na mesma ordem.
List<double> valoresGlitchify(EffectInstance e, Duration local) {
  final v = _leitor(e, local);
  final t = local.inMicroseconds / 1e6;
  final seed = v('seed');
  final vel = v('speed') / 50;
  final stG = (t * 10 * vel).floorToDouble();
  final stS = (t * 10 * vel * v('split_speed')).floorToDouble();
  final stC = (t * 10 * vel * v('scale_speed')).floorToDouble();
  final stB = (t * .2 * vel * v('block_speed')).floorToDouble();
  final stT = (t * 2 * vel * v('transform_speed')).floorToDouble();
  final wq = v('wiggle_quantization');
  double w(double k) => ((_hash(stT, seed + k) * 2 - 1) * wq).roundToDouble() / wq;
  final dx = v('position_x') + v('position_wiggle') * w(1);
  final dy = v('position_y') + v('position_wiggle') * w(2);
  final esc = (v('scale') + v('scale_wiggle') * w(3)) / 100;
  final cw = v('crop_wiggle') / 100;
  final tfOn = v('transform_enable') > .5 &&
      (dx != 0 || dy != 0 || esc != 1 || v('transform_opacity') < 100 || v('crop_left') > 0 ||
          v('crop_top') > 0 || v('crop_right') < 100 || v('crop_bottom') < 100 || cw > 0 || v('composite_over') > .5);
  return [
    v('amount') / 100, v('completion') / 100, v('softness') / 100, seed,
    stG, stS, stC, stB,
    dx, dy, esc, v('transform_opacity') / 100,
    v('transform_mode'), v('composite_over'), v('crop_left') / 100 + cw * .01 * w(4).abs(), v('crop_top') / 100 + cw * .01 * w(5).abs(),
    v('crop_right') / 100 - cw * .01 * w(6).abs(), v('crop_bottom') / 100 - cw * .01 * w(7).abs(), tfOn ? 1 : 0, v('channel_enable'),
    v('split_channel'), v('split_horizontal'), v('split_vertical'), v('channel_scale'),
    v('scale_channel'), v('scale_vertical'), v('scale_offset') / 100, v('channel_mode'),
    v('color_enable'), v('color_offset') / 100, v('color_amount') / 100, v('color_opacity') / 100,
    v('color_mode'), v('image_enable'), v('pixel_streak') / 100, v('vertical_sort'),
    v('slice_amount') / 100, v('slice_position') / 100, v('slice_opacity') / 100, v('slice_mode'),
    v('block_amount') / 100, v('block_width'), v('block_height'), v('block_direction'),
    v('block_mode'), v('compression_enable'), v('compression_multiplier') / 100, v('dither_amount') / 100,
    v('jpeg_amount') / 100, v('noise_gain'), v('lacunarity'), v('octaves').roundToDouble(),
    v('repeat_edge'), v('max_random_shift') / 100, v('slice_quantization').roundToDouble(), v('grouping_factor'),
    v('noise_cutoff') / 100, v('use_noise'), v('slice_quality'), v('scale_quantization').roundToDouble(),
    v('fill_gaps'), (t * 30).floorToDouble(), 0, 0,
  ];
}

/// Envelope de um operador do Twitch: tiques em janelas aleatorias.
/// Devolve (forca 0..1, sorteio -1..1, sorteio 2 -1..1).
(double, double, double) _tique(double t, double taxa, double seed, double op, double suave) {
  if (taxa <= 0) return (0, 0, 0);
  final x = t * taxa + _hash(seed, op);
  final n = x.floorToDouble();
  final f = x - n;
  if (_hash(n, seed + op * 7) > .6) return (0, 0, 0);
  const dur = .4;
  if (f > dur) return (0, 0, 0);
  final y = f / dur;
  final e = 1 + (math.sin(math.pi * y) - 1) * suave;
  final mag = .4 + .6 * _hash(n + .5, seed + op * 13);
  return (e * mag, _hash(n + .25, seed + op * 17) * 2 - 1, _hash(n + .75, seed + op * 19) * 2 - 1);
}

/// 64 floats do `shaders/twitch.frag`.
List<double> valoresTwitch(EffectInstance e, Duration local) {
  final v = _leitor(e, local);
  final t = local.inMicroseconds / 1e6;
  final seed = v('seed');
  final mestre = v('amount') / 100;
  final suave = v('behavior') / 100;
  double taxa(String k) => v('speed') / 50 * v(k) / 50 * 2;

  var blur = 0.0;
  if (v('blur_enable') > .5) blur = _tique(t, taxa('blur_speed'), seed, 1, suave).$1 * mestre * v('blur_amount') / 100 * 40;
  var luz = 0.0;
  if (v('light_enable') > .5) {
    final (en, r, _) = _tique(t, taxa('light_speed'), seed, 2, suave);
    final b = v('light_behavior');
    final sinal = b < .5 ? 1.0 : b < 1.5 ? -1.0 : (r >= 0 ? 1.0 : -1.0);
    luz = en * mestre * v('light_amount') / 100 * 1.5 * sinal;
  }
  var escala = 0.0, ox = v('scale_origin_x') / 100, oy = v('scale_origin_y') / 100;
  if (v('scale_enable') > .5) {
    final (en, r1, r2) = _tique(t, taxa('scale_speed'), seed, 3, suave);
    escala = en * mestre * v('scale_amount') / 100 * .5;
    final ra = v('origin_random') / 100 * .5;
    ox = (ox + r1 * ra).clamp(0.0, 1.0);
    oy = (oy + r2 * ra).clamp(0.0, 1.0);
  }
  var sx = 0.0, sy = 0.0, split = 0.0;
  if (v('slide_enable') > .5) {
    final (en, r1, r2) = _tique(t, taxa('slide_speed'), seed, 4, suave);
    final esp = v('slide_spread') / 100;
    final tend = v('slide_tendency') / 100;
    final d = (r1 * esp + (1 - esp) * (r1 >= 0 ? 1 : -1) + tend).clamp(-1.0, 1.0);
    final m = en * mestre * v('slide_amount') / 100 * .25;
    final dir = v('slide_direction');
    if (dir < .5) {
      sx = m * d;
    } else if (dir < 1.5) {
      sy = m * d;
    } else {
      sx = m * d;
      sy = m * r2;
    }
    split = (en > 0 ? 1.0 : 0.0) * mestre * v('rgb_split') / 100 * 30 * (.5 + en);
  }
  var cor = 0.0;
  var tint = [1.0, 1.0, 1.0];
  if (v('color_enable') > .5) {
    final (en, r1, _) = _tique(t, taxa('color_speed'), seed, 5, suave);
    cor = en * mestre * v('color_amount') / 100;
    final hue = (r1 * .5 + .5) * v('color_random') / 100;
    tint = [
      for (final k in [0.0, 2 / 3, 1 / 3]) ((((hue + k) % 1) * 6 - 3).abs() - 1).clamp(0.0, 1.0),
    ];
  }
  return [
    mestre, seed, v('border'), (t * 30).floorToDouble(),
    blur, v('blur_opacity') / 100, v('blur_mode'), v('blur_boost') / 100,
    luz, escala, ox, oy,
    sx, sy, v('motion_blur') / 100, split,
    cor, tint[0], tint[1], tint[2],
    for (var i = 0; i < 44; i++) 0,
  ];
}

/// Envelope do Cross Glitch no quadro [q]: (indice do glitch, fracao 0..1 da
/// vida dele, fator de intensidade) ou fracao < 0 fora de um glitch.
(double, double, double) _glitchCruzado(double q, double seed, double intervalo, double rInt, double dur,
    double rDur, double rInten, bool comecaZero) {
  var inicio = comecaZero ? 0.0 : intervalo * _hash(seed, 1);
  for (var k = 0; k < 20000; k++) {
    final passo = math.max(1.0, intervalo * (1 + rInt * (_hash(k.toDouble(), seed + 2) - .5) * 2));
    final vida = math.max(1.0, passo * dur * (1 + rDur * (_hash(k.toDouble(), seed + 3) - .5)));
    if (q < inicio) return (k.toDouble(), -1, 0);
    if (q < inicio + vida) {
      return (k.toDouble(), (q - inicio) / vida, 1 - rInten * _hash(k.toDouble(), seed + 4));
    }
    inicio += passo;
  }
  return (0, -1, 0);
}

double _pico(double u, double pico) {
  if (u < 0) return 0;
  final p = pico.clamp(.001, .999);
  return u < p ? u / p : (1 - u) / (1 - p);
}

/// 64 floats do `shaders/cross_glitch.frag`; cor de fundo em c0.
List<double> valoresCrossGlitch(EffectInstance e, Duration local) {
  final v = _leitor(e, local);
  final t = local.inMicroseconds / 1e6;
  final q = (t * 30).floorToDouble();
  final seed = v('seed');
  final (k, u, fator) = _glitchCruzado(q, seed, v('interval'), v('interval_random') / 100, v('duration') / 100,
      v('duration_random') / 100, v('intensity_random') / 100, v('start_at_zero') > .5);
  final mestre = u < 0 ? 0.0 : v('intensity') / 100 * fator;
  double grupo(String on, String inten, String pico) =>
      v(on) > .5 ? v(inten) / 100 * _pico(u, pico.isEmpty ? .5 : v(pico) / 100) : 0;
  final bl = grupo('block_enable', 'block_intensity', 'block_peak');
  final sh = grupo('shift_enable', 'shift_intensity', 'shift_peak');
  final sk = grupo('shake_enable', 'shake_intensity', 'shake_peak') * mestre;
  final fl = grupo('flicker_enable', 'flicker_intensity', '') * mestre;
  double r(double s) => _hash(q, seed + s) * 2 - 1;
  final brilho = 1 + fl * (v('brightness') / 100 * r(21) + v('brightness_offset') / 100);
  final sat = math.max(0.0, 1 + fl * (v('saturation') / 100 * r(22) + v('saturation_offset') / 100));
  const tamanhos = [8.0, 16.0, 32.0, 64.0, 128.0];
  return [
    mestre, seed, v('edge'), q,
    bl, tamanhos[v('block_size').round().clamp(0, 4)], v('block_run') / 100, v('block_saturation') / 100,
    v('pattern_amount') / 100, v('pattern_complexity') / 100, v('pattern_opacity') / 100, v('vary_pattern_color') / 100,
    sh, v('line_duplication') / 100, v('shift_amount') / 100, v('shift_density') / 100,
    v('shift_run') / 100, v('skew') / 100, v('jitter') / 100, v('line_drop') / 100,
    v('line_drop_density') / 100, v('shake_x') * sk * r(23), v('shake_y') * sk * r(24), v('rgb_split') * sk,
    v('rotate') * math.pi / 180 * sk * r(25), v('shake_skew') / 100 * sk * r(26), brilho, sat,
    v('use_background'), k * 3 + q * .0, k * 5 + (q / 2).floorToDouble(), 0,
    for (var i = 0; i < 32; i++) 0,
  ];
}
