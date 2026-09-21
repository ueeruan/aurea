import 'acabamento3d.dart';

import 'dart:convert';

import 'package:aurea/src/core/theme/aurea_colors.dart';

import 'package:aurea_render/aurea_render.dart';
import 'package:uuid/uuid.dart';

import 'audio_effect.dart';

import 'dart:typed_data';
import 'dart:ui';

import 'ajuste_da_midia.dart';
import 'animadores.dart';
import 'aprimoramento_ia.dart';
import 'camera3d.dart';
import 'caption.dart';
import 'caption_highlight.dart';
import 'desenho_livre.dart';
import 'effect.dart';
import 'element3d.dart';
import 'extrude3d.dart';
import 'grid_rig.dart';
import 'keyframe.dart';
import 'blend_extra.dart';
import 'camera_cuts.dart';
import 'layer.dart';
import 'layer_meta.dart';
import 'mask.dart';
import 'model_asset3d.dart';
import 'panorama3d.dart';
import 'modelo_do_texto3d.dart';
import 'scene3d.dart';
import 'texto3d.dart';
import 'shape.dart';
import 'shape_ops.dart';
import 'text_anim.dart';
import 'text_path.dart';
import 'text_animator.dart';
import 'video_project.dart';
import 'lod3d.dart';

/// Serializacao JSON do projeto inteiro (persistencia em disco).
/// Regra: toda propriedade animavel vira {b: base, k: [keyframes]};
/// duracoes em microssegundos; cores em ARGB int.

// ------------------------------------------------------------ primitivos

int _dur(Duration d) => d.inMicroseconds;
Duration _asDur(dynamic v) => Duration(microseconds: (v as num).toInt());

/// A MESMA COISA, mas aceitando ausencia.
///
/// Campo que so existe em projeto novo precisa disto: `_asDur` estoura
/// em nulo, e um projeto antigo nao tem por que deixar de abrir por
/// causa de uma medida que ninguem tinha guardado ainda.
Duration? _asDurOuNulo(dynamic v) =>
    v is num ? Duration(microseconds: v.toInt()) : null;

int _col(Color c) => c.toARGB32();
Color _asCol(dynamic v) => Color((v as num).toInt());

Map<String, dynamic> _easing(Easing e) => {
  't': e.type.index,
  'x1': e.x1,
  'y1': e.y1,
  'x2': e.x2,
  'y2': e.y2,
  'c': e.count,
  's': e.smooth,
  'i': e.intensity,
  if (e.type == EasingType.spring) ...{
    'response': e.response,
    'damping': e.damping,
    'initialVelocity': e.initialVelocity,
  },
};

/// O tipo da curva pelo INDICE gravado, com guarda de faixa.
///
/// Um projeto salvo por uma versao mais nova pode trazer um tipo que esta
/// versao nao conhece. `values[indice]` estourava `RangeError` e o projeto
/// inteiro deixava de abrir por causa de UMA curva; fora da faixa, a curva
/// cai na bezier com as alcas que vieram no arquivo (leitura tolerante).
EasingType _asTipoDeEasing(dynamic v) {
  final i = v is num ? v.toInt() : 0;
  return i >= 0 && i < EasingType.values.length
      ? EasingType.values[i]
      : EasingType.cubicBezier;
}

/// O loop de uma trilha, o MESMO formato para numero e para ponto.
Map<String, dynamic> _loop(LoopSpec l) => {
  'm': l.mode.index,
  'w': l.when.index,
  'n': l.count,
};

LoopSpec _asLoop(dynamic v) {
  if (v is! Map) return LoopSpec.none;
  final m = (v['m'] as num?)?.toInt() ?? 0;
  final w = (v['w'] as num?)?.toInt() ?? 0;
  return LoopSpec(
    mode: m >= 0 && m < LoopMode.values.length
        ? LoopMode.values[m]
        : LoopMode.none,
    when: w >= 0 && w < LoopWhen.values.length
        ? LoopWhen.values[w]
        : LoopWhen.after,
    count: (v['n'] as num?)?.toInt() ?? 0,
  );
}

Easing _asEasing(Map<String, dynamic> m) => Easing(
  type: _asTipoDeEasing(m['t']),
  x1: (m['x1'] as num).toDouble(),
  y1: (m['y1'] as num).toDouble(),
  x2: (m['x2'] as num).toDouble(),
  y2: (m['y2'] as num).toDouble(),
  count: (m['c'] as num).toInt(),
  smooth: (m['s'] as num).toDouble(),
  intensity: (m['i'] as num).toDouble(),
  response: (m['response'] as num?)?.toDouble() ?? 0.55,
  damping: (m['damping'] as num?)?.toDouble() ?? 0.825,
  initialVelocity: (m['initialVelocity'] as num?)?.toDouble() ?? 0,
);

/// A CURVA NO FORMATO DO ARQUIVO, para quem grava trilha fora daqui: os
/// ajustes por caractere do Texto 3D (`AjusteDeCaracteres`) guardam o
/// keyframe no bloco deles, e a curva tem de sair com a MESMA codificacao
/// das outras trilhas — um formato de curva so no projeto inteiro.
Map<String, dynamic> easingToJson(Easing e) => _easing(e);

/// A leitura estrita de [easingToJson] (campo faltando lanca); quem precisa
/// de tolerancia pega o erro e cai na linear.
Easing easingFromJson(Map<String, dynamic> m) => _asEasing(m);

Map<String, dynamic> _ad(AnimatedDouble a) => {
  'b': a.base,
  // A expressao e texto: vai como veio. Ausente = sem expressao.
  if (a.hasExpression) 'x': a.expression,
  if (a.animador != null) 'an': a.animador!.toJson(),
  if (a.keyframes.isNotEmpty)
    'k': [
      for (final k in a.keyframes)
        {'t': _dur(k.time), 'v': k.value, 'e': _easing(k.ease)},
    ],
  if (a.loop.active) 'loop': _loop(a.loop),
};

/// A ESCALA POR EIXO DE UM NO DA CENA VALE A PENA GRAVAR?
///
/// Tres trilhas a mais por no, em cenas com centenas de nos, so para
/// dizer "1" trezentas vezes. Grava-se apenas o que alguem mexeu: valor
/// diferente de 1, keyframe, expressao, animador ou loop.
bool _escalaDeEixoMexida(AnimatedDouble a) =>
    (a.base - 1).abs() > 1e-9 ||
    a.keyframes.isNotEmpty ||
    a.hasExpression ||
    a.animador != null ||
    a.loop.active;

/// Numero que VIROU animavel (constituicao, regra 6): projeto antigo
/// gravou um numero solto; o novo grava a trilha inteira. Os dois abrem.
AnimatedDouble _asAdOuNumero(dynamic v, double padrao) {
  if (v == null) return AnimatedDouble(padrao);
  if (v is num) return AnimatedDouble(v.toDouble());
  return _asAd(v);
}

AnimatedDouble _asAd(dynamic v) {
  final m = v as Map<String, dynamic>;
  return AnimatedDouble(
    (m['b'] as num).toDouble(),
    [
      for (final k in (m['k'] as List? ?? const []))
        Keyframe<double>(
          time: _asDur(k['t']),
          value: (k['v'] as num).toDouble(),
          ease: _asEasing(k['e'] as Map<String, dynamic>),
        ),
    ],
    _asLoop(m['loop']),
    m['x'] as String?,
    AnimadorAutomatico.fromJson(m['an']),
  );
}

Map<String, dynamic> _ao(AnimatedOffset a) => {
  'x': a.base.dx,
  'y': a.base.dy,
  if (a.animador != null) 'an': a.animador!.toJson(),
  if (a.keyframes.isNotEmpty)
    'k': [
      for (final k in a.keyframes)
        {
          't': _dur(k.time),
          'x': k.value.dx,
          'y': k.value.dy,
          'e': _easing(k.ease),
        },
    ],
  // O loop de Posicao e de Pivo NAO era gravado: "Loop: repetir" sumia ao
  // salvar e reabrir. Aditivo — arquivo antigo, sem a chave, abre igual.
  if (a.loop.active) 'loop': _loop(a.loop),
};

AnimatedOffset _asAo(dynamic v) {
  final m = v as Map<String, dynamic>;
  return AnimatedOffset(
    Offset((m['x'] as num).toDouble(), (m['y'] as num).toDouble()),
    [
      for (final k in (m['k'] as List? ?? const []))
        Keyframe<Offset>(
          time: _asDur(k['t']),
          value: Offset((k['x'] as num).toDouble(), (k['y'] as num).toDouble()),
          ease: _asEasing(k['e'] as Map<String, dynamic>),
        ),
    ],
    _asLoop(m['loop']),
    AnimadorAutomatico.fromJson(m['an']),
  );
}

// -------------------------------------------------------------- mascaras

Map<String, dynamic> _bezier(BezierPath p) => {
  'closed': p.closed,
  'v': [
    for (final v in p.vertices)
      {
        'x': v.p.dx,
        'y': v.p.dy,
        'ix': v.inT.dx,
        'iy': v.inT.dy,
        'ox': v.outT.dx,
        'oy': v.outT.dy,
        'c': v.corner,
      },
  ],
};

BezierPath _asBezier(Map<String, dynamic> m) => BezierPath(
  closed: m['closed'] as bool,
  vertices: [
    for (final v in (m['v'] as List))
      PathVertex(
        p: Offset((v['x'] as num).toDouble(), (v['y'] as num).toDouble()),
        inT: Offset((v['ix'] as num).toDouble(), (v['iy'] as num).toDouble()),
        outT: Offset((v['ox'] as num).toDouble(), (v['oy'] as num).toDouble()),
        corner: v['c'] as bool,
      ),
  ],
);

Map<String, dynamic> _apath(AnimatedPath a) => {
  'base': _bezier(a.base),
  if (a.keyframes.isNotEmpty)
    'k': [
      for (final k in a.keyframes)
        {'t': _dur(k.time), 'v': _bezier(k.value), 'e': _easing(k.ease)},
    ],
};

AnimatedPath _asApath(Map<String, dynamic> m) =>
    AnimatedPath(_asBezier(m['base'] as Map<String, dynamic>), [
      for (final k in (m['k'] as List? ?? const []))
        Keyframe<BezierPath>(
          time: _asDur(k['t']),
          value: _asBezier(k['v'] as Map<String, dynamic>),
          ease: _asEasing(k['e'] as Map<String, dynamic>),
        ),
    ]);

Map<String, dynamic> _mask(LayerMask m) => {
  'id': m.id,
  'name': m.name,
  'mode': m.mode.index,
  'inv': m.inverted,
  'path': _apath(m.path),
  'feather': _ad(m.feather),
  // So sai no arquivo quando os eixos estao soltos.
  if (m.featherY != null) 'featherY': _ad(m.featherY!),
  'op': _ad(m.opacity),
  'exp': _ad(m.expansion),
};

LayerMask _asMask(Map<String, dynamic> m) => LayerMask(
  id: m['id'] as String,
  name: m['name'] as String,
  mode: MaskMode.values[(m['mode'] as num).toInt()],
  inverted: m['inv'] as bool,
  path: _asApath(m['path'] as Map<String, dynamic>),
  feather: _asAd(m['feather']),
  featherY: m['featherY'] == null ? null : _asAd(m['featherY']),
  opacity: _asAd(m['op']),
  expansion: _asAd(m['exp']),
);

// ------------------------------------------------------------------ grid

/// Compat: rigs antigos salvavam numeros crus; agora todo parametro e
/// uma trilha animavel.
AnimatedDouble _adCompat(dynamic v) =>
    v is num ? AnimatedDouble(v.toDouble()) : _asAd(v);

Map<String, dynamic> _rig(GridRig g) => {
  'assets': g.assets,
  'cols': g.columns,
  'sx': _ad(g.spacingX),
  'sy': _ad(g.spacingY),
  'radius': _ad(g.radius),
  'spread': g.spread,
  'rot': _ad(g.gridRotationDeg),
  'twist': _ad(g.twistDeg),
  'stagger': _ad(g.staggerDeg),
  'zd': _ad(g.zDepth),
  'sf': _ad(g.scaleFront),
  'sb': _ad(g.scaleBack),
  'gop': g.globalOpacity,
  'ro': _ad(g.randomOffset),
  'seed': g.seed,
  'shuffle': g.shuffle,
  'trans': _ad(g.transition),
  if (g.controllerId != null) 'ctrl': g.controllerId,
  if (g.proximity != null)
    'prox': {
      'on': g.proximity!.enabled,
      'e': _ao(g.proximity!.effector),
      'ez': _ad(g.proximity!.effectorZ),
      'r': _ad(g.proximity!.radius),
      'f': _ad(g.proximity!.falloff),
      'smin': g.proximity!.scaleMin,
      'smax': g.proximity!.scaleMax,
      'omin': g.proximity!.opacityMin,
      'omax': g.proximity!.opacityMax,
      'att': _ad(g.proximity!.attract),
    },
};

GridRig _asRig(Map<String, dynamic> m) => GridRig(
  assets: [for (final a in (m['assets'] as List)) a as String],
  columns: (m['cols'] as num).toInt(),
  spacingX: _adCompat(m['sx']),
  spacingY: _adCompat(m['sy']),
  radius: _adCompat(m['radius']),
  spread: (m['spread'] as num).toDouble(),
  gridRotationDeg: _adCompat(m['rot']),
  twistDeg: _adCompat(m['twist']),
  staggerDeg: _adCompat(m['stagger']),
  zDepth: _adCompat(m['zd']),
  scaleFront: _adCompat(m['sf']),
  scaleBack: _adCompat(m['sb']),
  globalOpacity: (m['gop'] as num).toDouble(),
  randomOffset: _adCompat(m['ro']),
  seed: (m['seed'] as num).toInt(),
  shuffle: m['shuffle'] as bool,
  transition: _asAd(m['trans']),
  controllerId: m['ctrl'] as String?,
  proximity: m['prox'] == null
      ? null
      : ProximityGroup(
          enabled: (m['prox'] as Map)['on'] as bool,
          effector: _asAo((m['prox'] as Map)['e']),
          effectorZ: _asAd((m['prox'] as Map)['ez']),
          radius: _asAd((m['prox'] as Map)['r']),
          falloff: _asAd((m['prox'] as Map)['f']),
          scaleMin: ((m['prox'] as Map)['smin'] as num).toDouble(),
          scaleMax: ((m['prox'] as Map)['smax'] as num).toDouble(),
          opacityMin: ((m['prox'] as Map)['omin'] as num).toDouble(),
          opacityMax: ((m['prox'] as Map)['omax'] as num).toDouble(),
          attract: _asAd((m['prox'] as Map)['att']),
        ),
);

// -------------------------------------------------------------- efeitos

Map<String, dynamic> _effect(EffectInstance e) => {
  'id': e.id,
  // O identificador ESTAVEL e o que manda na leitura; o indice do
  // enum fica so para o aplicativo antigo continuar abrindo o
  // arquivo novo.
  'kind': effectIdOf(e.type),
  'type': e.type.index,
  'color': _col(e.color),
  if (e.extraColors.isNotEmpty)
    'colors': [for (final c in e.extraColors) _col(c)],
  'enabled': e.enabled,
  'depth': e.depth.index,
  // VERSAO: alguns numeros mudaram de UNIDADE no nivel 3 (raio em
  // pixel, limite e intensidade em porcentagem). Sem isto nao da
  // para saber se o numero lido ja esta na unidade nova.
  'v': kEffectVersion,
  'params': {for (final p in e.params.entries) p.key: _ad(p.value)},
};

/// Converte a trilha inteira (base e keyframes) para a unidade de agora.
AnimatedDouble _migrarTrilha(
  EffectType tipo,
  String chave,
  AnimatedDouble t,
  int versao,
) {
  final base = migrateParamValue(tipo, chave, t.base, versao);
  if (base == t.base && t.keyframes.isEmpty) return t;
  return AnimatedDouble(
    base,
    [
      for (final k in t.keyframes)
        Keyframe<double>(
          time: k.time,
          value: migrateParamValue(tipo, chave, k.value, versao),
          ease: k.ease,
        ),
      // A migracao reconstroi a trilha para converter unidades; sem
      // repassar a expressao aqui ela morria ao ABRIR o projeto — e so em
      // trilhas com keyframe, que e o caso que ninguem testa a mao.
    ],
    t.loop,
    t.expression,
  );
}

/// O id que o Time Remap tinha quando ainda era efeito.

/// Este item da lista de efeitos e o Time Remap de antes?
///
/// Ele saiu do catalogo em 16/09 e virou campo da camada. O arquivo
/// antigo continua trazendo o efeito, e reconhece-lo aqui e o que permite
/// MIGRAR a trilha em vez de descartar: quem tinha rampa de velocidade,
/// congelamento ou reverso abre o projeto com eles no lugar.
bool _eOTimeRemapAntigo(Map<String, dynamic> m) {
  // Named Time Remap is a normal effect again; retain id/order/enabled.
  // Arquivo antigo o bastante para nao ter `kind`: o indice 26 era ele.
  if (m['kind'] == null &&
      (m['type'] as num?)?.toInt() == indiceLegadoDoTimeRemap) {
    return true;
  }
  return false;
}

