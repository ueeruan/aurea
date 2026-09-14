import 'dart:ui';

import 'package:uuid/uuid.dart';

import 'keyframe.dart';

/// Efeitos aplicaveis a uma camada (blocos combinaveis, ordem importa).
/// Novos tipos SEMPRE no fim: a serializacao guarda o indice.
enum EffectType {
  gaussianBlur,
  lightGlow,
  tint,
  glowVol,
  tremor,
  glitch,
  rgbSplit,
  echo,
  spatialEcho,
  radialAberration,
  // --- catalogo, lote 1 (spec AUREA-catalogo-de-efeitos) ---
  levels,
  vibrance,
  whiteBalance,
  colorWheels,
  unmult,
  vignette,
  directionalBlur,
  radialBlur,
  lightRays,
  mosaic,
  filmGrain,
  fractalNoise,
  digitalDamage,
  zoomWarp,
  posterize,
  curves,
  // --- lote 2 ---
  timeRemap,
  pixelSort,
  blobTracker,
  turbulentDisplace,
  unsharpMask,
  motionTile,
  bend,
  ccScatterize,
  ccSplit,
  vhs,
  filmDamage,
  glitchify,
  // --- lote 3 (AUREA-six-effects-english) ---
  forceMotionBlur,
  flicker,
  gradient4,
  liquidGlass,
  corrections,

  // --- recorte (keying) e deteccao de borda ---
  chromaKey,
  lumaKey,
  colorKey,
  findEdges,
  oscillate,
  twirl,
  fisheye,
  kaleidoscope,
  venetianBlinds,
  blockDissolve,
  offset,
  invert,
  waveWarp,
  opticalFlow,
  // --- coloring de edit (pesquisa 14/09/2026: FFmpeg, W3C, AE, AM) ---
  colorBalance,
  selectiveColor,
  channelMixer,
  photoFilter,
  gradientMap,
  brightnessContrast,
  colorTune,
  // --- one frame edits e twitch (pesquisa 14/09/2026) ---
  flash,
  strobe,
  zoomPunch,
  sliceGlitch,
  twitch,
  // --- tempo da camada inteira (pesquisa 14/09/2026) ---
  timeSlice,
  posterizeTime,
  // --- camada de ajuste de um edit no After (dono, 14/09/2026) ---
  hueSaturation,
  sFlicker,
  mathOps,
}

/// O tipo a partir do IDENTIFICADOR estavel.
///
/// E o caminho de leitura do arquivo. Guardar o efeito pelo INDICE do
/// enum era uma bomba-relogio: bastava alguem inserir um efeito no meio
/// da lista para todo projeto salvo virar outro efeito. O id nunca muda.
EffectType? effectTypeFromId(String id) {
  for (final e in effectSpecs.entries) {
    if (e.value.id == id) return e.key;
  }
  return _aliasesDeId[id];
}

/// NOMES ANTIGOS que ainda aparecem em arquivo. Renomear nao pode
/// quebrar o que ja existe.
const _aliasesDeId = <String, EffectType>{
  'cc_split': EffectType.ccSplit,
  'cc_scatterize': EffectType.ccScatterize,
  'cc_semear': EffectType.ccScatterize,
  'glow_vol': EffectType.glowVol,
  'volumetric_glow': EffectType.glowVol,
  'tremor': EffectType.tremor,
  's_shake': EffectType.tremor,
  'light_glow': EffectType.lightGlow,
  'radial_aberration': EffectType.radialAberration,
  'spatial_echo': EffectType.spatialEcho,
  'pixel_sort': EffectType.pixelSort,
};

/// PARAMETROS RENOMEADOS: chave antiga -> chave nova.
///
/// Renomear nao pode quebrar preset nem projeto salvo. Quem le o arquivo
/// passa por aqui antes de montar o efeito, e a chave antiga vira a
/// nova sem ninguem perceber.
const effectParamAliases = <EffectType, Map<String, String>>{
  EffectType.tremor: {
    'frequencia': 'frequency',
    'estilo': 'style',
    'inclinacao': 'tilt_random_amplitude',
    'semente': 'seed',
    'fase': 'phase',
    'rgb': 'rgb_randomness',
  },
  EffectType.blobTracker: {
    'quantidade': 'max_blobs',
    'tamanho': 'min_blob_size',
    'espalhar': 'merge_distance',
    'velocidade': 'smoothing',
    'traco': 'thickness',
    'cantos': 'style',
    'semente': 'seed',
  },
  // NIVEL 3: o raio virou pixel de verdade, com o nome do documento.
  EffectType.gaussianBlur: {'amount': 'raio'},
  EffectType.lightGlow: {'diffusion': 'raio'},
  EffectType.pixelSort: {
    'limiar': 'threshold',
    'comprimento': 'radius_length',
    'direcao': 'sort_angle',
    'densidade': 'random_restart',
    'semente': 'seed',
  },
  EffectType.glowVol: {
    'raio': 'radius',
    'intensidade': 'exposure',
    'aberracao': 'red_radius_multiplier',
    'tonalizar': 'tint_amount',
  },
  EffectType.motionTile: {
    'largura': 'tile_width',
    'altura': 'tile_height',
    'saidaLargura': 'output_width',
    'saidaAltura': 'output_height',
    'deslocX': 'phase',
    'espelhar': 'mirror_edges',
  },
};

/// A chave de hoje para uma chave que pode ser de ontem.
String resolveParamKey(EffectType type, String key) =>
    effectParamAliases[type]?[key] ?? key;

/// VERSAO DO EFEITO NO ARQUIVO. Sobe quando um numero muda de UNIDADE —
/// nao basta renomear a chave, o valor tambem tem de ser convertido.
const kEffectVersion = 3;

/// Fator que converte o numero da versao anterior para a de agora.
///
/// Nivel 3: o desfoque e o glow deixaram de ser fracao 0..1 e passaram a
/// ser raio em pixel (pensado em 1080p); limite e intensidade do glow
/// viraram porcentagem. Projeto antigo abre com o MESMO resultado.
const effectParamRescale = <EffectType, Map<String, double>>{
  EffectType.gaussianBlur: {'raio': 43.2},
  EffectType.lightGlow: {'raio': 59.4, 'threshold': 100.0, 'intensity': 100.0},
};

/// Converte os numeros de um efeito lido de um arquivo na versao [versao].
double migrateParamValue(
  EffectType type,
  String key,
  double value,
  int versao,
) {
  if (versao >= kEffectVersion) return value;
  final fator = effectParamRescale[type]?[key];
  return fator == null ? value : value * fator;
}

/// O identificador de um tipo.
String effectIdOf(EffectType t) => effectSpecs[t]!.id;

/// TIPO do parametro (PR-C1). Sem isto, todo efeito que precisa de uma
/// cor ou de um ponto fica morto na tela: a UI so sabia desenhar numero.
enum ParamKind {
  number,
  color,

  /// Ponto 2D arrastavel no palco (guardado em 0..1 do tamanho).
  point,

  /// Lista de opcoes em chips.
  choice,

  /// Numero inteiro que semeia ruido (nunca interpola).
  seed,

  /// Alterna liga/desliga.
  toggle,
}

class EffectParam {
  const EffectParam(
    this.label,
    this.initial,
    this.min,
    this.max, {
    this.kind = ParamKind.number,
    this.options = const [],
    this.relative = false,
  });

  final String label;
  final double initial;
  final double min;
  final double max;
  final ParamKind kind;

  /// Rotulos das opcoes quando [kind] e choice.
  final List<String> options;

  /// RELATIVO AO TAMANHO (presets §2.3): distancia, raio e ponto sao
  /// normalizados pelo tamanho da camada ao salvar um preset e
  /// desnormalizados ao aplicar — senao um preset feito em 1080x1920
  /// sai errado numa camada 1920x1080. Cor, angulo e switch NAO.
  final bool relative;
}

/// AS TRES PROFUNDIDADES (constituicao, regra 2).
///
/// `pronto` toca e acabou (tres presets). `montar` sao ate tres numeros
/// com nome humano na superficie de arrasto. `avancado` e a ficha
/// inteira, com o nome tecnico. Descer e subir NUNCA perde o que ja foi
/// feito: e sempre o mesmo efeito, os mesmos parametros; muda so quanto
/// se ve. Por isso o avancado ja abre com o que o montar deixou.
enum EffectDepth { pronto, montar, avancado }

/// Um preset da camada `pronto`: um nome e os numeros que ele crava.
class EffectPronto {
  const EffectPronto(this.nome, this.valores, {this.cor});

  final String nome;
  final Map<String, double> valores;

  /// Preset que tambem manda na cor do efeito (glow, vinheta).
  final Color? cor;
}

class EffectSpec {
  const EffectSpec({
    required this.id,
    required this.name,
    required this.params,
    this.hasColor = false,
    this.defaultColor = const Color(0xFFFF5566),
    this.extraColors = 0,
    this.defaultExtraColors = const [],
    this.category = 'Estilizar',
    this.synonyms = const [],
    this.cost = 1,
    this.procedural = false,
    this.montar = const [],
    this.presets = const [],
  });

  /// IDENTIFICADOR ESTAVEL, em snake_case e em ingles.
  ///
  /// E o que vai no arquivo. O nome muda de idioma, a categoria muda de
  /// arrumacao, a posicao no enum muda quando entra efeito novo — o id
  /// nao muda nunca, e e por isso que projeto e preset salvos continuam
  /// abrindo.
  final String id;

  /// Nome canonico, em INGLES. Onde o efeito existe no After Effects,
  /// segue o vocabulario de la.
  final String name;

  final Map<String, EffectParam> params;

  /// Cor principal do efeito (alem dos parametros de cor).
  final bool hasColor;

  /// A COR COM QUE O EFEITO NASCE.
  ///
  /// Um efeito que NAO tem seletor de cor ([hasColor] falso) usa esta e
  /// so esta — e por isso ela precisa ser NEUTRA para o que o efeito
  /// faz: preto para a vinheta (escurecer), branco para brilho, raios e
  /// ruido (multiplicar sem tingir). O padrao rosa existe para os
  /// efeitos que abrem o seletor e querem uma cor visivel de saida.
  final Color defaultColor;

  /// Quantas cores ALEM da principal o efeito pede (gradiente de
  /// quatro cores pede tres).
  final int extraColors;

  /// AS CORES EXTRAS COM QUE O EFEITO NASCE. Vazio = a paleta generica.
  /// Um mapa de gradiente precisa nascer com sombra escura, meio-tom e
  /// luz clara; a paleta generica (violeta, ciano, ambar) pintava o
  /// quadro de roxo antes de qualquer escolha.
  final List<Color> defaultExtraColors;

  /// Categoria do catalogo.
  final String category;

  /// BUSCA COM SINONIMOS (§5): quem digita "rgb split" acha Separacao
  /// RGB; "bloom" acha Glow; "green screen" acharia Chroma Key.
  final List<String> synonyms;

  /// Custo estimado por frame (1 = barato, 3 = caro).
  final int cost;

  /// Movimento gerado por procedimento — pode ser ASSADO em keyframes.
  final bool procedural;

  /// As chaves que aparecem em `montar` — no maximo tres (regra 2).
  final List<String> montar;

  /// Os tres presets de `pronto`.
  final List<EffectPronto> presets;

  /// Efeito com as tres profundidades prontas (nivel 3).
  bool get temProfundidades => presets.isNotEmpty && montar.isNotEmpty;
}

