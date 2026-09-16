import 'dart:math' as math;

import 'effect.dart';

/// S_Shake e S_DissolveShake (Boris FX Sapphire), ABA DISTORCER (16/09).
///
/// Nomes, faixas e padroes LIDOS do After Effects 2026. O tremor por quadro
/// (ruido suave no tempo por eixo, onda regular, estilos Normal / Tremido /
/// Saltos, canais R/G/B com amplitude, fase e aleatoriedade proprias) sai
/// PRONTO daqui: o `shaders/shake.frag` so reamostra a imagem com a
/// translacao, a escala (Z) e a rotacao (Tilt) de cada canal, com as bordas
/// Nenhum / Repetir / Espelhar e o borrao de movimento entre as duas pontas
/// do obturador.
///
/// Medido no AE (padroes, imagem 736): em 0,5 s a imagem anda (-63, -31) px
/// e em 1,5 s (-62, +40) px, ou seja ~1/3 da amplitude (192 x 96). O desenho
/// exato do ruido do Sapphire nao se copia; a escala do movimento sim.
const efeitosShake = <EffectType, EffectSpec>{
  EffectType.tremor: EffectSpec(
    id: 's_shake',
    name: 'S_Shake',
    category: 'Distort',
    procedural: true,
    cost: 2,
    synonyms: ['shake', 's_shake', 'tremor', 'tremer', 'camera shake', 'camera na mao', 'handheld', 'terremoto', 'sapphire'],
    params: {
      'style': EffectParam('Estilo', 0, 0, 2, kind: ParamKind.choice, options: ['Normal', 'Tremido', 'Saltos']),
      'amplitude': EffectParam('Amplitude', 1, 0, 100, decimals: 3, dragStep: .005),
      'frequency': EffectParam('Frequência', 8, 0, 200, decimals: 2, dragStep: .02),
      'phase': EffectParam('Fase', 0, -1000, 1000, decimals: 3, dragStep: .005),
      'stillness': EffectParam('Quietude', .7, 0, 1, decimals: 3, dragStep: .002),
      'twitch_frequency': EffectParam('Frequência dos trancos', 2, 0, 100, decimals: 2, dragStep: .01),
      'drift': EffectParam('Deriva', .3, 0, 1, decimals: 3, dragStep: .002),
      'center_bias': EffectParam('Tendência ao centro', 0, 0, 1, decimals: 3, dragStep: .002),
      'z_dist': EffectParam('Distância Z', 1, .001, 100, decimals: 3, dragStep: .002),
      'motion_blur': EffectParam('Borrão de movimento', 0, 0, 1, kind: ParamKind.toggle),
      'mo_blur_length': EffectParam('Comprimento do borrão', 1, 0, 10, decimals: 3, dragStep: .005),
      'seed': EffectParam('Semente', 0, 0, 32767, kind: ParamKind.seed),
      'wrap_x': EffectParam('Borda X', 2, 0, 2, kind: ParamKind.choice, options: ['Nenhuma', 'Repetir', 'Espelhar']),
      'wrap_y': EffectParam('Borda Y', 2, 0, 2, kind: ParamKind.choice, options: ['Nenhuma', 'Repetir', 'Espelhar']),
      ..._eixos,
      ..._canais,
    },
    montar: ['amplitude', 'frequency', 'style'],
    presets: [
      EffectPronto('Câmera na mão', {'amplitude': .4, 'frequency': 3}),
      EffectPronto('Trancos', {'style': 1, 'amplitude': 1.2, 'frequency': 14}),
      EffectPronto('Terremoto RGB', {'amplitude': 1.5, 'frequency': 12, 'red_amplitude': 1.3, 'blue_amplitude': .7, 'motion_blur': 1}),
    ],
  ),
  EffectType.dissolveShake: EffectSpec(
    id: 's_dissolve_shake',
    name: 'S_DissolveShake',
    category: 'Distort',
    procedural: true,
    cost: 2,
    synonyms: ['dissolve shake', 's_dissolveshake', 'dissolver tremendo', 'transicao', 'tremor', 'sumir', 'sapphire'],
    params: {
      'transition_dir': EffectParam('Direção', 0, 0, 1, kind: ParamKind.choice, options: ['Sumir', 'Surgir']),
      'dissolve_percent': EffectParam('Dissolver', 0, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'dissolve_speed': EffectParam('Velocidade do dissolver', 3, 1, 100, decimals: 2, dragStep: .01),
      'amplitude': EffectParam('Amplitude', 3, 0, 100, decimals: 3, dragStep: .005),
      'frequency': EffectParam('Frequência', 10, 0, 200, decimals: 2, dragStep: .02),
      'motion_blur': EffectParam('Borrão de movimento', 1, 0, 1, kind: ParamKind.toggle),
      'mo_blur_length': EffectParam('Comprimento do borrão', .5, 0, 10, decimals: 3, dragStep: .005),
      'seed': EffectParam('Semente', 0, 0, 32767, kind: ParamKind.seed),
      'wrap_x': EffectParam('Borda X', 2, 0, 2, kind: ParamKind.choice, options: ['Nenhuma', 'Repetir', 'Espelhar']),
      'wrap_y': EffectParam('Borda Y', 2, 0, 2, kind: ParamKind.choice, options: ['Nenhuma', 'Repetir', 'Espelhar']),
      ..._eixos,
      ..._canais,
    },
    montar: ['dissolve_percent', 'amplitude', 'transition_dir'],
    presets: [
      EffectPronto('Sumir tremendo', {'dissolve_percent': 50}),
      EffectPronto('Surgir tremendo', {'transition_dir': 1, 'dissolve_percent': 50}),
      EffectPronto('Dissolver suave', {'dissolve_percent': 50, 'amplitude': 1, 'dissolve_speed': 1.5}),
    ],
  ),
};

const _eixos = <String, EffectParam>{
  'x_rand_amp': EffectParam('X aleatório', 192, 0, 5000, decimals: 1, dragStep: .5),
  'x_rand_freq': EffectParam('Frequência X aleatória', 1, 0, 100, decimals: 3, dragStep: .005),
  'x_wave_amp': EffectParam('X em onda', 0, 0, 5000, decimals: 1, dragStep: .5),
  'x_wave_freq': EffectParam('Frequência X da onda', .5, 0, 100, decimals: 3, dragStep: .005),
  'x_phase': EffectParam('Fase X', 0, -1000, 1000, decimals: 3, dragStep: .005),
  'y_rand_amp': EffectParam('Y aleatório', 96, 0, 5000, decimals: 1, dragStep: .5),
  'y_rand_freq': EffectParam('Frequência Y aleatória', 1, 0, 100, decimals: 3, dragStep: .005),
  'y_wave_amp': EffectParam('Y em onda', 0, 0, 5000, decimals: 1, dragStep: .5),
  'y_wave_freq': EffectParam('Frequência Y da onda', .5, 0, 100, decimals: 3, dragStep: .005),
  'y_phase': EffectParam('Fase Y', 0, -1000, 1000, decimals: 3, dragStep: .005),
  'z_rand_amp': EffectParam('Zoom aleatório', 0, 0, 10, decimals: 3, dragStep: .002),
  'z_rand_freq': EffectParam('Frequência Z aleatória', 1, 0, 100, decimals: 3, dragStep: .005),
  'z_wave_amp': EffectParam('Zoom em onda', 0, 0, 10, decimals: 3, dragStep: .002),
  'z_wave_freq': EffectParam('Frequência Z da onda', .5, 0, 100, decimals: 3, dragStep: .005),
  'z_phase': EffectParam('Fase Z', 0, -1000, 1000, decimals: 3, dragStep: .005),
  'tilt_rand_amp': EffectParam('Inclinação aleatória', 0, 0, 360, unit: '°', decimals: 2, dragStep: .05),
  'tilt_rand_freq': EffectParam('Frequência da inclinação aleatória', 1, 0, 100, decimals: 3, dragStep: .005),
  'tilt_wave_amp': EffectParam('Inclinação em onda', 0, 0, 360, unit: '°', decimals: 2, dragStep: .05),
  'tilt_wave_freq': EffectParam('Frequência da inclinação em onda', .5, 0, 100, decimals: 3, dragStep: .005),
  'tilt_phase': EffectParam('Fase da inclinação', 0, -1000, 1000, decimals: 3, dragStep: .005),
};

const _canais = <String, EffectParam>{
  'red_amplitude': EffectParam('Amplitude do vermelho', 1, 0, 10, decimals: 3, dragStep: .005),
  'green_amplitude': EffectParam('Amplitude do verde', 1, 0, 10, decimals: 3, dragStep: .005),
  'blue_amplitude': EffectParam('Amplitude do azul', 1, 0, 10, decimals: 3, dragStep: .005),
  'red_phase': EffectParam('Fase do vermelho', 0, -1000, 1000, decimals: 3, dragStep: .005),
  'green_phase': EffectParam('Fase do verde', 0, -1000, 1000, decimals: 3, dragStep: .005),
  'blue_phase': EffectParam('Fase do azul', 0, -1000, 1000, decimals: 3, dragStep: .005),
  'rgb_randomness': EffectParam('Aleatoriedade RGB', 0, 0, 10, decimals: 3, dragStep: .005),
  'rgb_frequency': EffectParam('Frequência RGB', 2, 0, 100, decimals: 3, dragStep: .005),
};

double _hash(double a, double b) {
  final s = math.sin(a * 12.9898 + b * 78.233) * 43758.5453;
  return s - s.floorToDouble();
}

/// Ruido suave no tempo, -1..1 (duas oitavas, interpolacao quintica).
double _ruido(double x, double semente) {
  double oit(double x, double s) {
    final i = x.floorToDouble();
    final f = x - i;
    final u = f * f * f * (f * (f * 6 - 15) + 10);
    final a = _hash(i, s) * 2 - 1, b = _hash(i + 1, s) * 2 - 1;
    return a + (b - a) * u;
  }

  // .45: no AE o tremor tipico fica em ~1/3 da amplitude (63/192, 31/96, 40/96).
  return .45 * (oit(x, semente) * .75 + oit(x * 2.13 + 17.1, semente + 7.7) * .35) / 1.1;
}

/// Leitor de parametro com trava na faixa.
double Function(String) _leitor(EffectInstance e, Duration local) => (k) {
  final p = efeitosShake[e.type]!.params[k]!;
  final bruto = e.paramAt(k, local);
  if (!bruto.isFinite) return p.initial;
  return bruto.clamp(p.min, p.max).toDouble();
};

/// Deslocamento cru de um eixo no instante [t] (s): ruido + onda.
/// `eixo` separa as sementes (0 X, 1 Y, 2 Z, 3 Tilt).
List<double> _tremor(
  double Function(String) v,
  double t, {
  required double estilo,
  required double freq,
  required double semente,
  required double ampCanal,
  required double faseCanal,
  required double rgbAleat,
  required double rgbFreq,
  required int canal,
}) {
  const nomes = ['x', 'y', 'z', 'tilt'];
  final tc = t + faseCanal / math.max(freq, .001);
  // Envelope do estilo Tremido: fica parado em `stillness` do tempo.
  var envelope = 1.0;
  var tempoRuido = tc;
  if (estilo == 1) {
    final s = v('stillness');
    final n = _ruido(tc * v('twitch_frequency') * .5, semente + 91) * .5 + .5;
    envelope = ((n - s) / .08).clamp(0.0, 1.0);
  } else if (estilo == 2) {
    // Saltos: posicao nova a cada salto, deriva lenta entre eles.
    final taxa = math.max(freq * .25, .001);
    final k = (tc * taxa).floorToDouble();
    final volta = _hash(k, semente + 55) < v('center_bias');
    final deriva = (tc * taxa - k) * v('drift');
    tempoRuido = (volta ? 0 : k * 3.17) + deriva;
    // volta ao centro: o salto zera e so a deriva afasta aos poucos
    if (volta) envelope = deriva.clamp(0.0, 1.0);
  }
  final out = <double>[];
  for (var ei = 0; ei < 4; ei++) {
    final n = nomes[ei];
    final fase = v('${n}_phase');
    final taxa = freq * v('${n}_rand_freq') * .25;
    final base = estilo == 2 ? tempoRuido + fase * taxa : (tempoRuido + fase) * taxa;
    var r = _ruido(base, semente + ei * 13.1) * v('${n}_rand_amp');
    if (rgbAleat > 0) {
      r += rgbAleat * .5 * v('${n}_rand_amp') * _ruido((tc + fase) * rgbFreq, semente + 31.7 * (canal + 1) + ei);
    }
    final w = v('${n}_wave_amp') * math.sin(2 * math.pi * v('${n}_wave_freq') * (tc + fase));
    out.add((r * envelope + w) * ampCanal);
  }
  return out;
}

/// Transformacao pronta de um canal: [tx px AE, ty px AE, escala, angulo rad].
List<double> _transf(List<double> d, double amp, double zDist) {
  final z = math.max(zDist + d[2] * amp, .05);
  return [d[0] * amp, d[1] * amp, 1 / z, d[3] * amp * math.pi / 180];
}

List<double> _pacote(
  double Function(String) v,
  double t, {
  required double amp,
  required double estilo,
  required double freq,
  required double zDist,
  required double opacidade,
}) {
  final semente = v('seed');
  final blur = v('motion_blur') > .5 ? v('mo_blur_length') : 0.0;
  final tA = t - blur / 60, tB = t + blur / 60;
  const canais = ['red', 'green', 'blue'];
  final amps = [for (final c in canais) v('${c}_amplitude')];
  final fases = [for (final c in canais) v('${c}_phase')];
  final rgbAleat = v('rgb_randomness');
  final mono = amps[0] == amps[1] && amps[1] == amps[2] && fases[0] == fases[1] && fases[1] == fases[2] && rgbAleat == 0;
  List<double> ponta(double tt) => [
    for (var c = 0; c < 3; c++)
      ..._transf(
        _tremor(v, tt, estilo: estilo, freq: freq, semente: semente, ampCanal: amps[c], faseCanal: fases[c], rgbAleat: rgbAleat, rgbFreq: v('rgb_frequency'), canal: c),
        amp,
        zDist,
      ),
  ];
  return [
    ...ponta(tA), // p0..p2: R, G, B no inicio do obturador
    ...ponta(tB), // p3..p5: R, G, B no fim
    v('wrap_x'), v('wrap_y'), blur > 0 ? (mono ? 8 : 5) : 1, mono ? 1 : 0, // p6
    opacidade, 0, 0, 0, // p7
  ];
}

/// Floats de p0..p7 do `shaders/shake.frag` (32) para o S_Shake.
List<double> valoresShake(EffectInstance e, Duration local) {
  final v = _leitor(e, local);
  final t = local.inMicroseconds / 1e6;
  return _pacote(
    v,
    t + v('phase'),
    amp: v('amplitude'),
    estilo: v('style').roundToDouble(),
    freq: v('frequency'),
    zDist: v('z_dist'),
    opacidade: 1,
  );
}

/// Floats de p0..p7 do `shaders/shake.frag` (32) para o S_DissolveShake:
/// o tremor cresce com o Dissolver e a camada some (ou surge) no meio,
/// tanto mais rapido quanto maior a Velocidade.
List<double> valoresDissolveShake(EffectInstance e, Duration local) {
  final v = _leitor(e, local);
  final t = local.inMicroseconds / 1e6;
  final f = v('dissolve_percent') / 100;
  final surgir = v('transition_dir') > .5;
  final andamento = surgir ? 1 - f : f; // 0 = camera parada e camada inteira
  final some = ((andamento - .5) * v('dissolve_speed') + .5).clamp(0.0, 1.0);
  final envelope = math.sin(math.min(andamento, 1.0) * math.pi / 2);
  return _pacote(
    v,
    t,
    amp: v('amplitude') * envelope,
    estilo: 0,
    freq: v('frequency'),
    zDist: 1,
    opacidade: 1 - some,
  );
}