/// Nulo quando o tipo nao existe mais: nao ha efeito a construir.
EffectInstance? _asEffect(Map<String, dynamic> m) {
  final tipo = _tipoDoEfeito(m);
  if (tipo == null) return null;
  final versao = (m['v'] as num?)?.toInt() ?? 0;
  return EffectInstance(
    id: m['id'] as String,
    type: tipo,
    color: _asCol(m['color']),
    extraColors: m['colors'] == null
        ? null
        : [for (final c in (m['colors'] as List)) _asCol(c)],
    enabled: m['enabled'] as bool,
    // A chave passa pela tabela de alias: parametro renomeado nao pode
    // fazer o efeito voltar ao padrao sem aviso.
    params: {
      for (final p in (m['params'] as Map<String, dynamic>).entries)
        resolveParamKey(tipo, p.key): _migrarTrilha(
          tipo,
          resolveParamKey(tipo, p.key),
          _asAd(p.value),
          versao,
        ),
    },
    depth: m['depth'] == null
        ? EffectDepth.avancado
        : EffectDepth.values[((m['depth'] as num).toInt()).clamp(
            0,
            EffectDepth.values.length - 1,
          )],
  );
}

/// O tipo do efeito: pelo id quando ha, pelo indice do enum antigo quando
/// o arquivo e antigo. NULO = nao existe mais, e quem chama decide.
///
/// O indice NAO pode ser lido contra o enum de hoje: tirar o Time Remap
/// do meio deslocou 101 tipos uma casa, e um arquivo de 31/08 abriria
/// "Pixel Sort" onde havia outro efeito, sem aviso nenhum. [tipoPorIndiceLegado]
/// e a ponte.
EffectType? _tipoDoEfeito(Map<String, dynamic> m) {
  final kind = m['kind'] as String?;
  if (kind != null) {
    final t = effectTypeFromId(kind);
    if (t != null) return t;
  }
  return tipoPorIndiceLegado((m['type'] as num?)?.toInt() ?? 0);
}

// --------------------------------------------------------------- formas

Map<String, dynamic> _shapeItem(ShapeItem s) => switch (s) {
  ShapePath p => {
    'kind': 'path',
    'id': p.id,
    'prim': p.primitive.index,
    'w': p.width,
    'h': p.height,
    'cr': p.cornerRadius,
    'pts': p.points,
    'irr': p.innerRadiusRatio,
    'sa': p.startAngle,
    'sw': p.sweepAngle,
    'th': p.thickness,
    'amp': p.amplitude,
    'fq': p.frequency,
  },
  ShapeParametric sp => {
    'kind': 'param',
    'id': sp.id,
    'pk': sp.kind.index,
    'sx': _ad(sp.sizeX),
    'sy': _ad(sp.sizeY),
    'round': _ad(sp.roundness),
    if (sp.cornerTopLeft != null) 'rtl': _ad(sp.cornerTopLeft!),
    if (sp.cornerTopRight != null) 'rtr': _ad(sp.cornerTopRight!),
    if (sp.cornerBottomRight != null) 'rbr': _ad(sp.cornerBottomRight!),
    if (sp.cornerBottomLeft != null) 'rbl': _ad(sp.cornerBottomLeft!),
    'roundPct': sp.roundnessPercent,
    'pts': _ad(sp.points),
    'ro': _ad(sp.outerRadius),
    'ri': _ad(sp.innerRadius),
    'rdo': _ad(sp.outerRoundness),
    'rdi': _ad(sp.innerRoundness),
    'srot': _ad(sp.shapeRotation),
    'sa': _ad(sp.startAngle),
    'sw': _ad(sp.sweep),
    'si': _ad(sp.sectorInner),
    if (sp.extras.isNotEmpty)
      'ex': {for (final e in sp.extras.entries) e.key: _ad(e.value)},
  },
  ShapeDesenho d => {
    'kind': 'desenho',
    'id': d.id,
    'tracos': [for (final traco in d.tracos) traco.toJson()],
  },
  ShapeMediaFill f => {
    'kind': 'mfill',
    'id': f.id,
    'src': f.sourcePath,
    'fit': f.encaixe.index,
    'op': f.opacity,
  },
  ShapeFill f => {
    'kind': 'fill',
    'id': f.id,
    'color': _col(f.color),
    'op': f.opacity,
    'eo': f.evenOdd,
  },
  ShapeStroke st => {
    'kind': 'stroke',
    'id': st.id,
    'color': _col(st.color),
    'w': _ad(st.width),
    'cap': st.cap.index,
    'join': st.join.index,
    'miter': st.miterLimit,
    if (st.inicio != TerminacaoDoTraco.nenhuma) 'tIni': st.inicio.index,
    if (st.fim != TerminacaoDoTraco.nenhuma) 'tFim': st.fim.index,
    if (st.tamanhoDaTerminacao != 3) 'tSz': st.tamanhoDaTerminacao,
    'op': _ad(st.opacity),
    'dash': _ad(st.dashLength),
    'gap': _ad(st.gapLength),
    'doff': _ad(st.dashOffset),
  },
  ShapeGradientFill g => {
    'kind': 'gfill',
    'id': g.id,
    'ca': _col(g.colorA),
    'cb': _col(g.colorB),
    'ang': g.angleDeg,
    'radial': g.radial,
    if (g.varredura) 'sweep': true,
    'op': g.opacity,
    if (g.extras.isNotEmpty) 'mid': [for (final c in g.extras) _col(c)],
    if (g.stops.isNotEmpty) 'stops': g.stops,
    if (g.center != Offset.zero) 'center': [g.center.dx, g.center.dy],
    if (g.radiusScale != 1) 'radiusScale': g.radiusScale,
    if (g.colorFrames.isNotEmpty)
      'colorFrames': [
        for (final k in g.colorFrames)
          {
            't': _dur(k.time),
            'v': [for (final c in k.value) _col(c)],
            'e': _easing(k.ease),
          },
      ],
  },
  ShapeSvgPath p => {
    'kind': 'svg',
    'id': p.id,
    'd': p.pathData,
    'size': p.size,
  },
  ShapeBezier b => {'kind': 'bezier', 'id': b.id, 'path': _apath(b.path)},
  ShapeMorph m => {
    'kind': 'morph',
    'id': m.id,
    'from': _shapeItem(m.from),
    'to': _shapeItem(m.to),
    'prog': _ad(m.progress),
  },
  TrimOperator t => {
    'kind': 'trim',
    'id': t.id,
    'start': _ad(t.start),
    'end': _ad(t.end),
    'offset': _ad(t.offset),
    'ind': t.individually,
  },
  OffsetPathOperator o => {
    'kind': 'offsetPath',
    'id': o.id,
    'amt': _ad(o.amount),
  },
  RoundCornersOperator r => {
    'kind': 'roundCorners',
    'id': r.id,
    'r': _ad(r.radius),
  },
  ZigZagOperator z => {
    'kind': 'zigzag',
    'id': z.id,
    'amp': _ad(z.amplitude),
    'ridges': _ad(z.ridges),
    'smooth': z.smooth,
  },
  PuckerBloatOperator pb => {
    'kind': 'pucker',
    'id': pb.id,
    'amt': _ad(pb.amount),
  },
  TwistOperator tw => {'kind': 'twist', 'id': tw.id, 'ang': _ad(tw.angle)},
  WigglePathOperator w => {
    'kind': 'wigglePath',
    'id': w.id,
    'amt': _ad(w.amount),
    'detail': _ad(w.detail),
    'evo': _ad(w.evolution),
    'seed': w.seed,
  },
  MergePathsOperator mp => {
    'kind': 'merge',
    'id': mp.id,
    'mode': mp.mode.index,
  },
  RepeaterOperator r => {
    'kind': 'repeater',
    'id': r.id,
    'copies': r.copies,
    'dx': r.dx,
    'dy': r.dy,
    'rot': _ad(r.rotation),
    'step': r.scaleStep,
  },
};

/// Um item de forma para JSON e de volta — publico para os testes de
/// compatibilidade (numero que virou animavel continua abrindo).
Map<String, dynamic> shapeItemToJson(ShapeItem s) => _shapeItem(s);
ShapeItem shapeItemFromJson(Map<String, dynamic> m) => _asShapeItem(m);

ShapeItem _asShapeItem(Map<String, dynamic> m) => switch (m['kind']) {
  'path' => ShapePath(
    id: m['id'] as String,
    primitive: ShapePrimitive.values[(m['prim'] as num).toInt()],
    width: (m['w'] as num).toDouble(),
    height: (m['h'] as num).toDouble(),
    cornerRadius: (m['cr'] as num).toDouble(),
    points: (m['pts'] as num).toInt(),
    innerRadiusRatio: (m['irr'] as num).toDouble(),
    startAngle: (m['sa'] as num).toDouble(),
    sweepAngle: (m['sw'] as num).toDouble(),
    thickness: (m['th'] as num).toDouble(),
    amplitude: (m['amp'] as num).toDouble(),
    frequency: (m['fq'] as num).toDouble(),
  ),
  'param' => ShapeParametric(
    id: m['id'] as String,
    kind:
        ParamShapeKind.values[(m['pk'] as num).toInt().clamp(
          0,
          ParamShapeKind.values.length - 1,
        )],
    extras: m['ex'] is Map
        ? {
            for (final e in (m['ex'] as Map).entries)
              e.key as String: _asAd(e.value),
          }
        : null,
    sizeX: _asAd(m['sx']),
    sizeY: _asAd(m['sy']),
    roundness: _asAd(m['round']),
    cornerTopLeft: m['rtl'] == null ? null : _asAd(m['rtl']),
    cornerTopRight: m['rtr'] == null ? null : _asAd(m['rtr']),
    cornerBottomRight: m['rbr'] == null ? null : _asAd(m['rbr']),
    cornerBottomLeft: m['rbl'] == null ? null : _asAd(m['rbl']),
    roundnessPercent: m['roundPct'] as bool? ?? true,
    points: _asAd(m['pts']),
    outerRadius: _asAd(m['ro']),
    innerRadius: _asAd(m['ri']),
    outerRoundness: _asAd(m['rdo']),
    innerRoundness: _asAd(m['rdi']),
    shapeRotation: _asAd(m['srot']),
    startAngle: _asAd(m['sa']),
    sweep: _asAd(m['sw']),
    sectorInner: _asAd(m['si']),
  ),
  'desenho' => ShapeDesenho(
    id: m['id'] as String,
    tracos: [
      for (final raw in (m['tracos'] as List? ?? const []))
        ?TracoDoDesenho.fromJson(raw),
    ],
  ),
  'mfill' => ShapeMediaFill(
    id: m['id'] as String,
    sourcePath: m['src'] as String,
    encaixe:
        EncaixeNaForma.values[((m['fit'] as num?)?.toInt() ?? 0).clamp(
          0,
          EncaixeNaForma.values.length - 1,
        )],
    opacity: (m['op'] as num?)?.toDouble() ?? 1,
  ),
  'fill' => ShapeFill(
    id: m['id'] as String,
    color: _asCol(m['color']),
    opacity: (m['op'] as num).toDouble(),
    evenOdd: m['eo'] as bool? ?? false,
  ),
  'stroke' => ShapeStroke(
    id: m['id'] as String,
    color: _asCol(m['color']),
    width: _asAdOuNumero(m['w'], 12),
    cap: StrokeCap.values[(m['cap'] as num).toInt()],
    join: m['join'] == null
        ? StrokeJoin.round
        : StrokeJoin.values[(m['join'] as num).toInt()],
    miterLimit: (m['miter'] as num?)?.toDouble() ?? 4,
    inicio: _terminacao(m['tIni']),
    fim: _terminacao(m['tFim']),
    tamanhoDaTerminacao: (m['tSz'] as num?)?.toDouble() ?? 3,
    opacity: _asAdOuNumero(m['op'], 1),
    dashLength: _asAdOuNumero(m['dash'], 0),
    gapLength: _asAdOuNumero(m['gap'], 0),
    dashOffset: m['doff'] == null ? AnimatedDouble(0) : _asAd(m['doff']),
  ),
  'gfill' => ShapeGradientFill(
    id: m['id'] as String,
    colorA: _asCol(m['ca']),
    colorB: _asCol(m['cb']),
    angleDeg: (m['ang'] as num).toDouble(),
    radial: m['radial'] as bool,
    varredura: m['sweep'] as bool? ?? false,
    opacity: (m['op'] as num).toDouble(),
    extras: [for (final c in (m['mid'] as List? ?? const [])) _asCol(c)],
    stops: [
      for (final s in (m['stops'] as List? ?? const [])) (s as num).toDouble(),
    ],
    center: m['center'] is List && (m['center'] as List).length == 2
        ? Offset(
            (m['center'][0] as num).toDouble(),
            (m['center'][1] as num).toDouble(),
          )
        : Offset.zero,
    radiusScale: (m['radiusScale'] as num?)?.toDouble() ?? 1,
    colorFrames: [
      for (final k in (m['colorFrames'] as List? ?? const []))
        Keyframe(
          time: _asDur(k['t']),
          value: [for (final c in k['v']) _asCol(c)],
          ease: k['e'] == null
              ? const Easing()
              : _asEasing(k['e'] as Map<String, dynamic>),
        ),
    ],
  ),
  'svg' => ShapeSvgPath(
    id: m['id'] as String,
    pathData: m['d'] as String,
    size: (m['size'] as num).toDouble(),
  ),
  'bezier' => ShapeBezier(
    id: m['id'] as String,
    path: _asApath(m['path'] as Map<String, dynamic>),
  ),
  'morph' => ShapeMorph(
    id: m['id'] as String,
    from: _asShapeItem(m['from'] as Map<String, dynamic>) as ShapePath,
    to: _asShapeItem(m['to'] as Map<String, dynamic>) as ShapePath,
    progress: _asAd(m['prog']),
  ),
  'trim' => TrimOperator(
    id: m['id'] as String,
    start: _asAd(m['start']),
    end: _asAd(m['end']),
    offset: _asAd(m['offset']),
    individually: m['ind'] as bool? ?? true,
  ),
  'repeater' => RepeaterOperator(
    id: m['id'] as String,
    copies: (m['copies'] as num).toInt(),
    dx: (m['dx'] as num).toDouble(),
    dy: (m['dy'] as num).toDouble(),
    rotation: _asAd(m['rot']),
    scaleStep: (m['step'] as num).toDouble(),
  ),
  'offsetPath' => OffsetPathOperator(
    id: m['id'] as String,
    amount: _asAd(m['amt']),
  ),
  'roundCorners' => RoundCornersOperator(
    id: m['id'] as String,
    radius: _asAd(m['r']),
  ),
  'zigzag' => ZigZagOperator(
    id: m['id'] as String,
    amplitude: _asAd(m['amp']),
    ridges: _asAd(m['ridges']),
    smooth: m['smooth'] as bool? ?? false,
  ),
  'pucker' => PuckerBloatOperator(
    id: m['id'] as String,
    amount: _asAd(m['amt']),
  ),
  'twist' => TwistOperator(id: m['id'] as String, angle: _asAd(m['ang'])),
  'wigglePath' => WigglePathOperator(
    id: m['id'] as String,
    amount: _asAd(m['amt']),
    detail: _asAd(m['detail']),
    evolution: _asAd(m['evo']),
    seed: (m['seed'] as num?)?.toInt() ?? 1,
  ),
  'merge' => MergePathsOperator(
    id: m['id'] as String,
    mode: MergeMode.values[(m['mode'] as num).toInt()],
  ),
  _ => throw FormatException('ShapeItem desconhecido: ${m['kind']}'),
};

// ----------------------------------------------------- animadores de texto

Map<String, dynamic> _selector(TextSelector s) => switch (s) {
  RangeSelector r => {
    'kind': 'range',
    'id': r.id,
    'mode': r.mode.index,
    'basedOn': r.basedOn.index,
    'units': r.units.index,
    'start': _ad(r.start),
    'end': _ad(r.end),
    'offset': _ad(r.offset),
    'shape': r.shape.index,
    'amount': _ad(r.amount),
    'smooth': _ad(r.smoothness),
    'easeHigh': _ad(r.easeHigh),
    'easeLow': _ad(r.easeLow),
    'rand': r.randomizeOrder,
    'seed': r.randomSeed,
    'order': r.order.index,
    'hold': r.holdBeyond,
  },
  WigglySelector w => {
    'kind': 'wiggly',
    'id': w.id,
    'mode': w.mode.index,
    'basedOn': w.basedOn.index,
    'max': _ad(w.maxAmount),
    'min': _ad(w.minAmount),
    'wps': _ad(w.wigglesPerSecond),
    'corr': _ad(w.correlation),
    'tph': _ad(w.temporalPhase),
    'sph': _ad(w.spatialPhase),
    'lock': w.lockDimensions,
    'seed': w.randomSeed,
  },
  ExpressionSelector e => {
    'kind': 'expr',
    'id': e.id,
    'mode': e.mode.index,
    'basedOn': e.basedOn.index,
    // O TEXTO da expressao, como a pessoa escreveu. Guardar a arvore
    // compilada seria menor e seria errado: quem abre o projeto amanha
    // tem de poder LER e editar o que escreveu.
    'src': e.source,
    'amount': _ad(e.amount),
  },
  // Seletor escalonado nao e serializado aqui: ele nasce da
  // compilacao da animacao do catalogo, que ja e salva em 'anims'.
  StaggerSelector g => {
    'kind': 'stagger',
    'id': g.id,
    'mode': g.mode.index,
    'basedOn': g.basedOn.index,
    'start': g.start.inMicroseconds,
    'dur': g.duration.inMicroseconds,
    'stag': g.stagger.inMicroseconds,
    'order': g.order.index,
    'seed': g.seed,
    'ease': g.ease.index,
    'amp': g.amplitude,
    'freq': g.frequency,
    'decay': g.decay,
    'cov': g.startCovered,
    'loop': g.loop,
    'shape': g.loopShape.index,
  },
};

