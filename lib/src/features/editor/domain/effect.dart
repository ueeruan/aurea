import 'luz_e_diversos.dart';
import 'motion_tile.dart';
import 'dart:ui';

import 'package:flutter/foundation.dart' show debugPrint;
import 'package:uuid/uuid.dart';

import 'correcao_de_cor.dart';
import 'estilizar.dart';
import 'jpeg_damage.dart';
import 'distorcao_ae.dart';
import 'shake.dart';
import 'glitch_distorcao.dart';
import 'vhs_damage.dart';
import 'tv_damage.dart';
import 'pixel_sort_sapphire.dart';
import 'auto_paint.dart';
import 'preenchimento.dart';
import 'sombra_projetada.dart';
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
  //
  // ATENCAO: `timeRemap` MORAVA AQUI (indice 26) ate 17/09 e foi removido.
  // O enum perdeu uma variante no MEIO, entao todo indice depois deste
  // anda uma casa para tras. Arquivo antigo guardou o indice, nao o nome:
  // quem le esse arquivo passa por [_ordemLegada] antes de virar tipo.
  // Ver `_tipoDoEfeito` em `project_store.dart`.
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
  sSharpen,
  looks,
  // --- lote AE do dono (15/09/2026): sweep, saber, lens blur, 8-bit ---
  lightSweep,
  saber,
  lensBlur,
  bit8,
  smear,
  bubbleBlur,
  // --- v1.1.1: o que faltava em opacidade e visibilidade ---
  dissolver,
  pena,
  aparecerSumir,
  // --- v1.1.1: repetir a camada sem duplicar camada ---
  repetirEmLinha,
  repetirEmGrade,
  repetirEmCirculo,
  espalharCopias,
  // --- v1.1.1: desenhos que a propria camada gera ---
  nuvens,
  xadrez,
  listras,
  pontos,
  estrelas,
  raios,
  // --- v1.1.1: cortina, recorte e borda ---
  cortina,
  cortinaRadial,
  apertarRecorte,
  meioTom,
  contorno,
  brilhoPorDentro,
  bordasAsperas,
  // --- recomeco do zero (16/09): correcao de cor com a conta do AE ---
  exposure,
  // --- aba Estilizar, lote 1 (16/09): CC e Sapphire ---
  threshold,
  thresholdRgb,
  blockLoad,
  scanLines,
  halfTone,
  edgeColorize,
  // --- aba Estilizar, lote 2 (16/09): Sapphire ---
  jpegDamage,
  autoPaint,
  tvDamage,
  vhsDamage,
  // --- aba Distorcer (16/09): AE, CC e Sapphire ---
  ccLens,
  opticsCompensation,
  dissolveShake,
  crossGlitch,
  chromaKeyPro,
  sRays,
  deepGlow,
  brilho,
  sSpotLight,
  sGlint,
  sGlintRainbow,
  sGlowRings,
  sEdgeRays,
  sGlowAura,
  sGlowDarks,

  // ---- LOTE LIDO DO AFTER EFFECTS (18/09) ----
  //
  // NO FIM, e nao no meio: o indice do enum e o que o arquivo antigo
  // guarda quando nao acha o id, e inserir no meio faria todo projeto
  // salvo abrir com outro efeito.
  preenchimento,
  sombraProjetada,
}

