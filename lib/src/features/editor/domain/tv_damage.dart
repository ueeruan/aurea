import 'dart:math' as math;
import 'dart:ui';

import 'effect.dart';

/// S_TVDamage (Boris FX Sapphire), ABA ESTILIZAR, LOTE 2 (16/09).
///
/// Nomes, faixas e padroes LIDOS do After Effects 2026 do dono. As contas
/// sairam de renders do plugin original, um componente por vez, sobre
/// imagens de teste (cinza, preto, branco, rampa, linha de 1 px):
///
///   Estatico      campo B-spline de ruido uniforme por celula de TV
///                 (~largura/TvPixels), zona morta 1 - sqrt(densidade) e
///                 ganho 1/densidade, canal a canal, novo a cada quadro
///   Interferencia pontos de ~3,5 x 2,9 px de TV numa rede de varredura:
///                 tau = linha + x/largura, um ponto a cada 1/Frequencia;
///                 linha = 2,88 px de TV; cor aleatoria +-1 por ponto;
///                 Dots Speed X em 1/200 de largura por s, Y em linhas/s
///   Fantasmas     (img + soma h_i img_i) / max(1 + soma h_i, 1), com
///                 h = amp g (positivo) ou -amp g/2 (negativo); posicoes
///                 Spacing * largura * (i/(n-1) - (1-Shift)/2)
///   H hold        deslocamento por linha (fbm), periodo largura + borda,
///                 borda preta = Border Width * largura / 2
///   V hold        episodios aleatorios de rolagem (~255 px/s a V Speed 2
///                 numa imagem de 736), periodo altura (1 + Border Height)
///   Barras        s = .5 - .75(1-2w) + nitidez (cos - (1-2w)), barra 2
///                 multiplica por 2 clamp(.5 + nitidez2 cos), sinal
///                 (2/3) s1 s2 - 1/3, periodo altura / (2 freq)
///   Listras       x + A sen(fase, fase -+ 120 graus) min(x, 1-x),
///                 2 x Color Frequency ciclos por largura
///   Linhas        x + A sen(2 pi y / (7,2 px de TV)) min(x, 1-x)
///   Avanco rapido faixas de 75 % de Band Height com rasgo 0,095 largura
///   Dropouts      mistura para o branco em riscos por faixa de linhas
///
/// O padrao aleatorio do Sapphire nao se copia pixel a pixel: densidade,
/// tamanho, cor e intensidade sim. O desenho roda em
/// `shaders/tv_damage.frag`, numa passada.
const efeitosTvDamage = <EffectType, EffectSpec>{
  EffectType.tvDamage: EffectSpec(
    id: 's_tv_damage',
    name: 'S_TVDamage',
    category: 'Stylize',
    procedural: true,
    cost: 3,
    hasColor: true,
    defaultColor: Color(0xFFFFFFFF),
    extraColors: 1,
    defaultExtraColors: [Color(0xFF000000)],
    colorLabels: ['Tingir luzes', 'Tingir sombras'],
    synonyms: ['tv damage', 'tv', 'televisao', 'chuvisco', 'estatico', 'vhs', 'analogico', 'crt', 'interferencia', 'sapphire'],
    params: {
      'reception_master': EffectParam('Recepção geral', .4, 0, 10, decimals: 3, dragStep: .002),
      'interference_amp': EffectParam('Interferência', .6, 0, 10, decimals: 3, dragStep: .002),
      'ghost_amp': EffectParam('Fantasmas', .6, 0, 10, decimals: 3, dragStep: .002),
      'horizontal_hold': EffectParam('Sincronismo horizontal', .5, 0, 10, decimals: 3, dragStep: .002),
      'vertical_hold': EffectParam('Sincronismo vertical', .8, 0, 10, decimals: 3, dragStep: .002),
      'bars_brightness': EffectParam('Barras', .3, 0, 10, decimals: 3, dragStep: .002),
      'color_stripes': EffectParam('Listras de cor', .2, 0, 10, decimals: 3, dragStep: .002),
      'fast_forward': EffectParam('Avanço rápido', 0, 0, 10, decimals: 3, dragStep: .002),
      'tape_dropout': EffectParam('Falhas de fita', 0, 0, 10, decimals: 3, dragStep: .002),
      'vignette_darkness': EffectParam('Vinheta', 0, 0, 1, decimals: 3, dragStep: .002),
      'static_amplitude': EffectParam('Estático', .8, 0, 10, decimals: 3, dragStep: .002),
      'static_density': EffectParam('Densidade do estático', .7, .01, 1, decimals: 3, dragStep: .002),
      'frequency': EffectParam('Frequência da interferência', 1.275, 0, 500, decimals: 3, dragStep: .001),
      'dots_speed_x': EffectParam('Velocidade dos pontos X', 100, -1000, 1000, decimals: 1, dragStep: .5),
      'dots_speed_y': EffectParam('Velocidade dos pontos Y', -10, -1000, 1000, decimals: 1, dragStep: .5),
      'jitter_amount': EffectParam('Tremulação', 10, 0, 1000, decimals: 1, dragStep: .2),
      'num_ghosts': EffectParam('Número de fantasmas', 5, 0, 8, decimals: 0, dragStep: .05),
      'negative_ghosts': EffectParam('Fantasmas negativos', .5, 0, 1, decimals: 3, dragStep: .002),
      'spacing': EffectParam('Espaçamento', .2, 0, 2, decimals: 3, dragStep: .002),
      'vary_position': EffectParam('Variar posição', .3, 0, 1, decimals: 3, dragStep: .002),
      'shift': EffectParam('Deslocar fantasmas', .5, -1, 1, decimals: 3, dragStep: .002),
      'ghost_blur': EffectParam('Desfocar fantasmas', 0, 0, 500, decimals: 1, dragStep: .2),
      'h_frequency': EffectParam('Frequência H', 1.25, 0, 50, decimals: 3, dragStep: .005),
      'h_time_vary': EffectParam('Variação no tempo H', .5, 0, 10, decimals: 3, dragStep: .002),
      'h_octaves': EffectParam('Oitavas H', 3, 1, 6, decimals: 0, dragStep: .05),
      'border_width': EffectParam('Largura da borda', .05, 0, 2, decimals: 3, dragStep: .001),
      'v_frequency': EffectParam('Frequência V', 2, 0, 50, decimals: 3, dragStep: .005),
      'v_speed': EffectParam('Velocidade V', 2, 0, 50, decimals: 3, dragStep: .005),
      'v_random': EffectParam('Aleatoriedade V', .1, 0, 10, decimals: 3, dragStep: .002),
      'border_height': EffectParam('Altura da borda', .1, 0, 2, decimals: 3, dragStep: .001),
      'border_data': EffectParam('Dados na borda', 1, 0, 10, decimals: 3, dragStep: .005),
      'bar_roll_speed': EffectParam('Rolagem das barras', .5, -10, 10, decimals: 3, dragStep: .005),
      'bar_sharpness': EffectParam('Nitidez das barras', .5, .1, 10, decimals: 3, dragStep: .005),
      'bar_frequency': EffectParam('Frequência das barras', 1, .1, 50, decimals: 3, dragStep: .005),
      'bar1_width': EffectParam('Largura da barra 1', .35, 0, 1, decimals: 3, dragStep: .002),
      'bar2_rel_frequency': EffectParam('Frequência relativa da barra 2', 6, 1, 100, decimals: 2, dragStep: .02),
      'bar2_sharpness': EffectParam('Nitidez da barra 2', .5, .01, 10, decimals: 3, dragStep: .005),
      'color_frequency': EffectParam('Frequência das listras', 10, 1, 40, decimals: 2, dragStep: .02),
      'color_angle': EffectParam('Ângulo das listras', -160, -360, 360, unit: '°', decimals: 1, dragStep: .2),
      'roll_speed': EffectParam('Rolagem das listras', 3, 0, 100, decimals: 3, dragStep: .005),
      'band_frequency': EffectParam('Frequência das faixas', 4, 0, 50, decimals: 3, dragStep: .005),
      'band_shift': EffectParam('Deslocar faixas', .1, 0, 10, decimals: 3, dragStep: .002),
      'band_height': EffectParam('Altura das faixas', .16, 0, 1, decimals: 3, dragStep: .002),
      'dropout_length': EffectParam('Comprimento das falhas', .25, 0, 1, decimals: 3, dragStep: .002),
      'dropout_gap_length': EffectParam('Intervalo das falhas', .2, 0, 2, decimals: 3, dragStep: .002),
      'dropout_y_freq': EffectParam('Frequência Y das falhas', 5, 0, 50, decimals: 2, dragStep: .02),
      'dropout_y_threshold': EffectParam('Limiar Y das falhas', .75, 0, 1, decimals: 3, dragStep: .002),
      'dropouts_always': EffectParam('Frequência das falhas no tempo', 1, 0, 1, decimals: 3, dragStep: .002),
      'vignette_radius': EffectParam('Raio da vinheta', 1, 0, 10, decimals: 3, dragStep: .005),
      'vignette_edge_softness': EffectParam('Suavidade da vinheta', .5, 0, 10, decimals: 3, dragStep: .005),
      'vignette_rel_height': EffectParam('Altura relativa da vinheta', .75, .1, 10, decimals: 3, dragStep: .005),
      'scanlines': EffectParam('Linhas de varredura', .1, 0, 10, decimals: 3, dragStep: .002),
      'scanlines_rel_freq': EffectParam('Frequência das linhas', 1, 0, 50, decimals: 3, dragStep: .005),
      'orthicon': EffectParam('Orthicon', 0, 0, 10, decimals: 3, dragStep: .002),
      'threshold': EffectParam('Limiar do orthicon', .7, 0, 10, decimals: 3, dragStep: .002),
      'darks_width': EffectParam('Largura do escuro', .2, 0, 10, decimals: 3, dragStep: .002),
      'hue_shift': EffectParam('Girar matiz', 0, -360, 360, unit: '°', decimals: 1, dragStep: .2),
      'saturation': EffectParam('Saturação', 1, -10, 10, decimals: 3, dragStep: .005),
      'scale_lights': EffectParam('Escala das luzes', 1, 0, 10, decimals: 3, dragStep: .005),
      'offset_darks': EffectParam('Deslocar sombras', 0, -10, 10, decimals: 3, dragStep: .002),
      'turn_off': EffectParam('Desligar', 0, 0, 1, decimals: 3, dragStep: .002),
      'flare_width': EffectParam('Largura do clarão', 349.09, 0, 5000, decimals: 1, dragStep: .5, relative: true),
      'flare_brightness': EffectParam('Brilho do clarão', 2, 0, 50, decimals: 3, dragStep: .005),
      'fade_out_time': EffectParam('Tempo do apagar', .7, 0, 1, decimals: 3, dragStep: .002),
      'fish_eye': EffectParam('Olho de peixe', 0, -10, 10, decimals: 3, dragStep: .002),
      'tv_pixels': EffectParam('Pixels da TV', 720, 1, 4000, decimals: 0, dragStep: 1),
      'downsample': EffectParam('Reduzir resolução', 1, 0, 1, kind: ParamKind.toggle),
      'seed': EffectParam('Semente', .123, 0, 1000, kind: ParamKind.seed, decimals: 3),
    },
    montar: ['reception_master', 'static_amplitude', 'tv_pixels'],
    presets: [
      EffectPronto('TV velha', {
        'reception_master': .9, 'static_amplitude': .7,
        'vignette_darkness': .35, 'fish_eye': 2, 'tv_pixels': 480,
      }),
      EffectPronto('Sem sinal', {'reception_master': 1.5, 'static_amplitude': 1.2, 'static_density': 1}),
      EffectPronto('Desligando', {'reception_master': .2, 'turn_off': .8, 'vignette_darkness': .6}),
    ],
  ),
};

