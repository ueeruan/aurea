import 'dart:math' as math;
import 'dart:ui';

import '../../../core/utils/mini_xml.dart';
import '../../editor/domain/effect.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/layer_meta.dart';
import '../../editor/domain/mask.dart';
import '../../editor/domain/shape.dart';
import '../../editor/domain/svg_path.dart';
import '../../editor/domain/video_project.dart';

/// IMPORTADOR DE CENA EM XML (projetos e presets de editores de celular).
///
/// O formato e o de um editor de motion do Android que exporta a cena
/// como XML: `<scene>` com camadas `shape`, `text`, `embedScene`
/// (precomp), `audio` e `nullobj`; transformacao em `<transform>` com
/// `<location> <scale> <rotation> <pivot> <opacity>`, cada uma fixa
/// (`value=`) ou com keyframes `<kf t v e>`; cor em `<fillColor>`;
/// contorno em `<path-stroke>`; geometria em `s=".rect"` mais
/// `<property name="size">`, ou num `<path d=...>` de SVG.
///
/// TRES DETALHES QUE MUDAM TUDO, e que sao a razao de este arquivo
/// existir em vez de um leitor generico:
///
///  1. O `t` do keyframe e NORMALIZADO (0..1) na duracao da camada, nao
///     um tempo. Lido como tempo, tudo cai no primeiro quadro.
///  2. A curva `e` do keyframe descreve a chegada NELE; a nossa mora no
///     keyframe que COMECA o trecho. Sem deslocar um, toda animacao sai
///     com o easing errado.
///  3. A ordem dos filhos e a ordem de PINTURA (o primeiro fica atras).
///     A nossa lista e ao contrario: indice 0 e a de cima.
///
/// A leitura e tolerante: o que nao se reconhece entra em [ignored] em
/// portugues, para a pessoa saber o que precisa refazer a mao.
class CenaXmlResult {
  const CenaXmlResult({
    required this.project,
    required this.layersImported,
    required this.keyframesImported,
    required this.ignored,
  });

  final VideoProject project;

  /// Camadas de primeiro nivel (os filhos de grupo contam a parte).
  final int layersImported;
  final int keyframesImported;

  /// O que ficou de fora, em portugues.
  final List<String> ignored;
}

class CenaXmlException implements Exception {
  const CenaXmlException(this.message);
  final String message;
  @override
  String toString() => message;
}

/// Le [xml] e devolve um projeto novo.
CenaXmlResult importarCenaXml(String xml, {String? nome}) {
  final root = parseXml(xml);
  final cena = _acharCena(root);
  if (cena == null) {
    throw const CenaXmlException(
      'Isso nao parece uma cena em XML: nao achei <scene> com tamanho.',
    );
  }

  final largura = _num(cena.attr(['width', 'exportwidth'])) ?? 1920;
  final altura = _num(cena.attr(['height', 'exportheight'])) ?? 1080;
  final fps = (_num(cena.attr(['fps'])) ?? 30).round().clamp(12, 120);
  final ctx = _Contexto(largura: largura, altura: altura, fps: fps);

  // Tabela de midias: uri -> nome do arquivo, so para dizer o que falta.
  for (final m in cena.descendants()) {
    if (m.tag != 'media') continue;
    final uri = m.attr(['uri', 'src']);
    final arquivo = m.attr(['filename', 'title']);
    if (uri != null && arquivo != null) ctx.midias[uri] = arquivo;
  }

  final camadas = _camadasDaCena(cena, ctx);
  // Os vinculos de pai so agora: um filho de grupo pode apontar para uma
  // camada de fora, que ainda nao existia quando ele foi lido.
  _resolvePais(ctx);
  if (camadas.isEmpty) {
    throw CenaXmlException(
      'Nenhuma camada reconhecida na cena'
      '${ctx.ignorados.isEmpty ? '' : ' (${ctx.ignorados.length} coisas ignoradas)'}.',
    );
  }

  final projeto = VideoProject(
    name: nome ?? _texto(cena.attr(['title'])) ?? 'Cena importada',
    createdAt: DateTime.now(),
    aspectRatio: largura / altura,
    fps: fps,
    resolutionHeight: altura.round(),
    layers: camadas,
    links: ctx.links,
    meta: ctx.meta,
    backgroundColor: _cor(cena.attr(['bgcolor'])) ?? const Color(0xFF000000),
  );

  return CenaXmlResult(
    project: projeto,
    layersImported: camadas.length,
    keyframesImported: ctx.keyframes,
    ignored: ctx.ignorados,
  );
}

