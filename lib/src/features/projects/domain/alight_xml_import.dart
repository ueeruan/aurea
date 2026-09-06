import 'dart:ui';

import '../../editor/domain/effect.dart';
import '../../editor/domain/keyframe.dart';
import '../../editor/domain/layer.dart';
import '../../editor/domain/shape.dart';
import '../../editor/domain/video_project.dart';

/// IMPORTADOR DE XML DO ALIGHT MOTION (presets e projetos).
///
/// O formato nao e documentado e muda entre versoes, entao a leitura e
/// TOLERANTE: procura o que reconhece (cena, camadas de forma, texto,
/// imagem, video, grupo; posicao, escala, rotacao, opacidade — fixas
/// ou com keyframes; cor; efeitos por nome) e conta o que ignorou. O
/// resultado diz quantas camadas entraram e o que ficou de fora, para a
/// pessoa saber o que precisa refazer a mao.
class AlightImportResult {
  const AlightImportResult({
    required this.project,
    required this.layersImported,
    required this.keyframesImported,
    required this.ignored,
  });

  final VideoProject project;
  final int layersImported;
  final int keyframesImported;

  /// Motivos, um por item ignorado ("video sem arquivo: intro.mp4").
  final List<String> ignored;
}

class AlightImportException implements Exception {
  const AlightImportException(this.message);
  final String message;
  @override
  String toString() => message;
}

// ---------------------------------------------------------------- XML

/// Elemento de XML minimo: tag, atributos, filhos e texto.
class XmlNode {
  XmlNode(this.tag, this.attrs, [this.parent]);

  final String tag;
  final Map<String, String> attrs;
  final XmlNode? parent;
  final List<XmlNode> children = [];
  final StringBuffer text = StringBuffer();

  String? attr(List<String> names) {
    for (final n in names) {
      for (final e in attrs.entries) {
        if (e.key.toLowerCase() == n.toLowerCase()) return e.value;
      }
    }
    return null;
  }

  Iterable<XmlNode> descendants() sync* {
    for (final c in children) {
      yield c;
      yield* c.descendants();
    }
  }
}

/// Parser de XML pequeno e permissivo: elementos, atributos (com aspas
/// simples ou duplas), texto, comentarios, CDATA e declaracoes. Nao
/// valida nada — o que importa e nao cair com arquivo estranho.
XmlNode parseXml(String src) {
  final root = XmlNode('#root', const {});
  var cur = root;
  var i = 0;
  final n = src.length;
  final attrRe = RegExp(r'''([A-Za-z_:][\w:.\-]*)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s/>]+))''');
  while (i < n) {
    final lt = src.indexOf('<', i);
    if (lt < 0) {
      cur.text.write(src.substring(i));
      break;
    }
    if (lt > i) cur.text.write(src.substring(i, lt));
    if (src.startsWith('<!--', lt)) {
      final fim = src.indexOf('-->', lt);
      i = fim < 0 ? n : fim + 3;
      continue;
    }
    if (src.startsWith('<![CDATA[', lt)) {
      final fim = src.indexOf(']]>', lt);
      cur.text.write(src.substring(lt + 9, fim < 0 ? n : fim));
      i = fim < 0 ? n : fim + 3;
      continue;
    }
    if (src.startsWith('<?', lt) || src.startsWith('<!', lt)) {
      final fim = src.indexOf('>', lt);
      i = fim < 0 ? n : fim + 1;
      continue;
    }
    final gt = _fimDaTag(src, lt);
    if (gt < 0) break;
    final corpo = src.substring(lt + 1, gt).trim();
    i = gt + 1;
    if (corpo.startsWith('/')) {
      final nome = corpo.substring(1).trim().toLowerCase();
      // Fecha ate achar a tag (tolerante a fechamento fora de ordem).
      var p = cur;
      while (p.parent != null && p.tag != nome) {
        p = p.parent!;
      }
      cur = p.parent ?? root;
      continue;
    }
    final autoFecha = corpo.endsWith('/');
    final semBarra = autoFecha ? corpo.substring(0, corpo.length - 1) : corpo;
    final espaco = semBarra.indexOf(RegExp(r'\s'));
    final nome =
        (espaco < 0 ? semBarra : semBarra.substring(0, espaco)).toLowerCase();
    final attrs = <String, String>{};
    if (espaco >= 0) {
      for (final m in attrRe.allMatches(semBarra.substring(espaco))) {
        attrs[m.group(1)!] = _desescapa(m.group(2) ?? m.group(3) ?? m.group(4) ?? '');
      }
    }
    final el = XmlNode(nome, attrs, cur);
    cur.children.add(el);
    if (!autoFecha) cur = el;
  }
  return root;
}