double _hash(double a, double b) {
  final s = math.sin(a * 12.9898 + b * 78.233) * 43758.5453;
  return s - s.floorToDouble();
}

double _ruidoSuave(double x, double semente) {
  final i = x.floorToDouble();
  final f = x - i;
  final u = f * f * (3 - 2 * f);
  final a = _hash(i, semente), b = _hash(i + 1, semente);
  return a + (b - a) * u; // 0..1
}

/// Os 64 floats de p0..p15 do `shaders/tv_damage.frag`, no instante [local]
/// (segundos da camada). Os valores que so dependem do tempo (episodio do
/// V hold, jitter dos pontos, fase das barras e listras, quadro das falhas)
/// saem daqui prontos.
List<double> valoresTvDamage(EffectInstance e, Duration local) {
  double v(String k) {
    final p = e.spec.params[k]!;
    final bruto = e.paramAt(k, local);
    if (!bruto.isFinite) return p.initial;
    return bruto.clamp(p.min, p.max).toDouble();
  }

  final t = local.inMicroseconds / 1e6;
  final quadro = (t * 30).floorToDouble();
  final rm = v('reception_master');
  final semente = v('seed');

  // Pontos da interferencia: velocidade + tremor por quadro.
  final jit = v('jitter_amount') * .005 * (_hash(quadro, semente + 1) * 2 - 1);
  final offX = v('dots_speed_x') * .005 * t + jit;
  final offY = v('dots_speed_y') * t;

  // H hold: amplitude modulada no tempo.
  final hMod = 1 + v('h_time_vary') * (_ruidoSuave(t * .7, semente + 2) * 2 - 1);
  final hAmp = rm * v('horizontal_hold') * math.max(hMod, 0);

  // V hold: episodios em que o sinal nao trava; dentro deles rola.
  final vh = rm * v('vertical_hold');
  final vFreq = math.max(v('v_frequency'), .01);
  bool ativo(double tt) => _ruidoSuave(tt * .5, semente + 3) > 1 - vh;
  var vOff = 0.0;
  if (vh > 0 && ativo(t)) {
    var inicio = t;
    for (var i = 0; i < 1800; i++) {
      final tt = t - (i + 1) / 30;
      if (tt < 0 || !ativo(tt)) break;
      inicio = tt;
    }
    final dur = t - inicio;
    final x = v('v_speed') * .157 * dur +
        v('v_random') * .3 * (_ruidoSuave(t * vFreq, semente + 4) - .5) +
        .1 * (_ruidoSuave(t * vFreq * .5, semente + 5) - .5).sign * math.min(dur * vFreq, 1);
    vOff = x - x.floorToDouble();
  }

  // Falhas de fita: quadro com ou sem falha.
  final comFalha = _hash(quadro, semente + 6) < v('dropouts_always');
  const graus = math.pi / 180;

  return [
    rm * v('static_amplitude'), v('static_density'), rm * v('interference_amp'), v('frequency'),
    offX, offY, semente, rm * v('ghost_amp'),
    v('num_ghosts').roundToDouble(), v('negative_ghosts'), v('spacing'), v('vary_position'),
    v('shift'), v('ghost_blur'), hAmp, v('h_frequency'),
    v('h_octaves').roundToDouble(), v('border_width'), vOff, v('border_height'),
    v('border_data'), rm * v('bars_brightness'), v('bar_roll_speed') * t, v('bar_sharpness'),
    v('bar_frequency'), v('bar1_width'), v('bar2_rel_frequency'), v('bar2_sharpness'),
    rm * v('color_stripes'), v('color_frequency'), v('color_angle') * graus, v('roll_speed') * t,
    v('fast_forward'), v('band_frequency'), v('band_shift'), v('band_height'),
    comFalha ? v('tape_dropout') : 0, v('dropout_length'), v('dropout_gap_length'), v('dropout_y_freq'),
    v('dropout_y_threshold'), _hash(quadro, semente + 7), v('vignette_darkness'), v('vignette_radius'),
    v('vignette_edge_softness'), v('vignette_rel_height'), v('scanlines'), v('scanlines_rel_freq'),
    v('orthicon'), v('threshold'), v('darks_width'), v('hue_shift') * graus,
    v('saturation'), v('scale_lights'), v('offset_darks'), v('turn_off'),
    v('flare_width'), v('flare_brightness'), v('fade_out_time'), v('fish_eye'),
    v('tv_pixels'), v('downsample'), quadro, 0,
  ];
}