/// O `<scene>` de primeiro nivel (ou qualquer elemento com tamanho).
XmlNode? _acharCena(XmlNode root) {
  for (final e in root.children) {
    if (e.tag == 'scene') return e;
  }
  for (final e in root.descendants()) {
    if (e.tag == 'scene') return e;
    if (e.attr(['width']) != null && e.attr(['height']) != null) return e;
  }
  return null;
}

class _Contexto {
  _Contexto({required this.largura, required this.altura, required this.fps});

  final double largura;
  final double altura;
  final int fps;
  final List<String> ignorados = [];
  final Map<String, String> midias = {};
  final List<PropertyLink> links = [];
  final Map<String, LayerMeta> meta = {};

  /// id do arquivo -> id da camada nossa (para resolver `parent=`).
  final Map<String, String> porId = {};

  /// Vinculos de pai pendentes: (id da camada nossa, id do pai no arquivo).
  final List<(String, String)> paisPendentes = [];

  int keyframes = 0;

  void ignora(String o) {
    if (ignorados.length < 200) ignorados.add(o);
  }
}

const _tagsDeCamada = {'shape', 'text', 'embedscene', 'audio', 'nullobj'};

/// As camadas de uma cena, ja na NOSSA ordem (indice 0 = a de cima).
List<Layer> _camadasDaCena(XmlNode cena, _Contexto ctx) {
  final out = <Layer>[];
  for (final e in cena.children) {
    if (!_tagsDeCamada.contains(e.tag)) continue;
    final l = _camada(e, ctx);
    if (l != null) out.add(l);
  }
  // O primeiro do arquivo e o que fica ATRAS.
  return out.reversed.toList();
}

void _resolvePais(_Contexto ctx) {
  if (ctx.paisPendentes.isEmpty) return;
  for (final (filho, paiNoArquivo) in [...ctx.paisPendentes]) {
    final pai = ctx.porId[paiNoArquivo];
    if (pai == null || pai == filho) continue;
    if (ctx.links.any((l) => l.targetLayerId == filho)) continue;
    ctx.links.add(
      PropertyLink(
        targetLayerId: filho,
        targetProp: LayerProp.parent,
        sourceLayerId: pai,
      ),
    );
  }
  ctx.paisPendentes.clear();
}