TextSelector _asSelector(Map<String, dynamic> m) => switch (m['kind']) {
  'range' => RangeSelector(
    id: m['id'] as String,
    mode: SelectorMode.values[(m['mode'] as num).toInt()],
    basedOn: SelectorBasedOn.values[(m['basedOn'] as num).toInt()],
    units: SelectorUnits.values[(m['units'] as num).toInt()],
    start: _asAd(m['start']),
    end: _asAd(m['end']),
    offset: _asAd(m['offset']),
    shape: SelectorShape.values[(m['shape'] as num).toInt()],
    amount: _asAd(m['amount']),
    smoothness: _asAd(m['smooth']),
    easeHigh: _asAd(m['easeHigh']),
    easeLow: _asAd(m['easeLow']),
    randomizeOrder: m['rand'] as bool,
    randomSeed: (m['seed'] as num).toInt(),
    order: m['order'] == null
        ? SelectorOrder.identity
        : SelectorOrder.values[(m['order'] as num).toInt()],
    holdBeyond: m['hold'] as bool? ?? false,
  ),
  'wiggly' => WigglySelector(
    id: m['id'] as String,
    mode: SelectorMode.values[(m['mode'] as num).toInt()],
    basedOn: SelectorBasedOn.values[(m['basedOn'] as num).toInt()],
    maxAmount: _asAd(m['max']),
    minAmount: _asAd(m['min']),
    wigglesPerSecond: _asAd(m['wps']),
    correlation: _asAd(m['corr']),
    temporalPhase: _asAd(m['tph']),
    spatialPhase: _asAd(m['sph']),
    lockDimensions: m['lock'] as bool,
    randomSeed: (m['seed'] as num).toInt(),
  ),
  'expr' => ExpressionSelector(
    id: m['id'] as String,
    mode: SelectorMode.values[(m['mode'] as num).toInt()],
    basedOn: SelectorBasedOn.values[(m['basedOn'] as num).toInt()],
    source: m['src'] as String? ?? '',
    amount: _asAd(m['amount']),
  ),
  'stagger' => StaggerSelector(
    id: m['id'] as String,
    mode: SelectorMode.values[(m['mode'] as num).toInt()],
    basedOn: SelectorBasedOn.values[(m['basedOn'] as num).toInt()],
    start: Duration(microseconds: (m['start'] as num).toInt()),
    duration: Duration(microseconds: (m['dur'] as num).toInt()),
    stagger: Duration(microseconds: (m['stag'] as num).toInt()),
    order: TextAnimOrder.values[(m['order'] as num).toInt()],
    seed: (m['seed'] as num).toInt(),
    ease: TextAnimEase.values[(m['ease'] as num).toInt()],
    amplitude: (m['amp'] as num).toDouble(),
    frequency: (m['freq'] as num).toDouble(),
    decay: (m['decay'] as num).toDouble(),
    startCovered: m['cov'] as bool? ?? true,
    loop: m['loop'] as bool? ?? false,
    loopShape: LoopShape.values[(m['shape'] as num).toInt()],
  ),
  _ => throw FormatException('Seletor desconhecido: ${m['kind']}'),
};

/// ANIMACAO DO CATALOGO (modelo AM). Guarda o id da animacao e os seis
/// controles — o animador em si e recompilado na leitura, entao melhorar
/// uma animacao do catalogo melhora os projetos ja salvos.
Map<String, dynamic>? _textPath(TextPathSpec t) => !t.active
    ? null
    : {
        'kind': t.kind.index,
        'r': t.radius,
        'sweep': t.sweepDeg,
        'start': t.startDeg,
        if (t.shapeLayerId != null) 'shape': t.shapeLayerId,
        'off': t.offset,
        'sp': t.spacing,
        'align': t.align.index,
        'perp': t.perpendicular,
        'rev': t.reverse,
      };

TextPathSpec _asTextPath(Object? raw) {
  if (raw is! Map) return const TextPathSpec();
  final m = raw.cast<String, dynamic>();
  return TextPathSpec(
    kind: TextPathKind.values[(m['kind'] as num?)?.toInt() ?? 0],
    radius: (m['r'] as num?)?.toDouble() ?? 180,
    sweepDeg: (m['sweep'] as num?)?.toDouble() ?? 180,
    startDeg: (m['start'] as num?)?.toDouble() ?? -90,
    shapeLayerId: m['shape'] as String?,
    offset: (m['off'] as num?)?.toDouble() ?? 0,
    spacing: (m['sp'] as num?)?.toDouble() ?? 0,
    align: TextPathAlign.values[(m['align'] as num?)?.toInt() ?? 1],
    perpendicular: m['perp'] as bool? ?? true,
    reverse: m['rev'] as bool? ?? false,
  );
}

Map<String, dynamic>? _audioSpec(AudioSpec a) => a.isNeutral
    ? null
    : {
        'in': a.fadeIn.inMicroseconds,
        'out': a.fadeOut.inMicroseconds,
        'gain': a.gain,
        'mute': a.muted,
        if (a.duckAgainstId != null) 'duck': a.duckAgainstId,
        'duckAmt': a.duckAmount,
        if (a.duckAttack != const Duration(milliseconds: 120))
          'duckAtk': a.duckAttack.inMicroseconds,
        if (a.duckRelease != const Duration(milliseconds: 450))
          'duckRel': a.duckRelease.inMicroseconds,
        if (a.duckThreshold != 0.05) 'duckThr': a.duckThreshold,
        if (a.normalizeTargetLufs != null) 'lufs': a.normalizeTargetLufs,
        if (!a.processing.isNeutral) 'proc': _processing(a.processing),
        if (!a.preservePitch) 'pitch': false,
        if (a.volumeAnimado != null) 'vol': _ad(a.volumeAnimado!),
      };

Map<String, dynamic> _opcoesDaCamera(OpcoesDaCamera o) => {
  if (o.ortografica) 'orto': true,
  if (o.focoLigado) 'foco': true,
  'fd': _ad(o.distanciaDoFoco),
  'fi': _ad(o.intensidadeDoFoco),
  'fp': _ad(o.profundidadeDeCampo),
  if (o.neblinaLigada) 'nev': true,
  'nc': _col(o.corDaNeblina),
  'np': _ad(o.neblinaPerto),
  'nl': _ad(o.neblinaLonge),
};

OpcoesDaCamera _asOpcoesDaCamera(Object? raw) {
  if (raw is! Map) return OpcoesDaCamera();
  final m = raw.cast<String, dynamic>();
  AnimatedDouble? ad(String k) => m[k] is Map ? _asAd(m[k]) : null;
  return OpcoesDaCamera(
    ortografica: m['orto'] == true,
    focoLigado: m['foco'] == true,
    distanciaDoFoco: ad('fd'),
    intensidadeDoFoco: ad('fi'),
    profundidadeDeCampo: ad('fp'),
    neblinaLigada: m['nev'] == true,
    corDaNeblina: m['nc'] is num ? _asCol(m['nc']) : AureaColors.bg,
    neblinaPerto: ad('np'),
    neblinaLonge: ad('nl'),
  );
}

Map<String, dynamic> _processing(AudioProcessing p) => {
  if (p.denoise != 0) 'dn': p.denoise,
  if (p.voice != 0) 'voz': p.voice,
  if (p.deEsser != 0) 'ess': p.deEsser,
  if (p.lowDb != 0) 'lo': p.lowDb,
  if (p.midDb != 0) 'mid': p.midDb,
  if (p.highDb != 0) 'hi': p.highDb,
  if (p.effects.isNotEmpty)
    'effects': p.effects.map((e) => e.toJson()).toList(),
};

AudioProcessing _asProcessing(Object? raw) {
  if (raw is! Map) return const AudioProcessing();
  final m = raw.cast<String, dynamic>();
  return AudioProcessing(
    denoise: (m['dn'] as num?)?.toDouble() ?? 0,
    voice: (m['voz'] as num?)?.toDouble() ?? 0,
    deEsser: (m['ess'] as num?)?.toDouble() ?? 0,
    lowDb: (m['lo'] as num?)?.toDouble() ?? 0,
    midDb: (m['mid'] as num?)?.toDouble() ?? 0,
    highDb: (m['hi'] as num?)?.toDouble() ?? 0,
    effects: m['effects'] is List
        ? (m['effects'] as List)
              .map(AudioEffect.fromJson)
              .whereType<AudioEffect>()
              .toList()
        : const [],
  );
}

AudioSpec _asAudioSpec(Object? raw) {
  if (raw is! Map) return const AudioSpec();
  final m = raw.cast<String, dynamic>();
  return AudioSpec(
    fadeIn: Duration(microseconds: (m['in'] as num?)?.toInt() ?? 0),
    fadeOut: Duration(microseconds: (m['out'] as num?)?.toInt() ?? 0),
    gain: (m['gain'] as num?)?.toDouble() ?? 1.0,
    muted: m['mute'] as bool? ?? false,
    duckAgainstId: m['duck'] as String?,
    duckAmount: (m['duckAmt'] as num?)?.toDouble() ?? 0.7,
    duckAttack: Duration(
      microseconds: (m['duckAtk'] as num?)?.toInt() ?? 120000,
    ),
    duckRelease: Duration(
      microseconds: (m['duckRel'] as num?)?.toInt() ?? 450000,
    ),
    duckThreshold: (m['duckThr'] as num?)?.toDouble() ?? 0.05,
    normalizeTargetLufs: (m['lufs'] as num?)?.toDouble(),
    processing: _asProcessing(m['proc']),
    preservePitch: m['pitch'] as bool? ?? true,
    volumeAnimado: m['vol'] is Map ? _asAd(m['vol']) : null,
  );
}

Map<String, dynamic> _highlight(CaptionHighlightStyle h) => {
  'on': h.ativo,
  'lay': h.layout.name,
  'sz': h.destaque,
  'cd': h.corDestaque.toARGB32(),
  'cc': h.corContexto.toARGB32(),
  if (h.fonteDestaque != null) 'fd': h.fonteDestaque,
  if (h.fonteContexto != null) 'fc': h.fonteContexto,
  'up': h.maiusculas,
  'tr': h.tracking,
  'lh': h.entrelinha,
  'dur': h.duracaoInflar.inMicroseconds,
  'ctx': h.contextoPorLado,
  if (h.atrasDaPessoa) 'atras': true,
};

CaptionHighlightStyle _asHighlight(Object? raw) {
  if (raw is! Map) return const CaptionHighlightStyle();
  final m = raw.cast<String, dynamic>();
  return CaptionHighlightStyle(
    ativo: m['on'] as bool? ?? false,
    layout: HighlightLayout.values.firstWhere(
      (l) => l.name == m['lay'],
      orElse: () => HighlightLayout.atravessada,
    ),
    destaque: (m['sz'] as num?)?.toDouble() ?? 1.9,
    corDestaque: Color((m['cd'] as num?)?.toInt() ?? 0xFFE23B3B),
    corContexto: Color((m['cc'] as num?)?.toInt() ?? 0xFFFFFFFF),
    fonteDestaque: m['fd'] as String?,
    fonteContexto: m['fc'] as String?,
    maiusculas: m['up'] as bool? ?? true,
    tracking: (m['tr'] as num?)?.toDouble() ?? 0,
    entrelinha: (m['lh'] as num?)?.toDouble() ?? 1.05,
    duracaoInflar: Duration(
      microseconds: (m['dur'] as num?)?.toInt() ?? 220000,
    ),
    contextoPorLado: (m['ctx'] as num?)?.toInt() ?? kMaxContextoPorLado,
    atrasDaPessoa: m['atras'] as bool? ?? false,
  );
}

// AS TRANSICOES DE CLIPE FORAM REMOVIDAS DO PRODUTO (17/09).
//
// Nao ha mais escrita: o campo `transitionIn` nao existe na camada. E nao ha
// mais leitura: o carregador simplesmente IGNORA a chave, porque o projeto
// antigo continua sendo lido campo a campo e uma chave desconhecida nunca
// chega a lugar nenhum. Era o comportamento seguro que ja existia para
// efeito removido do catalogo, e vale igual aqui.
//
// O que NAO pode acontecer, e o teste de projeto antigo cobre: a chave
// sobrar em algum ponto do arquivo e derrubar o carregamento. O `catch` de
// camada engoliria a excecao e a CAMADA INTEIRA iria embora junto. Meio
// segundo de transicao nao vale um clipe.

Map<String, dynamic> _textAnim(TextAnim a) => {
  'id': a.id,
  'spec': a.specId,
  'slot': a.slot.index,
  'unit': a.unit.index,
  'start': a.start.inMicroseconds,
  'dur': a.duration.inMicroseconds,
  'stag': a.stagger.inMicroseconds,
  'order': a.order.index,
  'ease': a.ease.index,
  'seed': a.seed,
  'on': a.enabled,
  'amp': a.amplitude,
  'freq': a.frequency,
  'decay': a.decay,
  'params': a.params,
};

TextAnim _asTextAnim(Map<String, dynamic> m) => TextAnim(
  id: m['id'] as String?,
  specId: m['spec'] as String,
  slot: TextAnimSlot.values[(m['slot'] as num).toInt()],
  unit: TextAnimUnit.values[(m['unit'] as num).toInt()],
  start: Duration(microseconds: (m['start'] as num).toInt()),
  duration: Duration(microseconds: (m['dur'] as num).toInt()),
  stagger: Duration(microseconds: (m['stag'] as num).toInt()),
  order: TextAnimOrder.values[(m['order'] as num).toInt()],
  ease: TextAnimEase.values[(m['ease'] as num).toInt()],
  seed: (m['seed'] as num?)?.toInt() ?? 1,
  enabled: m['on'] as bool? ?? true,
  amplitude: (m['amp'] as num?)?.toDouble() ?? 1,
  frequency: (m['freq'] as num?)?.toDouble() ?? 1.8,
  decay: (m['decay'] as num?)?.toDouble() ?? 5,
  params: {
    for (final e in (m['params'] as Map? ?? const {}).entries)
      e.key as String: (e.value as num).toDouble(),
  },
);

Map<String, dynamic> _animator(TextAnimator a) => {
  'id': a.id,
  'name': a.name,
  'enabled': a.enabled,
  'overshoot': a.allowOvershoot,
  'selectors': [for (final s in a.selectors) _selector(s)],
  'props': [
    for (final p in a.properties)
      {'id': p.id, 'type': p.type.index, 'value': _ad(p.value)},
  ],
};

TextAnimator _asAnimator(Map<String, dynamic> m) => TextAnimator(
  id: m['id'] as String,
  name: m['name'] as String,
  enabled: m['enabled'] as bool,
  allowOvershoot: m['overshoot'] as bool,
  selectors: [
    for (final s in (m['selectors'] as List))
      _asSelector(s as Map<String, dynamic>),
  ],
  properties: [
    for (final p in (m['props'] as List))
      AnimatorProperty(
        id: p['id'] as String,
        type: TextAnimProp.values[(p['type'] as num).toInt()],
        value: _asAd(p['value']),
      ),
  ],
);

// -------------------------------------------------------------- legendas

Map<String, dynamic> _cue(Cue c) => {
  'id': c.id,
  's': _dur(c.start),
  'e': _dur(c.end),
  't': c.text,
  'l': c.locked,
};

Cue _asCue(Map<String, dynamic> m) => Cue(
  id: m['id'] as String,
  start: _asDur(m['s']),
  end: _asDur(m['e']),
  text: m['t'] as String,
  locked: m['l'] as bool,
);

Map<String, dynamic> _capStyle(CaptionStyle s) => {
  'size': s.fontSize,
  'color': _col(s.color),
  'bg': _col(s.backgroundColor),
  'bgOp': s.backgroundOpacity,
  'bold': s.bold,
};

CaptionStyle _asCapStyle(Map<String, dynamic> m) => CaptionStyle(
  fontSize: (m['size'] as num).toDouble(),
  color: _asCol(m['color']),
  backgroundColor: _asCol(m['bg']),
  backgroundOpacity: (m['bgOp'] as num).toDouble(),
  bold: m['bold'] as bool,
);

// --------------------------------------------------------------- camadas