int _fimDaTag(String s, int lt) {
  var aspas = '';
  for (var i = lt + 1; i < s.length; i++) {
    final c = s[i];
    if (aspas.isNotEmpty) {
      if (c == aspas) aspas = '';
      continue;
    }
    if (c == '"' || c == "'") {
      aspas = c;
    } else if (c == '>') {
      return i;
    }
  }
  return -1;
}

String _desescapa(String s) => s
    .replaceAll('&lt;', '<')
    .replaceAll('&gt;', '>')
    .replaceAll('&quot;', '"')
    .replaceAll('&apos;', "'")
    .replaceAll('&#39;', "'")
    .replaceAll('&amp;', '&');

// ----------------------------------------------------------- leitura

const _tagsDeCamada = {
  'layer', 'shape', 'text', 'image', 'video', 'solid', 'group', 'precomp',
  'rect', 'rectangle', 'ellipse', 'circle', 'polygon', 'star', 'null',
  'textlayer', 'shapelayer', 'imagelayer', 'videolayer', 'grouplayer',
};

AlightImportResult importAlightXml(String xml, {String? nome}) {
  final root = parseXml(xml);
  if (root.children.isEmpty) {
    throw const AlightImportException('Isso nao parece um XML.');
  }
  // CENA: o primeiro elemento com largura/altura, senao 1080x1920.
  XmlNode? cena;
  for (final e in [...root.children, ...root.descendants()]) {
    if (e.attr(['width', 'w', 'projectWidth']) != null &&
        e.attr(['height', 'h', 'projectHeight']) != null) {
      cena = e;
      break;
    }
  }
  final largura = _num(cena?.attr(['width', 'w', 'projectWidth'])) ?? 1080;
  final altura = _num(cena?.attr(['height', 'h', 'projectHeight'])) ?? 1920;
  final fps = (_num(cena?.attr(['fps', 'frameRate', 'framerate'])) ?? 30)
      .round()
      .clamp(12, 120);
  final duracaoCena = _tempo(cena?.attr(['duration', 'length', 'totalTime']));

  final ignorados = <String>[];
  final camadas = <Layer>[];
  var keyframes = 0;

  // Origem: o Alight guarda posicao a partir do CENTRO; se aparece
  // alguma coordenada negativa, e centro; senao, canto.
  final nosDeCamada = <XmlNode>[
    for (final e in root.descendants())
      if (_ehCamada(e)) e,
  ];
  var origemCentro = false;
  for (final e in nosDeCamada) {
    final p = _posicao(e);
    if (p != null && (p.dx < 0 || p.dy < 0)) origemCentro = true;
  }
  final centro = Offset(largura / 2, altura / 2);

  for (final e in nosDeCamada) {
    // Filho de outra camada que ja foi lida como grupo: pula (o grupo
    // le os filhos dele).
    var p = e.parent;
    var dentroDeGrupo = false;
    while (p != null) {
      if (_ehCamada(p) && _tipo(p) == 'group') dentroDeGrupo = true;
      p = p.parent;
    }
    if (dentroDeGrupo) continue;
    final r = _lerCamada(e, largura, altura, origemCentro, centro,
        duracaoCena, fps, ignorados);
    if (r != null) {
      camadas.add(r.$1);
      keyframes += r.$2;
    }
  }
  if (camadas.isEmpty) {
    throw AlightImportException(
        'Nenhuma camada reconhecida no XML${ignorados.isEmpty ? '' : ' (${ignorados.length} ignoradas)'}.');
  }
  final projeto = VideoProject(
    name: nome ?? 'Preset do Alight',
    createdAt: DateTime.now(),
    aspectRatio: largura / altura,
    fps: fps,
    resolutionHeight: altura.round(),
    layers: camadas,
  );
  return AlightImportResult(
    project: projeto,
    layersImported: camadas.length,
    keyframesImported: keyframes,
    ignored: ignorados,
  );
}