Layer? _camada(XmlNode e, _Contexto ctx) {
  final inicio = _ms(e.attr(['starttime'])) ?? Duration.zero;
  final fim = _ms(e.attr(['endtime'])) ?? (inicio + const Duration(seconds: 3));
  var dur = fim - inicio;
  if (dur < const Duration(milliseconds: 100)) {
    dur = const Duration(milliseconds: 100);
  }
  final nome = _texto(e.attr(['label'])) ?? _nomePadrao(e.tag);
  final t = _transform(e, dur, ctx);

  Layer? camada;
  switch (e.tag) {
    case 'shape':
      camada = _forma(e, nome, inicio, dur, t, ctx);
    case 'text':
      camada = _textoCamada(e, nome, inicio, dur, t, ctx);
    case 'embedscene':
      camada = _grupo(e, nome, inicio, dur, t, ctx);
    case 'nullobj':
      camada = NullLayer(
        name: nome,
        startTime: inicio,
        duration: dur,
        position: t.pos,
        scaleX: t.sx,
        scaleY: t.sy,
        rotation: t.rot,
        opacity: t.op,
        pivot: t.pivot,
        positionZ: t.z,
      );
    case 'audio':
      // O som aponta para um arquivo do celular de origem (content://),
      // que nao existe aqui: a camada viria muda e quebraria o tocador.
      final arquivo =
          ctx.midias[e.attr(['src']) ?? ''] ?? _texto(e.attr(['label']));
      ctx.ignora('audio "${arquivo ?? nome}" — reimporte o arquivo');
      return null;
  }
  if (camada == null) return null;

  ctx.keyframes += t.keyframes;
  final idNoArquivo = e.attr(['id']);
  if (idNoArquivo != null) ctx.porId[idNoArquivo] = camada.id;
  final pai = e.attr(['parent']);
  if (pai != null) ctx.paisPendentes.add((camada.id, pai));

  // Olho fechado e motion blur da camada viram meta.
  final oculta = (e.attr(['hidden']) ?? '').toLowerCase() == 'true';
  final blur = e.children.any(
    (c) => c.tag == 'effect' && (c.attr(['id']) ?? '').contains('motionblur'),
  );
  if (oculta || blur) {
    ctx.meta[camada.id] = LayerMeta(hidden: oculta, motionBlur: blur);
  }

  final efeitos = _efeitos(e, ctx);
  if (efeitos.isNotEmpty) camada = camada.copyLayer(effects: efeitos);

  // FADE: o efeito de entrada/saida vira keyframes de opacidade — e o
  // que faz a animacao chegar igual, em vez de "ignorado".
  final fade = _fade(e, dur);
  if (fade != null && !camada.opacity.isAnimated) {
    camada = camada.copyLayer(opacity: fade);
    ctx.keyframes += fade.keyframes.length;
  }
  return camada;
}

String _nomePadrao(String tag) => switch (tag) {
  'text' => 'Texto',
  'embedscene' => 'Grupo',
  'nullobj' => 'Nulo',
  'audio' => 'Som',
  _ => 'Forma',
};

// ------------------------------------------------------------ transform

class _Transform {
  const _Transform({
    required this.pos,
    required this.sx,
    required this.sy,
    required this.rot,
    required this.op,
    required this.pivot,
    required this.z,
    required this.keyframes,
  });

  final AnimatedOffset pos;
  final AnimatedDouble sx;
  final AnimatedDouble sy;
  final AnimatedDouble rot;
  final AnimatedDouble op;
  final AnimatedOffset pivot;
  final AnimatedDouble z;
  final int keyframes;
}

_Transform _transform(XmlNode e, Duration dur, _Contexto ctx) {
  final tr = e.child('transform');
  var n = 0;

  final loc = tr?.child('location');
  final pos = _offsetProp(loc, dur, Offset(ctx.largura / 2, ctx.altura / 2));
  final z = _doubleProp(_eixoZ(loc), dur, 0);
  final esc = tr?.child('scale');
  final escala = _offsetProp(esc, dur, const Offset(1, 1));
  final rot = _doubleProp(tr?.child('rotation'), dur, 0);
  final op = _doubleProp(tr?.child('opacity'), dur, 1);
  final piv = _offsetProp(tr?.child('pivot'), dur, Offset.zero);

  n +=
      pos.keyframes.length +
      escala.keyframes.length * 2 +
      rot.keyframes.length +
      op.keyframes.length +
      piv.keyframes.length;

  return _Transform(
    pos: pos,
    sx: AnimatedDouble(escala.base.dx, [
      for (final k in escala.keyframes)
        Keyframe(time: k.time, value: k.value.dx, ease: k.ease),
    ]),
    sy: AnimatedDouble(escala.base.dy, [
      for (final k in escala.keyframes)
        Keyframe(time: k.time, value: k.value.dy, ease: k.ease),
    ]),
    rot: rot,
    op: op,
    pivot: piv,
    z: z,
    keyframes: n,
  );
}