Map<String, dynamic> layerToJson(Layer l) {
  final base = <String, dynamic>{
    'id': l.id,
    'name': l.name,
    'start': _dur(l.startTime),
    'dur': _dur(l.duration),
    'pos': _ao(l.position),
    'sx': _ad(l.scaleX),
    'sy': _ad(l.scaleY),
    'rot': _ad(l.rotation),
    'rotX': _ad(l.rotationX),
    'rotY': _ad(l.rotationY),
    'op': _ad(l.opacity),
    'skx': _ad(l.skewX),
    'sky': _ad(l.skewY),
    'pivot': _ao(l.pivot),
    // So grava quando sai do zero: projeto antigo nao incha, e abrir um
    // projeto sem a chave da exatamente o que dava antes.
    if (l.pivotZ.base != 0 || l.pivotZ.isAnimated) 'pivotZ': _ad(l.pivotZ),
    // A ORIENTACAO so vai ao arquivo quando sai do zero.
    if (l.orientX.base != 0 || l.orientX.isAnimated) 'oriX': _ad(l.orientX),
    if (l.orientY.base != 0 || l.orientY.isAnimated) 'oriY': _ad(l.orientY),
    if (l.orientZ.base != 0 || l.orientZ.isAnimated) 'oriZ': _ad(l.orientZ),
    'blend': l.blendMode.index,
    // So sai no arquivo quando a camada usa um modo proprio.
    if (l.customBlend != null) 'blendX': l.customBlend!.index,
    'is3D': l.is3D,
    'z': _ad(l.positionZ),
    'effects': [for (final e in l.effects) _effect(e)],
    if (l.masks.isNotEmpty) 'masks': [for (final m in l.masks) _mask(m)],
    if (l.matteMode != MatteMode.none) 'matte': l.matteMode.index,
    if (l.matteSourceId != null) 'matteSrc': l.matteSourceId,
  };
  switch (l) {
    case VideoLayer v:
      base['kind'] = 'video';
      base['src'] = v.sourcePath;
      base['srcOffset'] = _dur(v.sourceOffset);
      // QUANTO O ARQUIVO TEM. So aparece quando ha o numero: projeto
      // antigo continua legivel, e um projeto novo cujo probe falhou
      // nao inventa um teto.
      if (v.sourceDuration != null) {
        base['srcDur'] = v.sourceDuration!.inMicroseconds;
      }
      if (v.speed != 1.0) base['speed'] = v.speed;
      if (v.reverse) base['reverse'] = true;
      // A MESMA chave do precomp (`remap`): a trilha de tempo e uma so,
      // e quem a le nao precisa saber se a camada e video ou grupo.
      if (v.speedBlur) base['speedBlur'] = true;
      if (v.interpolacao != InterpolacaoDeQuadros.nenhuma) {
        base['interpolacao'] = v.interpolacao.name;
      }
      if (v.aprimorar) base['aprimorar'] = true;
      if (v.forcaDoAprimoramento != 1.0) {
        base['forcaAprimoramento'] = v.forcaDoAprimoramento;
      }
      if (v.perfilDoAprimoramento != PerfilDoAprimoramento.videoReal) {
        base['perfilAprimoramento'] = v.perfilDoAprimoramento.name;
      }
      if (v.reducaoDeRuido != reducaoDeRuidoPadrao) {
        base['ruidoAprimoramento'] = v.reducaoDeRuido;
      }
      _gravarAjuste(base, v.ajuste, v.proporcaoDaFonte);
      base['volume'] = v.volume;
      final va = _audioSpec(v.audio);
      if (va != null) base['audio'] = va;
    case ImageLayer i:
      base['kind'] = 'image';
      base['src'] = i.sourcePath;
      _gravarAjuste(base, i.ajuste, i.proporcaoDaFonte);
    case TextLayer t:
      base['kind'] = 'text';
      base['text'] = t.text;
      base['fontSize'] = t.fontSize;
      base['color'] = _col(t.color);
      base['bold'] = t.bold;
      if (t.fontFamily != null) base['font'] = t.fontFamily;
      if (t.alinhamento != TextAlign.center) base['align'] = t.alinhamento.name;
      base['animators'] = [for (final a in t.animators) _animator(a)];
      if (t.anims.isNotEmpty) {
        base['anims'] = [for (final a in t.anims) _textAnim(a)];
      }
      final tp = _textPath(t.textPath);
      if (tp != null) base['textPath'] = tp;
    case ShapeLayer s:
      base['kind'] = 'shape';
      base['contents'] = [for (final i in s.contents) _shapeItem(i)];
    case GroupLayer g:
      base['kind'] = 'group';
      base['children'] = [for (final c in g.children) layerToJson(c)];
      if (g.sourceDuration != null) {
        base['innerDur'] = g.sourceDuration!.inMicroseconds;
      }
      if (g.timeRemap != null) base['remap'] = _ad(g.timeRemap!);
      if (g.collapse) base['collapse'] = true;
      if (!g.clipToComp) base['noClip'] = true;
      if (g.contentOffset != Duration.zero) {
        base['contentOffset'] = g.contentOffset.inMicroseconds;
      }
    case CaptionLayer c:
      base['kind'] = 'caption';
      base['cues'] = [for (final q in c.cues) _cue(q)];
      base['style'] = _capStyle(c.style);
      if (!c.highlight.isNeutro) base['hi'] = _highlight(c.highlight);
    case AudioLayer a:
      base['kind'] = 'audio';
      base['src'] = a.sourcePath;
      base['srcOffset'] = _dur(a.sourceOffset);
      if (a.sourceDuration != null) {
        base['srcDur'] = a.sourceDuration!.inMicroseconds;
      }
      if (a.speed != 1.0) base['speed'] = a.speed;
      base['volume'] = a.volume;
      final aa = _audioSpec(a.audio);
      if (aa != null) base['audio'] = aa;
    case CameraLayer cam:
      base['kind'] = 'camera';
      base['zoom'] = _ad(cam.zoom);
      base['opc'] = _opcoesDaCamera(cam.opcoes);
    case NullLayer nl:
      base['kind'] = 'null';
      if (nl.grid != null) base['grid'] = _rig(nl.grid!);
    case AdjustmentLayer _:
      base['kind'] = 'adjust';
    case ParticulasLayer p:
      // AS CHAVES SAO AS MESMAS DE ANTES, e de proposito: um projeto
      // gravado por esta versao continua abrindo numa anterior (menos os
      // parametros que so o motor novo tem, que ela ignora). O que mudou
      // foi a ORIGEM dos numeros — eles saem da receita do motor, e nao
      // mais de campos soltos na camada.
      final q = p.parametros;
      base['kind'] = 'particles';
      // MARCA DE VERSAO: sem ela nao da para saber se `emitter: 3` quer
      // dizer "anel" (o motor antigo) ou "linha" (o novo).
      base['pv'] = 2;
      base['count'] = q.maximo;
      base['seed'] = q.semente;
      base['speed'] = q.velocidade;
      base['spread'] = q.aberturaGraus;
      base['dir'] = q.direcaoGraus;
      base['gravity'] = q.gravidade;
      base['size'] = q.tamanho;
      base['life'] = (q.vidaS * 1000).round();
      base['depth'] = q.profundidade;
      base['emitW'] = q.largura;
      base['emitH'] = q.altura;
      base['twinkle'] = q.cintilar;
      base['color'] = q.corInicio;
      base['star'] = q.forma == FormaDaParticula.estrela;
      base['emitter'] = q.emissor.index;
      base['emitMode'] = q.modoDeEmissao.index;
      base['windX'] = q.ventoX;
      base['windY'] = q.ventoY;
      base['drag'] = q.arrasto;
      base['turb'] = q.turbulencia;
      base['turbScale'] = q.turbulenciaEscala;
      if (q.faiscas > 0) {
        base['auxN'] = q.faiscas;
        base['auxLife'] = (q.faiscaVidaS * 1000).round();
        base['auxInh'] = q.faiscaHeranca;
        base['auxSpd'] = q.faiscaVelocidade;
        base['auxSize'] = q.faiscaTamanho;
        base['auxStart'] = q.faiscaInicio;
      }
      base['turbSpeed'] = q.turbulenciaVelocidade;
      base['sizeLife'] = q.tamanhoNaVida.index;
      base['sizeRnd'] = q.tamanhoVariacao;
      base['opLife'] = q.opacidadeNaVida.index;
      base['opRnd'] = q.opacidadeVariacao;
      if (q.temCorFim) base['colorEnd'] = q.corFim;
      base['shape'] = q.forma.index;
      base['spin'] = q.giroGrausS;
      base['trail'] = q.rastro;
      base['lifeRnd'] = q.vidaVariacao;
      base['glow'] = q.brilho;
      // ---- o que so o motor novo tem ----
      if (q.taxaDeNascimento > 0) base['rate'] = q.taxaDeNascimento;
      if (q.ventoZ != 0) base['windZ'] = q.ventoZ;
      if (q.atracao != 0) {
        base['attract'] = q.atracao;
        base['attractX'] = q.atracaoX;
        base['attractY'] = q.atracaoY;
        base['attractZ'] = q.atracaoZ;
      }
      if (q.raio != 200) base['emitterR'] = q.raio;
      if (q.tamanhoNaVida != TamanhoNaVida.fixo) {
        base['sizeLife'] = q.tamanhoNaVida.index;
      }
      if (!q.cintilar) base['twinkle'] = false;
      if (q.focal != 1200) base['focal'] = q.focal;
    case Element3DLayer e:
      base['kind'] = 'el3d';
      // POR NOME, e nao so pelo indice do enum: tirar um tipo do meio da
      // lista deslocaria todos os seguintes, e um projeto salvo abriria
      // com outra forma — sem erro, sem aviso, so errado. O `el` continua
      // sendo gravado para uma versao anterior conseguir ler.
      base['el'] = e.kind.index;
      base['eln'] = nomeDoElemento3D(e.kind);
      base['acab'] = nomeDoAcabamentoArquivo(e.acabamento);
      base['size'] = e.size;
      base['color'] = _col(e.color);
      base['edges'] = e.edges;
      base['reflect'] = e.reflect;
      base['env'] = e.environment.index;
      if (e.imagePath != null) base['img'] = e.imagePath;
      if (e.meshPath != null) base['mesh'] = e.meshPath;
      base['mat'] = e.material;
      base['grad'] = [for (final c in e.gradient) _col(c)];
      base['shine'] = e.shininess;
    case Scene3DLayer s:
      base['kind'] = 'scene3d';
      if (s.extraCameras.isNotEmpty) {
        base['cams'] = [for (final c in s.extraCameras) _camera(c)];
      }
      if (s.cameraParentLayerId != null) {
        base['camPai'] = s.cameraParentLayerId;
      }
      if (s.shots.isNotEmpty) {
        base['shots'] = [
          for (final t in s.shots)
            {
              'us': t.time.inMicroseconds,
              'cam': t.cameraId,
              if (t.transition > Duration.zero)
                'tr': t.transition.inMicroseconds,
            },
        ];
      }
      base['scene'] = _scene(s.scene);
      base['cam'] = _camera(s.camera);
      base['view'] = s.view.index;
      base['helpers'] = s.showHelpers;
  }
  return base;
}

// ------------------------------------------------------- cena 3D

Map<String, dynamic> _vec(Vec3 v) => {'x': v.x, 'y': v.y, 'z': v.z};

Vec3 _asVec(dynamic v) {
  final m = v as Map<String, dynamic>;
  return Vec3(
    (m['x'] as num).toDouble(),
    (m['y'] as num).toDouble(),
    (m['z'] as num).toDouble(),
  );
}

T _enumValue<T extends Enum>(Object? raw, List<T> values, T fallback) {
  if (raw is String) {
    for (final value in values) {
      if (value.name == raw) return value;
    }
  } else if (raw is num) {
    final index = raw.toInt();
    if (index >= 0 && index < values.length) return values[index];
  }
  return fallback;
}

/// Cache compacto da geometria otimizada importada. O projeto nao depende
/// de o arquivo original continuar no mesmo lugar para reabrir o modelo.
String _meshBlob(Element3DMesh mesh) {
  final faceWords = mesh.faces.fold<int>(
    0,
    (sum, face) => sum + 1 + face.length,
  );
  final data = ByteData(8 + mesh.verts.length * 12 + faceWords * 4);
  var offset = 0;
  data.setUint32(offset, mesh.verts.length, Endian.little);
  offset += 4;
  data.setUint32(offset, mesh.faces.length, Endian.little);
  offset += 4;
  for (final vertex in mesh.verts) {
    for (var axis = 0; axis < 3; axis++) {
      data.setFloat32(offset, vertex[axis], Endian.little);
      offset += 4;
    }
  }
  for (final face in mesh.faces) {
    data.setUint32(offset, face.length, Endian.little);
    offset += 4;
    for (final index in face) {
      data.setUint32(offset, index, Endian.little);
      offset += 4;
    }
  }
  return base64Encode(data.buffer.asUint8List());
}

Element3DMesh? _asMeshBlob(Object? raw) {
  if (raw is! String || raw.isEmpty) return null;
  try {
    final bytes = base64Decode(raw);
    if (bytes.length < 8) return null;
    final data = ByteData.sublistView(bytes);
    var offset = 0;
    final vertexCount = data.getUint32(offset, Endian.little);
    offset += 4;
    final faceCount = data.getUint32(offset, Endian.little);
    offset += 4;
    if (vertexCount > 2000000 || faceCount > 4000000) return null;
    if (offset + vertexCount * 12 > bytes.length) return null;
    final vertices = <List<double>>[];
    for (var i = 0; i < vertexCount; i++) {
      vertices.add([
        data.getFloat32(offset, Endian.little),
        data.getFloat32(offset + 4, Endian.little),
        data.getFloat32(offset + 8, Endian.little),
      ]);
      offset += 12;
    }
    final faces = <List<int>>[];
    for (var i = 0; i < faceCount; i++) {
      if (offset + 4 > bytes.length) return null;
      final length = data.getUint32(offset, Endian.little);
      offset += 4;
      if (length < 3 || length > 1024 || offset + length * 4 > bytes.length) {
        return null;
      }
      final face = <int>[];
      for (var j = 0; j < length; j++) {
        final index = data.getUint32(offset, Endian.little);
        offset += 4;
        if (index >= vertexCount) return null;
        face.add(index);
      }
      faces.add(face);
    }
    return Element3DMesh(vertices, faces);
  } catch (_) {
    return null;
  }
}

/// Os LODs por agrupamento de vertices (ver lod3d.dart), uma vez por
/// malha; [stride] so diz qual dos dois se quer.
final _lodsDe = Expando<({Element3DMesh medio, Element3DMesh baixo})>();

Element3DMesh _meshLod(Element3DMesh mesh, int stride) {
  final lods = _lodsDe[mesh] ??= gerarLods(mesh);
  return stride >= 4 ? lods.baixo : lods.medio;
}

Map<String, dynamic> _panorama(Panorama3D p) => {
  'preset': p.preset.name,
  'source': p.source.name,
  if (p.sourcePath != null) 'path': p.sourcePath,
  if (p.sourceLayerId != null) 'layer': p.sourceLayerId,
  'rotation': p.rotationDegrees,
  'intensity': p.intensity,
  'blur': p.backgroundBlur,
  'show': p.showBackground,
  'highlight': p.highlightBoost,
  'approx': p.approximate,
  'coverage': p.coverageDegrees,
  'mirror': p.mirrorTo360,
  'seam': p.seamSoftness,
  'poles': p.fillZenithNadir,
  'converted': p.convertedAtImport,
};

Panorama3D _asPanorama(Object? raw) {
  if (raw is! Map) return const Panorama3D();
  final m = raw.cast<String, dynamic>();
  return Panorama3D(
    preset: _enumValue(
      m['preset'],
      PanoramaPreset.values,
      PanoramaPreset.estudio,
    ),
    source: _enumValue(
      m['source'],
      PanoramaSource.values,
      PanoramaSource.preset,
    ),
    sourcePath: m['path'] as String?,
    sourceLayerId: m['layer'] as String?,
    rotationDegrees: (m['rotation'] as num?)?.toDouble() ?? 0,
    intensity: (m['intensity'] as num?)?.toDouble() ?? 1,
    backgroundBlur: (m['blur'] as num?)?.toDouble() ?? 0,
    showBackground: m['show'] as bool? ?? false,
    highlightBoost: (m['highlight'] as num?)?.toDouble() ?? 0,
    approximate: m['approx'] as bool? ?? false,
    coverageDegrees: (m['coverage'] as num?)?.toDouble() ?? 360,
    mirrorTo360: m['mirror'] as bool? ?? false,
    seamSoftness: (m['seam'] as num?)?.toDouble() ?? 0,
    fillZenithNadir: m['poles'] as bool? ?? false,
    convertedAtImport: m['converted'] as bool? ?? false,
  );
}

Map<String, dynamic> _probe(ReflectionProbe3D p) => {
  'id': p.id,
  'enabled': p.enabled,
  'quality': p.quality.name,
  'mode': p.updateMode.name,
  'perObject': p.perObject,
  'position': {'x': p.position.x, 'y': p.position.y, 'z': p.position.z},
  'include': p.includeNodeIds.toList(),
  'exclude': p.excludeNodeIds.toList(),
};