bool _ehCamada(XmlNode e) {
  if (_tagsDeCamada.contains(e.tag)) return e.tag != 'text' || e.children.isEmpty || e.attrs.isNotEmpty || e.parent?.tag == 'layers' || e.parent?.tag == 'scene';
  return false;
}

String _tipo(XmlNode e) {
  final t = (e.attr(['type', 'kind', 'layerType']) ?? e.tag).toLowerCase();
  if (t.contains('text')) return 'text';
  if (t.contains('image') || t.contains('photo') || t.contains('bitmap')) {
    return 'image';
  }
  if (t.contains('video') || t.contains('media')) return 'video';
  if (t.contains('group') || t.contains('precomp') || t.contains('comp')) {
    return 'group';
  }
  if (t.contains('null')) return 'null';
  if (t.contains('solid') ||
      t.contains('shape') ||
      t.contains('rect') ||
      t.contains('ellipse') ||
      t.contains('circle') ||
      t.contains('polygon') ||
      t.contains('star') ||
      t == 'layer') {
    return 'shape';
  }
  return 'shape';
}

(Layer, int)? _lerCamada(
  XmlNode e,
  double largura,
  double altura,
  bool origemCentro,
  Offset centro,
  Duration? duracaoCena,
  int fps,
  List<String> ignorados,
) {
  final tipo = _tipo(e);
  final nome = e.attr(['name', 'label', 'title']) ?? _nomePadrao(tipo);
  final inicio = _tempo(e.attr(['startTime', 'start', 'inTime', 'in', 'offset'])) ??
      Duration.zero;
  var fim = _tempo(e.attr(['endTime', 'end', 'outTime', 'out']));
  final dur = _tempo(e.attr(['duration', 'length']));
  if (fim == null && dur != null) fim = inicio + dur;
  fim ??= duracaoCena ?? const Duration(seconds: 5);
  var duracao = fim - inicio;
  if (duracao <= Duration.zero) duracao = const Duration(seconds: 5);

  var kf = 0;
  Offset paraComp(Offset p) => origemCentro ? centro + p : p;

  // TRANSFORM
  final posBase = _posicao(e) ?? (origemCentro ? Offset.zero : centro);
  final posKfs = _keyframesOffset(e, const ['position', 'pos', 'translate']);
  kf += posKfs.length;
  final position = AnimatedOffset(paraComp(posBase), [
    for (final k in posKfs) Keyframe(time: k.time, value: paraComp(k.value), ease: k.ease),
  ]);
  final escalaBase = _num(e.attr(['scale', 'scaleX'])) ?? _numDoFilho(e, 'scale') ?? 1;
  final escalaY = _num(e.attr(['scaleY'])) ?? escalaBase;
  final escKfs = _keyframesDouble(e, const ['scale', 'scaleX']);
  kf += escKfs.length;
  final rotBase = _num(e.attr(['rotation', 'angle', 'rotate'])) ?? _numDoFilho(e, 'rotation') ?? 0;
  final rotKfs = _keyframesDouble(e, const ['rotation', 'angle']);
  kf += rotKfs.length;
  var opBase = _num(e.attr(['opacity', 'alpha'])) ?? _numDoFilho(e, 'opacity') ?? 1;
  if (opBase > 1) opBase /= 100;
  final opKfs = _keyframesDouble(e, const ['opacity', 'alpha']);
  kf += opKfs.length;

  AnimatedDouble ad(double base, List<Keyframe<double>> ks, {bool pct = false}) =>
      AnimatedDouble(base, [
        for (final k in ks)
          Keyframe(
              time: k.time,
              value: pct && k.value > 1 ? k.value / 100 : k.value,
              ease: k.ease),
      ]);

  final scaleX = ad(escalaBase, escKfs);
  final scaleY = ad(escalaY, escKfs);
  final rotation = ad(rotBase, rotKfs);
  final opacity = ad(opBase.clamp(0.0, 1.0), opKfs, pct: true);
  final efeitos = _efeitos(e, ignorados);

  switch (tipo) {
    case 'text':
      final texto = e.attr(['text', 'value', 'content', 'string']) ??
          _textoDoFilho(e) ??
          'Texto';
      final tamanho = _num(e.attr(['fontSize', 'size', 'textSize'])) ?? 120;
      return (
        TextLayer(
          name: nome,
          startTime: inicio,
          duration: duracao,
          text: texto,
          fontSize: tamanho.clamp(8, 800).toDouble(),
          color: _cor(e.attr(['color', 'fill', 'fillColor', 'textColor'])) ??
              const Color(0xFFFFFFFF),
          position: position,
          scaleX: scaleX,
          scaleY: scaleY,
          rotation: rotation,
          opacity: opacity,
          effects: efeitos,
        ),
        kf
      );
    case 'image':
    case 'video':
      final src = e.attr(['src', 'path', 'file', 'uri', 'source', 'media']);
      ignorados.add(
          '${tipo == 'image' ? 'imagem' : 'video'} "$nome": arquivo${src == null ? '' : ' $src'} nao esta neste aparelho');
      return null;
    case 'group':
      final filhos = <Layer>[];
      var kfs = 0;
      for (final c in e.descendants()) {
        if (!_ehCamada(c)) continue;
        // So filhos DIRETOS de camada (netos vao pelo grupo deles).
        var p = c.parent;
        var aninhado = false;
        while (p != null && p != e) {
          if (_ehCamada(p) && _tipo(p) == 'group') aninhado = true;
          p = p.parent;
        }
        if (aninhado) continue;
        final r = _lerCamada(c, largura, altura, origemCentro, centro,
            duracaoCena, fps, ignorados);
        if (r != null) {
          filhos.add(r.$1);
          kfs += r.$2;
        }
      }
      if (filhos.isEmpty) {
        ignorados.add('grupo "$nome" vazio');
        return null;
      }
      return (
        GroupLayer(
          name: nome,
          startTime: inicio,
          duration: duracao,
          children: filhos,
          position: position,
          scaleX: scaleX,
          scaleY: scaleY,
          rotation: rotation,
          opacity: opacity,
          effects: efeitos,
        ),
        kf + kfs
      );
    case 'null':
      return (
        NullLayer(
          name: nome,
          startTime: inicio,
          duration: duracao,
          position: position,
          scaleX: scaleX,
          scaleY: scaleY,
          rotation: rotation,
          opacity: opacity,
        ),
        kf
      );
    default:
      final forma = _forma(e, largura, altura);
      final cor = _cor(e.attr(['color', 'fill', 'fillColor', 'backgroundColor'])) ??
          const Color(0xFF7C62FF);
      return (
        ShapeLayer(
          name: nome,
          startTime: inicio,
          duration: duracao,
          contents: [forma, ShapeFill(color: cor)],
          position: position,
          scaleX: scaleX,
          scaleY: scaleY,
          rotation: rotation,
          opacity: opacity,
          effects: efeitos,
        ),
        kf
      );
  }
}