/// A terceira componente do `<location>` (profundidade), quando existe.
XmlNode? _eixoZ(XmlNode? loc) {
  if (loc == null) return null;
  final v = loc.attr(['value']);
  if (v != null && v.split(',').length < 3) return null;
  return _zDe(loc);
}

/// Vista do `<location>` que le a terceira componente como numero.
XmlNode _zDe(XmlNode loc) {
  final copia = XmlNode('z', {
    if (loc.attrs['value'] != null) 'value': _comp(loc.attrs['value']!, 2),
  });
  for (final k in loc.children) {
    if (k.tag != 'kf') continue;
    final v = k.attr(['v']);
    copia.children.add(
      XmlNode('kf', {...k.attrs, if (v != null) 'v': _comp(v, 2)}, copia),
    );
  }
  return copia;
}

String _comp(String csv, int i) {
  final p = csv.split(',');
  return i < p.length ? p[i] : '0';
}

// ------------------------------------------------------------ keyframes

/// Os keyframes de uma propriedade, ja no NOSSO tempo e com a curva no
/// lugar certo. Devolve null quando a propriedade e fixa.
List<Keyframe<T>>? _keyframes<T>(
  XmlNode? prop,
  Duration dur,
  T? Function(String) parse,
) {
  if (prop == null) return null;
  final kfs = prop.children.where((c) => c.tag == 'kf').toList();
  if (kfs.isEmpty) return null;
  final us = dur.inMicroseconds;
  final lidos = <({double t, T v, String? e})>[];
  for (final k in kfs) {
    final t = _num(k.attr(['t']));
    final v = k.attr(['v']);
    if (t == null || v == null) continue;
    final valor = parse(v);
    if (valor == null) continue;
    lidos.add((t: t.clamp(0.0, 1.0), v: valor, e: k.attr(['e'])));
  }
  if (lidos.isEmpty) return null;
  lidos.sort((a, b) => a.t.compareTo(b.t));
  return [
    for (var i = 0; i < lidos.length; i++)
      Keyframe<T>(
        time: Duration(microseconds: (lidos[i].t * us).round()),
        value: lidos[i].v,
        // A curva do arquivo descreve a CHEGADA no keyframe; a nossa
        // descreve a SAIDA. Por isso a do proximo entra aqui.
        ease: i + 1 < lidos.length ? _curva(lidos[i + 1].e) : Easing.linear,
      ),
  ];
}

AnimatedDouble _doubleProp(XmlNode? prop, Duration dur, double padrao) {
  final kfs = _keyframes<double>(prop, dur, _num);
  final fixo = _num(prop?.attr(['value']));
  final base = fixo ?? (kfs?.isNotEmpty ?? false ? kfs!.first.value : padrao);
  return AnimatedDouble(base, kfs);
}

AnimatedOffset _offsetProp(XmlNode? prop, Duration dur, Offset padrao) {
  final kfs = _keyframes<Offset>(prop, dur, _vec2);
  final fixo = _vec2(prop?.attr(['value']) ?? '');
  final base = fixo ?? (kfs?.isNotEmpty ?? false ? kfs!.first.value : padrao);
  return AnimatedOffset(base, kfs);
}

/// `cubicBezier x1 y1 x2 y2`, `elastic ...`, `bounce ...`, `hold`.
Easing _curva(String? e) {
  if (e == null || e.trim().isEmpty) return Easing.linear;
  final p = e.trim().split(RegExp(r'\s+'));
  final nome = p.first.toLowerCase();
  double at(int i) => i < p.length ? (double.tryParse(p[i]) ?? 0) : 0;
  return switch (nome) {
    'cubicbezier' || 'bezier' || 'cubic' => Easing(
      x1: at(1).clamp(0.0, 1.0),
      y1: at(2),
      x2: at(3).clamp(0.0, 1.0),
      y2: at(4),
    ),
    'elastic' => const Easing(type: EasingType.elastic),
    'bounce' => const Easing(type: EasingType.bounce),
    'hold' ||
    'step' ||
    'steps' ||
    'constant' => const Easing(type: EasingType.steps),
    'ease' || 'easeinout' => Easing.easeInOut,
    'easein' => Easing.easeIn,
    'easeout' => Easing.easeOut,
    _ => Easing.linear,
  };
}