/// A ORDEM DO ENUM ANTES DE O TIME REMAP SAIR (17/09).
///
/// O arquivo de projeto guarda o INDICE do tipo quando ele e antigo o
/// bastante para nao ter o `kind` (a chave estavel entrou em 01/09). O
/// timeRemap ocupava a posicao 26, entao tirar ele do enum deslocou 101
/// tipos uma casa — e um arquivo antigo passaria a abrir "Pixel Sort"
/// onde havia "Efeitos de cor", em silencio.
///
/// A posicao 26 e `null` de proposito: ali nao havia um efeito a
/// recuperar, havia o Time Remap, que virou campo proprio da camada. Quem
/// le um indice antigo e encontra nulo sabe que precisa MIGRAR a trilha
/// de tempo para o campo, e nao descartar.
///
/// Ela e a unica memoria do enum antigo e nao deve ser editada: so serve
/// para ler arquivo velho. Tipo novo entra no enum, nunca aqui.
const List<EffectType?> _ordemLegada = [
  EffectType.gaussianBlur, EffectType.lightGlow, EffectType.tint, EffectType.glowVol,
  EffectType.tremor, EffectType.glitch, EffectType.rgbSplit, EffectType.echo,
  EffectType.spatialEcho, EffectType.radialAberration, EffectType.levels, EffectType.vibrance,
  EffectType.whiteBalance, EffectType.colorWheels, EffectType.unmult, EffectType.vignette,
  EffectType.directionalBlur, EffectType.radialBlur, EffectType.lightRays, EffectType.mosaic,
  EffectType.filmGrain, EffectType.fractalNoise, EffectType.digitalDamage, EffectType.zoomWarp,
  EffectType.posterize, EffectType.curves,
  // era o timeRemap: virou campo da camada, nao e mais efeito
  null,
  EffectType.pixelSort,
  EffectType.blobTracker, EffectType.turbulentDisplace, EffectType.unsharpMask, EffectType.motionTile,
  EffectType.bend, EffectType.ccScatterize, EffectType.ccSplit, EffectType.vhs,
  EffectType.filmDamage, EffectType.glitchify, EffectType.forceMotionBlur, EffectType.flicker,
  EffectType.gradient4, EffectType.liquidGlass, EffectType.corrections, EffectType.chromaKey,
  EffectType.lumaKey, EffectType.colorKey, EffectType.findEdges, EffectType.oscillate,
  EffectType.twirl, EffectType.fisheye, EffectType.kaleidoscope, EffectType.venetianBlinds,
  EffectType.blockDissolve, EffectType.offset, EffectType.invert, EffectType.waveWarp,
  EffectType.opticalFlow, EffectType.colorBalance, EffectType.selectiveColor, EffectType.channelMixer,
  EffectType.photoFilter, EffectType.gradientMap, EffectType.brightnessContrast, EffectType.colorTune,
  EffectType.flash, EffectType.strobe, EffectType.zoomPunch, EffectType.sliceGlitch,
  EffectType.twitch, EffectType.timeSlice, EffectType.posterizeTime, EffectType.hueSaturation,
  EffectType.sFlicker, EffectType.mathOps, EffectType.sSharpen, EffectType.looks,
  EffectType.lightSweep, EffectType.saber, EffectType.lensBlur, EffectType.bit8,
  EffectType.smear, EffectType.bubbleBlur, EffectType.dissolver, EffectType.pena,
  EffectType.aparecerSumir, EffectType.repetirEmLinha, EffectType.repetirEmGrade, EffectType.repetirEmCirculo,
  EffectType.espalharCopias, EffectType.nuvens, EffectType.xadrez, EffectType.listras,
  EffectType.pontos, EffectType.estrelas, EffectType.raios, EffectType.cortina,
  EffectType.cortinaRadial, EffectType.apertarRecorte, EffectType.meioTom, EffectType.contorno,
  EffectType.brilhoPorDentro, EffectType.bordasAsperas, EffectType.exposure, EffectType.threshold,
  EffectType.thresholdRgb, EffectType.blockLoad, EffectType.scanLines, EffectType.halfTone,
  EffectType.edgeColorize, EffectType.jpegDamage, EffectType.autoPaint, EffectType.tvDamage,
  EffectType.vhsDamage, EffectType.ccLens, EffectType.opticsCompensation, EffectType.dissolveShake,
  EffectType.crossGlitch, EffectType.chromaKeyPro, EffectType.sRays, EffectType.deepGlow,
  EffectType.brilho, EffectType.sSpotLight, EffectType.sGlint, EffectType.sGlintRainbow,
  EffectType.sGlowRings, EffectType.sEdgeRays, EffectType.sGlowAura, EffectType.sGlowDarks,
];

/// Posicao do Time Remap no enum antigo.
const int indiceLegadoDoTimeRemap = 26;