String _nomePadrao(String tipo) => switch (tipo) {
      'text' => 'Texto',
      'image' => 'Imagem',
      'video' => 'Video',
      'group' => 'Grupo',
      'null' => 'Nulo',
      _ => 'Forma',
    };

ShapeParametric _forma(XmlNode e, double largura, double altura) {
  final t = ((e.attr(['shape', 'shapeType', 'type']) ?? e.tag)).toLowerCase();
  final w = _num(e.attr(['width', 'w', 'sizeX'])) ??
      _num(e.attr(['size', 'radius']))?.let((r) => t.contains('circ') ? r * 2 : r) ??
      (t.contains('solid') ? largura : 300);
  final h = _num(e.attr(['height', 'h', 'sizeY'])) ??
      (t.contains('solid') ? altura : w);
  final raio = _num(e.attr(['cornerRadius', 'radius', 'roundness', 'corner'])) ?? 0;
  ParamShapeKind kind;
  if (t.contains('ellipse') || t.contains('circ') || t.contains('oval')) {
    kind = ParamShapeKind.ellipse;
  } else if (t.contains('star')) {
    kind = ParamShapeKind.star;
  } else if (t.contains('poly') || t.contains('tri') || t.contains('hex')) {
    kind = ParamShapeKind.polygon;
  } else {
    kind = ParamShapeKind.rect;
  }
  final lados = _num(e.attr(['sides', 'points', 'corners']));
  return ShapeParametric(
    kind: kind,
    sizeX: AnimatedDouble(w),
    sizeY: AnimatedDouble(h),
    roundness: AnimatedDouble(kind == ParamShapeKind.rect && t.contains('circ') ? 0 : raio),
    roundnessPercent: false,
    points: lados == null ? null : AnimatedDouble(lados.clamp(3, 24)),
    outerRadius: AnimatedDouble(w / 2),
    innerRadius: AnimatedDouble(w / 4),
  );
}

