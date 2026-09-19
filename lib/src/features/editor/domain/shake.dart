import 'dart:math' as math;

import 'effect.dart';

/// O SHAKE, refeito do zero (18/09).
///
/// O S_Shake ANTIGO NAO EXISTE MAIS. Ele era uma copia do plugin de
/// referencia, com 33 parametros — estilo, quietude, trancos, deriva,
/// tendencia ao centro, canais R/G/B com amplitude e fase proprias — e a
/// pessoa tinha de descobrir sozinha qual dos 33 fazia a camera tremer.
/// Nada disso respondia ao que se pede de um tremor: "trema assim".
///
/// O QUE ENTROU NO LUGAR: um sistema com nove numeros GLOBAIS e quatro
/// EIXOS, cada eixo com os mesmos cinco numeros. Nada de estilo, nada de
/// canal de cor.
///
///   GLOBAL   Amplitude, Frequencia, Fase, Semente, Mistura, Borrao de
///            movimento, Comprimento do borrao, Borda X, Borda Y
///   X        Amplitude aleatoria, Frequencia aleatoria,
///   Y        Amplitude da onda, Frequencia da onda, Fase
///   Z
///   TILT
///
/// ONDE CADA EIXO AGE: X e Y deslocam, Z aproxima e afasta (profundidade
/// de camera) e TILT gira. NENHUM deles mexe no transform da camada —
/// Deslocamento, Escala e Rotacao continuam sendo da pessoa, e o tremor
/// entra por cima. E a soma que o dono pediu: transform base + animacao da
/// pessoa + transform do tremor.
///
/// POR QUE O TEMPO E O DA TIMELINE, E NAO O QUADRO. Todo o calculo sai de
/// `local.inMicroseconds / 1e6` — segundos. Um tremor a 30 fps e o mesmo
/// tremor a 60 fps, o mesmo na previa, o mesmo ao arrastar o cursor e o
/// mesmo no arquivo exportado, porque nao existe nenhum contador de
/// quadros em lugar nenhum deste arquivo.
///
/// POR QUE NAO HA `rand()`. O ruido e uma funcao pura de (semente, eixo,
/// instante): o mesmo instante com a mesma semente da sempre o mesmo
/// numero. Salvar, fechar e reabrir o projeto devolve exatamente o mesmo
/// tremor — e assar em keyframes da exatamente o que se via na tela.
///
/// O SHADER E QUE FAZ O TRABALHO PESADO. Aqui so se calcula, por instante,
/// seis numeros por eixo: quanto anda em X e em Y, o quanto gira e o
/// quanto cresce. Quem reamostra a imagem, repete a borda e borra o
/// movimento entre as duas pontas do obturador e `shaders/shake.frag`, na
/// GPU — o mesmo desenho que ja estava medido contra a referencia.
const efeitosShake = <EffectType, EffectSpec>{
  EffectType.tremor: EffectSpec(
    // O ID MUDOU DE `s_shake` PARA `advanced_shake`, e o antigo continua
    // sendo lido: `_aliasesDeId` guarda os dois. Projeto salvo com o
    // S_Shake abre com este efeito, e os numeros que os dois tem em comum
    // (amplitude, frequencia, fase, semente, X/Y/Z/TILT) atravessam
    // intactos.
    id: 'advanced_shake',
    name: 'Advanced Shake',
    category: 'Distort',
    procedural: true,
    cost: 2,
    synonyms: [
      'shake', 'tremor', 'tremer', 'advanced shake', 's_shake',
      'camera shake', 'camera na mao', 'handheld', 'terremoto',
      'vibracao', 'sacudida',
    ],
    params: {
      ..._globais,
      ..._eixoX,
      ..._eixoY,
      ..._eixoZ,
      ..._eixoTilt,
    },
    grupos: [
      EffectGrupo('Global', [
        'amplitude', 'frequency', 'phase', 'seed', 'mix',
        'motion_blur', 'mo_blur_length', 'wrap_x', 'wrap_y',
      ]),
      EffectGrupo('X Shake', [
        'x_rand_amp', 'x_rand_freq', 'x_wave_amp', 'x_wave_freq', 'x_phase',
      ]),
      EffectGrupo('Y Shake', [
        'y_rand_amp', 'y_rand_freq', 'y_wave_amp', 'y_wave_freq', 'y_phase',
      ]),
      EffectGrupo('Z Shake', [
        'z_dist', 'z_rand_amp', 'z_rand_freq', 'z_wave_amp', 'z_wave_freq',
        'z_phase',
      ]),
      EffectGrupo('Tilt Shake', [
        'tilt_rand_amp', 'tilt_rand_freq', 'tilt_wave_amp', 'tilt_wave_freq',
        'tilt_phase',
      ]),
    ],
    montar: ['amplitude', 'frequency', 'mo_blur_length'],
    presets: [
      EffectPronto('Câmera na mão sutil', {
        'amplitude': .3, 'frequency': 5,
        'x_rand_amp': 70, 'y_rand_amp': 50, 'z_rand_amp': .002,
        'tilt_rand_amp': .3,
        'motion_blur': 1, 'mo_blur_length': .3,
      }),
      EffectPronto('Câmera na mão média', {
        'amplitude': .7, 'frequency': 7,
        'x_rand_amp': 140, 'y_rand_amp': 90, 'z_rand_amp': .006,
        'tilt_rand_amp': .8,
        'motion_blur': 1, 'mo_blur_length': .5,
      }),
      EffectPronto('Câmera pesada', {
        'amplitude': 1.4, 'frequency': 9,
        'x_rand_amp': 240, 'y_rand_amp': 150, 'z_rand_amp': .015,
        'tilt_rand_amp': 1.8,
        'motion_blur': 1, 'mo_blur_length': .7,
      }),
      EffectPronto('Impacto', {
        'amplitude': 1.2, 'frequency': 3.5,
        'x_rand_amp': 300, 'y_rand_amp': 200, 'z_rand_amp': .03,
        'tilt_rand_amp': 2.5,
        'x_wave_amp': 40, 'x_wave_freq': 1.5,
        'motion_blur': 1, 'mo_blur_length': 1.2,
      }),
      EffectPronto('Terremoto', {
        'amplitude': 1.6, 'frequency': 12,
        'x_rand_amp': 320, 'y_rand_amp': 300, 'z_rand_amp': .025,
        'tilt_rand_amp': 3,
        'motion_blur': 1, 'mo_blur_length': 1,
      }),
      EffectPronto('Vibração', {
        'amplitude': .35, 'frequency': 40,
        'x_rand_amp': 60, 'y_rand_amp': 60,
        'x_rand_freq': 1.6, 'y_rand_freq': 1.6,
      }),
      EffectPronto('Tremor horizontal', {
        'amplitude': 1, 'frequency': 8,
        'x_rand_amp': 300, 'x_wave_amp': 30, 'x_wave_freq': .6,
      }),
      EffectPronto('Tremor vertical', {
        'amplitude': 1, 'frequency': 8,
        'y_rand_amp': 300, 'y_wave_amp': 30, 'y_wave_freq': .45,
      }),
      // SO ONDA, ZERO ALEATORIO: e o unico jeito de o movimento ficar
      // previsivel e liso — o ruido, por definicao, nao e.
      EffectPronto('Flutuação suave', {
        'amplitude': .5, 'frequency': 2,
        'x_rand_amp': 0, 'y_rand_amp': 0,
        'x_wave_amp': 90, 'x_wave_freq': .18,
        'y_wave_amp': 60, 'y_wave_freq': .13,
        'z_wave_amp': .006, 'z_wave_freq': .1,
        'tilt_wave_amp': .5, 'tilt_wave_freq': .07,
        'motion_blur': 1, 'mo_blur_length': .4,
      }),
      EffectPronto('Tranco rápido', {
        'amplitude': .9, 'frequency': 26,
        'x_rand_amp': 180, 'y_rand_amp': 120, 'tilt_rand_amp': 2,
        'x_rand_freq': 2.2, 'y_rand_freq': 1.7, 'tilt_rand_freq': 2,
      }),
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

/// OS NOVE NUMEROS GLOBAIS. Nesta ordem, que e a da ficha e a do grupo.
const _globais = <String, EffectParam>{
  'amplitude': EffectParam('Amplitude', 1, 0, 100, decimals: 3, dragStep: .005),
  'frequency': EffectParam('Frequência', 8, 0, 200, decimals: 2, dragStep: .02),
  'phase': EffectParam('Fase', 0, -1000, 1000, decimals: 3, dragStep: .005),
  'seed': EffectParam('Semente', 0, 0, 32767, kind: ParamKind.seed),
  // MISTURA: 100% e o tremor inteiro, 0% e a camada parada. Serve para
  // dosar o tremor contra a imagem que ja esta na tela — e e o unico
  // parametro que da para animar de 0 a 100 sem tocar em mais nada.
  'mix': EffectParam('Mistura', 100, 0, 100, unit: '%', decimals: 1, dragStep: .2),
  'motion_blur': EffectParam('Borrão de movimento', 0, 0, 1, kind: ParamKind.toggle),
  'mo_blur_length': EffectParam('Comprimento do borrão', 1, 0, 10, decimals: 3, dragStep: .005),
  // A BORDA NUNCA COMECA EM "NENHUMA". Repetir ou espelhar e o que impede
  // a faixa preta na beirada quando o tremor empurra a imagem para fora —
  // "nenhuma" fica para quem quer justamente o vazio.
  'wrap_x': EffectParam('Borda X', 2, 0, 2, kind: ParamKind.choice, options: ['Nenhuma', 'Repetir', 'Espelhar']),
  'wrap_y': EffectParam('Borda Y', 2, 0, 2, kind: ParamKind.choice, options: ['Nenhuma', 'Repetir', 'Espelhar']),
};

const _eixoX = <String, EffectParam>{
  'x_rand_amp': EffectParam('Amplitude aleatória', 192, 0, 5000, decimals: 1, dragStep: .5),
  'x_rand_freq': EffectParam('Frequência aleatória', 1, 0, 100, decimals: 3, dragStep: .005),
  'x_wave_amp': EffectParam('Amplitude da onda', 0, 0, 5000, decimals: 1, dragStep: .5),
  'x_wave_freq': EffectParam('Frequência da onda', .5, 0, 100, decimals: 3, dragStep: .005),
  'x_phase': EffectParam('Fase', 0, -1000, 1000, decimals: 3, dragStep: .005),
};

const _eixoY = <String, EffectParam>{
  'y_rand_amp': EffectParam('Amplitude aleatória', 96, 0, 5000, decimals: 1, dragStep: .5),
  'y_rand_freq': EffectParam('Frequência aleatória', 1, 0, 100, decimals: 3, dragStep: .005),
  'y_wave_amp': EffectParam('Amplitude da onda', 0, 0, 5000, decimals: 1, dragStep: .5),
  'y_wave_freq': EffectParam('Frequência da onda', .5, 0, 100, decimals: 3, dragStep: .005),
  'y_phase': EffectParam('Fase', 0, -1000, 1000, decimals: 3, dragStep: .005),
};

/// Z E PROFUNDIDADE, NAO ESCALA. O numero que a pessoa ve em Escala
/// continua sendo dela: o que o Z faz e mudar a distancia da camera, e a
/// imagem cresce ou encolhe por causa disso — como no mundo real. A
/// [z_dist] e a distancia de repouso; com ela em 1 e o Z em zero, o eixo
/// nao faz nada.
const _eixoZ = <String, EffectParam>{
  'z_dist': EffectParam('Distância Z', 1, .001, 100, decimals: 3, dragStep: .002),
  'z_rand_amp': EffectParam('Zoom aleatório', 0, 0, 10, decimals: 3, dragStep: .002),
  'z_rand_freq': EffectParam('Frequência aleatória', 1, 0, 100, decimals: 3, dragStep: .005),
  'z_wave_amp': EffectParam('Zoom em onda', 0, 0, 10, decimals: 3, dragStep: .002),
  'z_wave_freq': EffectParam('Frequência da onda', .5, 0, 100, decimals: 3, dragStep: .005),
  'z_phase': EffectParam('Fase', 0, -1000, 1000, decimals: 3, dragStep: .005),
};

const _eixoTilt = <String, EffectParam>{
  'tilt_rand_amp': EffectParam('Inclinação aleatória', 0, 0, 360, unit: '°', decimals: 2, dragStep: .05),
  'tilt_rand_freq': EffectParam('Frequência aleatória', 1, 0, 100, decimals: 3, dragStep: .005),
  'tilt_wave_amp': EffectParam('Inclinação em onda', 0, 0, 360, unit: '°', decimals: 2, dragStep: .05),
  'tilt_wave_freq': EffectParam('Frequência da onda', .5, 0, 100, decimals: 3, dragStep: .005),
  'tilt_phase': EffectParam('Fase', 0, -1000, 1000, decimals: 3, dragStep: .005),
};

/// Os quatro eixos, na ordem X, Y, Z, TILT. E o que o S_DissolveShake
/// usa — ele nunca precisou de grupo, so dos numeros.
const _eixos = <String, EffectParam>{
  ..._eixoX,
  ..._eixoY,
  ..._eixoZ,
  ..._eixoTilt,
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
///
/// E UMA FUNCAO PURA: `_ruido(x, s)` com os mesmos dois numeros da sempre
/// o mesmo resultado, nesta versao do app e na proxima. O .45 e a medida
/// do padrao antigo no AE — um tremor tipico fica em ~1/3 da amplitude
/// declarada.
double _ruido(double x, double semente) {
  double oit(double x, double s) {
    final i = x.floorToDouble();
    final f = x - i;
    final u = f * f * f * (f * (f * 6 - 15) + 10);
    final a = _hash(i, s) * 2 - 1, b = _hash(i + 1, s) * 2 - 1;
    return a + (b - a) * u;
  }

  return .45 * (oit(x, semente) * .75 + oit(x * 2.13 + 17.1, semente + 7.7) * .35) / 1.1;
}

/// Leitor de parametro com trava na faixa.
///
/// CHAVE QUE NAO EXISTE NA FICHA NÃO ESTOURA. O S_Shake antigo lia
/// `params[k]!` e derrubava o quadro inteiro por causa de um numero que
/// nao estava mais la — foi assim que "Assar em keyframes" passou a
/// devolver lixo em silencio depois de um renomeio. Chave ausente agora
/// vale zero, que e o neutro de todos os eixos.
double Function(String) _leitor(EffectInstance e, Duration local) => (k) {
  final p = efeitosShake[e.type]?.params[k];
  if (p == null) return 0;
  final bruto = e.paramAt(k, local);
  if (!bruto.isFinite) return p.initial;
  return bruto.clamp(p.min, p.max).toDouble();
};

/// Deslocamento cru de um eixo no instante [t] (s): ruido + onda.
/// `eixo` separa as sementes (0 X, 1 Y, 2 Z, 3 Tilt).
List<double> _deslocamento(
  double Function(String) v,
  double t, {
  required double freq,
  required double semente,
  required double ampCanal,
  required double faseCanal,
  required double rgbAleat,
  required double rgbFreq,
  required int canal,
}) {
  const nomes = ['x', 'y', 'z', 'tilt'];
  final out = <double>[];
  for (var ei = 0; ei < 4; ei++) {
    final n = nomes[ei];
    final fase = v('${n}_phase');
    // A FREQUENCIA MULTIPLICA A ESCALA DE TEMPO, e nao o resultado: assim
    // ela desloca o ruido no tempo em vez de mudar a amplitude — que e o
    // que "tremer mais rapido" quer dizer.
    final taxa = freq * v('${n}_rand_freq') * .25;
    var r = _ruido((t + fase) * taxa, semente + ei * 13.1) * v('${n}_rand_amp');
    if (rgbAleat > 0) {
      r += rgbAleat * .5 * v('${n}_rand_amp') * _ruido((t + fase) * rgbFreq, semente + 31.7 * (canal + 1) + ei);
    }
    final w = v('${n}_wave_amp') * math.sin(2 * math.pi * v('${n}_wave_freq') * (t + fase));
    out.add((r + w) * ampCanal);
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
  required double freq,
  required double zDist,
  required double opacidade,
  required double mistura,
  List<double> amplitudes = const [1, 1, 1],
  List<double> fases = const [0, 0, 0],
  double rgbAleat = 0,
  double rgbFreq = 2,
}) {
  final semente = v('seed');
  final blur = v('motion_blur') > .5 ? v('mo_blur_length') : 0.0;
  // AS DUAS PONTAS DO OBTURADOR. O borrao de movimento do tremor nao e um
  // desfoque generico: sao a posicao no comeco e no fim do tempo de
  // obturaco, e o shader reamostra ao longo das duas. Um tremor rapido sai
  // borrado no sentido em que se move — e so nele.
  final tA = t - blur / 60, tB = t + blur / 60;
  final mono = amplitudes[0] == amplitudes[1] && amplitudes[1] == amplitudes[2] &&
      fases[0] == fases[1] && fases[1] == fases[2] && rgbAleat == 0;
  List<double> ponta(double tt) => [
    for (var c = 0; c < 3; c++)
      ..._transf(
        _deslocamento(
          v, tt,
          freq: freq, semente: semente,
          ampCanal: amplitudes[c], faseCanal: fases[c],
          rgbAleat: rgbAleat, rgbFreq: rgbFreq, canal: c,
        ),
        amp,
        zDist,
      ),
  ];
  return [
    ...ponta(tA), // p0..p2: R, G, B no inicio do obturador
    ...ponta(tB), // p3..p5: R, G, B no fim
    v('wrap_x'), v('wrap_y'), blur > 0 ? (mono ? 8 : 5) : 1, mono ? 1 : 0, // p6
    // p7: opacidade, MISTURA, -, -. A mistura e lida pelo shader contra a
    // imagem sem tremor, entao 0% e a camada intacta.
    opacidade, mistura, 0, 0,
  ];
}

/// Floats de p0..p7 do `shaders/shake.frag` (32) para o Advanced Shake.
List<double> valoresShake(EffectInstance e, Duration local) {
  final v = _leitor(e, local);
  final t = local.inMicroseconds / 1e6;
  return _pacote(
    v,
    t + v('phase'),
    amp: v('amplitude'),
    freq: v('frequency'),
    zDist: v('z_dist'),
    opacidade: 1,
    mistura: v('mix') / 100,
  );
}

/// A TRANSFORMACAO DO SHAKE NUM INSTANTE, em numeros crus.
///
/// E a MESMA conta que vai para o shader, sem o empacotamento: quem
/// precisa do tremor como transformacao — assar em keyframes — le daqui,
/// e nao de uma segunda formula que poderia divergir da imagem.
///
/// [dx] e [dy] saem em pixel de REFERENCIA (a mesma referencia de 1080 que
/// a Amplitude usa); [giroGraus] e o giro do eixo TILT; [escala] e o
/// quanto a profundidade Z faz a imagem crescer (1 = sem Z). Sem o borrao
/// de movimento, que e uma media de varios instantes e nao um estado.
({double dx, double dy, double giroGraus, double escala}) instantDoShake(
  EffectInstance e,
  Duration local,
) {
  final v = _leitor(e, local);
  final t = local.inMicroseconds / 1e6 + v('phase');
  final semente = v('seed');
  // MONO AQUI DE PROPOSITO: assar em keyframes gera UM transform para a
  // camada inteira, e um transform nao sabe separar canal de cor. Com
  // canais diferentes, o assado usa as medias — que e o mais perto que um
  // keyframe de posicao chega de um tremor RGB.
  final d = _transf(
    _deslocamento(
      v, t,
      freq: v('frequency'), semente: semente,
      ampCanal: 1, faseCanal: 0, rgbAleat: 0, rgbFreq: 2, canal: 0,
    ),
    v('amplitude'),
    v('z_dist'),
  );
  // A mistura e um peso: em 50% o tremor anda metade.
  final m = (v('mix') / 100).clamp(0.0, 1.0);
  return (
    dx: d[0] * m,
    dy: d[1] * m,
    giroGraus: d[3] * 180 / math.pi * m,
    escala: 1 + (d[2] - 1) * m,
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
    freq: v('frequency'),
    zDist: 1,
    opacidade: 1 - some,
    mistura: 1,
    amplitudes: [for (final c in const ['red', 'green', 'blue']) v('${c}_amplitude')],
    fases: [for (final c in const ['red', 'green', 'blue']) v('${c}_phase')],
    rgbAleat: v('rgb_randomness'),
    rgbFreq: v('rgb_frequency'),
  );
}