// ------------------------------------------------------------ formas

Layer _forma(
  XmlNode e,
  String nome,
  Duration inicio,
  Duration dur,
  _Transform t,
  _Contexto ctx,
) {
  final itens = <ShapeItem>[];
  final geo = _geometria(e, dur, ctx);
  if (geo != null) itens.add(geo);

  final fillType = (e.attr(['filltype']) ?? 'color').toLowerCase();
  if (fillType == 'media') {
    final arquivo = ctx.midias[e.attr(['fillimage']) ?? ''] ?? nome;
    ctx.ignora('imagem "$arquivo" — a forma veio sem a midia');
    itens.add(ShapeFill(color: const Color(0xFF3A414D)));
  } else if (fillType != 'none') {
    final fc = e.child('fillcolor');
    final kfs = _keyframes<Color>(fc, dur, _cor);
    final cor = _cor(fc?.attr(['value'])) ?? (kfs?.first.value);
    // Sem <fillColor> o arquivo quer o padrao dele, que e branco — nao a
    // cor de forma nova daqui (laranja), que faria o projeto chegar com
    // cara de outro projeto.
    itens.add(ShapeFill(color: cor ?? const Color(0xFFFFFFFF)));
    if (kfs != null && kfs.length > 1) {
      ctx.ignora('cor animada em "$nome" — ficou a primeira cor');
    }
  }

  final ps = e.child('path-stroke');
  if (ps != null) {
    final larguraStroke =
        _num(ps.child('size')?.attr(['value'])) ??
        _num(ps.attr(['end-size'])) ??
        6;
    itens.add(
      ShapeStroke(
        color:
            _cor(ps.child('color')?.attr(['value'])) ?? const Color(0xFFFFFFFF),
        width: AnimatedDouble(larguraStroke <= 0 ? 6 : larguraStroke),
      ),
    );
  }

  return ShapeLayer(
    name: nome,
    startTime: inicio,
    duration: dur,
    contents: itens,
    position: t.pos,
    scaleX: t.sx,
    scaleY: t.sy,
    rotation: t.rot,
    opacity: t.op,
    pivot: t.pivot,
    positionZ: t.z,
  );
}

/// As formas com nome do arquivo que existem na nossa biblioteca.
///
/// Cada uma tem parametros proprios (a gota vem de `radius`+`tail`, a
/// estrela de `pointCount`), entao a conversao e por forma, nao por
/// tabela: e o que faz o desenho chegar do mesmo tamanho.
ShapeItem? _formaNomeada(String nome, XmlNode e) {
  double prop(String n, double padrao) =>
      _num(_propriedade(e, n)?.attr(['value'])) ?? padrao;
  return switch (nome) {
    '.teardrop' => ShapePath(
      primitive: ShapePrimitive.drop,
      width: prop('radius', 100) * 2,
      height: prop('radius', 100) + prop('tail', 178),
    ),
    '.heart' => ShapePath(
      primitive: ShapePrimitive.heart,
      width: prop('size', 300),
      height: prop('size', 300),
    ),
    '.arrow' => ShapePath(primitive: ShapePrimitive.arrow),
    '.check' => ShapePath(primitive: ShapePrimitive.check),
    '.plus' || '.cross' => ShapePath(primitive: ShapePrimitive.plus),
    '.flower' => ShapePath(
      primitive: ShapePrimitive.flower,
      points: prop('pointCount', 6).round(),
    ),
    '.sparkle' => ShapePath(primitive: ShapePrimitive.sparkle),
    '.gear' => ShapePath(
      primitive: ShapePrimitive.gear,
      points: prop('pointCount', 8).round(),
    ),
    '.wave' => ShapePath(primitive: ShapePrimitive.wave),
    '.arc' => ShapePath(
      primitive: ShapePrimitive.arc,
      startAngle: prop('startAngle', 0),
      sweepAngle: prop('sweep', 270),
    ),
    '.ring' || '.donut' => ShapePath(
      primitive: ShapePrimitive.ring,
      thickness: prop('thickness', 60),
    ),
    _ => null,
  };
}