extension<T> on T {
  R let<R>(R Function(T) f) => f(this);
}

List<EffectInstance> _efeitos(XmlNode e, List<String> ignorados) {
  final out = <EffectInstance>[];
  for (final f in e.descendants()) {
    if (f.tag != 'effect' && f.tag != 'filter' && f.tag != 'fx') continue;
    // Efeito de camada-filha (grupo) fica com o filho.
    var p = f.parent;
    var deOutra = false;
    while (p != null && p != e) {
      if (_ehCamada(p)) deOutra = true;
      p = p.parent;
    }
    if (deOutra) continue;
    final nome = (f.attr(['id', 'name', 'type', 'effect']) ?? '').toLowerCase();
    if (nome.isEmpty) continue;
    final tipo = _efeitoPorNome(nome);
    if (tipo == null) {
      ignorados.add('efeito "$nome" sem equivalente');
      continue;
    }
    var inst = EffectInstance(type: tipo);
    // Parametros por nome (chave ou rotulo iguais).
    final spec = effectSpecs[tipo]!;
    void aplica(String chave, String? valor) {
      final v = _num(valor);
      if (v == null) return;
      for (final p in spec.params.entries) {
        if (p.key.toLowerCase() == chave.toLowerCase() ||
            p.value.label.toLowerCase() == chave.toLowerCase()) {
          inst = inst.withParamEdited(
              p.key, Duration.zero, v.clamp(p.value.min, p.value.max));
        }
      }
    }
    for (final a in f.attrs.entries) {
      aplica(a.key, a.value);
    }
    for (final c in f.children) {
      final chave = c.attr(['name', 'id', 'key']);
      final valor = c.attr(['value', 'v']) ?? c.text.toString().trim();
      if (chave != null) aplica(chave, valor);
    }
    out.add(inst);
  }
  return out;
}