/// O tipo a partir do IDENTIFICADOR estavel.
///
/// E o caminho de leitura do arquivo. Guardar o efeito pelo INDICE do
/// enum era uma bomba-relogio: bastava alguem inserir um efeito no meio
/// da lista para todo projeto salvo virar outro efeito. O id nunca muda.

/// Tipo pelo indice que o enum tinha ANTES de o Time Remap sair.
///
/// Fora da faixa devolve nulo: indice inventado nao pode virar efeito.
/// Tipo que voltou a existir por outro caminho (nome/`kind`) nem chega
/// aqui — quem chama tenta o nome primeiro.
EffectType? tipoPorIndiceLegado(int indice) =>
    indice >= 0 && indice < _ordemLegada.length ? _ordemLegada[indice] : null;

EffectType? effectTypeFromId(String id) {
  for (final e in effectSpecs.entries) {
    if (e.value.id == id) return e.key;
  }
  return _aliasesDeId[id];
}

/// NOMES ANTIGOS que ainda aparecem em arquivo. Renomear nao pode
/// quebrar o que ja existe.
const _aliasesDeId = <String, EffectType>{
  // A beta 84 gravou '8_bit'; id nao pode comecar com digito.
  '8_bit': EffectType.bit8,
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
///
/// UM EFEITO REMOVIDO TAMBEM TEM IDENTIFICADOR. Ele nao esta mais no
/// catalogo, mas continua DENTRO de projetos salvos: se gravar estourasse,
/// quem abrisse um projeto antigo nao conseguiria salvar de novo — perderia
/// o arquivo inteiro por causa de um efeito que nem desenha. O id derivado
/// do nome do enum e o mesmo que o efeito tinha quando era oficial
/// (camelCase -> snake_case), entao o arquivo sai igual e volta a ser lido.
String effectIdOf(EffectType t) =>
    effectSpecs[t]?.id ?? _idDerivado(t.name);

String _idDerivado(String nome) {
  final sb = StringBuffer();
  for (var i = 0; i < nome.length; i++) {
    final c = nome[i];
    final maiuscula = c != c.toLowerCase() && c == c.toUpperCase();
    if (maiuscula && i > 0) sb.write('_');
    sb.write(c.toLowerCase());
  }
  return sb.toString();
}

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
    this.unit = '',
    this.decimals,
    this.dragStep,
  });

  /// QUANTO O VALOR ANDA POR PIXEL DE DEDO na fita. Nulo = a faixa
  /// dividida em 500 px. Raio e gama precisam de passo proprio: 0,1 a 100
  /// dividido em 500 pula 0,2 px por pixel e ninguem acerta raio 1,3.
  final double? dragStep;

  final String label;
  final double initial;
  final double min;
  final double max;
  final ParamKind kind;

  /// O SUFIXO da caixa de valor ('%', '°'), como o After Effects mostra.
  final String unit;

  /// Casas decimais da caixa de valor. Nulo = decide pelo tamanho do
  /// numero (o formato antigo do painel).
  final int? decimals;

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
    this.colorLabels = const [],
  });

  /// Nomes das cores na ficha (principal primeiro, depois as extras).
  /// Vazio = "Cor", "Cor 2"...
  final List<String> colorLabels;

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