const effectSpecs = <EffectType, EffectSpec>{
  EffectType.oscillate: EffectSpec(
    id: 'oscillate',
    name: 'Oscilar',
    category: 'Distort',
    procedural: true,
    synonyms: [
      'oscilar',
      'oscillate',
      'oscilacao',
      'oscilação',
      'balancar',
      'balançar',
    ],
    params: {
      'amplitude': EffectParam('Amplitude', 50, 0, 2000, relative: true),
      'frequency': EffectParam('Frequencia (Hz)', 1, 0, 30),
      'angle': EffectParam('Direcao', 0, -360, 360),
      'phase': EffectParam('Fase', 0, -360, 360),
      'wave': EffectParam(
        'Onda',
        0,
        0,
        3,
        kind: ParamKind.choice,
        options: ['Seno', 'Triangulo', 'Quadrada', 'Dente de serra'],
      ),
    },
  ),
  EffectType.gaussianBlur: EffectSpec(
    id: 'gaussian_blur',
    name: 'Gaussian Blur',
    category: 'Blur',
    synonyms: ['desfoque', 'gaussiano', 'blur', 'gaussian', 'suavizar'],
    params: {
      // O raio E em pixel (pensado em 1080p): e o numero que a pessoa
      // reconhece de qualquer outro programa.
      'raio': EffectParam('Radius', 24.0, 0.0, 500.0, relative: true),
      'borda': EffectParam(
        'Borda',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Transparente', 'Repetir', 'Espelhar'],
      ),
      'qualidade': EffectParam(
        'Qualidade',
        1.0,
        0.0,
        1.0,
        kind: ParamKind.choice,
        options: ['Rapida', 'Alta'],
      ),
    },
    montar: ['raio'],
    presets: [
      EffectPronto('Leve', {'raio': 8}),
      EffectPronto('Medio', {'raio': 32}),
      EffectPronto('Forte', {'raio': 110}),
    ],
  ),
  EffectType.lightGlow: EffectSpec(
    id: 'glow',
    defaultColor: Color(0xFFFFFFFF),
    name: 'Glow',
    category: 'Light',
    synonyms: ['brilho', 'luz', 'glow', 'bloom', 'brilho'],
    params: {
      'threshold': EffectParam('Threshold', 70.0, 0.0, 100.0),
      'raio': EffectParam('Radius', 30.0, 0.0, 500.0, relative: true),
      // Passa de 100%: brilho estourado e uma escolha, nao um limite.
      'intensity': EffectParam('Intensity', 100.0, 0.0, 400.0),
      'mesclagem': EffectParam(
        'Mesclagem',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Somar', 'Tela', 'Clarear'],
      ),
      // Piramide: quantas passadas de desfoque em escalas dobradas —
      // e o que faz o halo grande sem custar o raio inteiro.
      'piramide': EffectParam('Piramide', 3.0, 1.0, 5.0),
      'mult_r': EffectParam('Mult R', 1.0, 0.0, 2.0),
      'mult_g': EffectParam('Mult G', 1.0, 0.0, 2.0),
      'mult_b': EffectParam('Mult B', 1.0, 0.0, 2.0),
    },
    hasColor: true,
    montar: ['threshold', 'raio', 'intensity'],
    presets: [
      EffectPronto('Suave', {
        'threshold': 75,
        'raio': 26,
        'intensity': 70,
        'piramide': 3,
      }, cor: Color(0xFFFFFFFF)),
      EffectPronto('Neon', {
        'threshold': 55,
        'raio': 60,
        'intensity': 240,
        'piramide': 4,
      }, cor: Color(0xFF35C4E7)),
      EffectPronto('Sonho', {
        'threshold': 30,
        'raio': 150,
        'intensity': 130,
        'piramide': 5,
      }, cor: Color(0xFFFFD8F0)),
    ],
  ),
  EffectType.tint: EffectSpec(
    id: 'tint',
    name: 'Tint',
    category: 'Color',
    synonyms: ['tonalizar', 'tint', 'colorir'],
    params: {'strength': EffectParam('Forca', 0.5, 0.0, 1.0)},
    hasColor: true,
    montar: ['strength'],
    presets: [
      EffectPronto('Leve', {'strength': 0.25}),
      EffectPronto('Medio', {'strength': 0.5}),
      EffectPronto('Forte', {'strength': 0.85}),
    ],
  ),
  // Glow com pirâmide de 3 niveis, aberracao RGB e tonalizacao opcional
  // (aproximacao do Glow Volumetrico; conservacao plena exige linear).
  EffectType.glowVol: EffectSpec(
    id: 'deep_glow',
    defaultColor: Color(0xFFFFFFFF),
    name: 'Deep Glow',
    category: 'Light',
    synonyms: [
      'glow',
      'volumetrico',
      'bloom',
      'volumetric',
      'brilho',
      'deep glow',
      'luz',
    ],
    cost: 3,
    params: {
      // --- Core ---
      // Raio em FRACAO do menor lado da composicao (0,04 ~ 80 px em
      // 1080p). Em pixel absoluto, o mesmo numero daria glows de
      // tamanhos diferentes so por trocar a resolucao.
      'radius': EffectParam('Radius', 0.04, 0.0, 1.0, relative: true),
      'exposure': EffectParam('Exposure', 1.0, -5.0, 5.0),
      'threshold': EffectParam('Threshold', 1.0, 0.0, 4.0),
      'threshold_mode': EffectParam(
        'Threshold Mode',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.choice,
        options: ['Luminance', 'Chrominance'],
      ),
      'threshold_softness': EffectParam('Threshold Softness', 0.2, 0.0, 1.0),
      'quality': EffectParam(
        'Quality',
        1.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Draft', 'Normal', 'High'],
      ),
      'downsample': EffectParam('Downsample', 2.0, 1.0, 8.0),
      'glow_mode': EffectParam(
        'Glow Mode',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.choice,
        options: ['Exponential', 'Iris'],
      ),

      // --- Color ---
      'red_radius_multiplier': EffectParam(
        'Red Radius Multiplier',
        1.0,
        0.5,
        2.0,
      ),
      'green_radius_multiplier': EffectParam(
        'Green Radius Multiplier',
        1.0,
        0.5,
        2.0,
      ),
      'blue_radius_multiplier': EffectParam(
        'Blue Radius Multiplier',
        1.0,
        0.5,
        2.0,
      ),
      'tint_mode': EffectParam(
        'Tint Mode',
        0.0,
        0.0,
        3.0,
        kind: ParamKind.choice,
        options: ['None', 'Solid', 'Gradient', 'Image Based'],
      ),
      'tint_amount': EffectParam('Tint Amount', 1.0, 0.0, 1.0),
      'glow_saturation': EffectParam('Glow Saturation', 100.0, 0.0, 200.0),

      // --- Look ---
      'aspect_ratio': EffectParam('Aspect Ratio', 1.0, 0.1, 10.0),
      'enable_angle': EffectParam(
        'Enable Angle',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.toggle,
      ),
      'angle': EffectParam('Angle', 0.0, -360.0, 360.0),
      'tonemapping': EffectParam(
        'Tonemapping',
        0.0,
        0.0,
        3.0,
        kind: ParamKind.choice,
        options: ['ACES Filmic', 'Reinhard', 'Reinhard 2', 'Clamp'],
      ),
      'lens_dirt_amount': EffectParam('Lens Dirt Amount', 50.0, 0.0, 200.0),
      'noise_reduction': EffectParam('Noise Reduction', 0.0, 0.0, 100.0),

      // --- Composite ---
      'blend_mode': EffectParam(
        'Blend Mode',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Add', 'Screen', 'Normal'],
      ),
      'glow_only': EffectParam(
        'Glow Only',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.toggle,
      ),
    },
    hasColor: true,
    montar: ['radius', 'exposure', 'threshold'],
    presets: [
      EffectPronto('Suave', {
        'radius': 0.03,
        'exposure': 0.6,
        'threshold': 1.2,
      }),
      EffectPronto('Medio', {
        'radius': 0.05,
        'exposure': 1.0,
        'threshold': 1.0,
      }),
      EffectPronto('Intenso', {
        'radius': 0.09,
        'exposure': 1.8,
        'threshold': 0.8,
      }),
    ],
  ),
  // Tremor de camera: aleatorio e REPETIVEL, com fase integrada — animar
  // a frequencia acelera de verdade, sem salto.
  EffectType.tremor: EffectSpec(
    id: 'shake',
    name: 'S_Shake',
    category: 'Distort',
    synonyms: [
      'tremor',
      'shake',
      's_shake',
      's shake',
      'camera shake',
      'tremer',
      'camera na mao',
      'handheld',
    ],
    procedural: true,
    cost: 2,
    params: {
      // --- General ---
      'style': EffectParam(
        'Style',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Normal', 'Nervous', 'Jumpy'],
      ),
      'amplitude': EffectParam('Amplitude', 1.0, 0.0, 20.0, relative: true),
      'frequency': EffectParam('Frequency', 8.0, 0.0, 60.0),
      'phase': EffectParam('Phase', 0.0, -360.0, 360.0),
      'stillness': EffectParam('Stillness', 0.7, 0.0, 1.0),
      'twitch_frequency': EffectParam('Twitch Frequency', 2.0, 0.0, 20.0),
      'drift': EffectParam('Drift', 0.3, 0.0, 1.0),
      'center_bias': EffectParam('Center Bias', 0.0, 0.0, 1.0),
      'z_distance': EffectParam('Z Distance', 1.0, 0.001, 10.0),
      'motion_blur': EffectParam(
        'Motion Blur',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.toggle,
      ),
      'blur_length': EffectParam('Blur Length', 1.0, 0.0, 10.0),
      'seed': EffectParam('Seed', 0.0, 0.0, 100.0, kind: ParamKind.seed),
      // Padrao NONE: refletir/repetir so faz sentido em camada que
      // enche o quadro (video). Numa forma ou texto, a "borda" e a
      // caixa da propria camada — e o reflexo virava uma copia
      // espelhada colada ao lado, lida como camada duplicada.
      'edges': EffectParam(
        'X / Y Edges',
        2.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Reflect', 'Tile', 'None'],
      ),

      // --- Por eixo: a componente ALEATORIA e a de ONDA sao separadas.
      // E o que faz parecer camera na mao em vez de senoide.
      'x_random_amplitude': EffectParam('X Random Amplitude', 0.2, 0.0, 5.0),
      'x_random_frequency': EffectParam('X Random Frequency', 1.0, 0.0, 10.0),
      'x_wave_amplitude': EffectParam('X Wave Amplitude', 0.0, 0.0, 5.0),
      'x_wave_frequency': EffectParam('X Wave Frequency', 0.5, 0.0, 20.0),
      'x_phase': EffectParam('X Phase', 0.0, -360.0, 360.0),

      'y_random_amplitude': EffectParam('Y Random Amplitude', 0.1, 0.0, 5.0),
      'y_random_frequency': EffectParam('Y Random Frequency', 1.0, 0.0, 10.0),
      'y_wave_amplitude': EffectParam('Y Wave Amplitude', 0.0, 0.0, 5.0),
      'y_wave_frequency': EffectParam('Y Wave Frequency', 0.5, 0.0, 20.0),
      'y_phase': EffectParam('Y Phase', 0.0, -360.0, 360.0),

      'z_random_amplitude': EffectParam('Z Random Amplitude', 0.0, 0.0, 5.0),
      'z_random_frequency': EffectParam('Z Random Frequency', 1.0, 0.0, 10.0),
      'z_wave_amplitude': EffectParam('Z Wave Amplitude', 0.0, 0.0, 5.0),
      'z_wave_frequency': EffectParam('Z Wave Frequency', 0.5, 0.0, 20.0),
      'z_phase': EffectParam('Z Phase', 0.0, -360.0, 360.0),

      'tilt_random_amplitude': EffectParam(
        'Tilt Random Amplitude',
        0.0,
        0.0,
        5.0,
      ),
      'tilt_random_frequency': EffectParam(
        'Tilt Random Frequency',
        1.0,
        0.0,
        10.0,
      ),
      'tilt_wave_amplitude': EffectParam('Tilt Wave Amplitude', 0.0, 0.0, 5.0),
      'tilt_wave_frequency': EffectParam('Tilt Wave Frequency', 0.5, 0.0, 20.0),
      'tilt_phase': EffectParam('Tilt Phase', 0.0, -360.0, 360.0),

      // --- RGB: fase por canal desloca o canal NO TEMPO. O vermelho se
      // move antes, os outros seguem — franja organica, muito melhor que
      // um deslocamento estatico.
      'red_amplitude': EffectParam('Red Amplitude', 1.0, 0.0, 5.0),
      'green_amplitude': EffectParam('Green Amplitude', 1.0, 0.0, 5.0),
      'blue_amplitude': EffectParam('Blue Amplitude', 1.0, 0.0, 5.0),
      'red_phase': EffectParam('Red Phase', 0.0, -360.0, 360.0),
      'green_phase': EffectParam('Green Phase', 0.0, -360.0, 360.0),
      'blue_phase': EffectParam('Blue Phase', 0.0, -360.0, 360.0),
      'rgb_randomness': EffectParam('RGB Randomness', 0.0, 0.0, 1.0),
      'rgb_frequency': EffectParam('RGB Frequency', 2.0, 0.0, 30.0),

      // --- EDIT (pesquisa 14/09/2026). Todos nascem NEUTROS: projeto
      // antigo abre tremendo igual.
      // Envelope: continuo (como sempre), impacto (nasce forte no inicio
      // da camada e cai pela metade a cada meia-vida) ou a cada N quadros.
      'envelope': EffectParam(
        'Envelope',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Continuous', 'Impact', 'Every N Frames'],
      ),
      'attack_frames': EffectParam('Attack Frames', 0.0, 0.0, 10.0),
      'half_life': EffectParam('Half-Life (Frames)', 6.0, 1.0, 60.0),
      'impact_period': EffectParam('Impact Period (Frames)', 12.0, 1.0, 120.0),
      // Zoom de soco no quadro da batida, que volta em N quadros.
      'zoom_punch': EffectParam('Zoom Punch', 0.0, 0.0, 50.0),
      'punch_frames': EffectParam('Punch Frames', 6.0, 1.0, 20.0),
      // fBm (oitavas do wiggle) e serrilhado (valor seco por ciclo).
      'octaves': EffectParam('Octaves', 1.0, 1.0, 4.0),
      'jaggedness': EffectParam('Jaggedness', 0.0, 0.0, 1.0),
    },
    montar: ['amplitude', 'frequency', 'seed'],
    presets: [
      EffectPronto('Camera na mao', {
        'style': 0,
        'amplitude': 1.4,
        'frequency': 3.5,
        'stillness': 0.55,
        'drift': 0.45,
        'twitch_frequency': 1.2,
      }),
      // IMPACTO DE EDIT: a receita S_Shake de edit (amplitude alta na
      // batida caindo em ~20 quadros, frequencia 15, X:Y de 100:150,
      // inclinacao) com o envelope de impacto e um zoom de soco.
      EffectPronto('Impacto', {
        'style': 0,
        'amplitude': 3.0,
        'frequency': 15.0,
        'envelope': 1,
        'half_life': 5.0,
        'zoom_punch': 8.0,
        'punch_frames': 6.0,
        'x_random_amplitude': 1.0,
        'y_random_amplitude': 1.5,
        'tilt_random_amplitude': 0.17,
        'motion_blur': 1,
      }),
      EffectPronto('Nervoso', {
        'style': 0,
        'amplitude': 3.2,
        'frequency': 26.0,
        'stillness': 0.2,
        'drift': 0.15,
        'twitch_frequency': 9.0,
      }),
    ],
  ),
  // Seis operadores sincronizados por um modulador mestre (quantidade +
  // velocidade); tiques deterministicos e seekaveis.
  EffectType.glitch: EffectSpec(
    id: 'glitch',
    name: 'Glitch',
    category: 'Glitch',
    synonyms: ['glitch', 'modular', 'glitch', 'datamosh', 'erro'],
    cost: 2,
    procedural: true,
    params: {
      'quantidade': EffectParam('Quantidade', 1.0, 0.0, 2.0),
      'velocidade': EffectParam('Velocidade', 1.0, 0.0, 10.0),
      'intervalo': EffectParam('Intervalo', 0.5, 0.05, 2.0),
      'deslize': EffectParam('Deslize', 0.6, 0.0, 1.0),
      'escala': EffectParam('Escala', 0.3, 0.0, 1.0),
      'cor': EffectParam('Cor', 0.4, 0.0, 1.0),
      'luz': EffectParam('Luz', 0.3, 0.0, 1.0),
      'desfoque': EffectParam('Desfoque', 0.2, 0.0, 1.0),
      'rgb': EffectParam('Separacao RGB', 0.5, 0.0, 1.0),
      'semente': EffectParam('Semente', 0.0, 0.0, 100.0, kind: ParamKind.seed),
    },
    montar: ['quantidade', 'velocidade', 'rgb'],
    presets: [
      EffectPronto('Sutil', {'quantidade': 0.4, 'velocidade': 0.6, 'rgb': 0.3}),
      EffectPronto('Medio', {'quantidade': 1.0, 'velocidade': 1.0, 'rgb': 0.5}),
      EffectPronto('Caotico', {
        'quantidade': 2.0,
        'velocidade': 4.0,
        'rgb': 1.0,
      }),
    ],
  ),
  EffectType.rgbSplit: EffectSpec(
    id: 'rgb_split',
    name: 'RGB Split',
    category: 'Lens',
    synonyms: ['separacao', 'rgb', 'rgb split', 'chromatic', 'canal'],
    params: {
      'deslocamento': EffectParam('Amount', 20.0, 0.0, 100.0, relative: true),
      'angulo': EffectParam('Angle', 0.0, -180.0, 180.0),
      // Quais canais se afastam: o par decide a cor das franjas.
      'canais': EffectParam(
        'Canais',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['R / B', 'R / G', 'G / B'],
      ),
      'suavizar': EffectParam('Suavizar borda', 0.0, 0.0, 1.0),
    },
    montar: ['deslocamento', 'angulo'],
    presets: [
      EffectPronto('Sutil', {'deslocamento': 6, 'suavizar': 0.2}),
      EffectPronto('Edit', {'deslocamento': 22, 'suavizar': 0.0}),
      EffectPronto('Extremo', {'deslocamento': 64, 'suavizar': 0.35}),
    ],
  ),
  // Eco: re-renderiza a camada em tempos anteriores (deterministico —
  // trilhas de movimento de keyframes/transform). Matiz > 0 = RASTRO
  // COLORIDO: cada copia ganha uma rotacao de matiz propria.
  EffectType.echo: EffectSpec(
    id: 'echo',
    name: 'Echo',
    category: 'Blur',
    synonyms: ['eco', 'rastro', 'echo', 'trail', 'rastro', 'motion trail'],
    cost: 3,
    params: {
      'ecos': EffectParam('Ecos', 3.0, 1.0, 8.0),
      'intervalo': EffectParam('Intervalo', 0.08, 0.02, 0.5),
      'decaimento': EffectParam('Decaimento', 0.55, 0.1, 0.95),
      'matiz': EffectParam('Matiz/copia', 0.0, 0.0, 120.0),
    },
    montar: ['ecos', 'intervalo', 'decaimento'],
    presets: [
      EffectPronto('Curto', {'ecos': 2, 'intervalo': 0.05, 'decaimento': 0.5}),
      EffectPronto('Medio', {'ecos': 3, 'intervalo': 0.08, 'decaimento': 0.55}),
      EffectPronto('Longo', {'ecos': 6, 'intervalo': 0.14, 'decaimento': 0.75}),
    ],
  ),
  // Eco ESPACIAL (AUREA-2 §2 item 28): repeticao no espaco com
  // transformacao progressiva por copia.
  EffectType.spatialEcho: EffectSpec(
    id: 'space_echo',
    name: 'Space Echo',
    category: 'Stylize',
    synonyms: ['eco', 'espacial', 'echo', 'repeat', 'repeticao'],
    cost: 2,
    params: {
      'copias': EffectParam('Copias', 5.0, 1.0, 12.0),
      'dx': EffectParam('Desloc X', 40.0, -300.0, 300.0, relative: true),
      'dy': EffectParam('Desloc Y', 0.0, -300.0, 300.0, relative: true),
      'escala': EffectParam('Escala/copia', 96.0, 50.0, 150.0),
      'rotacao': EffectParam('Rot/copia', 0.0, -90.0, 90.0),
      'decaimento': EffectParam('Decaimento', 0.7, 0.1, 1.0),
      'matiz': EffectParam('Matiz/copia', 0.0, 0.0, 120.0),
    },
    montar: ['copias', 'dx', 'decaimento'],
    presets: [
      EffectPronto('Poucas', {'copias': 3, 'dx': 30, 'decaimento': 0.6}),
      EffectPronto('Medio', {'copias': 5, 'dx': 40, 'decaimento': 0.7}),
      EffectPronto('Muitas', {'copias': 10, 'dx': 60, 'decaimento': 0.85}),
    ],
  ),
  // Aberracao cromatica RADIAL (item 14): cresce do centro para a
  // borda, como lente real — diferente do RGB Split.
  EffectType.radialAberration: EffectSpec(
    id: 'chromatic_aberration',
    name: 'Chromatic Aberration',
    category: 'Lens',
    synonyms: [
      'aberracao',
      'cromatica',
      'chromatic aberration',
      'franja',
      'lente',
    ],
    cost: 2,
    params: {'quantidade': EffectParam('Quantidade', 0.3, 0.0, 1.0)},
    montar: ['quantidade'],
    presets: [
      EffectPronto('Leve', {'quantidade': 0.15}),
      EffectPronto('Medio', {'quantidade': 0.3}),
      EffectPronto('Forte', {'quantidade': 0.7}),
    ],
  ),

  // ------------------------- catalogo, lote 1 -------------------------
  EffectType.levels: EffectSpec(
    id: 'levels',
    name: 'Levels',
    category: 'Color',
    synonyms: ['niveis', 'levels', 'contraste', 'gama', 'brilho'],
    params: {
      'entradaMin': EffectParam('Preto', 0.0, 0.0, 1.0),
      'entradaMax': EffectParam('Branco', 1.0, 0.0, 1.0),
      'gama': EffectParam('Gama', 1.0, 0.2, 3.0),
      'saidaMin': EffectParam('Saida min', 0.0, 0.0, 1.0),
      'saidaMax': EffectParam('Saida max', 1.0, 0.0, 1.0),
      'canal': EffectParam(
        'Canal',
        0.0,
        0.0,
        3.0,
        kind: ParamKind.choice,
        options: ['RGB', 'R', 'G', 'B'],
      ),
    },
    montar: ['entradaMin', 'entradaMax', 'gama'],
    presets: [
      EffectPronto('Contraste', {
        'entradaMin': 0.08,
        'entradaMax': 0.92,
        'gama': 1.0,
      }),
      EffectPronto('Clarear', {
        'entradaMin': 0.0,
        'entradaMax': 0.88,
        'gama': 1.35,
      }),
      EffectPronto('Escurecer', {
        'entradaMin': 0.10,
        'entradaMax': 1.0,
        'gama': 0.78,
      }),
    ],
  ),
  EffectType.curves: EffectSpec(
    id: 'curves',
    name: 'Curves',
    category: 'Color',
    synonyms: ['curvas', 'curves', 'curva', 'contraste'],
    params: {
      'contraste': EffectParam('Contraste S', 0.0, -1.0, 1.0),
      'brilho': EffectParam('Brilho', 0.0, -1.0, 1.0),
      'sombras': EffectParam('Levantar sombras', 0.0, 0.0, 1.0),
      'altas': EffectParam('Baixar altas', 0.0, 0.0, 1.0),
    },
    montar: ['contraste', 'brilho'],
    presets: [
      EffectPronto('Suave', {'contraste': 0.15, 'brilho': 0.0}),
      EffectPronto('Medio', {'contraste': 0.35, 'brilho': 0.0}),
      EffectPronto('Forte', {'contraste': 0.6, 'brilho': 0.05}),
    ],
  ),
  EffectType.vibrance: EffectSpec(
    id: 'vibrance',
    name: 'Vibrance',
    category: 'Color',
    synonyms: ['vibracao', 'vibrance', 'saturacao', 'vivid'],
    params: {
      'vibracao': EffectParam('Vibracao', 0.0, -1.0, 1.0),
      'saturacao': EffectParam('Saturacao', 0.0, -1.0, 1.0),
      // O que separa cor bonita de cor berrante: satura o resto sem
      // deixar o rosto laranja.
      'protecaoPele': EffectParam('Protecao de pele', 0.6, 0.0, 1.0),
    },
    montar: ['vibracao', 'saturacao'],
    presets: [
      EffectPronto('Leve', {'vibracao': 0.25, 'saturacao': 0.0}),
      EffectPronto('Medio', {'vibracao': 0.5, 'saturacao': 0.1}),
      EffectPronto('Forte', {'vibracao': 0.9, 'saturacao': 0.25}),
    ],
  ),
  EffectType.whiteBalance: EffectSpec(
    id: 'white_balance',
    name: 'White Balance',
    category: 'Color',
    synonyms: [
      'balanco',
      'branco',
      'white balance',
      'temperatura',
      'matiz',
      'wb',
    ],
    params: {
      'temperatura': EffectParam('Temperatura', 0.0, -1.0, 1.0),
      'matiz': EffectParam('Matiz', 0.0, -1.0, 1.0),
    },
    montar: ['temperatura', 'matiz'],
    presets: [
      EffectPronto('Quente', {'temperatura': 0.35, 'matiz': 0.0}),
      EffectPronto('Neutro', {'temperatura': 0.0, 'matiz': 0.0}),
      EffectPronto('Frio', {'temperatura': -0.35, 'matiz': 0.0}),
    ],
  ),
  EffectType.colorWheels: EffectSpec(
    id: 'color_wheels',
    name: 'Color Wheels',
    category: 'Color',
    synonyms: ['rodas', 'cor', 'color wheels', 'lift gamma gain', 'gradacao'],
    params: {
      'sombrasR': EffectParam('Sombras R', 0.0, -0.5, 0.5),
      'sombrasG': EffectParam('Sombras G', 0.0, -0.5, 0.5),
      'sombrasB': EffectParam('Sombras B', 0.0, -0.5, 0.5),
      'altasR': EffectParam('Altas R', 0.0, -0.5, 0.5),
      'altasG': EffectParam('Altas G', 0.0, -0.5, 0.5),
      'altasB': EffectParam('Altas B', 0.0, -0.5, 0.5),
    },
    montar: ['sombrasB', 'altasR'],
    presets: [
      EffectPronto('Teal e laranja', {'sombrasB': 0.15, 'altasR': 0.12}),
      EffectPronto('Neutro', {'sombrasB': 0.0, 'altasR': 0.0}),
      EffectPronto('Frio', {'sombrasB': 0.25, 'altasR': -0.1}),
    ],
  ),
  // O efeito mais subestimado da lista: fogo, fumaca, faisca e vazamento
  // de luz vem todos em video com fundo preto.
  EffectType.unmult: EffectSpec(
    id: 'unmult',
    name: 'Unmult',
    category: 'Light',
    synonyms: [
      'unmult',
      'tira',
      'preto',
      'unmult',
      'screen',
      'tirar fundo preto',
      'overlay',
    ],
    params: {
      'limiar': EffectParam('Limiar', 0.0, 0.0, 1.0),
      'suavidade': EffectParam('Suavidade', 0.5, 0.0, 1.0),
    },
    montar: ['limiar', 'suavidade'],
    presets: [
      EffectPronto('Leve', {'limiar': 0.05, 'suavidade': 0.6}),
      EffectPronto('Medio', {'limiar': 0.15, 'suavidade': 0.5}),
      EffectPronto('Forte', {'limiar': 0.35, 'suavidade': 0.3}),
    ],
  ),
  EffectType.vignette: EffectSpec(
    id: 'vignette',
    defaultColor: Color(0xFF000000),
    name: 'Vignette',
    category: 'Lens',
    synonyms: ['vinheta', 'vignette', 'borda escura'],
    params: {
      'quantidade': EffectParam('Amount', 0.5, 0.0, 1.0),
      'raio': EffectParam('Radius', 0.7, 0.1, 1.5, relative: true),
      'suavidade': EffectParam('Softness', 0.5, 0.0, 1.0),
      'forma': EffectParam(
        'Forma',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.choice,
        options: ['Circulo', 'Retangulo'],
      ),
      'centroX': EffectParam('Centro X', 0.5, 0.0, 1.0, kind: ParamKind.point),
      'centroY': EffectParam('Centro Y', 0.5, 0.0, 1.0, kind: ParamKind.point),
    },
    hasColor: true,
    montar: ['quantidade', 'raio', 'suavidade'],
    presets: [
      EffectPronto('Suave', {
        'quantidade': 0.35,
        'raio': 0.95,
        'suavidade': 0.75,
      }),
      EffectPronto('Cinema', {
        'quantidade': 0.62,
        'raio': 0.72,
        'suavidade': 0.55,
      }),
      EffectPronto('Dura', {'quantidade': 0.9, 'raio': 0.55, 'suavidade': 0.2}),
    ],
  ),
  EffectType.directionalBlur: EffectSpec(
    id: 'directional_blur',
    name: 'Directional Blur',
    category: 'Blur',
    synonyms: [
      'desfoque',
      'direcional',
      'directional blur',
      'motion blur',
      'movimento',
    ],
    cost: 2,
    params: {
      'comprimento': EffectParam(
        'Comprimento',
        20.0,
        0.0,
        120.0,
        relative: true,
      ),
      'angulo': EffectParam('Angulo', 0.0, -180.0, 180.0),
    },
    montar: ['comprimento', 'angulo'],
    presets: [
      EffectPronto('Leve', {'comprimento': 8, 'angulo': 0}),
      EffectPronto('Medio', {'comprimento': 20, 'angulo': 0}),
      EffectPronto('Forte', {'comprimento': 60, 'angulo': 0}),
    ],
  ),
  EffectType.radialBlur: EffectSpec(
    id: 'radial_blur',
    name: 'Radial Blur',
    category: 'Blur',
    synonyms: ['desfoque', 'radial', 'radial blur', 'zoom blur', 'giro'],
    cost: 3,
    params: {
      'quantidade': EffectParam('Quantidade', 0.3, 0.0, 1.0),
      'modo': EffectParam(
        'Modo',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.choice,
        options: ['Zoom', 'Giro'],
      ),
      'amostras': EffectParam('Amostras', 6.0, 2.0, 16.0),
    },
    montar: ['quantidade', 'modo'],
    presets: [
      EffectPronto('Leve', {'quantidade': 0.12}),
      EffectPronto('Medio', {'quantidade': 0.3}),
      EffectPronto('Forte', {'quantidade': 0.7}),
    ],
  ),
  EffectType.lightRays: EffectSpec(
    id: 'light_rays',
    defaultColor: Color(0xFFFFFFFF),
    name: 'Light Rays',
    category: 'Light',
    synonyms: [
      'raios',
      'volumetricos',
      'god rays',
      'light rays',
      'raios',
      'deus',
    ],
    cost: 3,
    params: {
      'comprimento': EffectParam('Comprimento', 0.4, 0.0, 1.0),
      'intensidade': EffectParam('Intensidade', 0.7, 0.0, 2.0),
      'amostras': EffectParam('Amostras', 8.0, 2.0, 20.0),
      'centroX': EffectParam(
        'Origem X',
        0.5,
        0.0,
        1.0,
        kind: ParamKind.point,
        relative: true,
      ),
      'centroY': EffectParam(
        'Origem Y',
        0.3,
        0.0,
        1.0,
        kind: ParamKind.point,
        relative: true,
      ),
    },
    hasColor: true,
    montar: ['comprimento', 'intensidade'],
    presets: [
      EffectPronto('Suave', {'comprimento': 0.25, 'intensidade': 0.4}),
      EffectPronto('Medio', {'comprimento': 0.4, 'intensidade': 0.7}),
      EffectPronto('Intenso', {'comprimento': 0.7, 'intensidade': 1.4}),
    ],
  ),
  EffectType.mosaic: EffectSpec(
    id: 'mosaic',
    name: 'Pixelar (Mosaico)',
    category: 'Stylize',
    synonyms: [
      'mosaico',
      'mosaic',
      'pixelate',
      'pixel',
      'pixelar',
      'pixelizar',
      'censura',
    ],
    params: {'blocos': EffectParam('Blocos', 24.0, 3.0, 160.0)},
    montar: ['blocos'],
    presets: [
      EffectPronto('Fino', {'blocos': 60}),
      EffectPronto('Medio', {'blocos': 24}),
      EffectPronto('Grosso', {'blocos': 8}),
    ],
  ),
  EffectType.filmGrain: EffectSpec(
    id: 'film_grain',
    name: 'Film Grain',
    category: 'Stylize',
    synonyms: ['grao', 'filme', 'grain', 'grao', 'ruido', 'filme'],
    procedural: true,
    params: {
      'intensidade': EffectParam('Intensidade', 0.25, 0.0, 1.0),
      'tamanho': EffectParam('Tamanho', 1.5, 0.5, 6.0),
      'semente': EffectParam('Semente', 1.0, 0.0, 100.0, kind: ParamKind.seed),
    },
    montar: ['intensidade', 'tamanho'],
    presets: [
      EffectPronto('Leve', {'intensidade': 0.12, 'tamanho': 1.0}),
      EffectPronto('Medio', {'intensidade': 0.25, 'tamanho': 1.5}),
      EffectPronto('Forte', {'intensidade': 0.5, 'tamanho': 2.5}),
    ],
  ),
  EffectType.fractalNoise: EffectSpec(
    id: 'fractal_noise',
    defaultColor: Color(0xFFFFFFFF),
    name: 'Fractal Noise',
    category: 'Generate',
    synonyms: [
      'ruido',
      'fractal',
      'fractal noise',
      'perlin',
      'nuvem',
      'fumaca',
    ],
    cost: 2,
    procedural: true,
    params: {
      'escala': EffectParam('Escala', 0.25, 0.02, 1.0),
      'complexidade': EffectParam('Complexidade', 3.0, 1.0, 6.0),
      'contraste': EffectParam('Contraste', 1.0, 0.1, 3.0),
      'evolucao': EffectParam('Evolucao', 0.0, 0.0, 20.0),
      'opacidade': EffectParam('Opacidade', 0.6, 0.0, 1.0),
      'semente': EffectParam('Semente', 3.0, 0.0, 100.0, kind: ParamKind.seed),
    },
    hasColor: true,
    montar: ['escala', 'complexidade', 'opacidade'],
    presets: [
      EffectPronto('Suave', {
        'escala': 0.4,
        'complexidade': 2,
        'opacidade': 0.4,
      }),
      EffectPronto('Medio', {
        'escala': 0.25,
        'complexidade': 3,
        'opacidade': 0.6,
      }),
      EffectPronto('Denso', {
        'escala': 0.12,
        'complexidade': 5,
        'opacidade': 0.8,
      }),
    ],
  ),
  EffectType.digitalDamage: EffectSpec(
    id: 'digital_damage',
    name: 'Digital Damage',
    category: 'Glitch',
    synonyms: [
      'dano',
      'digital',
      'digital damage',
      'blocos',
      'corrupcao',
      'datamosh',
    ],
    cost: 2,
    procedural: true,
    params: {
      'blocos': EffectParam('Blocos', 6.0, 1.0, 24.0),
      'altura': EffectParam('Altura', 0.08, 0.01, 0.4),
      'deslocamento': EffectParam(
        'Deslocamento',
        0.15,
        0.0,
        1.0,
        relative: true,
      ),
      'cor': EffectParam('Corrupcao de cor', 0.4, 0.0, 1.0),
      'intervalo': EffectParam('Intervalo', 0.4, 0.05, 2.0),
      'semente': EffectParam('Semente', 5.0, 0.0, 100.0, kind: ParamKind.seed),
    },
    montar: ['blocos', 'deslocamento', 'intervalo'],
    presets: [
      EffectPronto('Sutil', {
        'blocos': 3,
        'deslocamento': 0.08,
        'intervalo': 0.8,
      }),
      EffectPronto('Medio', {
        'blocos': 6,
        'deslocamento': 0.15,
        'intervalo': 0.4,
      }),
      EffectPronto('Pesado', {
        'blocos': 14,
        'deslocamento': 0.4,
        'intervalo': 0.15,
      }),
    ],
  ),
  EffectType.zoomWarp: EffectSpec(
    id: 'zoom_warp',
    name: 'Zoom Warp',
    category: 'Distort',
    synonyms: ['zoom', 'warp', 'zoom warp', 'punch', 'impacto', 'dolly'],
    cost: 2,
    params: {
      'quantidade': EffectParam('Quantidade', 0.2, -1.0, 1.0),
      'rastro': EffectParam('Rastro', 0.3, 0.0, 1.0),
      'amostras': EffectParam('Amostras', 5.0, 2.0, 12.0),
    },
    montar: ['quantidade', 'rastro'],
    presets: [
      EffectPronto('Leve', {'quantidade': 0.1, 'rastro': 0.2}),
      EffectPronto('Medio', {'quantidade': 0.2, 'rastro': 0.3}),
      EffectPronto('Forte', {'quantidade': 0.5, 'rastro': 0.6}),
    ],
  ),

  /// RECORTE POR CROMA — o fundo verde (ou azul) que vira transparencia.
  ///
  /// A distancia e medida no plano de croma, sem a luminancia: fundo
  /// verde tem sombra, e sombra e a mesma cor mais escura. Medindo em
  /// RGB a sombra escapa e sobra aquela moldura escura em volta do
  /// assunto que denuncia recorte malfeito.
  EffectType.chromaKey: EffectSpec(
    id: 'chroma_key',
    name: 'Chroma Key',
    category: 'Keying',
    synonyms: ['chroma', 'croma', 'fundo verde', 'green screen', 'recorte'],
    cost: 2,
    hasColor: true,
    // Verde de estudio: o ponto de partida que acerta na maioria.
    defaultColor: Color(0xFF00B140),
    params: {
      'tolerancia': EffectParam('Tolerancia', 0.18, 0.0, 1.0),
      'suavidade': EffectParam('Suavidade', 0.12, 0.0, 1.0),
      'difusao': EffectParam('Difusao da borda', 0.25, 0.0, 1.0),
      'supressao': EffectParam('Supressao de vazamento', 0.5, 0.0, 1.0),
    },
    montar: ['tolerancia', 'suavidade', 'supressao'],
    presets: [
      EffectPronto('Verde de estudio', {
        'tolerancia': 0.18,
        'suavidade': 0.12,
        'supressao': 0.55,
      }, cor: Color(0xFF00B140)),
      EffectPronto('Azul de estudio', {
        'tolerancia': 0.20,
        'suavidade': 0.14,
        'supressao': 0.5,
      }, cor: Color(0xFF0047BB)),
      EffectPronto('Borda de cabelo', {
        'tolerancia': 0.12,
        'suavidade': 0.26,
        'difusao': 0.6,
        'supressao': 0.7,
      }, cor: Color(0xFF00B140)),
    ],
  ),

  /// RECORTE POR BRILHO. Fundo preto (fumaca, fogo, faisca) ou fundo
  /// branco (tinta, papel). Inverter troca qual dos dois some.
  EffectType.lumaKey: EffectSpec(
    id: 'luma_key',
    name: 'Luma Key',
    category: 'Keying',
    synonyms: ['luma', 'brilho', 'fundo preto', 'fumaca', 'recorte'],
    cost: 1,
    params: {
      'limiar': EffectParam('Limiar', 0.12, 0.0, 1.0),
      'tolerancia': EffectParam('Tolerancia', 0.10, 0.0, 1.0),
      'difusao': EffectParam('Difusao', 0.06, 0.0, 1.0),
      'inverter': EffectParam(
        'Remover',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.choice,
        options: ['O escuro', 'O claro'],
      ),
    },
    montar: ['limiar', 'tolerancia', 'inverter'],
    presets: [
      EffectPronto('Fundo preto', {'limiar': .10, 'tolerancia': .12}),
      EffectPronto('Sombras', {'limiar': .22, 'tolerancia': .2, 'difusao': .1}),
      EffectPronto('Fundo branco', {
        'limiar': .90,
        'tolerancia': .12,
        'inverter': 1,
      }),
    ],
  ),

  /// RECORTE POR COR CHAPADA. Distancia direta em RGB — mais previsivel
  /// que o croma quando a cor a tirar e solida.
  EffectType.colorKey: EffectSpec(
    id: 'color_key',
    name: 'Color Key',
    category: 'Keying',
    synonyms: ['cor', 'remover cor', 'recorte por cor'],
    cost: 1,
    hasColor: true,
    defaultColor: Color(0xFF000000),
    params: {
      'tolerancia': EffectParam('Tolerancia', 0.15, 0.0, 1.0),
      'suavidade': EffectParam('Suavidade', 0.08, 0.0, 1.0),
    },
    montar: ['tolerancia', 'suavidade'],
    presets: [
      EffectPronto('Verde', {
        'tolerancia': .15,
        'suavidade': .08,
      }, cor: Color(0xFF1FD41F)),
      EffectPronto('Azul', {
        'tolerancia': .15,
        'suavidade': .08,
      }, cor: Color(0xFF1F5FD4)),
      EffectPronto('Preciso', {'tolerancia': .06, 'suavidade': .04}),
    ],
  ),

  /// CONTORNO. Sobel na luminancia; misturar traz a imagem de volta por
  /// baixo do traco.
  EffectType.findEdges: EffectSpec(
    id: 'find_edges',
    name: 'Find Edges',
    category: 'Stylize',
    synonyms: ['borda', 'contorno', 'edges', 'sobel', 'traco'],
    cost: 2,
    params: {
      'inverter': EffectParam(
        'Traco',
        1.0,
        0.0,
        1.0,
        kind: ParamKind.choice,
        options: ['Claro no escuro', 'Escuro no claro'],
      ),
      'mistura': EffectParam('Misturar com o original', 0.0, 0.0, 1.0),
    },
    montar: ['inverter', 'mistura'],
    presets: [
      EffectPronto('Contorno', {'inverter': 1, 'mistura': 0}),
      EffectPronto('Desenho', {'inverter': 1, 'mistura': .25}),
      EffectPronto('Neon', {'inverter': 0, 'mistura': .15}),
    ],
  ),

  EffectType.posterize: EffectSpec(
    id: 'posterize',
    name: 'Posterize',
    category: 'Stylize',
    synonyms: ['posterizar', 'posterize', 'niveis', 'cartoon'],
    params: {'niveis': EffectParam('Niveis', 6.0, 2.0, 32.0)},
    montar: ['niveis'],
    presets: [
      EffectPronto('Suave', {'niveis': 12}),
      EffectPronto('Medio', {'niveis': 6}),
      EffectPronto('Forte', {'niveis': 3}),
    ],
  ),

  // ------------------------------------------------------- lote 2

  /// REMAPEAMENTO DE TEMPO, igual ao do After Effects: em vez de mexer
  /// na velocidade, voce anima QUAL INSTANTE da camada aparece agora.
  /// Congelar, voltar, acelerar no meio — tudo vira keyframe de tempo.
  EffectType.opticalFlow: EffectSpec(
    id: 'optical_flow',
    name: 'Optical Flow',
    category: 'Time',
    synonyms: ['fluxo óptico', 'camera lenta', 'interpolação', 'slow motion'],
    params: {},
    montar: [],
    presets: [],
  ),
  EffectType.timeRemap: EffectSpec(
    id: 'time_remap',
    name: 'Time Remap',
    category: 'Time',
    synonyms: [
      'remapear',
      'tempo',
      'time remap',
      'tempo',
      'congelar',
      'freeze',
      'reverso',
      'velocidade',
      'speed ramp',
    ],
    cost: 1,
    params: {'tempo': EffectParam('Tempo (s)', 0.0, 0.0, 60.0)},
    montar: ['tempo'],
    presets: [
      EffectPronto('Inicio', {'tempo': 0}),
      EffectPronto('Meio', {'tempo': 2}),
      EffectPronto('Fim', {'tempo': 5}),
    ],
  ),

  EffectType.pixelSort: EffectSpec(
    id: 'pixel_sorter',
    name: 'Pixel Sorter',
    category: 'Stylize',
    synonyms: [
      'ordenar',
      'pixels',
      'pixel sort',
      'sorting',
      'databend',
      'arrastar',
      'derreter',
    ],
    cost: 3,
    params: {
      'mode': EffectParam(
        'Mode',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Linear', 'Radial', 'Circular'],
      ),

      // --- General ---
      'sort_angle': EffectParam('Sort Angle', 0.0, -360.0, 360.0),
      'threshold': EffectParam('Threshold', 0.3, 0.0, 1.0),
      'direction': EffectParam(
        'Direction',
        1.0,
        0.0,
        1.0,
        kind: ParamKind.choice,
        options: ['Below Threshold', 'Above Threshold'],
      ),
      'reverse_sort': EffectParam(
        'Reverse Sort',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.toggle,
      ),
      'random_restart': EffectParam('Random Restart', 100.0, 0.0, 1000.0),
      'seed': EffectParam('Seed', 0.273, 0.0, 999.0, kind: ParamKind.seed),
      'blend_with_original': EffectParam('Blend With Original', 0.0, 0.0, 1.0),
      // ORDENACAO POR CONTEUDO. Estes quatro ja estiveram na ficha, foram
      // tirados quando o pintor so esticava fatias por ruido, e voltam
      // agora que ele ordena pixels de verdade, em CPU.
      'sort_by': EffectParam(
        'Sort By',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Luminance', 'Hue', 'Saturation'],
      ),
      // A resolucao em que a ORDENACAO acontece (lado maior). Ordenar e
      // sequencial, em CPU: 360 sao poucos milissegundos e o preview
      // continua andando; para exportar, subir. O resultado e ampliado
      // de volta, e as faixas ficam mais grossas em resolucao menor.
      'sort_resolution': EffectParam('Sort Resolution', 720.0, 64.0, 1080.0),
      'downsample': EffectParam('Downsample', 1.0, 1.0, 4.0),
      // Desfoque 1D sobre o matte do limiar, em pixels do buffer: sem
      // ele, ruido de compressao abre e fecha trechos a cada pixel.
      'blur_threshold_matte': EffectParam(
        'Blur Threshold Matte',
        0.0,
        0.0,
        20.0,
      ),
      'show': EffectParam(
        'Show',
        0.0,
        0.0,
        3.0,
        kind: ParamKind.choice,
        options: ['Result', 'Raw Values', 'Threshold Matte', 'Restart Noise'],
      ),
      'soft_edges': EffectParam(
        'Soft Edges',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.toggle,
      ),

      // --- Radial ---
      'center_x': EffectParam(
        'Center X',
        0.5,
        0.0,
        1.0,
        kind: ParamKind.point,
        relative: true,
      ),
      'center_y': EffectParam(
        'Center Y',
        0.5,
        0.0,
        1.0,
        kind: ParamKind.point,
        relative: true,
      ),
      'start_angle': EffectParam('Start Angle', 0.0, -360.0, 360.0),
      'degrees_sorted': EffectParam('Degrees Sorted', 360.0, 0.0, 360.0),
      'inner_radius': EffectParam('Inner Radius', 0.1, 0.0, 2.0),
      'radius_length': EffectParam('Radius Length', 0.8, 0.0, 2.0),
      'radius_variation': EffectParam('Radius Variation', 0.1, 0.0, 1.0),

      // --- Circular ---
      'start_variation': EffectParam('Start Variation', 0.15, 0.0, 1.0),
      'thickness': EffectParam('Thickness', 1.1, 0.0, 4.0),
    },
    montar: ['threshold', 'sort_angle', 'blend_with_original'],
    presets: [
      EffectPronto('Sutil', {
        'threshold': 0.6,
        'sort_angle': 0,
        'blend_with_original': 0.5,
      }),
      EffectPronto('Medio', {
        'threshold': 0.3,
        'sort_angle': 0,
        'blend_with_original': 0.0,
      }),
      EffectPronto('Extremo', {
        'threshold': 0.1,
        'sort_angle': 90,
        'blend_with_original': 0.0,
      }),
    ],
  ),

  /// RASTREADOR DE BLOBS: os marcadores de rastreio como elemento
  /// grafico. Nao e visao computacional — sao alvos que voce posiciona e
  /// anima, que e para o que o efeito e usado em motion.
  EffectType.blobTracker: EffectSpec(
    id: 'blob_tracker',
    name: 'Blob Tracker',
    category: 'Stylize',
    synonyms: [
      'rastreador',
      'blobs',
      'blob tracker',
      'tracking',
      'alvo',
      'hud',
      'mira',
      'visao de maquina',
      'deteccao',
    ],
    hasColor: true,
    cost: 2,
    params: {
      // --- Detection ---
      'detect_by': EffectParam(
        'Detect By',
        0.0,
        0.0,
        3.0,
        kind: ParamKind.choice,
        options: ['Motion', 'Brightness', 'Color Key', 'Edges'],
      ),
      'threshold': EffectParam('Threshold', 35.0, 0.0, 100.0),
      'sensitivity': EffectParam('Sensitivity', 50.0, 0.0, 100.0),
      'min_blob_size': EffectParam('Min Blob Size', 400.0, 0.0, 20000.0),
      'max_blob_size': EffectParam('Max Blob Size', 0.0, 0.0, 200000.0),
      'max_blobs': EffectParam('Max Blobs', 20.0, 1.0, 100.0),
      'merge_distance': EffectParam('Merge Distance', 20.0, 0.0, 200.0),
      'persistence': EffectParam('Persistence', 8.0, 0.0, 60.0),
      'smoothing': EffectParam('Smoothing', 40.0, 0.0, 100.0),

      // --- Overlay ---
      'style': EffectParam(
        'Style',
        0.0,
        0.0,
        4.0,
        kind: ParamKind.choice,
        options: ['Full Box', 'Corner Box', 'Circle', 'Crosshair', 'None'],
      ),
      'show_center_marker': EffectParam(
        'Show Center Marker',
        1.0,
        0.0,
        1.0,
        kind: ParamKind.toggle,
      ),
      'show_connecting_lines': EffectParam(
        'Show Connecting Lines',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.toggle,
      ),
      'line_type': EffectParam(
        'Line Type',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Nearest', 'All Pairs', 'To Centroid'],
      ),
      'line_style': EffectParam(
        'Line Style',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Solid', 'Dashed', 'Dotted'],
      ),
      'palette': EffectParam(
        'Palette',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Single', 'Per-ID', 'Random'],
      ),
      'thickness': EffectParam('Thickness', 2.0, 0.5, 12.0),
      'opacity': EffectParam('Opacity', 100.0, 0.0, 100.0),
      'fill': EffectParam('Fill', 0.0, 0.0, 100.0),
      'corner_length': EffectParam('Corner Length', 20.0, 1.0, 50.0),

      // --- Labels ---
      'show_caption': EffectParam(
        'Show Caption',
        1.0,
        0.0,
        1.0,
        kind: ParamKind.toggle,
      ),
      'caption_content': EffectParam(
        'Caption Content',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['ID', 'ID + Size', 'ID + Coordinates'],
      ),
      'caption_position': EffectParam(
        'Caption Position',
        0.0,
        0.0,
        3.0,
        kind: ParamKind.choice,
        options: ['Top Left', 'Top Right', 'Bottom', 'Inside'],
      ),
      'font_size': EffectParam('Font Size', 12.0, 6.0, 48.0),

      // --- Composite ---
      'blend_mode': EffectParam(
        'Blend Mode',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Normal', 'Add', 'Screen'],
      ),
      'overlay_only': EffectParam(
        'Overlay Only',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.toggle,
      ),
      'seed': EffectParam('Seed', 0.0, 0.0, 999.0, kind: ParamKind.seed),
    },
    montar: ['threshold', 'sensitivity', 'max_blobs'],
    presets: [
      EffectPronto('Poucos', {
        'threshold': 50,
        'sensitivity': 40,
        'max_blobs': 5,
      }),
      EffectPronto('Medio', {
        'threshold': 35,
        'sensitivity': 50,
        'max_blobs': 20,
      }),
      EffectPronto('Muitos', {
        'threshold': 20,
        'sensitivity': 70,
        'max_blobs': 60,
      }),
    ],
  ),

  EffectType.turbulentDisplace: EffectSpec(
    id: 'turbulent_displace',
    name: 'Turbulent Displace',
    category: 'Distort',
    synonyms: [
      'deslocar',
      'turbulento',
      'turbulent displace',
      'turbulencia',
      'ondular',
      'liquido',
      'warp',
    ],
    cost: 3,
    params: {
      'quantidade': EffectParam('Quantidade', 40.0, 0.0, 300.0, relative: true),
      'tamanho': EffectParam('Tamanho', 60.0, 5.0, 300.0, relative: true),
      'complexidade': EffectParam('Complexidade', 2.0, 1.0, 5.0),
      'evolucao': EffectParam('Evolucao', 0.0, -3600.0, 3600.0),
      'semente': EffectParam('Semente', 1.0, 1.0, 999.0, kind: ParamKind.seed),
    },
    montar: ['quantidade', 'tamanho', 'complexidade'],
    presets: [
      EffectPronto('Leve', {
        'quantidade': 15,
        'tamanho': 80,
        'complexidade': 2,
      }),
      EffectPronto('Medio', {
        'quantidade': 40,
        'tamanho': 60,
        'complexidade': 2,
      }),
      EffectPronto('Forte', {
        'quantidade': 120,
        'tamanho': 40,
        'complexidade': 4,
      }),
    ],
  ),

  /// MASCARA DE NITIDEZ de verdade: original + quantidade * (original -
  /// borrado), com limiar para nao realcar ruido.
  EffectType.unsharpMask: EffectSpec(
    id: 'unsharp_mask',
    name: 'Unsharp Mask',
    category: 'Lens',
    synonyms: [
      'mascara',
      'nitidez',
      'unsharp mask',
      'nitidez',
      'sharpen',
      'foco',
    ],
    params: {
      'quantidade': EffectParam('Quantidade', 0.8, 0.0, 3.0),
      'raio': EffectParam('Raio', 3.0, 0.5, 40.0, relative: true),
      'limiar': EffectParam('Limiar', 0.0, 0.0, 1.0),
    },
    montar: ['quantidade', 'raio'],
    presets: [
      EffectPronto('Leve', {'quantidade': 0.4, 'raio': 2}),
      EffectPronto('Medio', {'quantidade': 0.8, 'raio': 3}),
      EffectPronto('Forte', {'quantidade': 1.8, 'raio': 6}),
    ],
  ),

  EffectType.motionTile: EffectSpec(
    id: 'motion_tile',
    name: 'Motion Tile',
    category: 'Stylize',
    synonyms: [
      'mosaico',
      'movimento',
      'motion tile',
      'ladrilho',
      'repetir',
      'tile',
      'espelhar',
    ],
    cost: 2,
    params: {
      // Todas as medidas sao % DAS DIMENSOES DA CAMADA DE ENTRADA, como
      // no After Effects. Em pixel, o mesmo numero daria ladrilhos de
      // tamanhos diferentes ao trocar a resolucao.
      'tile_center': EffectParam(
        'Tile Center X',
        0.5,
        0.0,
        1.0,
        kind: ParamKind.point,
        relative: true,
      ),
      'tile_center_y': EffectParam(
        'Tile Center Y',
        0.5,
        0.0,
        1.0,
        kind: ParamKind.point,
        relative: true,
      ),
      'tile_width': EffectParam('Tile Width', 100.0, 1.0, 300.0),
      'tile_height': EffectParam('Tile Height', 100.0, 1.0, 300.0),
      'output_width': EffectParam('Output Width', 100.0, 1.0, 600.0),
      'output_height': EffectParam('Output Height', 100.0, 1.0, 600.0),
      'mirror_edges': EffectParam(
        'Mirror Edges',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.toggle,
      ),
      'phase': EffectParam('Phase', 0.0, -360.0, 360.0),
      'horizontal_phase_shift': EffectParam(
        'Horizontal Phase Shift',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.toggle,
      ),
    },
    montar: ['tile_width', 'tile_height', 'output_width'],
    presets: [
      EffectPronto('Ladrilho 2x2', {'tile_width': 50, 'tile_height': 50}),
      EffectPronto('Ladrilho 3x3', {'tile_width': 33.3, 'tile_height': 33.3}),
      EffectPronto('Espelhado', {
        'tile_width': 50,
        'tile_height': 50,
        'mirror_edges': 1,
      }),
    ],
  ),

  EffectType.bend: EffectSpec(
    id: 'bend',
    name: 'Bend',
    category: 'Distort',
    synonyms: ['entortar', 'bend', 'curvar', 'arco', 'entortar', 'wave warp'],
    cost: 2,
    params: {
      'quantidade': EffectParam(
        'Quantidade',
        40.0,
        -300.0,
        300.0,
        relative: true,
      ),
      'eixo': EffectParam(
        'Eixo',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.choice,
        options: ['Horizontal', 'Vertical'],
      ),
      'curvatura': EffectParam('Curvatura', 1.0, 0.2, 4.0),
      'ancora': EffectParam('Ancora', 0.5, 0.0, 1.0),
    },
    montar: ['quantidade', 'curvatura'],
    presets: [
      EffectPronto('Leve', {'quantidade': 15, 'curvatura': 1}),
      EffectPronto('Medio', {'quantidade': 40, 'curvatura': 1}),
      EffectPronto('Forte', {'quantidade': 120, 'curvatura': 2}),
    ],
  ),

  /// CC SEMEAR (CC Scatterize): quebra a imagem em graos e espalha.
  EffectType.ccScatterize: EffectSpec(
    id: 'seed',
    name: 'Seed',
    category: 'Stylize',
    synonyms: [
      'semear',
      'cc scatterize',
      'semear',
      'dispersar',
      'scatter',
      'desintegrar',
      'particulas',
    ],
    cost: 3,
    params: {
      'dispersao': EffectParam('Dispersao', 60.0, 0.0, 400.0, relative: true),
      'grao': EffectParam('Grao', 24.0, 4.0, 120.0, relative: true),
      'rotacao': EffectParam('Rotacao', 0.0, -180.0, 180.0),
      'transferencia': EffectParam('Transferencia', 1.0, 0.0, 1.0),
      'gravidade': EffectParam('Gravidade', 0.0, -1.0, 1.0),
      'semente': EffectParam('Semente', 3.0, 1.0, 999.0, kind: ParamKind.seed),
    },
    montar: ['dispersao', 'grao'],
    presets: [
      EffectPronto('Leve', {'dispersao': 20, 'grao': 16}),
      EffectPronto('Medio', {'dispersao': 60, 'grao': 24}),
      EffectPronto('Forte', {'dispersao': 180, 'grao': 40}),
    ],
  ),

  /// CC SPLIT: a imagem se abre em duas metades a partir de dois pontos.
  EffectType.ccSplit: EffectSpec(
    id: 'split',
    name: 'Split',
    category: 'Distort',
    synonyms: ['split', 'cc split', 'dividir', 'rasgar', 'abrir', 'separar'],
    cost: 2,
    params: {
      'divisao': EffectParam('Divisao', 40.0, 0.0, 400.0, relative: true),
      'angulo': EffectParam('Angulo', 0.0, -180.0, 180.0),
      'centro': EffectParam('Centro', 0.5, 0.0, 1.0),
      'suavidade': EffectParam('Suavidade', 0.0, 0.0, 1.0),
    },
    montar: ['divisao', 'angulo'],
    presets: [
      EffectPronto('Leve', {'divisao': 15}),
      EffectPronto('Medio', {'divisao': 40}),
      EffectPronto('Forte', {'divisao': 120}),
    ],
  ),

  EffectType.vhs: EffectSpec(
    id: 'vhs',
    name: 'VHS',
    category: 'Stylize',
    synonyms: ['vhs', 'vhs', 'fita', 'analogico', 'retro', 'tv', 'scanline'],
    cost: 2,
    params: {
      'intensidade': EffectParam('Intensidade', 0.6, 0.0, 1.0),
      'linhas': EffectParam('Linhas', 0.5, 0.0, 1.0),
      'sangramento': EffectParam('Sangramento', 0.5, 0.0, 1.0),
      'tremor': EffectParam('Tremor', 0.35, 0.0, 1.0),
      'ruido': EffectParam('Ruido', 0.3, 0.0, 1.0),
      'desbotar': EffectParam('Desbotar', 0.4, 0.0, 1.0),
      'semente': EffectParam('Semente', 5.0, 1.0, 999.0, kind: ParamKind.seed),
    },
    montar: ['intensidade', 'linhas', 'ruido'],
    presets: [
      EffectPronto('Leve', {'intensidade': 0.3, 'linhas': 0.3, 'ruido': 0.15}),
      EffectPronto('Medio', {'intensidade': 0.6, 'linhas': 0.5, 'ruido': 0.3}),
      EffectPronto('Forte', {'intensidade': 1.0, 'linhas': 0.8, 'ruido': 0.6}),
    ],
  ),

  EffectType.filmDamage: EffectSpec(
    id: 'film_damage',
    name: 'Film Damage',
    category: 'Stylize',
    synonyms: [
      'filme',
      'danificado',
      'film damage',
      'filme',
      'velho',
      'riscos',
      'poeira',
      'super 8',
      'granulado',
      's_filmdamage',
      'filmdamage',
      'dano de filme',
    ],
    cost: 2,
    params: {
      'poeira': EffectParam('Poeira', 0.5, 0.0, 1.0),
      'riscos': EffectParam('Riscos', 0.4, 0.0, 1.0),
      'cintilacao': EffectParam('Cintilacao', 0.35, 0.0, 1.0),
      'granulacao': EffectParam('Granulacao', 0.4, 0.0, 1.0),
      'queimado': EffectParam('Queimado', 0.3, 0.0, 1.0),
      'salto': EffectParam('Salto de quadro', 0.25, 0.0, 1.0),
      'semente': EffectParam('Semente', 11.0, 1.0, 999.0, kind: ParamKind.seed),
      // S_FILMDAMAGE 2 (dono, 14/09/2026): SEMPRE NO FIM e todos neutros
      // no inicial. Projeto salvo sem estas chaves le o inicial da ficha
      // e abre exatamente como antes; os slots do shader dos sete de
      // cima nao mudam de lugar.
      'fios': EffectParam('Fios', 0, 0, 10),
      'balanco': EffectParam('Balanço', 0, 0, 1),
      'desfoque': EffectParam('Desfoque', 0, 0, 1),
      'vinheta': EffectParam('Vinheta', 0, 0, 1),
      'saturacao': EffectParam('Saturação', 1, 0, 2),
      'sepia': EffectParam('Tom sépia', 0, 0, 1),
      'tamanho_poeira': EffectParam('Tamanho da poeira', 1, .5, 3),
    },
    montar: ['poeira', 'riscos', 'cintilacao'],
    presets: [
      EffectPronto('Leve', {'poeira': 0.2, 'riscos': 0.15, 'cintilacao': 0.15}),
      EffectPronto('Medio', {'poeira': 0.5, 'riscos': 0.4, 'cintilacao': 0.35}),
      EffectPronto('Forte', {'poeira': 0.9, 'riscos': 0.8, 'cintilacao': 0.6}),
    ],
  ),

  EffectType.glitchify: EffectSpec(
    id: 'glitchify',
    name: 'Glitchify',
    category: 'Glitch',
    synonyms: [
      'glitchify',
      'glitch',
      'glitchify',
      'datamosh',
      'erro',
      'digital',
      'corromper',
    ],
    cost: 3,
    params: {
      'intensidade': EffectParam('Intensidade', 0.6, 0.0, 1.0),
      'blocos': EffectParam('Blocos', 8.0, 1.0, 40.0),
      'deslocamento': EffectParam(
        'Deslocamento',
        60.0,
        0.0,
        400.0,
        relative: true,
      ),
      'cor': EffectParam('Separacao de cor', 0.5, 0.0, 1.0),
      'velocidade': EffectParam('Velocidade', 8.0, 0.5, 40.0),
      'ruidoLinha': EffectParam('Linhas de erro', 0.4, 0.0, 1.0),
      'semente': EffectParam('Semente', 13.0, 1.0, 999.0, kind: ParamKind.seed),
    },
    montar: ['intensidade', 'blocos', 'deslocamento'],
    presets: [
      EffectPronto('Sutil', {
        'intensidade': 0.3,
        'blocos': 4,
        'deslocamento': 20,
      }),
      EffectPronto('Medio', {
        'intensidade': 0.6,
        'blocos': 8,
        'deslocamento': 60,
      }),
      EffectPronto('Caotico', {
        'intensidade': 1.0,
        'blocos': 20,
        'deslocamento': 200,
      }),
    ],
  ),

  EffectType.flicker: EffectSpec(
    id: 'flicker',
    name: 'Flicker',
    category: 'Stylize',
    synonyms: ['piscar', 'flicker', 'cintilar', 'strobe', 'tremular', 'luz'],
    cost: 1,
    params: {
      'amount': EffectParam('Intensidade', 0.6, 0.0, 1.0),
      'frequency': EffectParam('Frequencia', 12.0, 0.5, 60.0),
      'style': EffectParam(
        'Estilo',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Aleatorio', 'Strobe', 'Senoide'],
      ),
      'target': EffectParam(
        'Age em',
        0.0,
        0.0,
        1.0,
        kind: ParamKind.choice,
        options: ['Opacidade', 'Brilho'],
      ),
      'seed': EffectParam('Seed', 0.0, 0.0, 100.0, kind: ParamKind.seed),
    },
    montar: ['amount', 'frequency'],
    presets: [
      EffectPronto('Suave', {'amount': 0.3, 'frequency': 6}),
      EffectPronto('Medio', {'amount': 0.6, 'frequency': 12}),
      EffectPronto('Forte', {'amount': 1.0, 'frequency': 30}),
    ],
  ),

  EffectType.gradient4: EffectSpec(
    id: 'gradient4',
    name: 'Gradiente 4 cores',
    category: 'Color',
    synonyms: [
      'gradiente',
      'degrade',
      'quatro cores',
      '4 cores',
      'gradient',
      'cantos',
    ],
    cost: 1,
    hasColor: true,
    extraColors: 3,
    params: {
      'opacity': EffectParam('Opacidade', 1.0, 0.0, 1.0),
      'blend': EffectParam(
        'Mescla',
        0.0,
        0.0,
        3.0,
        kind: ParamKind.choice,
        options: ['Normal', 'Multiplicar', 'Tela', 'Sobrepor'],
      ),
      'angle': EffectParam('Giro', 0.0, -180.0, 180.0),
    },
    montar: ['opacity', 'angle'],
    presets: [
      EffectPronto('Suave', {'opacity': 0.5, 'angle': 0}),
      EffectPronto('Medio', {'opacity': 1.0, 'angle': 0}),
      EffectPronto('Diagonal', {'opacity': 1.0, 'angle': 45}),
    ],
  ),

  EffectType.liquidGlass: EffectSpec(
    id: 'liquid_glass',
    name: 'Vidro fosco',
    category: 'Stylize',
    synonyms: [
      'vidro',
      'glass',
      'liquid glass',
      'ios 26',
      'glassmorphism',
      'blur atras',
      'lente',
    ],
    cost: 2,
    hasColor: true,
    montar: ['blur', 'saturation', 'brightness'],
    presets: [
      EffectPronto('Vidro fosco', {
        'blur': 22,
        'saturation': 112,
        'brightness': 104,
        'refraction': 0,
        'rim': 0.32,
        'tint': 0.10,
        'grain': 0.015,
      }),
      EffectPronto('Vidro claro', {
        'blur': 30,
        'saturation': 118,
        'brightness': 108,
        'refraction': 0,
        'rim': 0.48,
        'tint': 0.08,
        'grain': 0.01,
      }),
      EffectPronto('Vidro escuro', {
        'blur': 26,
        'saturation': 90,
        'brightness': 86,
        'refraction': 0,
        'rim': 0.22,
        'tint': 0.18,
        'grain': 0.02,
      }),
    ],
    params: {
      'blur': EffectParam('Desfoque', 22.0, 0.0, 40.0),
      'saturation': EffectParam('Saturacao (%)', 112.0, 0.0, 200.0),
      'brightness': EffectParam('Brilho (%)', 104.0, 0.0, 200.0),
      'refraction': EffectParam('Refracao', 0.0, 0.0, 1.0),
      'rim': EffectParam('Brilho da borda', 0.32, 0.0, 1.0),
      'tint': EffectParam('Tingir', 0.10, 0.0, 1.0),
      'grain': EffectParam('Grao', 0.015, 0.0, 0.12),
      'radius': EffectParam('Cantos', 28.0, 0.0, 200.0),
      'shadow': EffectParam('Sombra', 0.35, 0.0, 1.0),
      'padding': EffectParam('Folga', 24.0, 0.0, 120.0),
    },
  ),

  EffectType.corrections: EffectSpec(
    id: 'corrections',
    name: 'Correcoes',
    category: 'Color',
    synonyms: [
      'correcao',
      'correção',
      'exposicao',
      'exposure',
      'contraste',
      'altas',
      'sombras',
      'temperatura',
      'lumetri',
      'basico',
      'ajustes',
      'highlights',
      'shadows',
    ],
    cost: 1,
    params: {
      'exposicao': EffectParam('Exposicao', 0.0, -3.0, 3.0),
      'contraste': EffectParam('Contraste', 0.0, -1.0, 1.0),
      'altas': EffectParam('Altas luzes', 0.0, -1.0, 1.0),
      'sombras': EffectParam('Sombras', 0.0, -1.0, 1.0),
      'temperatura': EffectParam('Temperatura', 0.0, -1.0, 1.0),
      'matiz': EffectParam('Verde / magenta', 0.0, -1.0, 1.0),
      'saturacao': EffectParam('Saturacao', 0.0, -1.0, 1.0),
      'gama': EffectParam('Gama', 1.0, 0.3, 3.0),
    },
    montar: ['exposicao', 'contraste', 'saturacao'],
    presets: [
      EffectPronto('Suave', {
        'exposicao': 0.2,
        'contraste': 0.1,
        'saturacao': 0.1,
      }),
      EffectPronto('Medio', {
        'exposicao': 0.4,
        'contraste': 0.25,
        'saturacao': 0.2,
      }),
      EffectPronto('Forte', {
        'exposicao': 0.8,
        'contraste': 0.5,
        'saturacao': 0.4,
      }),
    ],
  ),

  EffectType.forceMotionBlur: EffectSpec(
    id: 'force_motion_blur',
    name: 'Force Motion Blur',
    category: 'Blur',
    synonyms: [
      'motion blur',
      'borrao',
      'movimento',
      'forcar',
      'obturador',
      'shutter',
    ],
    cost: 3,
    params: {
      // Mais amostras do que a composicao permite, e funciona SEM
      // keyframe de transform: pega tambem o movimento que veio de
      // efeito, que o motion blur da composicao nao ve.
      'samples': EffectParam('Motion Blur Samples', 16.0, 2.0, 64.0),
      'shutter_angle': EffectParam('Shutter Angle', 180.0, 0.0, 720.0),
      'native_motion_blur': EffectParam(
        'Native Motion Blur',
        0.0,
        0.0,
        2.0,
        kind: ParamKind.choice,
        options: ['Off', 'On', 'Only'],
      ),
    },
    montar: ['samples', 'shutter_angle'],
    presets: [
      EffectPronto('Leve', {'samples': 8, 'shutter_angle': 90}),
      EffectPronto('Medio', {'samples': 16, 'shutter_angle': 180}),
      EffectPronto('Forte', {'samples': 32, 'shutter_angle': 360}),
    ],
  ),
  EffectType.twirl: EffectSpec(
    id: 'twirl',
    synonyms: ['torcer', 'redemoinho'],
    name: 'Twirl',
    category: 'Distort',
    params: {
      'amount': EffectParam('Angulo', 180, -1440, 1440),
      'radius': EffectParam('Raio', .65, .01, 3),
      'center_x': EffectParam('Centro X', .5, -2, 3),
      'center_y': EffectParam('Centro Y', .5, -2, 3),
    },
  ),
  EffectType.fisheye: EffectSpec(
    id: 'fisheye',
    synonyms: ['olho de peixe', 'lente'],
    name: 'Fisheye',
    category: 'Lens',
    params: {
      'amount': EffectParam('Distorcao', 1, -3, 3),
      'radius': EffectParam('Raio', 1, .01, 3),
      'center_x': EffectParam('Centro X', .5, -2, 3),
      'center_y': EffectParam('Centro Y', .5, -2, 3),
    },
  ),
  EffectType.kaleidoscope: EffectSpec(
    id: 'kaleidoscope',
    synonyms: ['caleidoscopio', 'espelho'],
    name: 'Kaleidoscope',
    category: 'Distort',
    params: {
      'amount': EffectParam('Mistura', 1, 0, 1),
      'count': EffectParam('Segmentos', 6, 1, 32),
      'angle': EffectParam('Rotacao', 0, -360, 360),
      'center_x': EffectParam('Centro X', .5, -2, 3),
      'center_y': EffectParam('Centro Y', .5, -2, 3),
    },
  ),
  EffectType.venetianBlinds: EffectSpec(
    id: 'venetian_blinds',
    synonyms: ['persianas', 'faixas'],
    name: 'Venetian Blinds',
    category: 'Keying',
    params: {
      'amount': EffectParam('Conclusao', .5, 0, 1),
      'count': EffectParam('Faixas', 12, 1, 200),
      'angle': EffectParam('Direcao', 90, -360, 360),
      'radius': EffectParam('Suavidade', .01, .0001, .3),
    },
  ),
  EffectType.blockDissolve: EffectSpec(
    id: 'block_dissolve',
    synonyms: ['blocos', 'dissolver'],
    name: 'Block Dissolve',
    category: 'Keying',
    params: {
      'amount': EffectParam('Conclusao', .5, 0, 1),
      'count': EffectParam('Blocos', 32, 1, 200),
      'seed': EffectParam('Semente', 0, 0, 1000),
    },
  ),
  EffectType.offset: EffectSpec(
    id: 'offset',
    synonyms: ['deslocamento', 'repetir'],
    name: 'Offset',
    category: 'Distort',
    params: {
      'center_x': EffectParam('Centro X', .5, -2, 3),
      'center_y': EffectParam('Centro Y', .5, -2, 3),
    },
  ),
  EffectType.invert: EffectSpec(
    id: 'invert',
    synonyms: ['inverter', 'negativo'],
    name: 'Invert',
    category: 'Color',
    params: {'amount': EffectParam('Mistura', 1, 0, 1)},
  ),
  EffectType.waveWarp: EffectSpec(
    id: 'wave_warp',
    synonyms: ['onda', 'ondular'],
    name: 'Wave Warp',
    category: 'Distort',
    params: {
      'amount': EffectParam('Amplitude', .04, -.5, .5),
      'count': EffectParam('Frequencia', 4, .1, 100),
      'angle': EffectParam('Fase', 0, -3600, 3600),
      'radius': EffectParam('Eixo Y', 0, 0, 1),
    },
  ),

  // ------------------------------------------------------------------
  // COLORING DE EDIT. "Coloring" e a pilha de ajustes que a comunidade
  // de edits copia do Photoshop/After Effects (e dos presets "CC" do
  // Alight Motion): equilibrio por faixa tonal, cor seletiva, mistura
  // de canais, filtro de foto, mapa de gradiente em Soft Light,
  // brilho/contraste e rodas lift/gamma/gain. As contas seguem
  // referencias publicas (FFmpeg e W3C Compositing) — as da Adobe nao
  // sao publicadas — e rodam no shader.
  // ------------------------------------------------------------------

  // Pesos por luminosidade do FFmpeg (vf_colorbalance): sombras, meios e
  // altas se sobrepoem sem degrau.
  EffectType.colorBalance: EffectSpec(
    id: 'color_balance',
    name: 'Color Balance',
    category: 'Color',
    synonyms: [
      'color balance',
      'equilibrio de cor',
      'balanco de cor',
      'coloring',
      'cc',
      'teal orange',
      'grading',
      'sombras meios altas',
    ],
    params: {
      'shadow_red': EffectParam('Shadow Red Balance', 0, -100, 100),
      'shadow_green': EffectParam('Shadow Green Balance', 0, -100, 100),
      'shadow_blue': EffectParam('Shadow Blue Balance', 0, -100, 100),
      'midtone_red': EffectParam('Midtone Red Balance', 0, -100, 100),
      'midtone_green': EffectParam('Midtone Green Balance', 0, -100, 100),
      'midtone_blue': EffectParam('Midtone Blue Balance', 0, -100, 100),
      'highlight_red': EffectParam('Highlight Red Balance', 0, -100, 100),
      'highlight_green': EffectParam('Highlight Green Balance', 0, -100, 100),
      'highlight_blue': EffectParam('Highlight Blue Balance', 0, -100, 100),
      'preserve_luminosity': EffectParam(
        'Preserve Luminosity',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
    },
    montar: ['shadow_blue', 'midtone_red', 'highlight_red'],
    presets: [
      EffectPronto('Teal & Orange', {
        'shadow_red': -12,
        'shadow_green': 2,
        'shadow_blue': 15,
        'highlight_red': 10,
        'highlight_green': 3,
        'highlight_blue': -8,
      }),
      EffectPronto('Anime', {
        'shadow_red': -10,
        'shadow_green': 30,
        'shadow_blue': 40,
        'midtone_red': -5,
        'midtone_green': -5,
        'midtone_blue': 20,
        'highlight_red': 20,
        'highlight_green': 10,
        'highlight_blue': 20,
      }),
      EffectPronto('PSD Soft', {
        'midtone_red': 16,
        'midtone_green': 12,
        'midtone_blue': -2,
        'highlight_blue': 13,
      }),
    ],
  ),

  // Uma faixa de cor por instancia (como a lista do AE): empilhe duas
  // instancias para mexer nos vermelhos e nos neutros. Conta do FFmpeg
  // (vf_selectivecolor), relativa por padrao como no Photoshop.
  EffectType.selectiveColor: EffectSpec(
    id: 'selective_color',
    name: 'Selective Color',
    category: 'Color',
    synonyms: [
      'selective color',
      'cor seletiva',
      'correcao seletiva',
      'cmyk',
      'coloring',
      'cc',
    ],
    params: {
      'colors': EffectParam(
        'Colors',
        0,
        0,
        8,
        kind: ParamKind.choice,
        options: [
          'Reds',
          'Yellows',
          'Greens',
          'Cyans',
          'Blues',
          'Magentas',
          'Whites',
          'Neutrals',
          'Blacks',
        ],
      ),
      'cyan': EffectParam('Cyan', 0, -100, 100),
      'magenta': EffectParam('Magenta', 0, -100, 100),
      'yellow': EffectParam('Yellow', 0, -100, 100),
      'black': EffectParam('Black', 0, -100, 100),
      'method': EffectParam(
        'Method',
        0,
        0,
        1,
        kind: ParamKind.choice,
        options: ['Relative', 'Absolute'],
      ),
    },
    montar: ['cyan', 'magenta', 'yellow'],
    presets: [
      EffectPronto('Warm Skin', {
        'colors': 0,
        'cyan': 3,
        'magenta': 10,
        'yellow': 12,
        'black': -3,
      }),
      EffectPronto('Cyan Pop', {'colors': 3, 'cyan': 27}),
      EffectPronto('Warm Whites', {'colors': 6, 'cyan': -30, 'yellow': 15}),
    ],
  ),

  EffectType.channelMixer: EffectSpec(
    id: 'channel_mixer',
    name: 'Channel Mixer',
    category: 'Color',
    synonyms: [
      'channel mixer',
      'misturador de canais',
      'canais',
      'preto e branco',
      'monocromatico',
      'coloring',
    ],
    params: {
      'red_red': EffectParam('Red-Red', 100, -200, 200),
      'red_green': EffectParam('Red-Green', 0, -200, 200),
      'red_blue': EffectParam('Red-Blue', 0, -200, 200),
      'red_const': EffectParam('Red-Const', 0, -200, 200),
      'green_red': EffectParam('Green-Red', 0, -200, 200),
      'green_green': EffectParam('Green-Green', 100, -200, 200),
      'green_blue': EffectParam('Green-Blue', 0, -200, 200),
      'green_const': EffectParam('Green-Const', 0, -200, 200),
      'blue_red': EffectParam('Blue-Red', 0, -200, 200),
      'blue_green': EffectParam('Blue-Green', 0, -200, 200),
      'blue_blue': EffectParam('Blue-Blue', 100, -200, 200),
      'blue_const': EffectParam('Blue-Const', 0, -200, 200),
      'monochrome': EffectParam(
        'Monochrome',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
    },
    montar: ['red_red', 'green_green', 'blue_blue'],
    presets: [
      EffectPronto('PSD Mix', {
        'red_red': 113,
        'red_green': -15,
        'green_red': -8,
        'green_green': 104,
        'green_blue': 3,
      }),
      EffectPronto('Black & White', {
        'monochrome': 1,
        'red_red': 50,
        'red_green': 40,
        'red_blue': 10,
      }),
      EffectPronto('Cross Process', {
        'red_blue': 10,
        'blue_green': 20,
        'blue_blue': 90,
      }),
    ],
  ),

  // Filtro de cor (Photoshop) OU temperatura em Kelvin (Alight Motion).
  // Kelvin maior = luz mais fria. A temperatura e aplicada em luz linear
  // e normalizada pela luminancia: esquentar nao escurece.
  EffectType.photoFilter: EffectSpec(
    id: 'photo_filter',
    name: 'Photo Filter',
    category: 'Color',
    hasColor: true,
    defaultColor: Color(0xFFEC8A00),
    synonyms: [
      'photo filter',
      'filtro de foto',
      'color temperature',
      'temperatura de cor',
      'kelvin',
      'quente',
      'frio',
      'warm',
      'cool',
    ],
    params: {
      'mode': EffectParam(
        'Mode',
        0,
        0,
        1,
        kind: ParamKind.choice,
        options: ['Color', 'Temperature'],
      ),
      'density': EffectParam('Density', 25, 0, 100),
      'temperature': EffectParam('Temperature (K)', 6500, 1000, 40000),
      'preserve_luminosity': EffectParam(
        'Preserve Luminosity',
        1,
        0,
        1,
        kind: ParamKind.toggle,
      ),
    },
    montar: ['density', 'temperature'],
    presets: [
      EffectPronto('Warm Vintage', {
        'mode': 1,
        'temperature': 4800,
        'density': 60,
      }),
      EffectPronto('Cool', {'mode': 1, 'temperature': 9000, 'density': 50}),
      EffectPronto(
        'Warming Filter',
        {'mode': 0, 'density': 25},
        cor: Color(0xFFEC8A00),
      ),
    ],
  ),

  // Mapa de gradiente pela luminancia: sombra, meio-tom e luz, misturado
  // no modo escolhido. Em Soft Light com opacidade baixa e o "coloring"
  // classico; com o meio-tom desligado vira duotone.
  EffectType.gradientMap: EffectSpec(
    id: 'gradient_map',
    name: 'Gradient Map',
    category: 'Color',
    hasColor: true,
    defaultColor: Color(0xFF0B2A3A),
    extraColors: 2,
    defaultExtraColors: [Color(0xFF4EABCD), Color(0xFFE8F4FF)],
    synonyms: [
      'gradient map',
      'mapa de gradiente',
      'tritone',
      'tritom',
      'duotone',
      'duotom',
      'toner',
      'coloring',
      'soft light',
    ],
    params: {
      'blend_mode': EffectParam(
        'Blending Mode',
        1,
        0,
        6,
        kind: ParamKind.choice,
        options: [
          'Normal',
          'Soft Light',
          'Overlay',
          'Multiply',
          'Screen',
          'Color',
          'Luminosity',
        ],
      ),
      'opacity': EffectParam('Opacity', 35, 0, 100),
      'midtones': EffectParam(
        'Use Midtones',
        1,
        0,
        1,
        kind: ParamKind.toggle,
      ),
      'balance': EffectParam('Midpoint', 50, 5, 95),
    },
    montar: ['opacity', 'balance'],
    presets: [
      EffectPronto('Soft Light', {'blend_mode': 1, 'opacity': 30}),
      EffectPronto('Duotone', {
        'blend_mode': 0,
        'opacity': 100,
        'midtones': 0,
      }),
      EffectPronto('Tritone', {'blend_mode': 0, 'opacity': 88}),
    ],
  ),

  // Brilho e contraste no estilo do Alight Motion: brilho empurra para o
  // branco (ou para o preto) sem estourar, contraste gira em torno do
  // cinza medio. As duas contas sao lineares.
  EffectType.brightnessContrast: EffectSpec(
    id: 'brightness_contrast',
    name: 'Brightness & Contrast',
    category: 'Color',
    synonyms: [
      'brightness',
      'contrast',
      'brilho',
      'contraste',
      'brightness contrast',
      'brightness and contrast',
      'brilho e contraste',
      'brilho/contraste',
      'cc',
    ],
    params: {
      'brightness': EffectParam('Brightness', 0, -100, 100),
      'contrast': EffectParam('Contrast', 0, -100, 300),
    },
    montar: ['brightness', 'contrast'],
    presets: [
      EffectPronto('PSD Contrast', {'contrast': 33}),
      EffectPronto('Punch', {'brightness': 5, 'contrast': 25}),
      EffectPronto('Faded', {'brightness': 8, 'contrast': -20}),
    ],
  ),

  // Rodas lift/gamma/gain/offset, cada uma com matiz, saturacao e
  // luminancia (os nomes do Color Tune do Alight Motion). O vetor de cor
  // da roda tem luminancia zero: girar a matiz tinge sem clarear.
  EffectType.colorTune: EffectSpec(
    id: 'color_tune',
    name: 'Color Tune',
    category: 'Color',
    synonyms: [
      'color tune',
      'lift gamma gain',
      'rodas de cor',
      'color wheels',
      'coloring',
      'grading',
      'cc',
    ],
    params: {
      'lift_hue': EffectParam('Lift Hue', 0, 0, 360),
      'lift_saturation': EffectParam('Lift Saturation', 0, 0, 100),
      'lift_luminance': EffectParam('Lift Luminance', 0, -1, 1),
      'gamma_hue': EffectParam('Gamma Hue', 0, 0, 360),
      'gamma_saturation': EffectParam('Gamma Saturation', 0, 0, 100),
      'gamma_luminance': EffectParam('Gamma Luminance', 0, -1, 1),
      'gain_hue': EffectParam('Gain Hue', 0, 0, 360),
      'gain_saturation': EffectParam('Gain Saturation', 0, 0, 100),
      'gain_luminance': EffectParam('Gain Luminance', 0, -1, 1),
      'offset_hue': EffectParam('Offset Hue', 0, 0, 360),
      'offset_saturation': EffectParam('Offset Saturation', 0, 0, 100),
      'offset_luminance': EffectParam('Offset Luminance', 0, -1, 1),
    },
    montar: ['lift_luminance', 'gamma_luminance', 'gain_luminance'],
    presets: [
      EffectPronto('Teal & Orange', {
        'lift_hue': 190,
        'lift_saturation': 25,
        'lift_luminance': -0.03,
        'gain_hue': 35,
        'gain_saturation': 20,
        'gain_luminance': 0.05,
      }),
      EffectPronto('Pastel', {
        'lift_luminance': 0.08,
        'gamma_hue': 330,
        'gamma_saturation': 10,
        'gamma_luminance': 0.1,
      }),
      EffectPronto('Night', {
        'offset_hue': 220,
        'offset_saturation': 30,
        'offset_luminance': -0.05,
        'gain_luminance': -0.1,
      }),
    ],
  ),

  // ------------------------------------------------------------------
  // ONE FRAME EDITS (pesquisa 14/09/2026). Efeitos de um ou dois quadros
  // no ritmo da musica: contam QUADROS (segurar, cair, repetir a cada N
  // ou disparar ao acaso) e dispensam keyframe. Coloque numa camada de
  // ajuste cortada no beat, ou use o gatilho para repetir sozinho.
  // ------------------------------------------------------------------

  // Clarao no quadro da batida: branco (normal, somar, tela), exposicao em
  // stops ou negativo. "Escuro primeiro" e o flash invertido.
  EffectType.flash: EffectSpec(
    id: 'flash',
    name: 'Flash',
    category: 'Light',
    hasColor: true,
    defaultColor: Color(0xFFFFFFFF),
    synonyms: [
      'flash',
      'one frame',
      'one framer',
      '1 frame',
      'um quadro',
      'batida',
      'beat',
      'clarao',
      'flash branco',
      'transicao flash',
    ],
    params: {
      'mode': EffectParam(
        'Mode',
        0,
        0,
        4,
        kind: ParamKind.choice,
        options: ['Normal', 'Add', 'Screen', 'Exposure', 'Invert'],
      ),
      'intensity': EffectParam('Intensity', 100, 0, 100),
      'stops': EffectParam('Exposure Stops', 3, 0, 6),
      'hold': EffectParam('Hold Frames', 1, 1, 8),
      'decay': EffectParam('Decay Frames', 2, 0, 24),
      'curve': EffectParam('Decay Curve', 1.5, 1, 3),
      'blur': EffectParam('Blur', 0, 0, 60, relative: true),
      'dark_first': EffectParam(
        'Dark First',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
      'trigger': EffectParam(
        'Trigger',
        0,
        0,
        3,
        kind: ParamKind.choice,
        options: ['Layer Start', 'Every N Frames', 'Random', 'Always On'],
      ),
      'period': EffectParam('Period (Frames)', 8, 1, 120),
      'probability': EffectParam('Probability', .5, 0, 1),
      'seed': EffectParam('Seed', 0, 0, 1000, kind: ParamKind.seed),
    },
    montar: ['intensity', 'hold', 'decay'],
    presets: [
      EffectPronto('White 1 Frame', {
        'mode': 0,
        'intensity': 100,
        'hold': 1,
        'decay': 0,
      }),
      EffectPronto('Exposure Hit', {
        'mode': 3,
        'stops': 3,
        'hold': 1,
        'decay': 3,
        'curve': 2,
        'blur': 20,
      }),
      EffectPronto('Inverted Flash', {
        'mode': 0,
        'dark_first': 1,
        'intensity': 80,
        'hold': 1,
        'decay': 2,
      }),
    ],
  ),

  // Pisca por quadros: some, vira cor, negativo, estoura ou apaga. E o
  // texto piscando e o "strobe 12 fps" dos edits.
  EffectType.strobe: EffectSpec(
    id: 'strobe',
    name: 'Strobe',
    category: 'Time',
    hasColor: true,
    defaultColor: Color(0xFFFFFFFF),
    synonyms: [
      'strobe',
      'estrobo',
      'estroboscopio',
      'pisca',
      'blink',
      'one frame',
      'texto piscando',
      'intercalar',
    ],
    params: {
      'mode': EffectParam(
        'Mode',
        0,
        0,
        1,
        kind: ParamKind.choice,
        options: ['Periodic', 'Random'],
      ),
      'period': EffectParam('Period (Frames)', 2, 1, 30),
      'duration': EffectParam('Duration (Frames)', 1, 1, 30),
      'probability': EffectParam('Probability', .5, 0, 1),
      'operation': EffectParam(
        'Operation',
        0,
        0,
        4,
        kind: ParamKind.choice,
        options: ['Transparent', 'Color', 'Invert', 'Exposure', 'Black'],
      ),
      'stops': EffectParam('Exposure Stops', 2, 0, 6),
      'blend': EffectParam('Blend With Original', 0, 0, 100),
      'seed': EffectParam('Seed', 0, 0, 1000, kind: ParamKind.seed),
    },
    montar: ['period', 'duration', 'blend'],
    presets: [
      EffectPronto('Strobe 12 fps', {
        'mode': 0,
        'period': 2,
        'duration': 1,
        'operation': 0,
      }),
      EffectPronto('Negative Blink', {
        'mode': 0,
        'period': 4,
        'duration': 2,
        'operation': 2,
      }),
      EffectPronto('Random Flashes', {
        'mode': 1,
        'duration': 1,
        'probability': .35,
        'operation': 3,
        'stops': 2.5,
      }),
    ],
  ),

  // Zoom que entra seco na batida e volta: suave, exponencial ou com
  // quique, com rastro de zoom.
  EffectType.zoomPunch: EffectSpec(
    id: 'zoom_punch',
    name: 'Zoom Punch',
    category: 'Distort',
    synonyms: [
      'zoom punch',
      'punch',
      'soco',
      'zoom na batida',
      'beat zoom',
      'one frame',
      'impacto',
      'bounce',
    ],
    params: {
      'peak': EffectParam('Peak Scale', 115, 100, 300),
      'attack': EffectParam('Attack Frames', 0, 0, 8),
      'hold': EffectParam('Hold Frames', 1, 0, 8),
      'release': EffectParam('Release Frames', 6, 0, 30),
      'curve': EffectParam(
        'Curve',
        0,
        0,
        2,
        kind: ParamKind.choice,
        options: ['Ease Out', 'Exponential', 'Bounce'],
      ),
      'center_x': EffectParam('Center X', .5, 0, 1, kind: ParamKind.point),
      'center_y': EffectParam('Center Y', .5, 0, 1, kind: ParamKind.point),
      'zoom_blur': EffectParam('Zoom Blur', .5, 0, 1),
      'trigger': EffectParam(
        'Trigger',
        0,
        0,
        2,
        kind: ParamKind.choice,
        options: ['Layer Start', 'Every N Frames', 'Random'],
      ),
      'period': EffectParam('Period (Frames)', 12, 1, 120),
      'probability': EffectParam('Probability', .5, 0, 1),
      'seed': EffectParam('Seed', 0, 0, 1000, kind: ParamKind.seed),
    },
    montar: ['peak', 'release', 'zoom_blur'],
    presets: [
      EffectPronto('Punch 1 Frame', {
        'peak': 120,
        'hold': 1,
        'release': 0,
        'zoom_blur': 0,
      }),
      EffectPronto('Soft Punch', {
        'peak': 112,
        'release': 8,
        'zoom_blur': .6,
      }),
      EffectPronto('Hard Beat', {
        'peak': 140,
        'attack': 1,
        'release': 5,
        'curve': 2,
      }),
    ],
  ),

  // Fatias deslocadas com separacao RGB, ruido de blocos, dessaturar e
  // posterizar — a receita do "one framer" de glitch. Roda no shader.
  EffectType.sliceGlitch: EffectSpec(
    id: 'slice_glitch',
    name: 'Slice Glitch',
    category: 'Glitch',
    synonyms: [
      'slice glitch',
      'fatias',
      'glitch de fatias',
      'one frame',
      'one framer',
      'block noise',
      'blocos',
      'glitch edit',
    ],
    params: {
      'slices': EffectParam('Slices', 18, 2, 64),
      'probability': EffectParam('Probability', .35, 0, 1),
      'offset': EffectParam('Max Offset', 6, 0, 50),
      'rgb_offset': EffectParam('RGB Offset', .3, 0, 1),
      'block_size': EffectParam('Block Size', 0, 0, 200, relative: true),
      'block_strength': EffectParam('Block Strength', .25, 0, 1),
      'desaturate': EffectParam('Desaturate', 0, 0, 100),
      'posterize': EffectParam('Posterize Levels', 0, 0, 16),
      'speed': EffectParam('Speed', 15, 0, 60),
      'seed': EffectParam('Seed', 0, 0, 1000, kind: ParamKind.seed),
    },
    montar: ['probability', 'offset', 'speed'],
    presets: [
      EffectPronto('One Framer', {
        'desaturate': 50,
        'posterize': 6,
        'probability': .35,
        'offset': 6,
        'speed': 12,
      }),
      EffectPronto('Block Hit', {
        'block_size': 64,
        'block_strength': 1,
        'probability': 0,
      }),
      EffectPronto('TV Snow', {
        'block_size': 20,
        'block_strength': .6,
        'desaturate': 100,
        'probability': .2,
      }),
    ],
  ),

  // "Um twitch e um valor aleatorio num instante aleatorio." Cinco
  // operadores (deslizar com RGB, escala, desfoque, luz, cor), cada um com
  // pulso proprio, quietude entre rajadas e subida/descida.
  EffectType.twitch: EffectSpec(
    id: 'twitch',
    name: 'Twitch',
    category: 'Glitch',
    hasColor: true,
    defaultColor: Color(0xFF00E5FF),
    cost: 2,
    synonyms: [
      'twitch',
      'espasmo',
      'tique',
      'glitch pulse',
      'pulso',
      'one frame',
      'nervoso',
      'aleatorio',
    ],
    params: {
      'amount': EffectParam('Amount', 100, 0, 200),
      'speed': EffectParam('Speed', 4, .1, 30),
      'stillness': EffectParam('Stillness', .5, 0, 1),
      'randomize_min': EffectParam('Randomize Min', 30, 0, 100),
      'duration': EffectParam('Duration (Frames)', 2, 1, 12),
      'ease_in': EffectParam('Ease In', .1, 0, 1),
      'ease_out': EffectParam('Ease Out', .5, 0, 1),
      'seed': EffectParam('Seed', 0, 0, 1000, kind: ParamKind.seed),
      'enable_slide': EffectParam(
        'Enable Slide',
        1,
        0,
        1,
        kind: ParamKind.toggle,
      ),
      'slide_amount': EffectParam('Slide Amount', 8, 0, 50),
      'slide_direction': EffectParam('Slide Direction', 0, 0, 360),
      'slide_spread': EffectParam('Slide Spread', 20, 0, 180),
      'slide_tendency': EffectParam('Slide Tendency', 0, -1, 1),
      'slide_rgb_split': EffectParam('Slide RGB Split', .3, 0, 1),
      'enable_scale': EffectParam(
        'Enable Scale',
        1,
        0,
        1,
        kind: ParamKind.toggle,
      ),
      'scale_amount': EffectParam('Scale Amount', 15, 0, 100),
      'enable_blur': EffectParam(
        'Enable Blur',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
      'blur_amount': EffectParam('Blur Amount', 30, 0, 200, relative: true),
      'blur_aspect': EffectParam('Blur Aspect', 0, -1, 1),
      'enable_light': EffectParam(
        'Enable Light',
        1,
        0,
        1,
        kind: ParamKind.toggle,
      ),
      'light_amount': EffectParam('Light Amount', 1.5, 0, 4),
      'light_behaviour': EffectParam(
        'Light Behaviour',
        2,
        0,
        2,
        kind: ParamKind.choice,
        options: ['Brighter', 'Darker', 'Both'],
      ),
      'enable_color': EffectParam(
        'Enable Color',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
      'color_amount': EffectParam('Color Amount', 60, 0, 100),
      'color_randomize': EffectParam('Color Randomize', 0, 0, 1),
      'edges': EffectParam(
        'Border',
        0,
        0,
        2,
        kind: ParamKind.choice,
        options: ['Mirror', 'Tile', 'None'],
      ),
    },
    montar: ['amount', 'speed', 'stillness'],
    presets: [
      EffectPronto('Classic Glitch', {
        'speed': 6,
        'stillness': .55,
        'duration': 2,
        'enable_slide': 1,
        'slide_amount': 8,
        'slide_spread': 10,
        'slide_rgb_split': .5,
        'enable_light': 1,
        'light_behaviour': 2,
        'light_amount': 1.5,
        'enable_scale': 0,
      }),
      EffectPronto('Light Pulse', {
        'speed': 3,
        'stillness': .3,
        'duration': 3,
        'ease_in': 0,
        'ease_out': .8,
        'enable_light': 1,
        'light_behaviour': 0,
        'light_amount': 2,
        'enable_blur': 1,
        'blur_amount': 25,
        'enable_scale': 1,
        'scale_amount': 6,
        'enable_slide': 0,
      }),
      EffectPronto('Nervous Zoom', {
        'speed': 8,
        'stillness': .6,
        'duration': 2,
        'enable_scale': 1,
        'scale_amount': 12,
        'enable_color': 1,
        'color_amount': 30,
        'color_randomize': 1,
        'enable_slide': 0,
        'enable_light': 0,
      }),
    ],
  ),

  // TIME SLICE: faixas paralelas, cada uma a mesma camada num instante
  // diferente. A distribuicao escada com o atraso maximo de (N-1)/2 e o
  // S_TimeSlice (um quadro por faixa); linear com ease out e a transicao
  // dos edits, em que as faixas do clipe chegam em sequencia.
  EffectType.timeSlice: EffectSpec(
    id: 'time_slice',
    name: 'Time Slice',
    category: 'Time',
    cost: 3,
    synonyms: [
      'time slice',
      'timeslice',
      'fatias de tempo',
      'slit scan',
      'time displacement',
      'deslocamento de tempo',
      'faixas',
      'hologram',
      'split',
    ],
    params: {
      'slices': EffectParam('Slices', 12, 1, 64),
      'angle': EffectParam('Slice Direction', 90, 0, 360),
      'distribution': EffectParam(
        'Distribution',
        0,
        0,
        4,
        kind: ParamKind.choice,
        options: ['Staircase', 'Linear', 'Center', 'Random', 'Wave'],
      ),
      'max_offset': EffectParam('Max Offset (Frames)', 6, -60, 60),
      'curve': EffectParam(
        'Curve',
        0,
        0,
        3,
        kind: ParamKind.choice,
        options: ['Linear', 'Ease In', 'Ease Out', 'Ease In Out'],
      ),
      'cycles': EffectParam('Cycles', 1, .25, 8),
      'phase': EffectParam('Phase', 0, 0, 1),
      'sweep': EffectParam('Sweep', 0, -10, 10),
      'frame_offset': EffectParam('Frame Offset', 0, -120, 120),
      'gap': EffectParam('Gap', 0, 0, 10, relative: true),
      'mix': EffectParam('Mix With Original', 100, 0, 100),
      'seed': EffectParam('Seed', 0, 0, 1000, kind: ParamKind.seed),
    },
    montar: ['slices', 'max_offset', 'angle'],
    presets: [
      EffectPronto('Sapphire 12', {
        'slices': 12,
        'angle': 90,
        'distribution': 0,
        'max_offset': 6,
      }),
      EffectPronto('Vertical Slide', {
        'slices': 16,
        'angle': 0,
        'distribution': 1,
        'max_offset': -15,
        'curve': 2,
      }),
      EffectPronto('Glitch Slices', {
        'slices': 24,
        'angle': 90,
        'distribution': 3,
        'max_offset': 8,
        'seed': 7,
        'gap': 1,
      }),
    ],
  ),

  // POSTERIZE TIME (Time Quantization no Alight Motion): a camada inteira
  // anda em degraus de N quadros por segundo — o "12 fps de anime".
  EffectType.posterizeTime: EffectSpec(
    id: 'posterize_time',
    name: 'Posterize Time',
    category: 'Time',
    synonyms: [
      'posterize time',
      'time quantization',
      'quantizacao de tempo',
      'stop motion',
      'choppy',
      'anime',
      '12 fps',
      'travado',
    ],
    params: {
      'rate': EffectParam('Frame Rate', 12, 1, 60),
      'phase': EffectParam('Phase', 0, 0, 1),
    },
    montar: ['rate'],
    presets: [
      EffectPronto('Anime 12 fps', {'rate': 12}),
      EffectPronto('Stop Motion', {'rate': 8}),
      EffectPronto('Choppy', {'rate': 4}),
    ],
  ),

  // ------------------------------------------------------------------
  // A CAMADA DE AJUSTE DO AFTER (dono, 14/09/2026). Um edit de referencia
  // tinha, empilhados: Magic Bullet Looks, S_Sharpen, S_Flicker,
  // S_MathOps, S_FilmDamage, Hue/Saturation e Brightness & Contrast. As
  // contas de referencia estao em domain/efeitos_do_after.dart; o
  // desenho roda no shader, com a mesma conta.
  // ------------------------------------------------------------------

  // HUE/SATURATION, o mestre do After e do Photoshop: a matiz gira no
  // HSL, a saturacao escala o croma em volta da luminosidade (o cinza
  // continua cinza) e a luminosidade mistura com o branco ou o preto.
  // Colorir troca matiz e saturacao de todos os pixels e guarda so a
  // luminancia — o sepia e o duotom de um toque.
  EffectType.hueSaturation: EffectSpec(
    id: 'hue_saturation',
    name: 'Hue/Saturation',
    category: 'Color',
    synonyms: [
      'hue/saturation',
      'hue saturation',
      'matiz/saturação',
      'matiz e saturação',
      'matiz',
      'saturação',
      'luminosidade',
      'colorir',
      'colorize',
      'dessaturar',
      'preto e branco',
      'sépia',
      'coloring',
      'cc',
    ],
    params: {
      'master_hue': EffectParam('Matiz', 0, -180, 180),
      'master_saturation': EffectParam('Saturação', 0, -100, 100),
      'master_lightness': EffectParam('Luminosidade', 0, -100, 100),
      'colorize': EffectParam(
        'Colorir',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
      'colorize_hue': EffectParam('Matiz ao colorir', 0, 0, 360),
      'colorize_saturation': EffectParam('Saturação ao colorir', 25, 0, 100),
    },
    montar: ['master_hue', 'master_saturation', 'master_lightness'],
    presets: [
      EffectPronto('Preto e branco', {'master_saturation': -100}),
      EffectPronto('Sépia', {
        'colorize': 1,
        'colorize_hue': 35,
        'colorize_saturation': 30,
      }),
      EffectPronto('Cores vivas', {'master_saturation': 35}),
    ],
  ),

  // S_FLICKER (Sapphire): "escala as cores da camada por quantias
  // diferentes ao longo do tempo". Um aleatorio suave no brilho, outro
  // por canal e uma onda com fase por canal; cada canal recebe a sua
  // parte e o Brilho escala o resultado. O ganho do quadro e uma conta
  // pura de (tempo, semente) que entra como UMA matriz de cor. O Flicker
  // antigo continua como era: projeto salvo nao muda.
  EffectType.sFlicker: EffectSpec(
    id: 's_flicker',
    name: 'S_Flicker',
    category: 'Time',
    synonyms: [
      's_flicker',
      's flicker',
      'sapphire flicker',
      'flicker',
      'cintilar',
      'cintilação',
      'piscar',
      'tremular',
      'filme antigo',
      'lâmpada',
    ],
    params: {
      // Escala TUDO: zero desliga o efeito.
      'amplitude': EffectParam('Amplitude', .2, 0, 2),
      'rand_luma_amp': EffectParam('Brilho aleatório', 1, 0, 2),
      'rand_color_amp': EffectParam('Cor aleatória', 0, 0, 2),
      'rand_freq': EffectParam('Frequência aleatória', 30, 0, 60),
      'wave_amp': EffectParam('Amplitude da onda', 0, 0, 2),
      'wave_freq': EffectParam('Frequência da onda', 5, 0, 60),
      'wave_red_phase': EffectParam('Fase da onda R', 0, -360, 360),
      'wave_green_phase': EffectParam('Fase da onda G', 0, -360, 360),
      'wave_blue_phase': EffectParam('Fase da onda B', 0, -360, 360),
      // Quanto do pisca vai para cada canal.
      'red_amp': EffectParam('Força em R', 1, 0, 2),
      'green_amp': EffectParam('Força em G', 1, 0, 2),
      'blue_amp': EffectParam('Força em B', 1, 0, 2),
      // Escala o resultado inteiro.
      'brightness': EffectParam('Brilho', 1, 0, 3),
      'seed': EffectParam('Semente', 0, 0, 1000, kind: ParamKind.seed),
    },
    montar: ['amplitude', 'rand_freq', 'brightness'],
    presets: [
      EffectPronto('Filme antigo', {
        'amplitude': .15,
        'rand_luma_amp': 1,
        'rand_color_amp': 0,
        'rand_freq': 16,
        'wave_amp': 0,
      }),
      EffectPronto('Lâmpada ruim', {
        'amplitude': .6,
        'rand_luma_amp': 1,
        'rand_color_amp': .3,
        'rand_freq': 12,
        'wave_amp': 0,
      }),
      EffectPronto('Onda RGB', {
        'amplitude': .3,
        'rand_luma_amp': 0,
        'rand_color_amp': 0,
        'wave_amp': 1,
        'wave_freq': 2,
        'wave_red_phase': 0,
        'wave_green_phase': 120,
        'wave_blue_phase': 240,
      }),
    ],
  ),

  // S_MATHOPS (Sapphire): combina a camada (A) com uma fonte B numa
  // operacao de pixel. Antes, cada entrada passa por Luzes (escala),
  // Sombras (desloca os escuros: c*luzes + sombras*(1-c)) e Saturacao (em
  // volta da luma Rec.709); depois, o destino passa pelos mesmos tres. A
  // mascara de luma limita onde o resultado aparece. Somar com a fonte B
  // Nenhuma (preto) e tudo no neutro devolve a imagem intacta. So no
  // shader: a fonte desfocada e a mascara leem a vizinhanca do pixel.
  EffectType.mathOps: EffectSpec(
    id: 'math_ops',
    name: 'S_MathOps',
    category: 'Color',
    synonyms: [
      's_mathops',
      's mathops',
      'math ops',
      'mathops',
      'operações',
      'somar',
      'multiplicar',
      'screen',
      'diferença',
      'blend',
      'coloring',
    ],
    params: {
      'operation': EffectParam(
        'Operação',
        0,
        0,
        8,
        kind: ParamKind.choice,
        options: [
          'Somar',
          'Subtrair',
          'Multiplicar',
          'Tela',
          'Média',
          'Sobrepor',
          'Mínimo',
          'Máximo',
          'Diferença',
        ],
      ),
      'source_b': EffectParam(
        'Fonte B',
        0,
        0,
        2,
        kind: ParamKind.choice,
        options: ['Nenhuma', 'A própria camada', 'A camada desfocada'],
      ),
      // Pixel pensado em 1080p; so conta com a camada desfocada.
      'b_blur': EffectParam('Desfoque de B', 20, 0, 100, relative: true),
      'a_lights': EffectParam('Luzes de A', 1, 0, 3),
      'a_darks': EffectParam('Sombras de A', 0, -1, 1),
      'a_saturation': EffectParam('Saturação de A', 1, 0, 3),
      'b_lights': EffectParam('Luzes de B', 1, 0, 3),
      'b_darks': EffectParam('Sombras de B', 0, -1, 1),
      'b_saturation': EffectParam('Saturação de B', 1, 0, 3),
      'dest_lights': EffectParam('Luzes do destino', 1, 0, 3),
      'dest_darks': EffectParam('Sombras do destino', 0, -1, 1),
      'dest_saturation': EffectParam('Saturação do destino', 1, 0, 3),
      'mask': EffectParam(
        'Máscara',
        0,
        0,
        1,
        kind: ParamKind.choice,
        options: ['Nenhuma', 'Luma'],
      ),
      'mask_blur': EffectParam(
        'Desfoque da máscara',
        0,
        0,
        100,
        relative: true,
      ),
      'invert_mask': EffectParam(
        'Inverter máscara',
        0,
        0,
        1,
        kind: ParamKind.toggle,
      ),
    },
    montar: ['a_lights', 'a_darks', 'dest_saturation'],
    presets: [
      // A camada de ajuste do edit de referencia: Somar sem fonte B,
      // sombras de A em -0,03 e desfoque da mascara 12.
      EffectPronto('Edit de referência', {
        'operation': 0,
        'source_b': 0,
        'a_darks': -.03,
        'mask_blur': 12,
      }),
      EffectPronto('Brilho difuso', {
        'operation': 3,
        'source_b': 2,
        'b_blur': 30,
        'b_lights': .6,
      }),
      EffectPronto('Contraste de sobreposição', {
        'operation': 5,
        'source_b': 1,
        'dest_saturation': .9,
      }),
    ],
  ),
};

/// OS EFEITOS DE EDIT, na ordem em que se procura: batida, glitch,
/// tempo e coloring. E o atalho "Edits" da galeria — quem vem do Alight
/// Motion procura por isso, e nao pela categoria tecnica de cada um.
const efeitosDeEdit = <EffectType>[
  EffectType.flash,
  EffectType.zoomPunch,
  EffectType.strobe,
  EffectType.sFlicker,
  EffectType.sliceGlitch,
  EffectType.twitch,
  EffectType.tremor,
  EffectType.timeSlice,
  EffectType.posterizeTime,
  EffectType.filmDamage,
  EffectType.rgbSplit,
  EffectType.glitch,
  EffectType.colorBalance,
  EffectType.gradientMap,
  EffectType.colorTune,
  EffectType.hueSaturation,
  EffectType.photoFilter,
  EffectType.channelMixer,
  EffectType.selectiveColor,
  EffectType.brightnessContrast,
  EffectType.mathOps,
];

/// Categorias do catalogo, na ordem em que aparecem.
/// CATEGORIAS, em ingles como os nomes. A arrumacao do catalogo e a
/// primeira coisa que a pessoa le, e misturar idioma ali confunde mais
/// do que ajuda.
const effectCategories = <String>[
  'Color',
  'Light',
  'Lens',
  'Blur',
  'Distort',
  'Stylize',
  'Glitch',
  'Time',
  'Generate',
  'Keying',
  'Utility',
];

/// O ROTULO da categoria, em portugues, para a galeria (Fase 4).
String categoriaDoEfeito(String categoria) => switch (categoria) {
  'Color' => 'Cor',
  'Light' => 'Luz',
  'Lens' => 'Lente',
  'Blur' => 'Desfoque',
  'Distort' => 'Distorcer',
  'Stylize' => 'Estilizar',
  'Glitch' => 'Glitch',
  'Time' => 'Tempo',
  'Generate' => 'Gerar',
  'Keying' => 'Recorte',
  'Utility' => 'Utilitário',
  _ => categoria,
};

/// A categoria escrita em portugues, para a busca aceitar os dois
/// idiomas: quem digita "cor" acha os efeitos de Color.
const _categoriaEmPortugues = <String, String>{
  'Color': 'cor',
  'Light': 'luz',
  'Lens': 'lente',
  'Blur': 'desfoque',
  'Distort': 'distorcer distorcao',
  'Stylize': 'estilizar',
  'Glitch': 'glitch',
  'Time': 'tempo',
  'Generate': 'gerar textura',
  'Keying': 'recorte chroma fundo verde transparencia',
  'Utility': 'utilitario',
};

/// BUSCA (§5): nome, categoria e SINONIMOS. Quem digita "bloom" acha
/// Glow; quem digita "pixelate" acha Mosaico.
///
/// SEM ACENTO E SEM CAIXA, dos dois lados. O teclado do celular poe o
/// acento sozinho: "partículas" nao achava o sinonimo "particulas", e
/// "saturacao" nao acharia "Saturação". A busca compara as duas pontas
/// ja normalizadas, entao tanto faz como o sinonimo foi escrito.
List<EffectType> searchEffects(String query) {
  final q = normalizarBusca(query.trim());
  if (q.isEmpty) return effectSpecs.keys.toList();
  bool contem(String texto) => normalizarBusca(texto).contains(q);
  return [
    for (final e in effectSpecs.entries)
      if (contem(e.value.name) ||
          contem(e.value.id) ||
          contem(e.value.category) ||
          contem(_categoriaEmPortugues[e.value.category] ?? '') ||
          e.value.synonyms.any(contem))
        e.key,
  ];
}

/// Minusculas e sem acento: "Saturação" vira "saturacao".
///
/// O Dart nao traz normalizacao Unicode (NFD) no nucleo, entao a tabela
/// cobre as letras latinas acentuadas dos idiomas do app. O que nao esta
/// nela passa intacto — hangul, kana e cirilico nao tem acento a tirar.
String normalizarBusca(String texto) {
  final saida = StringBuffer();
  for (final runa in texto.toLowerCase().runes) {
    final letra = String.fromCharCode(runa);
    saida.write(_semAcento[letra] ?? letra);
  }
  return saida.toString();
}

const _semAcento = <String, String>{
  'á': 'a',
  'à': 'a',
  'â': 'a',
  'ã': 'a',
  'ä': 'a',
  'å': 'a',
  'é': 'e',
  'è': 'e',
  'ê': 'e',
  'ë': 'e',
  'í': 'i',
  'ì': 'i',
  'î': 'i',
  'ï': 'i',
  'ó': 'o',
  'ò': 'o',
  'ô': 'o',
  'õ': 'o',
  'ö': 'o',
  'ú': 'u',
  'ù': 'u',
  'û': 'u',
  'ü': 'u',
  'ç': 'c',
  'ñ': 'n',
  'ý': 'y',
  'ÿ': 'y',
};

List<EffectType> effectsInCategory(String category) => [
  for (final e in effectSpecs.entries)
    if (e.value.category == category) e.key,
];

/// Instancia de efeito numa camada. TODO parametro numerico e animavel
/// (trilha de keyframes propria, avaliada no tempo local da camada).
class EffectInstance {
  EffectInstance({
    String? id,
    required this.type,
    Map<String, AnimatedDouble>? params,
    Color? color,
    this.enabled = true,
    this.depth = EffectDepth.pronto,
    List<Color>? extraColors,
  }) : id = id ?? const Uuid().v4(),
       color = color ?? effectSpecs[type]!.defaultColor,
       extraColors = List.unmodifiable(
         extraColors ??
             List<Color>.generate(
               effectSpecs[type]!.extraColors,
               (i) => i < effectSpecs[type]!.defaultExtraColors.length
                   ? effectSpecs[type]!.defaultExtraColors[i]
                   : _coresExtrasPadrao[i % _coresExtrasPadrao.length],
             ),
       ),
       params = Map.unmodifiable(
         params ??
             {
               for (final e in effectSpecs[type]!.params.entries)
                 e.key: AnimatedDouble(e.value.initial),
             },
       );

  final String id;
  final EffectType type;
  final Map<String, AnimatedDouble> params;
  final Color color;
  final bool enabled;

  /// Onde a pessoa parou: pronto, montar ou avancado (regra 2).
  final EffectDepth depth;

  /// Cores alem da principal, na ordem da ficha ([EffectSpec.extraColors]).
  final List<Color> extraColors;

  static const _coresExtrasPadrao = [
    Color(0xFF7C62FF),
    Color(0xFF35C4E7),
    Color(0xFFFFB020),
  ];

  Color extraColor(int i) => i < extraColors.length
      ? extraColors[i]
      : _coresExtrasPadrao[i % _coresExtrasPadrao.length];

  EffectInstance withExtraColor(int i, Color c) {
    final lista = [...extraColors];
    while (lista.length <= i) {
      lista.add(_coresExtrasPadrao[lista.length % _coresExtrasPadrao.length]);
    }
    lista[i] = c;
    return copyWith(extraColors: lista);
  }

  EffectSpec get spec => effectSpecs[type]!;

  AnimatedDouble track(String key) =>
      params[key] ?? AnimatedDouble(spec.params[key]?.initial ?? 0);

  /// Valor do parametro no tempo local da camada.
  double paramAt(String key, Duration local) => track(key).valueAt(local);

  EffectInstance copyWith({
    Map<String, AnimatedDouble>? params,
    Color? color,
    bool? enabled,
    List<Color>? extraColors,
    EffectDepth? depth,
  }) {
    return EffectInstance(
      id: id,
      type: type,
      params: params ?? this.params,
      color: color ?? this.color,
      enabled: enabled ?? this.enabled,
      extraColors: extraColors ?? this.extraColors,
      depth: depth ?? this.depth,
    );
  }

  /// Desce ou sobe de profundidade sem tocar em nenhum numero.
  EffectInstance withDepth(EffectDepth d) => copyWith(depth: d);

  /// Aplica um preset de `pronto`: crava os numeros dele e deixa o
  /// resto como estava (o preset e um ponto de partida, nao um reset).
  EffectInstance withPreset(EffectPronto preset) {
    final novos = <String, AnimatedDouble>{...params};
    for (final e in preset.valores.entries) {
      novos[e.key] = AnimatedDouble(e.value);
    }
    return copyWith(
      params: novos,
      color: preset.cor ?? color,
      depth: EffectDepth.pronto,
    );
  }

  /// Edita valor: keyframe automatico se o parametro ja anima.
  /// KEYFRAME UNIVERSAL (como no Alight Motion): o efeito tem UM
  /// diamante, e cada keyframe guarda TODOS os parametros.
  ///
  /// Num efeito sem animacao, editar so muda o valor. Num efeito que ja
  /// tem keyframe em qualquer parametro, editar um parametro neste
  /// instante grava o keyframe deste instante com todos os parametros —
  /// o editado com o valor novo, os outros com o valor que tinham. E o
  /// que faz "marco no inicio, vou ao fim e mexo" funcionar sem pensar
  /// em qual parametro tem diamante.
  ///
  /// EDITAR VALOR NUNCA CRIA KEYFRAME, e este era um dos piores casos:
  /// num efeito que ja animava, mexer num parametro em QUALQUER
  /// instante cravava a marca universal ali — todos os parametros de
  /// uma vez, num tempo que ninguem escolheu. Agora so escreve sobre
  /// marca que ja existe; fora dela quem crava e o losango
  /// (`docs/keyframe-explicito.md`). [forcar] e o caminho do losango,
  /// e da edicao pendente que ele grava.
  EffectInstance withParamEdited(
    String key,
    Duration local,
    double value, {
    bool forcar = false,
  }) {
    if (!hasAnimation) {
      return copyWith(
        params: {...params, key: track(key).edited(local, value)},
      );
    }
    if (!forcar && !hasKeyframeAt(local)) return this;
    final novos = <String, AnimatedDouble>{...params};
    for (final k in {...spec.params.keys, ...params.keys}) {
      final t = track(k);
      if (k == key) {
        novos[k] = t.withKeyframe(local, value);
      } else if (!t.hasKeyframeAt(local)) {
        novos[k] = t.withKeyframe(local, t.valueAt(local));
      }
    }
    return copyWith(params: novos);
  }

  /// A edicao em [local] chega ao projeto, ou precisa do losango antes?
  bool aceitaEdicaoEm(Duration local) => !hasAnimation || hasKeyframeAt(local);

  /// Ha keyframe do EFEITO neste instante: qualquer parametro basta.
  bool hasKeyframeAt(Duration local) =>
      params.values.any((t) => t.hasKeyframeAt(local));

  /// O diamante do efeito: liga o keyframe universal neste instante
  /// (todos os parametros, com o valor atual) ou desliga de todos.
  EffectInstance withKeyframeToggled(Duration local) {
    final ligar = !hasKeyframeAt(local);
    final novos = <String, AnimatedDouble>{...params};
    for (final k in {...spec.params.keys, ...params.keys}) {
      final t = track(k);
      novos[k] = ligar
          ? (t.hasKeyframeAt(local)
                ? t
                : t.withKeyframe(local, t.valueAt(local)))
          : t.withoutKeyframe(local);
    }
    return copyWith(params: novos);
  }

  /// Diamante de UM parametro: liga/desliga keyframe no tempo local.
  /// (Usado por pares x|y que completam um eixo por vez.)
  EffectInstance withParamKeyframeToggled(String key, Duration local) {
    final t = track(key);
    return copyWith(
      params: {
        ...params,
        key: t.hasKeyframeAt(local)
            ? t.withoutKeyframe(local)
            : t.withKeyframe(local, t.valueAt(local)),
      },
    );
  }

  /// O INSTANTE [de] DO EFEITO VAI PARA [para]: todo parametro com marca
  /// ali anda junto, com o valor e a curva dele.
  ///
  /// O keyframe do efeito e universal (ver [withKeyframeToggled]); mover
  /// so parte dos parametros deixaria metade da marca para tras. O proprio
  /// efeito, intacto, quando nenhum parametro tem marca em [de].
  EffectInstance comKeyframeMovido(Duration de, Duration para) {
    Map<String, AnimatedDouble>? novos;
    for (final p in params.entries) {
      final movida = p.value.comKeyframeMovido(de, para);
      if (identical(movida, p.value)) continue;
      (novos ??= {...params})[p.key] = movida;
    }
    return novos == null ? this : copyWith(params: novos);
  }

  /// TIRA a marca de [local] de todos os parametros. Intacto quando nao
  /// havia nenhuma.
  EffectInstance semKeyframeEm(Duration local) {
    Map<String, AnimatedDouble>? novos;
    for (final p in params.entries) {
      if (!p.value.hasKeyframeAt(local)) continue;
      (novos ??= {...params})[p.key] = p.value.withoutKeyframe(local);
    }
    return novos == null ? this : copyWith(params: novos);
  }

  /// Tempos (locais) com keyframe em qualquer parametro.
  Iterable<Duration> get keyframeTimes sync* {
    for (final t in params.values) {
      for (final k in t.keyframes) {
        yield k.time;
      }
    }
  }

  bool get hasAnimation => params.values.any((t) => t.isAnimated);

  EffectInstance duplicated() => EffectInstance(
    type: type,
    params: params,
    color: color,
    enabled: enabled,
    extraColors: extraColors,
    depth: depth,
  );
}