EffectType? _efeitoPorNome(String nome) {
  final n = nome.replaceAll(RegExp(r'[^a-z0-9]'), '');
  if (n.isEmpty) return null;
  // 1) nome igual a id, nome ou sinonimo; 2) o nome do XML contem o
  // candidato (o mais longo ganha); 3) o candidato contem o nome.
  EffectType? contido;
  var contidoTam = 0;
  EffectType? contem;
  var contemTam = 1 << 20;
  for (final s in effectSpecs.entries) {
    final candidatos = [s.value.id, s.value.name, ...s.value.synonyms];
    for (final c in candidatos) {
      final cc = c.toLowerCase().replaceAll(RegExp(r'[^a-z0-9]'), '');
      if (cc.length < 3) continue;
      if (n == cc) return s.key;
      if (n.contains(cc) && cc.length > contidoTam) {
        contido = s.key;
        contidoTam = cc.length;
      } else if (n.length >= 4 && cc.contains(n) && cc.length < contemTam) {
        contem = s.key;
        contemTam = cc.length;
      }
    }
  }
  return contido ?? contem;
}

// ------------------------------------------------------------- valores

double? _num(String? s) {
  if (s == null) return null;
  final t = s.trim().replaceAll('%', '').replaceAll('px', '').replaceAll(',', '.');
  return double.tryParse(t);
}

/// Tempo em ms (inteiros grandes) ou segundos (numeros pequenos).
Duration? _tempo(String? s) {
  final v = _num(s);
  if (v == null) return null;
  if (v >= 100 || s!.contains(RegExp(r'\d{3,}'))) {
    return Duration(microseconds: (v * 1000).round());
  }
  return Duration(microseconds: (v * 1e6).round());
}

Offset? _posicao(XmlNode e) {
  final p = e.attr(['position', 'pos', 'translate', 'center']);
  if (p != null) {
    final partes = p.split(RegExp(r'[,\s;]+')).where((x) => x.isNotEmpty).toList();
    if (partes.length >= 2) {
      final x = _num(partes[0]), y = _num(partes[1]);
      if (x != null && y != null) return Offset(x, y);
    }
  }
  final x = _num(e.attr(['x', 'posX', 'positionX', 'left']));
  final y = _num(e.attr(['y', 'posY', 'positionY', 'top']));
  if (x != null || y != null) return Offset(x ?? 0, y ?? 0);
  for (final c in e.children) {
    if (c.tag == 'transform' || c.tag == 'position') {
      final r = _posicao(c);
      if (r != null) return r;
      final cx = _num(c.attr(['x'])), cy = _num(c.attr(['y']));
      if (cx != null || cy != null) return Offset(cx ?? 0, cy ?? 0);
    }
  }
  return null;
}

double? _numDoFilho(XmlNode e, String nome) {
  for (final c in e.children) {
    if (c.tag == 'transform') {
      final v = _num(c.attr([nome]));
      if (v != null) return v;
    }
    if (c.tag == nome || (c.attr(['name', 'id']) ?? '').toLowerCase() == nome) {
      final v = _num(c.attr(['value', 'v', 'base'])) ?? _num(c.text.toString());
      if (v != null) return v;
    }
  }
  return null;
}

String? _textoDoFilho(XmlNode e) {
  for (final c in e.children) {
    if (c.tag == 'text' || c.tag == 'string' || c.tag == 'value') {
      final t = c.attr(['value']) ?? c.text.toString().trim();
      if (t.isNotEmpty) return t;
    }
  }
  final t = e.text.toString().trim();
  return t.isEmpty ? null : t;
}

/// Os nos que guardam keyframes de uma propriedade: `<property name=..>`,
/// `<keyframes property=..>`, `<position>`, `<animation for=..>` etc.
Iterable<XmlNode> _nosDaPropriedade(XmlNode e, List<String> nomes) sync* {
  for (final c in e.descendants()) {
    // Nao entra em camadas filhas.
    var p = c.parent;
    var deOutra = false;
    while (p != null && p != e) {
      if (_ehCamada(p)) deOutra = true;
      p = p.parent;
    }
    if (deOutra) continue;
    final rotulo = (c.attr(['name', 'property', 'for', 'id', 'target']) ?? c.tag)
        .toLowerCase();
    if (nomes.any((n) => rotulo == n.toLowerCase())) yield c;
  }
}