/// O CATALOGO DE EFEITOS, RECOMECADO DO ZERO (16/09).
///
/// O dono mandou apagar os 103 efeitos antigos (tag
/// `antes-de-apagar-efeitos`) para reconstruir com a conta do After
/// Effects. O primeiro lote e a correcao de cor, em
/// `correcao_de_cor.dart`, com shaders proprios.
///
/// Projeto antigo com um efeito que nao voltou continua ABRINDO — mas nao
/// porque o carregador pula o desconhecido, e sim porque a INSTANCIA
/// sobrevive: ela nasce inerte (sem ficha, sem parametros), o palco nao a
/// desenha e o painel a mostra como removivel. O texto anterior dizia que
/// o carregador "pula efeito desconhecido", o que nao era verdade — o
/// `_tipoDoEfeito` caia no indice do enum e devolvia um tipo qualquer,
/// silenciosamente errado.
///
/// O enum `EffectType` e a maquinaria antiga (o passe de pixel de 62
/// modos) continuam de pe de proposito: o corte segue por partes, cada
/// uma inteira.
const effectSpecs = <EffectType, EffectSpec>{
  ...efeitosDeCorrecaoDeCor,
  ...efeitosDeEstilizar,
  ...efeitosJpegDamage,
  ...efeitosDistorcaoAe,
  ...efeitosShake,
  ...efeitosGlitchDistorcao,
  ...efeitosVhsDamage,
  ...efeitosTvDamage,
  ...efeitosPixelSort,
  ...efeitosAutoPaint,
  ...efeitosPreenchimento,
  ...efeitosSombraProjetada,
  ...efeitosLuzEDiversos,
  ...efeitosMotionTile,
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
  EffectType.looks,
  EffectType.sSharpen,
];

/// Categorias do catalogo, na ordem em que aparecem.
/// CATEGORIAS, em ingles como os nomes. A arrumacao do catalogo e a
/// primeira coisa que a pessoa le, e misturar idioma ali confunde mais
/// do que ajuda.
const effectCategories = <String>[
  // A ordem das abas: Cor, Estilizar, Distorcer, Diversos, Glow e Luz.
  'Color',
  'Stylize',
  'Distort',
  'Misc',
  'Light',
  'Lens',
  'Blur',
  'Glitch',
  'Time',
  'Generate',
  'Keying',
  'Utility',
];