ReflectionProbe3D _asProbe(Object? raw) {
  if (raw is! Map) return const ReflectionProbe3D();
  final m = raw.cast<String, dynamic>();
  final pos = (m['position'] as Map?)?.cast<String, dynamic>() ?? const {};
  return ReflectionProbe3D(
    id: m['id'] as String? ?? 'scene-probe',
    enabled: m['enabled'] as bool? ?? false,
    quality: _enumValue(m['quality'], ProbeQuality.values, ProbeQuality.low),
    updateMode: _enumValue(
      m['mode'],
      ProbeUpdateMode.values,
      ProbeUpdateMode.onMove,
    ),
    perObject: m['perObject'] as bool? ?? false,
    position: ProbePoint3D(
      (pos['x'] as num?)?.toDouble() ?? 0,
      (pos['y'] as num?)?.toDouble() ?? 0,
      (pos['z'] as num?)?.toDouble() ?? 0,
    ),
    includeNodeIds: {
      for (final id in (m['include'] as List? ?? const [])) id as String,
    },
    excludeNodeIds: {
      for (final id in (m['exclude'] as List? ?? const [])) id as String,
    },
  );
}

/// OS PARAMETROS DE UM TEXTO 3D, no projeto.
///
/// Eles NAO substituem a malha (`modelAsset`) — quem desenha e ela. Estes
/// campos existem para a folha do texto 3D poder reabrir o que o dono
/// escolheu e refazer a geometria quando ele mexer: da malha pronta nao se
/// tira de volta o texto, a fonte nem a espessura.
Map<String, dynamic> _texto3d(Texto3D t) => {
  'texto': t.texto,
  'familia': t.familia,
  'tamanho': t.tamanho,
  'espessura': t.espessura,
  'chanfro': t.chanfro.name,
  'larguraDoChanfro': t.larguraDoChanfro,
  'segmentosDoChanfro': t.segmentosDoChanfro,
  'espacamento': t.espacamento,
  'qualidade': t.qualidade.name,
  'separarLetras': t.separarLetras,
  if (t.temRotacaoPorLetra) 'rotLetra': [t.rotLetraX, t.rotLetraY, t.rotLetraZ],
  // OS AJUSTES POR FAIXA DE CARACTERES (mover so o "C" de "ABCDE"). A
  // chave so entra quando ha ajuste: projeto antigo continua byte a byte
  // como era, e `ajustesDeJson` devolve lista vazia quando ela falta.
  if (t.ajustes.isNotEmpty)
    'ajustesDeCaracteres': [for (final a in t.ajustes) a.toJson()],
};

Texto3D? _asTexto3D(Object? json) {
  if (json is! Map) return null;
  final texto = json['texto'] as String?;
  if (texto == null) return null;
  return Texto3D(
    texto: texto,
    familia: json['familia'] as String? ?? 'Aurea Motion Sans',
    tamanho: (json['tamanho'] as num?)?.toDouble() ?? 100,
    espessura: (json['espessura'] as num?)?.toDouble() ?? 24,
    chanfro: _enumValue(
      json['chanfro'],
      TipoDeChanfro.values,
      TipoDeChanfro.redondo,
    ),
    larguraDoChanfro: (json['larguraDoChanfro'] as num?)?.toDouble() ?? 3,
    segmentosDoChanfro: (json['segmentosDoChanfro'] as num?)?.toInt() ?? 3,
    espacamento: (json['espacamento'] as num?)?.toDouble() ?? 0,
    qualidade: _enumValue(
      json['qualidade'],
      QualidadeDoTexto3D.values,
      QualidadeDoTexto3D.media,
    ),
    separarLetras: json['separarLetras'] as bool? ?? false,
    rotLetraX: _rotLetra(json['rotLetra'], 0),
    rotLetraY: _rotLetra(json['rotLetra'], 1),
    rotLetraZ: _rotLetra(json['rotLetra'], 2),
    ajustes: ajustesDeJson(json['ajustesDeCaracteres']),
  );
}

double _rotLetra(Object? v, int i) =>
    v is List && v.length > i && v[i] is num ? (v[i] as num).toDouble() : 0;

Map<String, dynamic> _material(Material3D mat) => {
  'n': mat.name,
  'c': _col(mat.baseColor),
  'met': mat.metallic,
  'rough': mat.roughness,
  'emi': mat.emissive,
  'op': mat.opacity,
  'kind': mat.kind.name,
  if (mat.textureLayerId != null) 'tex': mat.textureLayerId,
  if (mat.reflectivity != 0) 'refl': mat.reflectivity,
  if (mat.imagePath != null) 'img': mat.imagePath,
  // OS OUTROS QUATRO MAPAS. Um projeto salvo com relevo e metal por regiao
  // tem de reabrir com eles: sem estas linhas, fechar e abrir devolvia o
  // material aos numeros do painel e o modelo ficava liso — um "funcionou
  // ate eu salvar" que so aparece no dia seguinte.
  if (mat.normalPath != null) 'imgN': mat.normalPath,
  if (mat.metalRoughPath != null) 'imgMR': mat.metalRoughPath,
  if (mat.emissivePath != null) 'imgE': mat.emissivePath,
  if (mat.occlusionPath != null) 'imgO': mat.occlusionPath,
  if (mat.emissiveColor != null) 'emiC': _col(mat.emissiveColor!),
  if (mat.faceImagePaths.isNotEmpty)
    'faces': {
      for (final entry in mat.faceImagePaths.entries)
        entry.key.toString(): entry.value,
    },
  'normal': mat.normalStrength,
  'occlusion': mat.occlusionStrength,
  'cutoff': mat.alphaCutoff,
  'double': mat.doubleSided,
  'packed': mat.packedChannels,
  'wrapX': mat.textureWrapX.name,
  'wrapY': mat.textureWrapY.name,
};

Material3D _asMaterial(Map<String, dynamic> m) => Material3D(
  name: m['n'] as String? ?? 'Material',
  baseColor: _asCol(m['c']),
  metallic: (m['met'] as num).toDouble(),
  roughness: (m['rough'] as num).toDouble(),
  emissive: (m['emi'] as num).toDouble(),
  opacity: (m['op'] as num).toDouble(),
  kind: _enumValue(m['kind'], MaterialKind.values, MaterialKind.pbr),
  textureLayerId: m['tex'] as String?,
  reflectivity: (m['refl'] as num?)?.toDouble() ?? 0,
  imagePath: m['img'] as String?,
  normalPath: m['imgN'] as String?,
  metalRoughPath: m['imgMR'] as String?,
  emissivePath: m['imgE'] as String?,
  occlusionPath: m['imgO'] as String?,
  emissiveColor: m['emiC'] == null ? null : _asCol(m['emiC']),
  textureWrapX: _enumValue(m['wrapX'], TileMode.values, TileMode.clamp),
  textureWrapY: _enumValue(m['wrapY'], TileMode.values, TileMode.clamp),
  faceImagePaths: {
    for (final entry
        in ((m['faces'] as Map?)?.cast<String, dynamic>() ?? const {}).entries)
      int.parse(entry.key): entry.value as String,
  },
  normalStrength: (m['normal'] as num?)?.toDouble() ?? 1,
  occlusionStrength: (m['occlusion'] as num?)?.toDouble() ?? 1,
  alphaCutoff: (m['cutoff'] as num?)?.toDouble() ?? 0.5,
  doubleSided: m['double'] as bool? ?? false,
  packedChannels: m['packed'] as bool? ?? false,
);

Map<String, dynamic> _modelSource(ModelSource3D source) => {
  'path': source.path,
  'triangles': source.triangles,
  'bytes': source.bytes,
  'meshes': source.meshes,
  'materials': source.materials,
  'textures': source.textures,
  'animations': source.animations,
  'nodes': source.nodeNames,
  'clips': source.animationNames,
  'overBudget': source.overBudget,
  'lods': source.lodCount,
  if (source.warning != null) 'warning': source.warning,
};

ModelSource3D? _asModelSource(Object? raw) {
  if (raw is! Map) return null;
  final m = raw.cast<String, dynamic>();
  final path = m['path'] as String?;
  if (path == null || path.isEmpty) return null;
  return ModelSource3D(
    path: path,
    triangles: (m['triangles'] as num?)?.toInt() ?? 0,
    bytes: (m['bytes'] as num?)?.toInt() ?? 0,
    meshes: (m['meshes'] as num?)?.toInt() ?? 1,
    materials: (m['materials'] as num?)?.toInt() ?? 0,
    textures: (m['textures'] as num?)?.toInt() ?? 0,
    animations: (m['animations'] as num?)?.toInt() ?? 0,
    nodeNames: [
      for (final value in (m['nodes'] as List? ?? const [])) value as String,
    ],
    animationNames: [
      for (final value in (m['clips'] as List? ?? const [])) value as String,
    ],
    overBudget: m['overBudget'] as bool? ?? false,
    lodCount: (m['lods'] as num?)?.toInt() ?? 3,
    warning: m['warning'] as String?,
  );
}

Map<String, dynamic> _credit(ModelCredit3D credit) => {
  if (credit.author != null) 'author': credit.author,
  if (credit.license != null) 'license': credit.license,
  if (credit.url != null) 'url': credit.url,
  if (credit.title != null) 'title': credit.title,
  if (credit.source != null) 'source': credit.source,
  if (credit.authorUrl != null) 'authorUrl': credit.authorUrl,
};

ModelCredit3D _asCredit(Object? raw) {
  if (raw is! Map) return const ModelCredit3D();
  // LEITURA TOLERANTE: o credito e enfeite perto da cena. Uma chave com o
  // tipo errado (arquivo editado a mao, versao futura) vira "sem esse
  // dado" — nunca um projeto que nao abre.
  String? texto(Object? v) => v is String && v.isNotEmpty ? v : null;
  return ModelCredit3D(
    author: texto(raw['author']),
    license: texto(raw['license']),
    url: texto(raw['url']),
    title: texto(raw['title']),
    source: texto(raw['source']),
    authorUrl: texto(raw['authorUrl']),
  );
}

Map<String, dynamic> _scene(Scene3D s) => {
  'ambient': s.ambient,
  'env': s.environment.index,
  'envName': s.environment.name,
  'envk': s.envReflect,
  'sky': _col(s.skyColor),
  'ground': _col(s.groundColor),
  'tonemap': s.tonemap,
  'panorama': _panorama(s.panorama),
  'probe': _probe(s.reflectionProbe),
  'planar': s.planarFloorReflection,
  'planarRough': s.planarFloorRoughness,
  'fogDensity': s.fogDensity,
  'fogStart': s.fogStart,
  'fogColor': _col(s.fogColor),
  if (s.background != null) 'bg': _col(s.background!),
  'grid': s.showFloorGrid,
  'msaa': s.msaa,
  'draft': s.draftMode,
  'nodes': [
    for (final n in s.nodes)
      {
        'id': n.id,
        'n': n.name,
        'k': n.kind.index,
        'mat': _material(n.material),
        'x': _ad(n.x),
        'y': _ad(n.y),
        'z': _ad(n.z),
        'rx': _ad(n.rotX),
        'ry': _ad(n.rotY),
        'rz': _ad(n.rotZ),
        's': _ad(n.scale),
        // ESCALA POR EIXO: so entra no arquivo quando ALGUEM MEXEU. O
        // caso comum (1, 1, 1 sem keyframe) continua gravando o mesmo
        // JSON de antes — e um projeto salvo por uma versao anterior
        // volta com os tres em 1, que e exatamente o que ele desenhava.
        if (_escalaDeEixoMexida(n.scaleX)) 'sx': _ad(n.scaleX),
        if (_escalaDeEixoMexida(n.scaleY)) 'sy': _ad(n.scaleY),
        if (_escalaDeEixoMexida(n.scaleZ)) 'sz': _ad(n.scaleZ),
        'size': n.size,
        'vis': n.visible,
        if (n.instances.isNotEmpty)
          'inst': [for (final i in n.instances) _vec(i)],
        // O CONTORNO basta: a malha se refaz na leitura, e o arquivo
        // nao carrega milhares de vertices que saem em milissegundos.
        if (n.parentId != null) 'parent': n.parentId,
        if (n.compParentLayerId != null) 'compPai': n.compParentLayerId,
        if (n.isNull) 'null3d': true,
        if (n.locked) 'locked': true,
        'tag': _col(n.colorTag),
        'lod': n.lod.name,
        if (n.subdivisions != 0) 'subdivisions': n.subdivisions,
        if (!n.credit.isEmpty) 'credit': _credit(n.credit),
        if (n.modelSource != null) 'modelSource': _modelSource(n.modelSource!),
        if (n.animationClip != null) 'animationClip': n.animationClip,
        if (n.texto3d != null) ...{
          'texto3d': _texto3d(n.texto3d!),
          'estilo3d': n.estiloTexto3d?.index,
        },
        if (n.modelAsset != null) ...{
          'modelAsset': _peso(n.modelAsset!, () => n.modelAsset!.data),
          'modelMotion': n.modelMotion.toJson(),
          'useModelMaterials': n.useModelMaterials,
        },
        if (n.modelSource != null && n.mesh != null)
          'meshData': _peso(n.mesh!, () => _meshBlob(n.mesh!)),
        if (n.outline != null) ...{
          'outline': [
            for (final p in n.outline!) [p.dx, p.dy],
          ],
          'depth': n.extrudeDepth,
        },
      },
  ],
  'lights': [
    for (final l in s.lights)
      {
        'id': l.id,
        'k': l.kind.index,
        'kindName': l.kind.name,
        'c': _col(l.color),
        'i': _ad(l.intensity),
        'dir': _vec(l.direction),
        'pos': _vec(l.position),
        'range': l.range,
        'shadow': l.castsShadow,
        'cone': l.coneDegrees,
        'softness': l.softness,
      },
  ],
  if (s.cameraParentId != null) 'camParent': s.cameraParentId,
  if (s.savedViews.isNotEmpty)
    'views': [
      for (final v in s.savedViews)
        {'n': v.name, 'p': _vec(v.position), 't': _vec(v.target)},
    ],
};

List<Offset>? _asOutline(dynamic v) => v == null
    ? null
    : [
        for (final p in (v as List))
          Offset(((p as List)[0] as num).toDouble(), (p[1] as num).toDouble()),
      ];

/// O modelo importado, colando de volta o id do peso: sem isso o
/// salvamento seguinte acharia que o modelo e novo e reescreveria o
/// arquivo inteiro de novo.
ModelAsset3D? _asModelAsset(Object? bruto) {
  final ref = _semRef(bruto);
  final conteudo = ref.conteudo;
  if (conteudo is! Map) return null;
  final asset = ModelAsset3D(conteudo.cast<String, dynamic>());
  if (ref.id != null) _idDoPeso[asset] = ref.id;
  return asset;
}

SceneNode _asSceneNode(Map<String, dynamic> n) {
  final outline = _asOutline(n['outline']);
  final refDaMalha = _semRef(n['meshData']);
  final importedMesh = _asMeshBlob(refDaMalha.conteudo);
  if (importedMesh != null && refDaMalha.id != null) {
    _idDoPeso[importedMesh] = refDaMalha.id;
  }
  final mesh = outline == null
      ? importedMesh
      : extrudeOutline(
          outline,
          depth: (n['depth'] as num?)?.toDouble() ?? 40.0,
        );
  return SceneNode(
    id: n['id'] as String,
    name: n['n'] as String? ?? 'Objeto',
    kind: _enumValue(n['k'], Element3DKind.values, Element3DKind.cube),
    material: _asMaterial((n['mat'] as Map).cast<String, dynamic>()),
    x: _asAd(n['x']),
    y: _asAd(n['y']),
    z: _asAd(n['z']),
    rotX: _asAd(n['rx']),
    rotY: _asAd(n['ry']),
    rotZ: _asAd(n['rz']),
    scale: _asAd(n['s']),
    // LEITURA TOLERANTE (regra do QA 1.0): ausente vale 1, numero solto
    // tambem abre, e uma trilha ilegivel nao derruba o no inteiro.
    scaleX: _asAdOuNumero(n['sx'], 1),
    scaleY: _asAdOuNumero(n['sy'], 1),
    scaleZ: _asAdOuNumero(n['sz'], 1),
    size: (n['size'] as num).toDouble(),
    visible: n['vis'] as bool? ?? true,
    instances: [
      for (final instance in (n['inst'] as List? ?? const [])) _asVec(instance),
    ],
    parentId: n['parent'] as String?,
    compParentLayerId: n['compPai'] as String?,
    isNull: n['null3d'] as bool? ?? false,
    locked: n['locked'] as bool? ?? false,
    colorTag: n['tag'] == null ? AureaColors.selectionText : _asCol(n['tag']),
    lod: _enumValue(n['lod'], MeshLod3D.values, MeshLod3D.auto),
    subdivisions: (n['subdivisions'] as num?)?.toInt() ?? 0,
    credit: _asCredit(n['credit']),
    modelSource: _asModelSource(n['modelSource']),
    texto3d: _asTexto3D(n['texto3d']),
    estiloTexto3d: n['estilo3d'] == null
        ? null
        : _enumValue(
            n['estilo3d'],
            EstiloDoTexto3D.values,
            EstiloDoTexto3D.ouro,
          ),
    animationClip: n['animationClip'] as String?,
    modelAsset: _asModelAsset(n['modelAsset']),
    modelMotion: ModelMotion3D.fromJson(n['modelMotion']),
    useModelMaterials: n['useModelMaterials'] as bool? ?? true,
    outline: outline,
    extrudeDepth: (n['depth'] as num?)?.toDouble() ?? 40.0,
    mesh: mesh,
    mediumMesh: importedMesh == null ? null : _meshLod(importedMesh, 2),
    lowMesh: importedMesh == null ? null : _meshLod(importedMesh, 4),
  );
}