Iterable<XmlNode> _keyframesDe(XmlNode prop) sync* {
  for (final k in prop.descendants()) {
    if (k.tag == 'keyframe' || k.tag == 'kf' || k.tag == 'key') yield k;
  }
}

Easing _curva(XmlNode k) {
  final e = (k.attr(['easing', 'ease', 'interpolation', 'curve']) ?? '').toLowerCase();
  if (e.contains('inout') || e.contains('in_out') || e.contains('ease-in-out')) {
    return Easing.easeInOut;
  }
  if (e.contains('overshoot') || e.contains('back')) return Easing.overshoot;
  if (e.contains('out')) return Easing.easeOut;
  if (e.contains('in')) return Easing.easeIn;
  return Easing.linear;
}

List<Keyframe<double>> _keyframesDouble(XmlNode e, List<String> nomes) {
  final out = <Keyframe<double>>[];
  for (final prop in _nosDaPropriedade(e, nomes)) {
    for (final k in _keyframesDe(prop)) {
      final t = _tempo(k.attr(['time', 't', 'at', 'frame']));
      final v = _num(k.attr(['value', 'v'])) ?? _num(k.text.toString());
      if (t == null || v == null) continue;
      out.add(Keyframe(time: t, value: v, ease: _curva(k)));
    }
  }
  out.sort((a, b) => a.time.compareTo(b.time));
  return out;
}

List<Keyframe<Offset>> _keyframesOffset(XmlNode e, List<String> nomes) {
  final out = <Keyframe<Offset>>[];
  for (final prop in _nosDaPropriedade(e, nomes)) {
    for (final k in _keyframesDe(prop)) {
      final t = _tempo(k.attr(['time', 't', 'at', 'frame']));
      Offset? v;
      final raw = k.attr(['value', 'v']);
      if (raw != null) {
        final partes = raw.split(RegExp(r'[,\s;]+')).where((x) => x.isNotEmpty).toList();
        if (partes.length >= 2) {
          final x = _num(partes[0]), y = _num(partes[1]);
          if (x != null && y != null) v = Offset(x, y);
        }
      }
      v ??= () {
        final x = _num(k.attr(['x'])), y = _num(k.attr(['y']));
        return x == null && y == null ? null : Offset(x ?? 0, y ?? 0);
      }();
      if (t == null || v == null) continue;
      out.add(Keyframe(time: t, value: v, ease: _curva(k)));
    }
  }
  out.sort((a, b) => a.time.compareTo(b.time));
  return out;
}

Color? _cor(String? s) {
  if (s == null) return null;
  var t = s.trim();
  if (t.startsWith('#')) t = t.substring(1);
  if (t.toLowerCase().startsWith('0x')) t = t.substring(2);
  if (RegExp(r'^[0-9a-fA-F]{6}$').hasMatch(t)) {
    return Color(0xFF000000 | int.parse(t, radix: 16));
  }
  if (RegExp(r'^[0-9a-fA-F]{8}$').hasMatch(t)) {
    return Color(int.parse(t, radix: 16));
  }
  final partes = t.split(RegExp(r'[,\s]+')).where((x) => x.isNotEmpty).toList();
  if (partes.length >= 3) {
    final c = [for (final p in partes) _num(p)];
    if (c.take(3).any((x) => x == null)) return null;
    final escala = c.take(3).every((x) => x! <= 1.0) ? 255.0 : 1.0;
    int ch(double? x) => (x! * escala).round().clamp(0, 255);
    final a = partes.length >= 4 && c[3] != null
        ? (c[3]! <= 1.0 ? (c[3]! * 255).round() : c[3]!.round()).clamp(0, 255)
        : 255;
    return Color.fromARGB(a, ch(c[0]), ch(c[1]), ch(c[2]));
  }
  return null;
}