ShapeItem? _geometria(XmlNode e, Duration dur, _Contexto ctx) {
  // Um caminho desenhado a mao vem como path data de SVG: entra como
  // caminho EDITAVEL (da para pegar os nos depois).
  final d = e.child('path')?.attr(['d']);
  if (d != null && d.trim().isNotEmpty) {
    try {
      return ShapeBezier(path: AnimatedPath(svgPathToBezier(d)));
    } catch (_) {
      return ShapeSvgPath(pathData: d);
    }
  }

  final nome = (e.attr(['s']) ?? '').toLowerCase();
  final nomeada = _formaNomeada(nome, e);
  if (nomeada != null) return nomeada;

  final size = _propriedade(e, 'size');
  final tamanho = _offsetProp(size, dur, const Offset(200, 200));
  final kind = switch (nome) {
    '.rect' || '.roundrect' || 'rect' => ParamShapeKind.rect,
    '.circle' || '.ellipse' || 'circle' => ParamShapeKind.ellipse,
    '.star' || 'star' => ParamShapeKind.star,
    '.polygon' || 'polygon' => ParamShapeKind.polygon,
    '' => ParamShapeKind.rect,
    _ => null,
  };
  if (kind == null) {
    ctx.ignora(
      'forma "$nome" nao existe aqui — virou retangulo do mesmo tamanho',
    );
  }
  final raio = _num(_propriedade(e, 'cornerRadius')?.attr(['value']));
  final pontas = _num(_propriedade(e, 'pointCount')?.attr(['value']));

  return ShapeParametric(
    kind: kind ?? ParamShapeKind.rect,
    sizeX: AnimatedDouble(tamanho.base.dx, [
      for (final k in tamanho.keyframes)
        Keyframe(time: k.time, value: k.value.dx, ease: k.ease),
    ]),
    sizeY: AnimatedDouble(tamanho.base.dy, [
      for (final k in tamanho.keyframes)
        Keyframe(time: k.time, value: k.value.dy, ease: k.ease),
    ]),
    roundness: AnimatedDouble(raio ?? 0),
    roundnessPercent: false,
    points: AnimatedDouble(pontas ?? 5),
    outerRadius: AnimatedDouble(tamanho.base.dx / 2),
    innerRadius: AnimatedDouble(tamanho.base.dx / 4),
  );
}

XmlNode? _propriedade(XmlNode e, String nome) {
  for (final c in e.children) {
    if (c.tag == 'property' &&
        (c.attr(['name']) ?? '').toLowerCase() == nome.toLowerCase()) {
      return c;
    }
  }
  return null;
}

// ------------------------------------------------------------ texto

Layer _textoCamada(
  XmlNode e,
  String nome,
  Duration inicio,
  Duration dur,
  _Transform t,
  _Contexto ctx,
) {
  final conteudo = e.child('content')?.innerText.trim() ?? '';
  final texto = conteudo.isEmpty
      ? (_texto(e.attr(['label'])) ?? 'Texto')
      : conteudo;
  final fc = e.child('fillcolor');
  // O nome e o rotulo do arquivo; sem rotulo, o proprio texto (e o que
  // deixa a timeline legivel — "Texto" em dez camadas nao diz nada).
  final rotulo =
      _texto(e.attr(['label'])) ??
      (texto.length > 24 ? texto.substring(0, 24) : texto);
  return TextLayer(
    name: rotulo,
    startTime: inicio,
    duration: dur,
    text: texto,
    fontSize: _num(e.attr(['size'])) ?? 36,
    color: _cor(fc?.attr(['value'])) ?? const Color(0xFFFFFFFF),
    fontFamily: _fonte(e.attr(['font'])),
    position: t.pos,
    scaleX: t.sx,
    scaleY: t.sy,
    rotation: t.rot,
    opacity: t.op,
    pivot: t.pivot,
    positionZ: t.z,
  );
}