Scene3D _asScene(Map<String, dynamic> m) => Scene3D(
  cameraParentId: m['camParent'] as String?,
  environment: _enumValue(
    m['envName'] ?? m['env'],
    EnvironmentKind.values,
    EnvironmentKind.estudio,
  ),
  envReflect: (m['envk'] as num?)?.toDouble() ?? 0.7,
  ambient: (m['ambient'] as num?)?.toDouble() ?? 0.28,
  skyColor: m['sky'] == null ? const Color(0xFF8FB7E8) : _asCol(m['sky']),
  groundColor: m['ground'] == null
      ? const Color(0xFF3A3128)
      : _asCol(m['ground']),
  tonemap: m['tonemap'] as bool? ?? true,
  panorama: _asPanorama(m['panorama']),
  reflectionProbe: _asProbe(m['probe']),
  planarFloorReflection: m['planar'] as bool? ?? false,
  planarFloorRoughness: (m['planarRough'] as num?)?.toDouble() ?? 0.2,
  fogDensity: (m['fogDensity'] as num?)?.toDouble() ?? 0,
  fogStart: (m['fogStart'] as num?)?.toDouble() ?? 0,
  fogColor: m['fogColor'] == null
      ? const Color(0xFF101E28)
      : _asCol(m['fogColor']),
  background: m['bg'] == null ? null : _asCol(m['bg']),
  showFloorGrid: m['grid'] as bool? ?? true,
  msaa: m['msaa'] as bool? ?? true,
  draftMode: m['draft'] as bool? ?? false,
  nodes: [
    for (final n in (m['nodes'] as List? ?? const []))
      _asSceneNode((n as Map).cast<String, dynamic>()),
  ],
  lights: [
    for (final l in (m['lights'] as List? ?? const []))
      Light3D(
        id: l['id'] as String,
        kind: _enumValue(
          l['kindName'] ?? l['k'],
          Light3DKind.values,
          Light3DKind.directional,
        ),
        color: _asCol(l['c']),
        intensity: _asAd(l['i']),
        direction: _asVec(l['dir']),
        position: _asVec(l['pos']),
        range: (l['range'] as num).toDouble(),
        castsShadow: l['shadow'] as bool? ?? false,
        coneDegrees: (l['cone'] as num?)?.toDouble() ?? 45,
        softness: (l['softness'] as num?)?.toDouble() ?? 0.2,
      ),
  ],
  savedViews: [
    for (final v in (m['views'] as List? ?? const []))
      SavedView(
        name: v['n'] as String? ?? 'Vista',
        position: _asVec(v['p']),
        target: _asVec(v['t']),
      ),
  ],
);

Map<String, dynamic> _camera(Camera3D c) => {
  'id': c.id,
  'n': c.name,
  'kind': c.kind.index,
  'px': _ad(c.posX),
  'py': _ad(c.posY),
  'pz': _ad(c.posZ),
  'ax': _ad(c.poiX),
  'ay': _ad(c.poiY),
  'az': _ad(c.poiZ),
  'ox': _ad(c.orientX),
  'oy': _ad(c.orientY),
  'oz': _ad(c.orientZ),
  'rx': _ad(c.rotX),
  'ry': _ad(c.rotY),
  'rz': _ad(c.rotZ),
  'focal': _ad(c.focalLength),
  'film': c.filmWidth,
  'ortho': c.orthographic,
  'auto': c.autoOrient.index,
  if (c.lookAtNodeId != null) 'olhar': c.lookAtNodeId,
  'dof': {
    'on': c.dof.enabled,
    'focus': _ad(c.dof.focusDistance),
    'ap': _ad(c.dof.aperture),
    'blur': _ad(c.dof.blurLevel),
    'lock': c.dof.lockToZoom,
    'iris': c.dof.irisShape.index,
    'irot': _ad(c.dof.irisRotation),
    'iround': _ad(c.dof.irisRoundness),
    'iasp': _ad(c.dof.irisAspect),
    'fringe': _ad(c.dof.diffractionFringe),
    'gain': _ad(c.dof.highlightGain),
    'thr': _ad(c.dof.highlightThreshold),
    'sat': _ad(c.dof.highlightSaturation),
  },
};

Camera3D _asCamera(Map<String, dynamic> m) {
  final d = m['dof'] as Map<String, dynamic>;
  return Camera3D(
    id: m['id'] as String,
    name: m['n'] as String? ?? 'Camera',
    kind: CameraKind.values[(m['kind'] as num).toInt()],
    posX: _asAd(m['px']),
    posY: _asAd(m['py']),
    posZ: _asAd(m['pz']),
    poiX: _asAd(m['ax']),
    poiY: _asAd(m['ay']),
    poiZ: _asAd(m['az']),
    orientX: _asAd(m['ox']),
    orientY: _asAd(m['oy']),
    orientZ: _asAd(m['oz']),
    rotX: _asAd(m['rx']),
    rotY: _asAd(m['ry']),
    rotZ: _asAd(m['rz']),
    focalLength: _asAd(m['focal']),
    filmWidth: (m['film'] as num).toDouble(),
    orthographic: m['ortho'] as bool? ?? false,
    autoOrient: AutoOrient.values[(m['auto'] as num?)?.toInt() ?? 0],
    lookAtNodeId: m['olhar'] as String?,
    dof: DepthOfField(
      enabled: d['on'] as bool? ?? false,
      focusDistance: _asAd(d['focus']),
      aperture: _asAd(d['ap']),
      blurLevel: _asAd(d['blur']),
      lockToZoom: d['lock'] as bool? ?? false,
      irisShape: IrisShape.values[(d['iris'] as num).toInt()],
      irisRotation: _asAd(d['irot']),
      irisRoundness: _asAd(d['iround']),
      irisAspect: _asAd(d['iasp']),
      diffractionFringe: _asAd(d['fringe']),
      highlightGain: _asAd(d['gain']),
      highlightThreshold: _asAd(d['thr']),
      highlightSaturation: _asAd(d['sat']),
    ),
  );
}

/// Os EFEITOS da camada, pulando o que nao da para ler — e SEPARANDO a
/// trilha de tempo que morava dentro deles.
///
/// Um efeito ilegivel — de uma versao mais nova, com um parametro que
/// mudou de forma, com um pedaco do arquivo corrompido — levava a
/// CAMADA INTEIRA junto: a pessoa perdia o texto, a forma, os
/// keyframes e o trabalho todo por causa de um item da lista de
/// efeitos. Perder o efeito e um arranhao; perder a camada nao.
///
/// O Time Remap e o caso especial: ele NAO e descartado, e MIGRADO. A
/// trilha `tempo` sai do efeito e vai para o campo [VideoLayer.timeRemap]
/// / [GroupLayer.timeRemap], que ja era o desenho do precomp. Quem tinha
/// uma rampa de velocidade continua com ela.
({List<EffectInstance> efeitos, AnimatedDouble? remap}) _efeitosDoJson(
  Object? bruto,
) {
  if (bruto is! List) return (efeitos: const <EffectInstance>[], remap: null);
  final out = <EffectInstance>[];
  AnimatedDouble? remap;
  for (final e in bruto) {
    if (e is! Map) continue;
    final mapa = e.cast<String, dynamic>();
    if (_eOTimeRemapAntigo(mapa)) {
      try {
        final params = mapa['params'];
        if (params is Map && params['tempo'] != null) {
          remap ??= _asAd(params['tempo']);
        }
      } catch (_) {
        // Trilha ilegivel: a camada abre sem remapeamento, e nao some.
      }
      continue;
    }
    try {
      final efeito = _asEffect(mapa);
      if (efeito != null) out.add(efeito);
    } catch (_) {
      // Efeito ilegivel: fica de fora, a camada abre.
    }
  }
  return (efeitos: out, remap: remap);
}

/// Os VINCULOS do projeto, cada um por sua conta.
///
/// Um vinculo e um par de ids e um indice de propriedade. O indice vinha
/// de `LayerProp.values[...]` SEM LIMITE: um numero fora da faixa, de um
/// arquivo mais novo ou corrompido, lancava dentro de `projectFromJson`.
/// A excecao subia ate o `loadAll`, que registra "projeto ilegivel" e
/// SEGUE — e o projeto sumia da lista para sempre, com o arquivo intacto
/// no disco. Era a pior falha possivel do carregador: nao perdia o
/// efeito, perdia o projeto inteiro, calado.
///
/// Agora o indice e limitado, um vinculo ilegivel fica de fora sozinho e
/// os que apontam para camada que nao existe (porque a camada caiu por
/// outro erro) sao descartados: vinculo sem dono nao e dado, e lixo que
/// faria a interface procurar para sempre.
List<PropertyLink> _linksDoJson(Object? bruto, Set<String> idsVivos) {
  if (bruto is! List) return const [];
  final out = <PropertyLink>[];
  for (final l in bruto) {
    if (l is! Map) continue;
    try {
      final m = l.cast<String, dynamic>();
      final alvo = m['target'] as String;
      final fonte = m['source'] as String;
      if (!idsVivos.contains(alvo) || !idsVivos.contains(fonte)) continue;
      out.add(
        PropertyLink(
          id: m['id'] as String,
          targetLayerId: m['target'] as String,
          targetProp:
              LayerProp.values[((m['prop'] as num).toInt()).clamp(
                0,
                LayerProp.values.length - 1,
              )],
          sourceLayerId: m['source'] as String,
          scale: (m['scale'] as num).toDouble(),
          offsetX: (m['ox'] as num).toDouble(),
          offsetY: (m['oy'] as num).toDouble(),
          baseRotation: (m['bRot'] as num?)?.toDouble() ?? 0,
          baseScale: (m['bScale'] as num?)?.toDouble() ?? 1,
          baseRotationX: (m['bRotX'] as num?)?.toDouble() ?? 0,
          baseRotationY: (m['bRotY'] as num?)?.toDouble() ?? 0,
          baseZ: (m['bZ'] as num?)?.toDouble() ?? 0,
          delay: Duration(microseconds: (m['delayUs'] as num?)?.toInt() ?? 0),
        ),
      );
    } catch (_) {
      // Vinculo ilegivel: fica de fora, o projeto abre.
    }
  }
  return out;
}

/// As MASCARAS da camada, pela mesma regra dos efeitos.
List<LayerMask> _mascarasDoJson(Object? bruto) {
  if (bruto is! List) return const [];
  final out = <LayerMask>[];
  for (final k in bruto) {
    if (k is! Map) continue;
    try {
      out.add(_asMask(k.cast<String, dynamic>()));
    } catch (_) {
      // Mascara ilegivel: a camada aparece sem ela, e nao some.
    }
  }
  return out;
}