/// O ROTULO da categoria, em portugues, para a galeria (Fase 4).
String categoriaDoEfeito(String categoria) => switch (categoria) {
  'Color' => 'Cor',
  'Light' => 'Glow e Luz',
  'Misc' => 'Diversos',
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
  if (q.isEmpty) return efeitosDoCatalogo;
  bool contem(String texto) => normalizarBusca(texto).contains(q);
  return [
    for (final e in effectSpecs.entries)
      if (!efeitosInternos.contains(e.key) &&
          (contem(e.value.name) ||
              contem(e.value.id) ||
              contem(e.value.category) ||
              contem(_categoriaEmPortugues[e.value.category] ?? '') ||
              e.value.synonyms.any(contem)))
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
    if (e.value.category == category && !efeitosInternos.contains(e.key)) e.key,
];

/// TIME REMAP SAIU DO APP (14/09, pedido do dono). A trilha continua sendo
/// o jeito INTERNO de congelar quadro, fazer rampa pronta e cortar clipe em
/// reverso, entao o tipo e a spec ficam; o que some e a porta: galeria,
/// Tipos que existem no enum por COMPATIBILIDADE DE INDICE, mas nao sao
/// efeitos de verdade: nao aparecem na galeria, na busca nem no guia.
///
/// Ficou vazio quando o Time Remap saiu de vez (a trilha de tempo virou
/// campo proprio de [VideoLayer]). O conjunto continua existindo porque
/// e ele que diz "isto nao e escolhivel" — e porque a lista de exclusao
/// precisa de um lugar unico.
const efeitosInternos = <EffectType>{};

/// A ficha de [t], ou nulo quando o tipo nao tem mais ficha no catalogo.
///
/// NULO E UM RESULTADO LEGITIMO: projeto salvo antes do corte do catalogo
/// (16/09) pode trazer um efeito que nao voltou. Quem trata precisa
/// decidir entre migrar, ignorar ou avisar — nunca estourar.
EffectSpec? specDe(EffectType t) => effectSpecs[t];

/// O que a pessoa pode escolher na galeria.
List<EffectType> get efeitosDoCatalogo => [
  for (final t in effectSpecs.keys)
    if (!efeitosInternos.contains(t)) t,
];

/// Instancia de efeito numa camada. TODO parametro numerico e animavel
/// (trilha de keyframes propria, avaliada no tempo local da camada).
class EffectInstance {
  /// Construir um efeito SEM FICHA nao estoura.
  ///
  /// Ate 17/09 este construtor fazia `effectSpecs[type]!` quatro vezes. Um
  /// tipo removido do catalogo (o corte de 16/09 tirou 91) derrubava
  /// qualquer caminho que o construisse: seis templates empacotados, a
  /// folha de transicao, "Meus presets", a importacao de XML, os presets
  /// de edicao e as operacoes de tempo da camada. Nao era um efeito
  /// quebrado: era a tela inteira.
  ///
  /// A instancia agora nasce INERTE — sem parametros, sem cores extras e
  /// com cor neutra (branco multiplica sem tingir) — e [conhecido] diz
  /// que ela nao tem mais ficha. O palco a pula, o painel a mostra como
  /// removivel. O que se perde e o efeito; nunca a camada, o projeto ou
  /// a tela.
  EffectInstance({
    String? id,
    required this.type,
    Map<String, AnimatedDouble>? params,
    Color? color,
    this.enabled = true,
    this.depth = EffectDepth.pronto,
    List<Color>? extraColors,
  }) : id = id ?? const Uuid().v4(),
       _spec = effectSpecs[type],
       color = color ?? effectSpecs[type]?.defaultColor ?? _corNeutra,
       extraColors = List.unmodifiable(
         extraColors ??
             _coresPadraoDe(effectSpecs[type]),
       ),
       params = Map.unmodifiable(
         params ??
             {
               for (final e in effectSpecs[type]?.params.entries ??
                   const Iterable<MapEntry<String, EffectParam>>.empty())
                 e.key: AnimatedDouble(e.value.initial),
             },
       ) {
    if (_spec == null && !_avisados.contains(type)) {
      _avisados.add(type);
      debugPrint(
        'AUREA: efeito "${type.name}" nao existe mais no catalogo. '
        'A instancia fica inerte ate ser removida.',
      );
    }
  }

  /// A ficha do tipo, guardada na construcao. Nula = efeito removido.
  final EffectSpec? _spec;
  static final Set<EffectType> _avisados = <EffectType>{};

  /// Cor neutra: multiplicar por branco nao tinge, nada muda na tela.
  static const _corNeutra = Color(0xFFFFFFFF);

  static List<Color> _coresPadraoDe(EffectSpec? spec) {
    if (spec == null) return const [];
    return List<Color>.generate(
      spec.extraColors,
      (i) => i < spec.defaultExtraColors.length
          ? spec.defaultExtraColors[i]
          : _coresExtrasPadrao[i % _coresExtrasPadrao.length],
    );
  }

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

  /// Este efeito ainda existe no catalogo?
  ///
  /// Falso = tipo removido pelo corte de 16/09 que sobreviveu no projeto.
  /// O palco pula, o painel mostra como removivel.
  bool get conhecido => _spec != null;

  /// A ficha, ou nulo quando o efeito foi removido do catalogo. Use este
  /// quando a diferenca entre "existe" e "nao existe" mudar a decisao.
  EffectSpec? get specOuNulo => _spec;

  /// A FICHA DE UM EFEITO REMOVIDO, para o codigo que so quer desenhar um
  /// rotulo e nao tem como tratar nulo. O nome diz o que aconteceu, entao
  /// esquecer de checar [conhecido] aparece na tela em vez de estourar.
  static final EffectSpec _fichaInerte = EffectSpec(
    id: 'efeito_removido',
    name: 'Efeito removido',
    category: 'Indisponivel',
    params: const {},
  );

  EffectSpec get spec => _spec ?? _fichaInerte;

  AnimatedDouble track(String key) =>
      params[key] ?? _padroes.putIfAbsent(
        spec.params[key]?.initial ?? 0,
        () => AnimatedDouble(spec.params[key]?.initial ?? 0),
      );

  /// Um AnimatedDouble constante por valor padrao, reaproveitado: parametro
  /// que o projeto nao guardou criava um objeto novo por leitura, por quadro.
  static final Map<double, AnimatedDouble> _padroes = {};

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
    // A curva interna de tempo nao tem losango: arrastar um instante nao a
    // mexe.
    if (efeitosInternos.contains(type)) return this;
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
    if (efeitosInternos.contains(type)) return this;
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