/// `googlefonts?name=Roboto&weight=500` / `imported?name=Arquivo.ttf`.
String? _fonte(String? f) {
  if (f == null || f.isEmpty) return null;
  final m = RegExp(r'name=([^&]+)').firstMatch(f);
  var nome = m?.group(1) ?? f;
  nome = Uri.decodeComponent(nome.replaceAll('+', ' '));
  nome = nome.replaceAll(RegExp(r'\.(ttf|otf)$', caseSensitive: false), '');
  return nome.isEmpty ? null : nome;
}

// ------------------------------------------------------------ grupo

Layer _grupo(
  XmlNode e,
  String nome,
  Duration inicio,
  Duration dur,
  _Transform t,
  _Contexto ctx,
) {
  final dentro = e.child('scene');
  final filhos = dentro == null ? <Layer>[] : _camadasDaCena(dentro, ctx);
  final interna = _ms(dentro?.attr(['totaltime']));
  if (_num(e.attr(['intime'])) != null && (_num(e.attr(['intime'])) ?? 0) > 0) {
    ctx.ignora('grupo "$nome" comeca cortado no meio — o corte nao veio');
  }
  return GroupLayer(
    name: nome,
    startTime: inicio,
    duration: dur,
    children: filhos,
    sourceDuration: interna != null && interna > Duration.zero ? interna : null,
    position: t.pos,
    scaleX: t.sx,
    scaleY: t.sy,
    rotation: t.rot,
    opacity: t.op,
    pivot: t.pivot,
    positionZ: t.z,
  );
}

// ------------------------------------------------------------ efeitos

/// O fade de entrada/saida como keyframes de opacidade.
AnimatedDouble? _fade(XmlNode e, Duration dur) {
  for (final c in e.children) {
    if (c.tag != 'effect') continue;
    if (!(c.attr(['id']) ?? '').endsWith('.fade')) continue;
    final entra = _num(_propriedade(c, 'inTime')?.attr(['value'])) ?? 0;
    final sai = _num(_propriedade(c, 'outTime')?.attr(['value'])) ?? 0;
    if (entra <= 0 && sai <= 0) return null;
    final total = dur.inMicroseconds;
    final kfs = <Keyframe<double>>[];
    if (entra > 0) {
      kfs.add(const Keyframe(time: Duration.zero, value: 0));
      kfs.add(
        Keyframe(
          time: Duration(microseconds: (entra * 1e6).round().clamp(0, total)),
          value: 1,
        ),
      );
    }
    if (sai > 0) {
      final ini = total - (sai * 1e6).round();
      kfs.add(
        Keyframe(time: Duration(microseconds: math.max(0, ini)), value: 1),
      );
      kfs.add(Keyframe(time: Duration(microseconds: total), value: 0));
    }
    kfs.sort((a, b) => a.time.compareTo(b.time));
    return AnimatedDouble(kfs.first.value, kfs);
  }
  return null;
}