Layer layerFromJson(Map<String, dynamic> m) {
  final id = m['id'] as String;
  final name = m['name'] as String;
  final start = _asDur(m['start']);
  final dur = _asDur(m['dur']);
  final pos = _asAo(m['pos']);
  final sx = _asAd(m['sx']);
  final sy = _asAd(m['sy']);
  final rot = _asAd(m['rot']);
  final rotX = m['rotX'] == null ? AnimatedDouble(0) : _asAd(m['rotX']);
  final rotY = m['rotY'] == null ? AnimatedDouble(0) : _asAd(m['rotY']);
  final op = _asAd(m['op']);
  final skx = _asAd(m['skx']);
  final sky = _asAd(m['sky']);
  final pivot = _asAo(m['pivot']);
  final pivotZ = m['pivotZ'] == null ? null : _asAd(m['pivotZ']);
  final orientX = m['oriX'] == null ? null : _asAd(m['oriX']);
  final orientY = m['oriY'] == null ? null : _asAd(m['oriY']);
  final orientZ = m['oriZ'] == null ? null : _asAd(m['oriZ']);
  final blend = BlendMode.values[(m['blend'] as num).toInt()];
  final blendX = m['blendX'] == null
      ? null
      : AureaBlend.values[(m['blendX'] as num).toInt()];
  final is3D = m['is3D'] as bool;
  final z = _asAd(m['z']);
  final lidos = _efeitosDoJson(m['effects']);
  final effects = lidos.efeitos;
  // A trilha de tempo da camada: o campo do arquivo novo, ou a que veio
  // de dentro do Time Remap no arquivo antigo. Nula nas duas ausencias.
  final remap = m['remap'] != null ? _asAd(m['remap']) : lidos.remap;
  final masks = _mascarasDoJson(m['masks']);
  final matte = m['matte'] == null
      ? MatteMode.none
      : MatteMode.values[(m['matte'] as num).toInt()];
  final matteSrc = m['matteSrc'] as String?;

  switch (m['kind']) {
    case 'video':
      return VideoLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        sourcePath: m['src'] as String,
        sourceOffset: _asDur(m['srcOffset']),
        sourceDuration: _asDurOuNulo(m['srcDur']),
        speed: (m['speed'] as num?)?.toDouble() ?? 1.0,
        reverse: m['reverse'] as bool? ?? false,
        // A trilha vem do campo (arquivo novo) ou do Time Remap que
        // morava na lista de efeitos (arquivo antigo) — ver [_efeitosDoJson].
        timeRemap: remap,
        speedBlur: m['speedBlur'] as bool? ?? false,
        interpolacao: InterpolacaoDeQuadros.values.firstWhere(
          (i) => i.name == m['interpolacao'],
          orElse: () => InterpolacaoDeQuadros.nenhuma,
        ),
        aprimorar: m['aprimorar'] as bool? ?? false,
        // Numero ilegivel ou fora da faixa nao derruba o projeto.
        forcaDoAprimoramento: (() {
          final f = (m['forcaAprimoramento'] as num?)?.toDouble() ?? 1.0;
          return f.isFinite ? f.clamp(0.0, 1.0) : 1.0;
        })(),
        perfilDoAprimoramento: PerfilDoAprimoramento.values.firstWhere(
          (p) => p.name == m['perfilAprimoramento'],
          orElse: () => PerfilDoAprimoramento.videoReal,
        ),
        reducaoDeRuido: (() {
          final r =
              (m['ruidoAprimoramento'] as num?)?.toDouble() ??
              reducaoDeRuidoPadrao;
          return r.isFinite ? r.clamp(0.0, 1.0) : reducaoDeRuidoPadrao;
        })(),
        ajuste: _lerAjuste(m['ajuste']),
        proporcaoDaFonte: _lerProporcao(m['proporcao']),
        volume: (m['volume'] as num).toDouble(),
        audio: _asAudioSpec(m['audio']),
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'image':
      return ImageLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        sourcePath: m['src'] as String,
        ajuste: _lerAjuste(m['ajuste']),
        proporcaoDaFonte: _lerProporcao(m['proporcao']),
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'text':
      return TextLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        text: m['text'] as String,
        fontSize: (m['fontSize'] as num).toDouble(),
        color: _asCol(m['color']),
        bold: m['bold'] as bool,
        fontFamily: m['font'] as String?,
        alinhamento:
            TextAlign.values.asNameMap()[m['align']] ?? TextAlign.center,
        animators: [
          for (final a in (m['animators'] as List))
            _asAnimator(a as Map<String, dynamic>),
        ],
        anims: [
          for (final a in (m['anims'] as List? ?? const []))
            _asTextAnim(a as Map<String, dynamic>),
        ],
        textPath: _asTextPath(m['textPath']),
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'shape':
      return ShapeLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        contents: [
          for (final i in (m['contents'] as List))
            _asShapeItem(i as Map<String, dynamic>),
        ],
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'group':
      return GroupLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        children: [
          for (final c in (m['children'] as List))
            layerFromJson(c as Map<String, dynamic>),
        ],
        sourceDuration: m['innerDur'] == null
            ? null
            : Duration(microseconds: (m['innerDur'] as num).toInt()),
        // Grupo e video leem a MESMA trilha, inclusive a que veio de
        // dentro do Time Remap no arquivo antigo.
        timeRemap: remap,
        collapse: m['collapse'] as bool? ?? false,
        clipToComp: !(m['noClip'] as bool? ?? false),
        contentOffset: Duration(
          microseconds: (m['contentOffset'] as num? ?? 0).toInt(),
        ),
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'caption':
      return CaptionLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        cues: [
          for (final c in (m['cues'] as List))
            _asCue(c as Map<String, dynamic>),
        ],
        style: _asCapStyle(m['style'] as Map<String, dynamic>),
        highlight: _asHighlight(m['hi']),
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'audio':
      return AudioLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        sourcePath: m['src'] as String,
        sourceOffset: _asDur(m['srcOffset']),
        sourceDuration: _asDurOuNulo(m['srcDur']),
        speed: (m['speed'] as num?)?.toDouble() ?? 1.0,
        volume: (m['volume'] as num).toDouble(),
        audio: _asAudioSpec(m['audio']),
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'adjust':
      return AdjustmentLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'camera':
      return CameraLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        // PROJETO ANTIGO NAO TEM LENTE: cair na neutra deixa a
        // composicao exatamente como estava.
        zoom: _asAdOuNumero(m['zoom'], CameraLayer.lenteNeutra),
        opcoes: _asOpcoesDaCamera(m['opc']),
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'null':
      return NullLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        grid: m['grid'] == null
            ? null
            : _asRig(m['grid'] as Map<String, dynamic>),
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'particles':
      // A TRADUCAO DO QUE O PROJETO GUARDOU VIVE EM `layer.dart`, junto
      // da camada: e ela que sabe que `emitter: 3` de um arquivo antigo
      // quer dizer ANEL, e que `count` era quantas viviam ao mesmo tempo.
      return ParticulasLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        parametros: parametrosDeParticulasDoProjeto(m),
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'scene3d':
      return Scene3DLayer(
        extraCameras: [
          for (final c in (m['cams'] as List? ?? const []))
            _asCamera(c as Map<String, dynamic>),
        ],
        cameraParentLayerId: m['camPai'] as String?,
        shots: [
          for (final t in (m['shots'] as List? ?? const []))
            CameraShot(
              time: Duration(microseconds: ((t as Map)['us'] as num).toInt()),
              cameraId: t['cam'] as String,
              transition: Duration(
                microseconds: ((t['tr'] as num?) ?? 0).toInt(),
              ),
            ),
        ],
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        scene: _asScene(m['scene'] as Map<String, dynamic>),
        camera: _asCamera(m['cam'] as Map<String, dynamic>),
        view: SceneView.values[(m['view'] as num).toInt()],
        showHelpers: m['helpers'] as bool? ?? true,
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    case 'el3d':
      return Element3DLayer(
        id: id,
        name: name,
        startTime: start,
        duration: dur,
        // O NOME MANDA. O indice e o caminho de um arquivo gravado antes
        // desta versao — e um nome desconhecido (projeto de uma versao
        // mais nova) cai no indice, em vez de virar um cubo qualquer.
        kind:
            elemento3DPorNome(m['eln'] as String?) ??
            Element3DKind.values[(m['el'] as num).toInt().clamp(
              0,
              Element3DKind.values.length - 1,
            )],
        acabamento: acabamentoPorNome(m['acab'] as String?),
        size: (m['size'] as num).toDouble(),
        color: _asCol(m['color']),
        edges: m['edges'] as bool? ?? true,
        reflect: (m['reflect'] as num?)?.toDouble() ?? 0.65,
        environment:
            EnvironmentKind.values[((m['env'] as num?)?.toInt() ??
                    EnvironmentKind.estudioMetal.index)
                .clamp(0, EnvironmentKind.values.length - 1)],
        imagePath: m['img'] as String?,
        meshPath: m['mesh'] as String?,
        material: (m['mat'] as num?)?.toInt() ?? 0,
        gradient: m['grad'] is List && (m['grad'] as List).length >= 2
            ? [for (final c in m['grad'] as List) _asCol(c)]
            : const [Color(0xFF7A3FF2), Color(0xFF2F7BFF), Color(0xFFFF4FD8)],
        shininess: (m['shine'] as num?)?.toDouble() ?? 0.65,
        position: pos,
        scaleX: sx,
        scaleY: sy,
        rotation: rot,
        rotationX: rotX,
        rotationY: rotY,
        opacity: op,
        skewX: skx,
        skewY: sky,
        pivot: pivot,
        pivotZ: pivotZ,
        orientX: orientX,
        orientY: orientY,
        orientZ: orientZ,
        blendMode: blend,
        customBlend: blendX,
        is3D: is3D,
        positionZ: z,
        effects: effects,
        masks: masks,
        matteMode: matte,
        matteSourceId: matteSrc,
      );
    default:
      throw FormatException('Camada desconhecida: ${m['kind']}');
  }
}

// --------------------------------------------------------------- projeto

// ------------------------------------------------- camada de oficio

Map<String, dynamic> _shadow(ShadowStyle s) => {
  'on': s.enabled,
  'c': _col(s.color),
  'op': _ad(s.opacity),
  'ang': _ad(s.angleDeg),
  'dist': _ad(s.distance),
  'size': _ad(s.size),
  'spread': _ad(s.spread),
};

ShadowStyle _asShadow(Map<String, dynamic> m) => ShadowStyle(
  enabled: m['on'] as bool? ?? true,
  color: _asCol(m['c']),
  opacity: _asAd(m['op']),
  angleDeg: _asAd(m['ang']),
  distance: _asAd(m['dist']),
  size: _asAd(m['size']),
  spread: m['spread'] == null ? AnimatedDouble(0) : _asAd(m['spread']),
);

/// O ACABAMENTO da camada em JSON — o mesmo formato do projeto, aberto
/// para quem guarda estilo fora dele (os presets de estilo).
Map<String, dynamic> layerStylesToJson(LayerStyles s) => _styles(s);

LayerStyles layerStylesFromJson(Map<String, dynamic> m) => _asStyles(m);

Map<String, dynamic> _styles(LayerStyles s) => {
  if (s.dropShadow != null) 'ds': _shadow(s.dropShadow!),
  if (s.innerShadow != null) 'is': _shadow(s.innerShadow!),
  if (s.outerGlow != null)
    'og': {
      'on': s.outerGlow!.enabled,
      'c': _col(s.outerGlow!.color),
      'op': _ad(s.outerGlow!.opacity),
      'size': _ad(s.outerGlow!.size),
    },
  if (s.colorOverlay != null)
    'co': {
      'on': s.colorOverlay!.enabled,
      'c': _col(s.colorOverlay!.color),
      'op': _ad(s.colorOverlay!.opacity),
      'bm': s.colorOverlay!.blend.index,
    },
  if (s.gradientOverlay != null)
    'go': {
      'on': s.gradientOverlay!.enabled,
      'ca': _col(s.gradientOverlay!.colorA),
      'cb': _col(s.gradientOverlay!.colorB),
      'ang': _ad(s.gradientOverlay!.angleDeg),
      'op': _ad(s.gradientOverlay!.opacity),
    },
  if (s.stroke != null) 'st': _borda(s.stroke!),
  if (s.bordasExtras.isNotEmpty)
    'sts': [for (final b in s.bordasExtras) _borda(b)],
};

Map<String, dynamic> _borda(StrokeStyle b) => {
  'on': b.enabled,
  'c': _col(b.color),
  'w': _ad(b.width),
  'op': _ad(b.opacity),
  if (b.posicao != PosicaoDaBorda.fora) 'pos': b.posicao.index,
};

StrokeStyle _asBorda(Map m) => StrokeStyle(
  enabled: m['on'] as bool? ?? true,
  color: _asCol(m['c']),
  width: _asAd(m['w']),
  opacity: _asAd(m['op']),
  posicao:
      PosicaoDaBorda.values[((m['pos'] as num?)?.toInt() ?? 0).clamp(
        0,
        PosicaoDaBorda.values.length - 1,
      )],
);

LayerStyles _asStyles(Map<String, dynamic> m) => LayerStyles(
  dropShadow: m['ds'] == null
      ? null
      : _asShadow(m['ds'] as Map<String, dynamic>),
  innerShadow: m['is'] == null
      ? null
      : _asShadow(m['is'] as Map<String, dynamic>),
  outerGlow: m['og'] == null
      ? null
      : GlowStyle(
          enabled: (m['og'] as Map)['on'] as bool? ?? true,
          color: _asCol((m['og'] as Map)['c']),
          opacity: _asAd((m['og'] as Map)['op']),
          size: _asAd((m['og'] as Map)['size']),
        ),
  colorOverlay: m['co'] == null
      ? null
      : OverlayStyle(
          enabled: (m['co'] as Map)['on'] as bool? ?? true,
          color: _asCol((m['co'] as Map)['c']),
          opacity: _asAd((m['co'] as Map)['op']),
          blend: BlendMode.values[((m['co'] as Map)['bm'] as num).toInt()],
        ),
  gradientOverlay: m['go'] == null
      ? null
      : GradientOverlayStyle(
          enabled: (m['go'] as Map)['on'] as bool? ?? true,
          colorA: _asCol((m['go'] as Map)['ca']),
          colorB: _asCol((m['go'] as Map)['cb']),
          angleDeg: _asAd((m['go'] as Map)['ang']),
          opacity: _asAd((m['go'] as Map)['op']),
        ),
  stroke: m['st'] == null ? null : _asBorda(m['st'] as Map),
  bordasExtras: [
    for (final b in (m['sts'] as List? ?? const [])) _asBorda(b as Map),
  ],
);

Map<String, dynamic> _meta(LayerMeta m) => {
  if (m.label != null) 'label': {'c': _col(m.label!.color), 'n': m.label!.name},
  if (m.solo) 'solo': true,
  if (m.shy) 'shy': true,
  if (m.locked) 'lock': true,
  if (m.hidden) 'hide': true,
  if (m.folder != null) 'folder': m.folder,
  if (!m.styles.isEmpty) 'styles': _styles(m.styles),
  if (m.textBox != null)
    'box': {
      'mode': m.textBox!.mode.index,
      'w': m.textBox!.width,
      'h': m.textBox!.height,
      'anchor': m.textBox!.anchor.index,
    },
  if (m.container != null)
    'cont': {
      'target': m.container!.targetLayerId,
      'pl': m.container!.padLeft,
      'pr': m.container!.padRight,
      'pt': m.container!.padTop,
      'pb': m.container!.padBottom,
      'min': m.container!.minWidth,
      'max': m.container!.maxWidth,
      'anchor': m.container!.anchor.index,
      'follow': m.container!.follow,
    },
  if (m.stack != null)
    'stack': {
      'dir': m.stack!.direction.index,
      'gap': m.stack!.gap,
      'align': m.stack!.align.index,
      'pad': m.stack!.padding,
      'dist': m.stack!.distribution.index,
      'ext': m.stack!.extent,
    },
  if (m.counter != null)
    'counter': {'v': _ad(m.counter!.value), 'f': _numFmt(m.counter!.format)},
  if (m.colorRef != null) 'colorRef': m.colorRef,
  if (m.textStyleRef != null) 'styleRef': m.textStyleRef,
  if (m.motionBlur) 'mb': true,
  if (m.extrude > 0) 'ext': m.extrude,
};

Map<String, dynamic> _numFmt(NumberFormatSpec f) => {
  'd': f.decimals,
  't': f.thousands,
  'p': f.prefix,
  's': f.suffix,
  'pc': f.percent,
};

NumberFormatSpec _asNumFmt(Map<String, dynamic> m) => NumberFormatSpec(
  decimals: (m['d'] as num).toInt(),
  thousands: m['t'] as bool? ?? true,
  prefix: m['p'] as String? ?? '',
  suffix: m['s'] as String? ?? '',
  percent: m['pc'] as bool? ?? false,
);

LayerMeta _asMeta(Map<String, dynamic> m) => LayerMeta(
  label: m['label'] == null
      ? null
      : LayerLabel(
          color: _asCol((m['label'] as Map)['c']),
          name: (m['label'] as Map)['n'] as String? ?? '',
        ),
  solo: m['solo'] as bool? ?? false,
  shy: m['shy'] as bool? ?? false,
  locked: m['lock'] as bool? ?? false,
  hidden: m['hide'] as bool? ?? false,
  folder: m['folder'] as String?,
  styles: m['styles'] == null
      ? const LayerStyles()
      : _asStyles(m['styles'] as Map<String, dynamic>),
  textBox: m['box'] == null
      ? null
      : TextBoxSpec(
          mode: TextBoxMode.values[((m['box'] as Map)['mode'] as num).toInt()],
          width: ((m['box'] as Map)['w'] as num).toDouble(),
          height: ((m['box'] as Map)['h'] as num).toDouble(),
          anchor:
              GrowAnchor.values[((m['box'] as Map)['anchor'] as num).toInt()],
        ),
  container: m['cont'] == null
      ? null
      : ContainerSpec(
          targetLayerId: (m['cont'] as Map)['target'] as String,
          padLeft: ((m['cont'] as Map)['pl'] as num).toDouble(),
          padRight: ((m['cont'] as Map)['pr'] as num).toDouble(),
          padTop: ((m['cont'] as Map)['pt'] as num).toDouble(),
          padBottom: ((m['cont'] as Map)['pb'] as num).toDouble(),
          minWidth: ((m['cont'] as Map)['min'] as num).toDouble(),
          maxWidth: ((m['cont'] as Map)['max'] as num).toDouble(),
          anchor:
              GrowAnchor.values[((m['cont'] as Map)['anchor'] as num).toInt()],
          follow: (m['cont'] as Map)['follow'] as bool? ?? true,
        ),
  stack: m['stack'] == null
      ? null
      : StackSpec(
          direction: StackDirection
              .values[((m['stack'] as Map)['dir'] as num).toInt()],
          gap: ((m['stack'] as Map)['gap'] as num).toDouble(),
          align:
              StackAlign.values[((m['stack'] as Map)['align'] as num).toInt()],
          padding: ((m['stack'] as Map)['pad'] as num).toDouble(),
          distribution: StackDistribution
              .values[((m['stack'] as Map)['dist'] as num).toInt()],
          extent: ((m['stack'] as Map)['ext'] as num).toDouble(),
        ),
  counter: m['counter'] == null
      ? null
      : CounterSpec(
          value: _asAd((m['counter'] as Map)['v']),
          format: _asNumFmt((m['counter'] as Map)['f'] as Map<String, dynamic>),
        ),
  colorRef: m['colorRef'] as String?,
  textStyleRef: m['styleRef'] as String?,
  motionBlur: m['mb'] as bool? ?? false,
  extrude: (m['ext'] as num?)?.toDouble() ?? 0,
);

/// O PROJETO EM JSON, sem numero que o JSON nao aceita.
///
/// `NaN` e `Infinity` sao numeros validos em Dart e INVALIDOS em JSON:
/// bastava uma escala em cima de outra, ou uma expressao com divisao por
/// zero, para a gravacao falhar com "Converting object to an encodable
/// object failed" — e ai o projeto nao era salvo. O trabalho da sessao
/// inteira se perdia por causa de um numero.
///
/// A ultima peneira antes do arquivo troca esses numeros por zero. Um
/// valor zerado se conserta na tela; um projeto que nao grava, nao.
/// ============================================================ OS PESOS
///
/// Um modelo 3D importado e ENORME comparado ao resto do projeto: as
/// posicoes, normais, UVs e indices viram listas de milhoes de numeros.
/// Guardados dentro do arquivo do projeto, faziam cada salvamento
/// automatico reescrever dezenas de megabytes no fio que responde ao
/// toque — medido em `test/bancada_projeto_com_3d_test.dart`: 18,8 MB e
/// quase 350 ms para um glTF de 60 mil triangulos, em PC, em depuracao.
///
/// Agora o modelo mora num arquivo PROPRIO, ao lado do projeto, e o
/// projeto guarda so uma referencia. Um modelo nao muda depois de
/// importado: escreve-se uma vez e nunca mais.
///
/// COMPATIBILIDADE NOS DOIS SENTIDOS. Sem sink, tudo continua indo para
/// dentro do mapa, exatamente como antes — e por isso template,
/// exportacao e os testes de ida e volta seguem funcionando. Na leitura,
/// um arquivo antigo (modelo embutido) e um novo (referencia) abrem os
/// dois.
///
/// O id vive num [Expando], nao dentro do dado: o modelo do usuario nao
/// e lugar para bookkeeping nosso. Ao reler, o id volta a ser colado no
/// objeto, para o salvamento seguinte reconhecer que o arquivo ja existe
/// e nao reescrever nada.
final Expando<String> _idDoPeso = Expando<String>();

/// Para onde vao os pesos durante uma gravacao (nulo = vao inline).
Map<String, Object>? _sinkDePesos;

/// Os ids cujo arquivo JA existe em disco: nem se monta o conteudo.
Set<String> _pesosJaGravados = const {};

/// De onde vem os pesos durante uma leitura (nulo = so o que vier inline).
Map<String, Object>? _fonteDePesos;

/// O conteudo, ou uma referencia a ele, conforme a gravacao em curso.
///
/// [dono] carrega a identidade do peso (o modelo, a malha). [construir]
/// so e chamado quando o conteudo precisa mesmo ser montado — e isso que
/// evita refazer o base64 de uma malha inteira a cada salvamento.
Object _peso(Object dono, Object Function() construir) {
  final sink = _sinkDePesos;
  if (sink == null) return construir();
  final id = _idDoPeso[dono] ??= const Uuid().v4();
  if (!_pesosJaGravados.contains(id) && !sink.containsKey(id)) {
    sink[id] = _semNumeroImpossivel(construir()) ?? const <String, dynamic>{};
  }
  return <String, dynamic>{'ref': id};
}

/// O conteudo de um campo que pode ser uma referencia, e o id dela.
///
/// Conteudo nulo com id nao nulo significa referencia orfa: o arquivo do
/// peso sumiu. O projeto abre sem aquele modelo, em vez de nao abrir.
({Object? conteudo, String? id}) _semRef(Object? bruto) {
  if (bruto is Map && bruto.length == 1 && bruto['ref'] is String) {
    final id = bruto['ref'] as String;
    return (conteudo: _fonteDePesos?[id], id: id);
  }
  return (conteudo: bruto, id: null);
}

/// O PROJETO E OS PESOS, separados.
///
/// Os modelos saem do mapa e entram em [pesados], um por id.
/// [jaGravados] diz quais ids ja estao em disco, para nem montar o
/// conteudo deles.
Map<String, dynamic> projectToJsonSeparado(
  VideoProject p, {
  required Map<String, Object> pesados,
  Set<String> jaGravados = const {},
}) {
  _sinkDePesos = pesados;
  _pesosJaGravados = jaGravados;
  try {
    return projectToJson(p);
  } finally {
    _sinkDePesos = null;
    _pesosJaGravados = const {};
  }
}

/// O PROJETO, com os pesos que vieram dos arquivos ao lado.
VideoProject projectFromJsonComPesos(
  Map<String, dynamic> m,
  Map<String, Object> pesados,
) {
  _fonteDePesos = pesados;
  try {
    return projectFromJson(m);
  } finally {
    _fonteDePesos = null;
  }
}

/// Todo id de peso referenciado por [m] — para saber o que carregar.
Set<String> refsDeProjeto(Object? m) {
  final out = <String>{};
  void varrer(Object? v) {
    if (v is Map) {
      if (v.length == 1 && v['ref'] is String) {
        out.add(v['ref'] as String);
        return;
      }
      for (final e in v.values) {
        varrer(e);
      }
    } else if (v is List) {
      for (final e in v) {
        varrer(e);
      }
    }
  }

  varrer(m);
  return out;
}

Map<String, dynamic> projectToJson(VideoProject p) {
  final bruto = _projectToJson(p);
  final limpo = _semNumeroImpossivel(bruto);
  return limpo is Map<String, dynamic> ? limpo : bruto;
}

/// Troca NaN e infinito por zero, fundo abaixo (mapas, listas, numeros).
///
/// SO COPIA O QUE PRECISA MUDAR. A primeira versao reconstruia todo mapa
/// e toda lista que encontrasse, sempre. Num projeto comum isso nao se
/// nota; num projeto com um modelo 3D importado, cujas posicoes, normais
/// e indices sao listas de milhoes de numeros guardadas no arquivo, a
/// peneira sozinha custava dezenas de milissegundos a cada salvamento —
/// e o salvamento roda no fio que responde ao toque.
///
/// Agora o galho que nao tem numero impossivel volta como ELE MESMO, sem
/// copia. Numero impossivel e raro; copiar como se fosse comum era o
/// erro.
Object? _semNumeroImpossivel(Object? valor) {
  if (valor is double) return valor.isFinite ? valor : 0.0;

  if (valor is Map) {
    // As chaves precisam ser String no arquivo. Um mapa que ja e
    // Map<String, dynamic> e aproveitado inteiro quando nada muda.
    var mudou = valor is! Map<String, dynamic>;
    final saida = <String, dynamic>{};
    for (final e in valor.entries) {
      final v = _semNumeroImpossivel(e.value);
      if (!identical(v, e.value)) mudou = true;
      saida['${e.key}'] = v;
    }
    return mudou ? saida : valor;
  }

  if (valor is List) {
    var mudou = false;
    final saida = List<Object?>.filled(valor.length, null, growable: false);
    for (var i = 0; i < valor.length; i++) {
      final v = _semNumeroImpossivel(valor[i]);
      if (!identical(v, valor[i])) mudou = true;
      saida[i] = v;
    }
    return mudou ? saida : valor;
  }

  return valor;
}

Map<String, dynamic> _projectToJson(VideoProject p) => {
  'v': 1,
  'id': p.id,
  'name': p.name,
  'createdAt': p.createdAt.toIso8601String(),
  'aspect': p.aspectRatio,
  'fps': p.fps,
  'resH': p.resolutionHeight,
  if (p.backgroundColor.toARGB32() != 0xFF000000) 'bg': _col(p.backgroundColor),
  if (p.thumbTime != null) 'thumbUs': p.thumbTime!.inMicroseconds,
  if (p.introFim != null) 'introUs': p.introFim!.inMicroseconds,
  if (p.finalInicio != null) 'outroUs': p.finalInicio!.inMicroseconds,
  if (p.meta.isNotEmpty)
    'meta': {
      for (final e in p.meta.entries)
        if (!e.value.isEmpty) e.key: _meta(e.value),
    },
  'palette': {for (final e in p.palette.entries.entries) e.key: _col(e.value)},
  if (p.textStyles.isNotEmpty)
    'textStyles': [
      for (final s in p.textStyles)
        {
          'n': s.name,
          'fs': s.fontSize,
          'b': s.bold,
          'c': _col(s.color),
          if (s.colorRef != null) 'cr': s.colorRef,
          'tr': s.tracking,
          'lh': s.lineHeight,
        },
    ],
  if (p.exposed.isNotEmpty)
    'exposed': [
      for (final e in p.exposed)
        {
          'id': e.id,
          'layer': e.layerId,
          'prop': e.property,
          'label': e.label,
          'type': e.type.index,
          'group': e.group,
          if (e.min != null) 'min': e.min,
          if (e.max != null) 'max': e.max,
          if (e.step != null) 'step': e.step,
          if (e.options.isNotEmpty) 'options': e.options,
        },
    ],
  if (p.beats.isNotEmpty) 'beats': [for (final b in p.beats) b.inMicroseconds],
  if (p.bpm != null) 'bpm': p.bpm,
  if (p.markers.isNotEmpty)
    'markers': [
      for (final m in p.markers)
        {
          'us': m.time.inMicroseconds,
          if (m.label.isNotEmpty) 'label': m.label,
          'color': _col(m.color),
        },
    ],
  'guides': {
    'v': p.guides.vertical,
    'h': p.guides.horizontal,
    'cols': p.guides.columns,
    'gut': p.guides.gutter,
    'mar': p.guides.margin,
    'safe': p.guides.showSafeAreas,
    if (p.guides.framePreview != null) 'fp': p.guides.framePreview,
  },
  'mblur': {
    'on': p.motionBlur.enabled,
    'ang': p.motionBlur.shutterAngle,
    'ph': p.motionBlur.shutterPhase,
    'smp': p.motionBlur.samples,
    'lim': p.motionBlur.adaptiveLimit,
  },
  if (p.lottieMode) 'lottieMode': true,
  if (p.data != null)
    'data': {'n': p.data!.name, 'cols': p.data!.columns, 'rows': p.data!.rows},
  if (p.bindings.isNotEmpty)
    'bindings': [
      for (final b in p.bindings)
        {
          'layer': b.layerId,
          'col': b.column,
          'prop': b.property,
          'row': b.row,
          'f': _numFmt(b.format),
        },
    ],
  'layers': [for (final l in p.layers) layerToJson(l)],
  'links': [
    for (final l in p.links)
      {
        'id': l.id,
        'target': l.targetLayerId,
        'prop': l.targetProp.index,
        'source': l.sourceLayerId,
        'scale': l.scale,
        'ox': l.offsetX,
        'oy': l.offsetY,
        'bRot': l.baseRotation,
        'bScale': l.baseScale,
        'bRotX': l.baseRotationX,
        'bRotY': l.baseRotationY,
        'bZ': l.baseZ,
        if (l.delay > Duration.zero) 'delayUs': l.delay.inMicroseconds,
      },
  ],
};

/// UM PROJETO A PARTIR DO ARQUIVO — inclusive de um arquivo torto.
///
/// Antes, qualquer campo faltando derrubava a leitura inteira com um
/// erro de tipo: um projeto sem `createdAt` (vindo de uma versao antiga,
/// de um arquivo cortado pela bateria, de um template escrito a mao) NAO
/// ABRIA, e o aplicativo caia na cara de quem tentou. Perder um pedaco
/// de um arquivo estragado e aceitavel; perder o arquivo, nao.
///
/// Agora cada campo tem um valor de retorno razoavel, e uma CAMADA
/// quebrada e pulada sem levar o resto junto — quem abre ve o que
/// sobreviveu, e nao uma tela de erro.
/// Um numero que precisa ser maior que zero (fps, altura, proporcao):
/// o do arquivo quando serve, o padrao quando nao.
double _positivo(Object? bruto, double padrao) {
  // O arquivo pode trazer "trinta" no lugar de 30 (projeto escrito a
  // mao, exportador de outro app): o que nao for numero vira o padrao.
  final v = bruto is num ? bruto.toDouble() : double.tryParse('$bruto');
  return (v == null || !v.isFinite || v <= 0) ? padrao : v;
}

/// As camadas do arquivo, pulando o que nao da para ler.
///
/// Uma camada de um tipo que esta versao nao conhece (projeto salvo por
/// uma versao mais nova) ou com um campo estragado sai da lista; as
/// outras entram. O contrario — deixar a excecao subir — perderia o
/// projeto inteiro por causa de uma camada.
List<Layer> _camadasDoJson(Object? bruto) {
  if (bruto is! List) return const [];
  final out = <Layer>[];
  for (final l in bruto) {
    if (l is! Map) continue;
    try {
      out.add(layerFromJson(l.cast<String, dynamic>()));
    } catch (_) {
      // Camada ilegivel: fica de fora, e o resto do projeto abre.
    }
  }
  return out;
}

TerminacaoDoTraco _terminacao(Object? v) {
  final i = v is num ? v.toInt() : 0;
  return TerminacaoDoTraco.values[i.clamp(
    0,
    TerminacaoDoTraco.values.length - 1,
  )];
}

/// Um instante opcional gravado em microssegundos. Lixo vira nulo.
Duration? _duracaoOpcional(Object? v) {
  if (v is! num || !v.isFinite || v < 0) return null;
  return Duration(microseconds: v.toInt());
}

VideoProject projectFromJson(Map<String, dynamic> m) {
  // As camadas primeiro: os VINCULOS precisam saber quais ids
  // sobreviveram. Camada que caiu por erro proprio deixa o vinculo dela
  // orfao, e vinculo orfao faria a interface procurar um dono que nao
  // existe mais.
  final camadas = _camadasDoJson(m['layers']);
  final idsVivos = {for (final c in camadas) c.id};
  return projectFromJsonComCamadas(m, camadas, idsVivos);
}

VideoProject projectFromJsonComCamadas(
  Map<String, dynamic> m,
  List<Layer> camadas,
  Set<String> idsVivos,
) => VideoProject(
  // Sem id no arquivo, um id novo — dois projetos sem id nao podem
  // virar o mesmo projeto na lista de recentes.
  id: (m['id'] as String?) ?? 'p${DateTime.now().microsecondsSinceEpoch}',
  name: (m['name'] as String?) ?? 'Projeto',
  createdAt: DateTime.tryParse('${m['createdAt']}') ?? DateTime.now(),
  aspectRatio: _positivo(m['aspect'], 16 / 9),
  fps: _positivo(m['fps'], 30).round(),
  resolutionHeight: _positivo(m['resH'], 1080).round(),
  backgroundColor: m['bg'] == null ? const Color(0xFF000000) : _asCol(m['bg']),
  thumbTime: _duracaoOpcional(m['thumbUs']),
  introFim: _duracaoOpcional(m['introUs']),
  finalInicio: _duracaoOpcional(m['outroUs']),
  layers: camadas,
  links: _linksDoJson(m['links'], idsVivos),
  meta: {
    for (final e in (m['meta'] as Map<String, dynamic>? ?? const {}).entries)
      e.key: _asMeta(e.value as Map<String, dynamic>),
  },
  palette: m['palette'] == null
      ? Palette.aurea
      : Palette(
          entries: {
            for (final e in (m['palette'] as Map<String, dynamic>).entries)
              e.key: _asCol(e.value),
          },
        ),
  textStyles: [
    for (final s in (m['textStyles'] as List? ?? const []))
      TextStyleDef(
        name: s['n'] as String,
        fontSize: (s['fs'] as num).toDouble(),
        bold: s['b'] as bool? ?? true,
        color: _asCol(s['c']),
        colorRef: s['cr'] as String?,
        tracking: (s['tr'] as num?)?.toDouble() ?? 0,
        lineHeight: (s['lh'] as num?)?.toDouble() ?? 1.2,
      ),
  ],
  exposed: [
    for (final e in (m['exposed'] as List? ?? const []))
      ExposedProperty(
        id: e['id'] as String,
        layerId: e['layer'] as String,
        property: e['prop'] as String,
        label: e['label'] as String,
        type: ExposedType.values[(e['type'] as num).toInt()],
        group: e['group'] as String? ?? 'Geral',
        min: (e['min'] as num?)?.toDouble(),
        max: (e['max'] as num?)?.toDouble(),
        step: (e['step'] as num?)?.toDouble(),
        options: [
          for (final o in (e['options'] as List? ?? const [])) o as String,
        ],
      ),
  ],
  beats: [
    for (final v in (m['beats'] as List? ?? const []))
      Duration(microseconds: (v as num).toInt()),
  ],
  bpm: (m['bpm'] as num?)?.toDouble(),
  markers: [
    for (final v in (m['markers'] as List? ?? const []))
      Marker(
        time: Duration(microseconds: ((v as Map)['us'] as num).toInt()),
        label: (v['label'] as String?) ?? '',
        color: _asCol(v['color']),
      ),
  ],
  guides: m['guides'] == null
      ? const GuidesSpec()
      : GuidesSpec(
          vertical: [
            for (final v in ((m['guides'] as Map)['v'] as List? ?? const []))
              (v as num).toDouble(),
          ],
          horizontal: [
            for (final v in ((m['guides'] as Map)['h'] as List? ?? const []))
              (v as num).toDouble(),
          ],
          columns: ((m['guides'] as Map)['cols'] as num?)?.toInt() ?? 0,
          gutter: ((m['guides'] as Map)['gut'] as num?)?.toDouble() ?? 24,
          margin: ((m['guides'] as Map)['mar'] as num?)?.toDouble() ?? 48,
          showSafeAreas: (m['guides'] as Map)['safe'] as bool? ?? false,
          framePreview: ((m['guides'] as Map)['fp'] as num?)?.toDouble(),
        ),
  motionBlur: m['mblur'] == null
      ? const MotionBlurSpec()
      : MotionBlurSpec(
          enabled: (m['mblur'] as Map)['on'] as bool? ?? false,
          shutterAngle: ((m['mblur'] as Map)['ang'] as num?)?.toDouble() ?? 180,
          shutterPhase: ((m['mblur'] as Map)['ph'] as num?)?.toDouble() ?? -90,
          samples: ((m['mblur'] as Map)['smp'] as num?)?.toInt() ?? 16,
          adaptiveLimit: ((m['mblur'] as Map)['lim'] as num?)?.toInt() ?? 32,
        ),
  lottieMode: m['lottieMode'] as bool? ?? false,
  data: m['data'] == null
      ? null
      : DataSource(
          name: (m['data'] as Map)['n'] as String? ?? 'dados',
          columns: [
            for (final c in ((m['data'] as Map)['cols'] as List)) c as String,
          ],
          rows: [
            for (final r in ((m['data'] as Map)['rows'] as List))
              [for (final c in (r as List)) c as String],
          ],
        ),
  bindings: [
    for (final b in (m['bindings'] as List? ?? const []))
      DataBinding(
        layerId: b['layer'] as String,
        column: b['col'] as String,
        property: b['prop'] as String? ?? 'text',
        row: (b['row'] as num?)?.toInt() ?? 0,
        format: _asNumFmt(b['f'] as Map<String, dynamic>),
      ),
  ],
);

/// A serializacao do efeito, para quem guarda efeitos fora do projeto
/// (presets da pessoa). E a MESMA do projeto: o que o projeto sabe
/// guardar, o preset sabe.
Map<String, dynamic> effectToJson(EffectInstance e) => _effect(e);

/// Nulo quando o efeito guardado nao existe mais no catalogo — quem
/// chama decide entre ignorar e avisar. Era `!` disfarcado de garantia.
EffectInstance? effectFromJson(Map<String, dynamic> m) => _asEffect(m);

/// O AJUSTE DA MIDIA no arquivo. `largura` (o de antes) nao e gravado:
/// projeto antigo continua identico, byte a byte.
void _gravarAjuste(
  Map<String, dynamic> base,
  AjusteDaMidia ajuste,
  double? proporcao,
) {
  if (ajuste != AjusteDaMidia.largura) base['ajuste'] = ajuste.name;
  final a = proporcaoValida(proporcao);
  if (a != null) base['proporcao'] = a;
}

/// Leitura tolerante: nome desconhecido vira `largura`, o de antes.
AjusteDaMidia _lerAjuste(Object? v) => AjusteDaMidia.values.firstWhere(
  (a) => a.name == v,
  orElse: () => AjusteDaMidia.largura,
);

double? _lerProporcao(Object? v) =>
    v is num ? proporcaoValida(v.toDouble()) : null;