List<EffectInstance> _efeitos(XmlNode e, _Contexto ctx) {
  final out = <EffectInstance>[];
  for (final c in e.children) {
    if (c.tag != 'effect') continue;
    final id = c.attr(['id']) ?? '';
    final curto = id.split('.').last;
    // Ja tratados fora daqui.
    if (curto.startsWith('motionblur') || curto == 'fade') continue;
    final tipo = _efeitoPorNome(curto);
    if (tipo == null) {
      ctx.ignora('efeito "$curto" nao existe aqui');
      continue;
    }
    var fx = EffectInstance(type: tipo);
    final cor = _cor(
      _propriedade(c, 'color')?.attr(['value']) ??
          _propriedade(c, 'colorshadow')?.attr(['value']),
    );
    if (cor != null) fx = fx.copyWith(color: cor);
    out.add(fx);
  }
  return out;
}

EffectType? _efeitoPorNome(String nome) {
  final n = nome.toLowerCase();
  // COLORING (presets "CC" do Alight Motion): antes das regras genericas
  // abaixo, que confundiriam "colorbalance" com tint e "gradientmap" com
  // outra coisa.
  if (n.contains('colorbalance') || n.contains('color_balance')) {
    return EffectType.colorBalance;
  }
  if (n.contains('selectivecolor')) return EffectType.selectiveColor;
  if (n.contains('channelmix') || n.contains('channelremap')) {
    return EffectType.channelMixer;
  }
  // So os que NASCEM NEUTROS: o valor do arquivo nao e lido aqui, e um
  // mapa de gradiente ou filtro de foto com os valores de nascenca
  // tingiria a camada com uma cor que o arquivo nao pediu.
  if (n.contains('colortune') || n.contains('colorwheel')) {
    return EffectType.colorTune;
  }
  if (n.contains('brightness') && n.contains('contrast')) {
    return EffectType.brightnessContrast;
  }
  if (n.contains('gaussian') || n == 'blur') return EffectType.gaussianBlur;
  if (n.contains('glow')) return EffectType.lightGlow;
  if (n.contains('solidcolor') || n.contains('tint')) return EffectType.tint;
  if (n.contains('glitch')) return EffectType.glitch;
  if (n.contains('rgbsplit') || n.contains('chromatic')) {
    return EffectType.rgbSplit;
  }
  if (n.contains('shake') || n.contains('wiggle')) return EffectType.tremor;
  if (n.contains('vignette')) return EffectType.vignette;
  if (n.contains('mosaic') || n.contains('pixelate')) return EffectType.mosaic;
  if (n.contains('grain') || n.contains('noise')) return EffectType.filmGrain;
  if (n.contains('posterize')) return EffectType.posterize;
  if (n.contains('levels')) return EffectType.levels;
  if (n.contains('curves')) return EffectType.curves;
  if (n.contains('vibrance') || n.contains('saturation')) {
    return EffectType.vibrance;
  }
  if (n.contains('chroma') && n.contains('key')) return EffectType.chromaKey;
  if (n.contains('flicker')) return EffectType.flicker;
  if (n.contains('vhs')) return EffectType.vhs;
  if (n.contains('edge')) return EffectType.findEdges;
  return null;
}

// ------------------------------------------------------------ basicos

double? _num(String? s) {
  if (s == null) return null;
  return double.tryParse(s.trim());
}

/// Tempo do arquivo: milissegundos inteiros.
Duration? _ms(String? s) {
  final v = _num(s);
  if (v == null) return null;
  return Duration(microseconds: (v * 1000).round());
}

Offset? _vec2(String s) {
  final p = s.split(',');
  if (p.length < 2) return null;
  final x = double.tryParse(p[0].trim());
  final y = double.tryParse(p[1].trim());
  if (x == null || y == null) return null;
  return Offset(x, y);
}

/// `#aarrggbb` (o formato do arquivo) e tambem `#rrggbb`.
Color? _cor(String? s) {
  if (s == null) return null;
  var t = s.trim();
  if (t.startsWith('#')) t = t.substring(1);
  if (t.length == 6) t = 'ff$t';
  if (t.length != 8) return null;
  final v = int.tryParse(t, radix: 16);
  return v == null ? null : Color(v);
}

String? _texto(String? s) {
  final t = s?.trim();
  return t == null || t.isEmpty ? null : t;
}
