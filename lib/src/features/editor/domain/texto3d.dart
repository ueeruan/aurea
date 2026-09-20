import 'dart:math' as math;
import 'dart:typed_data';

import 'package:characters/characters.dart';

import 'fonte_truetype.dart';

/// TEXTO 3D, O DO ELEMENT 3D: letra extrudada, com chanfro e tres
/// materiais (frente, chanfro e lateral).
///
/// A extrusao de forma que ja existia (`extrudeOutline`) nao serve para
/// letra: pega so o maior contorno (o "O" perderia o furo), corta orelha
/// sem furo, nao tem chanfro, nem normal, nem UV. Um texto de metal
/// realista e justamente o chanfro pegando a luz — sem ele, ouro vira
/// plastico amarelo.
///
/// Este arquivo e a GEOMETRIA, em Dart puro (sem `dart:ui`): o que vira
/// no da cena mora em `texto3d_na_cena.dart`.

/// Como a borda da letra e cortada.
enum TipoDeChanfro { nenhum, angular, redondo }

/// Quanto a curva da letra e respeitada. Mais qualidade, mais triangulo.
enum QualidadeDoTexto3D { baixa, media, alta }

/// OS PARAMETROS DE UM TEXTO 3D.
///
/// Imutavel e com IGUALDADE POR VALOR: a malha so e refeita quando um
/// destes numeros muda. Comparar por identidade refaria a malha a cada
/// reconstrucao do no — e na GPU cada malha refeita e memoria que o motor
/// nao devolve.
///
/// Medidas em unidades da cena: [tamanho] e a altura do "em" da fonte,
/// [espessura] a profundidade da extrusao, [larguraDoChanfro] quanto a
/// borda entra na letra. [espacamento] e fracao do "em", somada ao
/// avanco de cada letra (o "tracking").
class Texto3D {
  const Texto3D({
    this.texto = 'TEXTO 3D',
    this.familia = 'Aurea Motion Sans',
    this.tamanho = 100,
    this.espessura = 24,
    this.chanfro = TipoDeChanfro.redondo,
    this.larguraDoChanfro = 3,
    this.segmentosDoChanfro = 3,
    this.espacamento = 0,
    this.qualidade = QualidadeDoTexto3D.media,
    this.separarLetras = false,
    this.rotLetraX = 0,
    this.rotLetraY = 0,
    this.rotLetraZ = 0,
  });

  final String texto;
  final String familia;
  final double tamanho;
  final double espessura;
  final TipoDeChanfro chanfro;
  final double larguraDoChanfro;
  final int segmentosDoChanfro;
  final double espacamento;
  final QualidadeDoTexto3D qualidade;
  final bool separarLetras;

  /// A ROTACAO DE CADA LETRA, em graus, EM TORNO DO CENTRO DELA — o
  /// "Per-character 3D" do After Effects. Nao e o giro do texto inteiro (esse
  /// e o da camada): aqui cada letra gira no proprio lugar, e e o que faz uma
  /// palavra virar uma fileira de placas ou de dominos.
  ///
  /// NAO MUDA A MALHA: a geometria da letra e a mesma, quem muda e a matriz
  /// do no dela. Por isso fica fora do [soGeometria] e mexer aqui nao refaz
  /// extrusao nem chanfro.
  final double rotLetraX;
  final double rotLetraY;
  final double rotLetraZ;

  bool get temRotacaoPorLetra =>
      rotLetraX != 0 || rotLetraY != 0 || rotLetraZ != 0;

  /// Quantos aneis o perfil do chanfro tem. Angular e um corte so;
  /// redondo precisa de tres ou quatro para a luz correr sem degrau.
  int get segmentosEfetivos => switch (chanfro) {
    TipoDeChanfro.nenhum => 0,
    TipoDeChanfro.angular => 1,
    TipoDeChanfro.redondo => segmentosDoChanfro.clamp(2, 6),
  };

  /// Erro maximo da curva, em fracao do "em".
  double get toleranciaEmEm => switch (qualidade) {
    QualidadeDoTexto3D.baixa => 0.012,
    QualidadeDoTexto3D.media => 0.004,
    QualidadeDoTexto3D.alta => 0.0015,
  };

  /// So o que muda a malha de UMA letra: o texto, a familia (que ja
  /// chega como contorno), o espacamento e a separacao ficam de fora.
  Texto3D get soGeometria => Texto3D(
    texto: '',
    familia: '',
    tamanho: tamanho,
    espessura: espessura,
    chanfro: chanfro,
    larguraDoChanfro: larguraDoChanfro,
    segmentosDoChanfro: segmentosDoChanfro,
    qualidade: qualidade,
  );

  Texto3D copyWith({
    String? texto,
    String? familia,
    double? tamanho,
    double? espessura,
    TipoDeChanfro? chanfro,
    double? larguraDoChanfro,
    int? segmentosDoChanfro,
    double? espacamento,
    QualidadeDoTexto3D? qualidade,
    bool? separarLetras,
    double? rotLetraX,
    double? rotLetraY,
    double? rotLetraZ,
  }) => Texto3D(
    texto: texto ?? this.texto,
    familia: familia ?? this.familia,
    tamanho: tamanho ?? this.tamanho,
    espessura: espessura ?? this.espessura,
    chanfro: chanfro ?? this.chanfro,
    larguraDoChanfro: larguraDoChanfro ?? this.larguraDoChanfro,
    segmentosDoChanfro: segmentosDoChanfro ?? this.segmentosDoChanfro,
    espacamento: espacamento ?? this.espacamento,
    qualidade: qualidade ?? this.qualidade,
    separarLetras: separarLetras ?? this.separarLetras,
    rotLetraX: rotLetraX ?? this.rotLetraX,
    rotLetraY: rotLetraY ?? this.rotLetraY,
    rotLetraZ: rotLetraZ ?? this.rotLetraZ,
  );

  @override
  bool operator ==(Object other) =>
      identical(this, other) ||
      other is Texto3D &&
          other.texto == texto &&
          other.familia == familia &&
          other.tamanho == tamanho &&
          other.espessura == espessura &&
          other.chanfro == chanfro &&
          other.larguraDoChanfro == larguraDoChanfro &&
          other.segmentosDoChanfro == segmentosDoChanfro &&
          other.espacamento == espacamento &&
          other.qualidade == qualidade &&
          other.separarLetras == separarLetras &&
          other.rotLetraX == rotLetraX &&
          other.rotLetraY == rotLetraY &&
          other.rotLetraZ == rotLetraZ;

  @override
  int get hashCode => Object.hash(
    texto,
    familia,
    tamanho,
    espessura,
    chanfro,
    larguraDoChanfro,
    segmentosDoChanfro,
    espacamento,
    qualidade,
    separarLetras,
    rotLetraX,
    rotLetraY,
    rotLetraZ,
  );

  Map<String, Object> toJson() => {
    't': texto,
    'f': familia,
    'tam': tamanho,
    'esp': espessura,
    'ch': chanfro.name,
    'chL': larguraDoChanfro,
    'chS': segmentosDoChanfro,
    'sp': espacamento,
    'q': qualidade.name,
    'sep': separarLetras,
  };

  /// Leitura TOLERANTE: campo ausente ou estranho volta ao padrao, e um
  /// numero absurdo e trazido para dentro da faixa — um projeto editado
  /// a mao nao pode gerar uma malha de um milhao de triangulos.
  factory Texto3D.fromJson(Map<dynamic, dynamic> m) {
    double numero(String k, double padrao, double lo, double hi) {
      final v = m[k];
      if (v is! num || !v.isFinite) return padrao;
      return v.toDouble().clamp(lo, hi);
    }

    T escolha<T extends Enum>(Object? v, List<T> valores, T padrao) {
      for (final e in valores) {
        if (e.name == v) return e;
      }
      return padrao;
    }

    const p = Texto3D();
    return Texto3D(
      texto: m['t'] is String ? m['t'] as String : p.texto,
      familia: m['f'] is String ? m['f'] as String : p.familia,
      tamanho: numero('tam', p.tamanho, 1, 100000),
      espessura: numero('esp', p.espessura, 0, 100000),
      chanfro: escolha(m['ch'], TipoDeChanfro.values, p.chanfro),
      larguraDoChanfro: numero('chL', p.larguraDoChanfro, 0, 100000),
      segmentosDoChanfro: numero('chS', 3, 1, 6).round(),
      espacamento: numero('sp', 0, -1, 4),
      qualidade: escolha(m['q'], QualidadeDoTexto3D.values, p.qualidade),
      separarLetras: m['sep'] == true,
    );
  }
}

// ------------------------------------------------------------ layout

/// Uma letra no lugar: onde a origem do glifo (a linha de base) cai, ja
/// com o texto centrado na origem. Unidades da cena, Y para cima.
class LetraDoTexto3D {
  const LetraDoTexto3D({
    required this.indice,
    required this.codigo,
    required this.x,
    required this.y,
    required this.glifo,
    this.unidade = -1,
  });

  /// A posicao entre as letras que DESENHAM (o espaco nao conta).
  final int indice;
  final int codigo;
  final double x;
  final double y;
  final GlifoDaFonte glifo;

  /// O INDICE DA UNIDADE DE TEXTO (grapheme) de onde esta letra veio, no
  /// texto limpo ('\r' fora) — a mesma contagem do motor de animadores de
  /// texto ([TextUnits]). E por ele que "aparecer letra por letra" sabe a
  /// vez de cada letra extrudada. -1 = layout antigo, sem mapa.
  final int unidade;
}

class DisposicaoDoTexto3D {
  const DisposicaoDoTexto3D({
    required this.letras,
    required this.largura,
    required this.altura,
    required this.faltando,
  });

  final List<LetraDoTexto3D> letras;
  final double largura;
  final double altura;

  /// Caracteres que a fonte nao tem (viram um espaco de meio "em").
  final Set<int> faltando;
}

/// A CAIXA DE ONDE A LETRA DESENHA, em unidades da fonte.
({double minX, double minY, double maxX, double maxY})? _caixaDoGlifo(
  GlifoDaFonte g,
  double tolerancia,
) {
  var minX = double.infinity, minY = double.infinity;
  var maxX = -double.infinity, maxY = -double.infinity;
  for (final c in g.contornos) {
    final p = c.planificar(tolerancia);
    for (var i = 0; i + 1 < p.length; i += 2) {
      minX = math.min(minX, p[i]);
      maxX = math.max(maxX, p[i]);
      minY = math.min(minY, p[i + 1]);
      maxY = math.max(maxY, p[i + 1]);
    }
  }
  return minX.isFinite
      ? (minX: minX, minY: minY, maxX: maxX, maxY: maxY)
      : null;
}

/// ONDE CADA LETRA FICA.
///
/// Avanco da fonte, kerning do par e o espacamento pedido, linha por
/// linha ("\n" quebra), cada linha centrada. O bloco inteiro e centrado
/// pela CAIXA DO QUE DESENHA, e nao pela metrica da fonte: e esse centro
/// que vira o pivo do nulo, e girar "MOTION" tem de girar em volta do
/// meio das letras, nao de um ponto acima delas.
DisposicaoDoTexto3D disporTexto3D(Texto3D t, FonteDeGlifos fonte) {
  if (t.tamanho <= 0 || fonte.unidadesPorEm <= 0) {
    return const DisposicaoDoTexto3D(
      letras: [],
      largura: 0,
      altura: 0,
      faltando: {},
    );
  }
  final upem = fonte.unidadesPorEm;
  final escala = t.tamanho / upem;
  final tolerancia = t.toleranciaEmEm * upem;
  final alturaDaLinha =
      (fonte.ascendente - fonte.descendente + fonte.entreLinhas) * escala;
  final faltando = <int>{};
  final brutas = <({int codigo, int u, double x, double y, GlifoDaFonte g})>[];
  var minX = double.infinity, minY = double.infinity;
  var maxX = -double.infinity, maxY = -double.infinity;
  final limpo = t.texto.replaceAll('\r', '');
  // A UNIDADE (grapheme) DE CADA RUNE do texto limpo: e o que liga cada
  // letra extrudada a contagem do motor de animadores de texto. Um
  // grapheme de varios runes aponta todos para a mesma unidade.
  final unidadeDoRune = <int>[];
  var grapheme = 0;
  for (final cluster in limpo.characters) {
    for (final _ in cluster.runes) {
      unidadeDoRune.add(grapheme);
    }
    grapheme++;
  }
  final linhas = limpo.split('\n');
  var cursor = 0; // rune corrente dentro de `limpo`
  for (var l = 0; l < linhas.length; l++) {
    if (l > 0) cursor++; // o '\n' que separa esta linha da anterior
    final daLinha =
        <({int codigo, int u, double x, double y, GlifoDaFonte g})>[];
    var caneta = 0.0;
    int? anterior;
    for (final codigo in linhas[l].runes) {
      final unidade = unidadeDoRune[cursor];
      cursor++;
      final g = fonte.glifoDoCaractere(codigo);
      if (g == null) {
        faltando.add(codigo);
        caneta += upem * 0.5 + t.espacamento * upem;
        anterior = null;
        continue;
      }
      if (anterior != null) caneta += fonte.kerningEntre(anterior, codigo);
      if (g.contornos.isNotEmpty) {
        daLinha.add((codigo: codigo, u: unidade, x: caneta, y: 0, g: g));
      }
      caneta += g.avanco + t.espacamento * upem;
      anterior = codigo;
    }
    // A linha e centrada pelo que DESENHA, sem o espaco do fim.
    var loX = double.infinity, hiX = -double.infinity;
    for (final b in daLinha) {
      final caixa = _caixaDoGlifo(b.g, tolerancia);
      if (caixa == null) continue;
      loX = math.min(loX, b.x + caixa.minX);
      hiX = math.max(hiX, b.x + caixa.maxX);
    }
    if (!loX.isFinite) continue;
    final desvio = -(loX + hiX) / 2;
    final base = -l * alturaDaLinha / escala;
    for (final b in daLinha) {
      final caixa = _caixaDoGlifo(b.g, tolerancia)!;
      final x = b.x + desvio, y = base;
      minX = math.min(minX, x + caixa.minX);
      maxX = math.max(maxX, x + caixa.maxX);
      minY = math.min(minY, y + caixa.minY);
      maxY = math.max(maxY, y + caixa.maxY);
      brutas.add((codigo: b.codigo, u: b.u, x: x, y: y, g: b.g));
    }
  }
  if (brutas.isEmpty) {
    return DisposicaoDoTexto3D(
      letras: const [],
      largura: 0,
      altura: 0,
      faltando: faltando,
    );
  }
  final cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
  return DisposicaoDoTexto3D(
    letras: [
      for (var i = 0; i < brutas.length; i++)
        LetraDoTexto3D(
          indice: i,
          codigo: brutas[i].codigo,
          unidade: brutas[i].u,
          x: (brutas[i].x - cx) * escala,
          y: (brutas[i].y - cy) * escala,
          glifo: brutas[i].g,
        ),
    ],
    largura: (maxX - minX) * escala,
    altura: (maxY - minY) * escala,
    faltando: faltando,
  );
}

// ------------------------------------------------------------- malha

/// As tres partes do texto, cada uma com o seu material.
enum ParteDoTexto3D { frente, chanfro, lateral }

/// Uma parte da malha: vertices com normal e UV, e dois jogos de indices
/// sobre os MESMOS vertices — o cheio e o rascunho sem chanfro (LOD).
class PrimitivaDoTexto3D {
  const PrimitivaDoTexto3D({
    required this.posicoes,
    required this.normais,
    required this.uvs,
    required this.indices,
    required this.indicesDoRascunho,
  });

  final Float64List posicoes;
  final Float64List normais;
  final Float64List uvs;
  final Uint32List indices;
  final Uint32List indicesDoRascunho;

  int get vertices => posicoes.length ~/ 3;
  int get triangulos => indices.length ~/ 3;
  int get triangulosDoRascunho => indicesDoRascunho.length ~/ 3;
}

class MalhaDoTexto3D {
  const MalhaDoTexto3D({
    required this.partes,
    required this.minX,
    required this.minY,
    required this.minZ,
    required this.maxX,
    required this.maxY,
    required this.maxZ,
  });

  final Map<ParteDoTexto3D, PrimitivaDoTexto3D> partes;
  final double minX, minY, minZ, maxX, maxY, maxZ;

  bool get vazia => triangulos == 0;

  int get triangulos => partes.values.fold(0, (s, p) => s + p.triangulos);

  int get triangulosDoRascunho =>
      partes.values.fold(0, (s, p) => s + p.triangulosDoRascunho);

  double get centroX => (minX + maxX) / 2;
  double get centroY => (minY + maxY) / 2;
  double get centroZ => (minZ + maxZ) / 2;

  /// A MAIOR meia-extensao: e o `size` do no, porque o modelo importado
  /// e normalizado pela caixa (meia-extensao 1) antes de ir para a cena.
  double get meiaExtensao =>
      math.max(maxX - minX, math.max(maxY - minY, maxZ - minZ)) / 2;
}

/// O CACHE DAS LETRAS, por valor.
///
/// "ELEMENT" tem tres "E": com a mesma fonte e os mesmos parametros, o
/// contorno e o mesmo, e a malha tambem — o lugar de cada um e do no.
/// Guardar por VALOR do contorno (e nao pelo objeto do glifo) faz o
/// projeto reaberto reencontrar a malha que ja existia.
final _letras = <_ChaveDaLetra, MalhaDoTexto3D>{};
const _letrasNoCache = 96;

/// A malha de UMA letra, com a origem do glifo em (0, 0) e o texto
/// extrudado em volta de z = 0.
MalhaDoTexto3D malhaDaLetra(
  GlifoDaFonte glifo,
  Texto3D t,
  double unidadesPorEm,
) {
  final chave = _ChaveDaLetra(glifo, t.soGeometria, unidadesPorEm);
  final guardada = _letras.remove(chave);
  if (guardada != null) {
    _letras[chave] = guardada;
    return guardada;
  }
  final malha = _MontadorDaLetra(glifo, t, unidadesPorEm).montar();
  _letras[chave] = malha;
  while (_letras.length > _letrasNoCache) {
    _letras.remove(_letras.keys.first);
  }
  return malha;
}

/// O TEXTO INTEIRO NUMA MALHA SO (o modo sem separar letras): cada letra
/// deslocada para o lugar dela.
MalhaDoTexto3D malhaDoTexto3D(
  DisposicaoDoTexto3D disposicao,
  Texto3D t,
  double unidadesPorEm,
) {
  final juntas = {for (final p in ParteDoTexto3D.values) p: _Buffer()};
  for (final letra in disposicao.letras) {
    final m = malhaDaLetra(letra.glifo, t, unidadesPorEm);
    for (final e in m.partes.entries) {
      juntas[e.key]!.anexar(e.value, letra.x, letra.y);
    }
  }
  return _fechar(juntas);
}

class _ChaveDaLetra {
  _ChaveDaLetra(this.glifo, this.geometria, this.upem)
    : _hash = Object.hash(geometria, upem, _hashDoGlifo(glifo));

  final GlifoDaFonte glifo;
  final Texto3D geometria;
  final double upem;
  final int _hash;

  static int _hashDoGlifo(GlifoDaFonte g) {
    var h = g.contornos.length;
    for (final c in g.contornos) {
      h = Object.hash(h, c.inicioX, c.inicioY, c.trechos.length);
      for (final t in c.trechos) {
        h = Object.hash(h, t.x, t.y, t.cx, t.cy);
      }
    }
    return h;
  }

  static bool _mesmoGlifo(GlifoDaFonte a, GlifoDaFonte b) {
    if (identical(a, b)) return true;
    if (a.contornos.length != b.contornos.length) return false;
    for (var i = 0; i < a.contornos.length; i++) {
      final ca = a.contornos[i], cb = b.contornos[i];
      if (ca.inicioX != cb.inicioX ||
          ca.inicioY != cb.inicioY ||
          ca.trechos.length != cb.trechos.length) {
        return false;
      }
      for (var j = 0; j < ca.trechos.length; j++) {
        final ta = ca.trechos[j], tb = cb.trechos[j];
        if (ta.x != tb.x || ta.y != tb.y || ta.cx != tb.cx || ta.cy != tb.cy) {
          return false;
        }
      }
    }
    return true;
  }

  @override
  bool operator ==(Object other) =>
      other is _ChaveDaLetra &&
      other._hash == _hash &&
      other.upem == upem &&
      other.geometria == geometria &&
      _mesmoGlifo(other.glifo, glifo);

  @override
  int get hashCode => _hash;
}

class _Buffer {
  final pos = <double>[];
  final nor = <double>[];
  final uv = <double>[];
  final idx = <int>[];
  final rascunho = <int>[];

  int vertice(
    double x,
    double y,
    double z,
    double nx,
    double ny,
    double nz,
    double u,
    double v,
  ) {
    final i = pos.length ~/ 3;
    pos
      ..add(x)
      ..add(y)
      ..add(z);
    nor
      ..add(nx)
      ..add(ny)
      ..add(nz);
    uv
      ..add(u)
      ..add(v);
    return i;
  }

  void triangulo(int a, int b, int c, bool doRascunho) =>
      (doRascunho ? rascunho : idx)
        ..add(a)
        ..add(b)
        ..add(c);

  void anexar(PrimitivaDoTexto3D p, double dx, double dy) {
    final base = pos.length ~/ 3;
    for (var i = 0; i < p.posicoes.length; i += 3) {
      pos
        ..add(p.posicoes[i] + dx)
        ..add(p.posicoes[i + 1] + dy)
        ..add(p.posicoes[i + 2]);
    }
    nor.addAll(p.normais);
    uv.addAll(p.uvs);
    for (final i in p.indices) {
      idx.add(base + i);
    }
    for (final i in p.indicesDoRascunho) {
      rascunho.add(base + i);
    }
  }
}

MalhaDoTexto3D _fechar(Map<ParteDoTexto3D, _Buffer> buffers) {
  var minX = double.infinity, minY = double.infinity, minZ = double.infinity;
  var maxX = -double.infinity, maxY = -double.infinity;
  var maxZ = -double.infinity;
  final partes = <ParteDoTexto3D, PrimitivaDoTexto3D>{};
  for (final e in buffers.entries) {
    final b = e.value;
    if (b.idx.isEmpty && b.rascunho.isEmpty) continue;
    for (var i = 0; i < b.pos.length; i += 3) {
      minX = math.min(minX, b.pos[i]);
      maxX = math.max(maxX, b.pos[i]);
      minY = math.min(minY, b.pos[i + 1]);
      maxY = math.max(maxY, b.pos[i + 1]);
      minZ = math.min(minZ, b.pos[i + 2]);
      maxZ = math.max(maxZ, b.pos[i + 2]);
    }
    partes[e.key] = PrimitivaDoTexto3D(
      posicoes: Float64List.fromList(b.pos),
      normais: Float64List.fromList(b.nor),
      uvs: Float64List.fromList(b.uv),
      indices: Uint32List.fromList(b.idx),
      indicesDoRascunho: Uint32List.fromList(b.rascunho),
    );
  }
  if (!minX.isFinite) {
    minX = minY = minZ = maxX = maxY = maxZ = 0;
  }
  return MalhaDoTexto3D(
    partes: partes,
    minX: minX,
    minY: minY,
    minZ: minZ,
    maxX: maxX,
    maxY: maxY,
    maxZ: maxZ,
  );
}

/// Uma ilha da letra: um contorno de fora e os furos dele. O "i" tem
/// duas ilhas (o corpo e o pingo); o "O", uma ilha com um furo.
class _Ilha {
  _Ilha(this.externo);
  final Float64List externo;
  final furos = <Float64List>[];
  List<Float64List> get contornos => [externo, ...furos];
}

/// O que cada vertice do contorno precisa para os aneis: o MITER (para
/// onde ele anda quando a borda recua), a normal lisa, as normais das
/// duas arestas vizinhas, se ali ha vinco, e o comprimento de arco.
class _Anel {
  _Anel(this.p) {
    final n = p.length ~/ 2;
    mx = Float64List(n);
    my = Float64List(n);
    lisaX = Float64List(n);
    lisaY = Float64List(n);
    antesX = Float64List(n);
    antesY = Float64List(n);
    depoisX = Float64List(n);
    depoisY = Float64List(n);
    vinco = List<bool>.filled(n, false);
    arco = Float64List(n);
    for (var i = 0; i < n; i++) {
      final a = (i - 1 + n) % n, b = (i + 1) % n;
      var e1x = p[2 * i] - p[2 * a], e1y = p[2 * i + 1] - p[2 * a + 1];
      var e2x = p[2 * b] - p[2 * i], e2y = p[2 * b + 1] - p[2 * i + 1];
      final l1 = math.max(1e-12, math.sqrt(e1x * e1x + e1y * e1y));
      final l2 = math.max(1e-12, math.sqrt(e2x * e2x + e2y * e2y));
      e1x /= l1;
      e1y /= l1;
      e2x /= l2;
      e2y /= l2;
      // Material a ESQUERDA de quem anda: a normal para fora e a direita.
      final n1x = e1y, n1y = -e1x, n2x = e2y, n2y = -e2x;
      antesX[i] = n1x;
      antesY[i] = n1y;
      depoisX[i] = n2x;
      depoisY[i] = n2y;
      final cosseno = n1x * n2x + n1y * n2y;
      var qx = n1x + n2x, qy = n1y + n2y;
      if (1 + cosseno < 1e-6) {
        // Espinho de 180 graus: sem bissetriz, anda pela normal de antes.
        qx = n1x * 4;
        qy = n1y * 4;
      } else {
        qx /= 1 + cosseno;
        qy /= 1 + cosseno;
      }
      // Canto muito agudo empurraria o vertice longe demais: o miter
      // e limitado (vira um canto um pouco cortado, nunca uma lanca).
      final comprimento = math.sqrt(qx * qx + qy * qy);
      if (comprimento > 4) {
        qx *= 4 / comprimento;
        qy *= 4 / comprimento;
      }
      mx[i] = qx;
      my[i] = qy;
      final sx = n1x + n2x, sy = n1y + n2y;
      final sl = math.sqrt(sx * sx + sy * sy);
      lisaX[i] = sl < 1e-9 ? n1x : sx / sl;
      lisaY[i] = sl < 1e-9 ? n1y : sy / sl;
      // VINCO acima de ~37 graus: ali a luz tem de quebrar, e a normal
      // media arredondaria a quina do "E" como se fosse um "O".
      vinco[i] = e1x * e2x + e1y * e2y < _cossenoDoVinco;
      if (i > 0) arco[i] = arco[i - 1] + l1;
    }
    final ultimo = n - 1;
    comprimento =
        arco[ultimo] +
        math.sqrt(
          math.pow(p[0] - p[2 * ultimo], 2) +
              math.pow(p[1] - p[2 * ultimo + 1], 2),
        );
  }

  final Float64List p;
  late final Float64List mx, my, lisaX, lisaY, antesX, antesY;
  late final Float64List depoisX, depoisY, arco;
  late final List<bool> vinco;
  late final double comprimento;

  int get n => p.length ~/ 2;
}

const _cossenoDoVinco = 0.7986; // cos(37 graus)

/// As COLUNAS de vertices ao longo do contorno: onde ha vinco, a mesma
/// posicao vira duas colunas (uma normal para cada aresta). O vertice 0
/// sempre vira duas, para o UV da lateral fechar a volta sem costura
/// (u = 0 de um lado, u = comprimento do outro).
class _Colunas {
  _Colunas(_Anel a) {
    for (var i = 0; i < a.n; i++) {
      if (i == 0 || a.vinco[i]) {
        final nx1 = a.vinco[i] ? a.antesX[i] : a.lisaX[i];
        final ny1 = a.vinco[i] ? a.antesY[i] : a.lisaY[i];
        final nx2 = a.vinco[i] ? a.depoisX[i] : a.lisaX[i];
        final ny2 = a.vinco[i] ? a.depoisY[i] : a.lisaY[i];
        entrada.add(_nova(i, nx1, ny1, i == 0 ? a.comprimento : a.arco[i]));
        saida.add(_nova(i, nx2, ny2, a.arco[i]));
      } else {
        final c = _nova(i, a.lisaX[i], a.lisaY[i], a.arco[i]);
        entrada.add(c);
        saida.add(c);
      }
    }
  }

  final entrada = <int>[];
  final saida = <int>[];
  final vertice = <int>[];
  final nx = <double>[];
  final ny = <double>[];
  final u = <double>[];

  int _nova(int v, double x, double y, double arco) {
    vertice.add(v);
    nx.add(x);
    ny.add(y);
    u.add(arco);
    return vertice.length - 1;
  }

  int get quantas => vertice.length;
}

class _MontadorDaLetra {
  _MontadorDaLetra(this.glifo, this.t, this.upem)
    : escala = t.tamanho / upem,
      h = math.max(0.0, t.espessura) / 2;

  final GlifoDaFonte glifo;
  final Texto3D t;
  final double upem;
  final double escala;
  final double h;
  final buffers = {for (final p in ParteDoTexto3D.values) p: _Buffer()};

  MalhaDoTexto3D montar() {
    if (glifo.contornos.isEmpty) return _fechar(buffers);
    final tolerancia = t.toleranciaEmEm * upem;
    final cheias = _ilhas(tolerancia);
    if (cheias.isEmpty) return _fechar(buffers);
    var segmentos = t.segmentosEfetivos;
    var recuo = 0.0;
    if (segmentos > 0 && t.larguraDoChanfro > 0 && h > 0) {
      recuo = _limiteDoRecuo(cheias, t.larguraDoChanfro);
      // Letra fina demais para o chanfro pedido: sem chanfro, e nao um
      // chanfro de largura zero, que so faria triangulo degenerado.
      if (recuo < t.larguraDoChanfro * 0.05) {
        segmentos = 0;
        recuo = 0;
      }
    } else {
      segmentos = 0;
    }
    for (final ilha in cheias) {
      _emitir(ilha, segmentos, recuo, doRascunho: false);
    }
    // O RASCUNHO (LOD): sem chanfro e com a curva mais grossa. E o que o
    // pintor de CPU usa quando a cena passa do que ele aguenta.
    for (final ilha in _ilhas(tolerancia * 3)) {
      _emitir(ilha, 0, 0, doRascunho: true);
    }
    return _fechar(buffers);
  }

  /// Contornos planificados, limpos, separados em ilhas com furos e
  /// orientados: de fora anti-horario, furo horario (Y para cima).
  List<_Ilha> _ilhas(double tolerancia) {
    final eps = math.max(1e-6 * t.tamanho, tolerancia * escala * 0.02);
    final poligonos = <Float64List>[];
    for (final c in glifo.contornos) {
      final p = c.planificar(tolerancia);
      for (var i = 0; i < p.length; i++) {
        p[i] *= escala;
      }
      final limpo = _limpar(p, eps);
      if (limpo != null) poligonos.add(limpo);
    }
    final n = poligonos.length;
    final areas = [for (final p in poligonos) _area(p)];
    // SEPARAR FURO POR ANINHAMENTO, e nao pelo sentido do contorno.
    //
    // A convencao TrueType e "de fora horario", a PostScript e o
    // contrario, e glifo composto espelhado inverte tudo. O que nao
    // mente e quem esta dentro de quem: profundidade par e contorno de
    // fora, impar e furo (o miolo do "O" dentro do "O").
    final contem = List.generate(n, (_) => List<bool>.filled(n, false));
    final profundidade = List<int>.filled(n, 0);
    for (var i = 0; i < n; i++) {
      for (var j = 0; j < n; j++) {
        if (i == j || areas[j].abs() <= areas[i].abs()) continue;
        if (_contido(poligonos[i], poligonos[j])) {
          contem[j][i] = true;
          profundidade[i]++;
        }
      }
    }
    final ilhas = <int, _Ilha>{};
    for (var i = 0; i < n; i++) {
      if (profundidade[i].isEven) {
        ilhas[i] = _Ilha(_orientar(poligonos[i], areas[i], true));
      }
    }
    for (var i = 0; i < n; i++) {
      if (profundidade[i].isOdd) {
        int? pai;
        for (var j = 0; j < n; j++) {
          if (contem[j][i] &&
              profundidade[j] == profundidade[i] - 1 &&
              (pai == null || areas[j].abs() < areas[pai].abs())) {
            pai = j;
          }
        }
        if (pai != null && ilhas[pai] != null) {
          ilhas[pai]!.furos.add(_orientar(poligonos[i], areas[i], false));
        }
      }
    }
    return ilhas.values.toList();
  }

  static Float64List _orientar(Float64List p, double area, bool externo) {
    if ((area > 0) == externo) return p;
    final n = p.length ~/ 2;
    final r = Float64List(p.length);
    for (var i = 0; i < n; i++) {
      r[2 * i] = p[2 * (n - 1 - i)];
      r[2 * i + 1] = p[2 * (n - 1 - i) + 1];
    }
    return r;
  }

  /// QUANTO A BORDA PODE ENTRAR sem virar a letra do avesso.
  ///
  /// Tres limites, e vale o menor:
  ///   - o pedido;
  ///   - cada vertice anda no maximo 45% da distancia ate a parede
  ///     oposta (medida na direcao em que ele anda) — fica sempre antes
  ///     do eixo do traco, onde o anel recuado se cruzaria;
  ///   - nenhuma aresta encolhe mais de 80% (aresta curta entre dois
  ///     cantos convexos some antes do resto).
  /// Um valor por letra, e nao por vertice: o chanfro sai uniforme na
  /// letra inteira, que e o que o olho espera de uma peca usinada.
  double _limiteDoRecuo(List<_Ilha> ilhas, double pedido) {
    final todos = [for (final i in ilhas) ...i.contornos];
    var d = pedido;
    for (var pi = 0; pi < todos.length; pi++) {
      final a = _Anel(todos[pi]);
      final n = a.n;
      for (var i = 0; i < n; i++) {
        final ml = math.sqrt(a.mx[i] * a.mx[i] + a.my[i] * a.my[i]);
        if (ml < 1e-9) continue;
        final dist = _raio(
          a.p[2 * i],
          a.p[2 * i + 1],
          -a.mx[i] / ml,
          -a.my[i] / ml,
          todos,
          pi,
          i,
        );
        if (dist.isFinite) d = math.min(d, 0.45 * dist / ml);
      }
      for (var i = 0; i < n; i++) {
        final j = (i + 1) % n;
        final ex = a.p[2 * j] - a.p[2 * i],
            ey = a.p[2 * j + 1] - a.p[2 * i + 1];
        final l = math.sqrt(ex * ex + ey * ey);
        if (l < 1e-12) continue;
        final den = ((a.mx[j] - a.mx[i]) * ex + (a.my[j] - a.my[i]) * ey) / l;
        if (den > 1e-9) d = math.min(d, 0.8 * l / den);
      }
    }
    return math.max(0.0, d);
  }

  /// Distancia ate a primeira parede na direcao (dx, dy), ignorando as
  /// duas arestas do proprio vertice.
  static double _raio(
    double ox,
    double oy,
    double dx,
    double dy,
    List<Float64List> todos,
    int pi,
    int vi,
  ) {
    var melhor = double.infinity;
    for (var qi = 0; qi < todos.length; qi++) {
      final q = todos[qi];
      final n = q.length ~/ 2;
      for (var j = 0; j < n; j++) {
        final k = (j + 1) % n;
        if (qi == pi && (j == vi || k == vi)) continue;
        final ax = q[2 * j], ay = q[2 * j + 1];
        final ex = q[2 * k] - ax, ey = q[2 * k + 1] - ay;
        final den = dx * ey - dy * ex;
        if (den.abs() < 1e-12) continue;
        final wx = ax - ox, wy = ay - oy;
        final tt = (wx * ey - wy * ex) / den;
        final s = (wx * dy - wy * dx) / den;
        if (tt > 1e-9 && s >= 0 && s <= 1 && tt < melhor) melhor = tt;
      }
    }
    return melhor;
  }

  /// Os aneis, a lateral, o chanfro e as tampas de uma ilha.
  void _emitir(
    _Ilha ilha,
    int segmentos,
    double recuo, {
    required bool doRascunho,
  }) {
    final frente = buffers[ParteDoTexto3D.frente]!;
    final chanfro = buffers[ParteDoTexto3D.chanfro]!;
    final lateral = buffers[ParteDoTexto3D.lateral]!;
    final s = segmentos;
    final d = s == 0 ? 0.0 : recuo;
    // A PROFUNDIDADE DO CHANFRO acompanha a largura (45 graus), mas nunca
    // passa de 45% da meia espessura: frente e tras nao podem se cruzar,
    // e sempre sobra uma lateral de pe.
    final bz = s == 0 ? 0.0 : math.min(d, 0.45 * h);
    final redondo = t.chanfro == TipoDeChanfro.redondo;
    double recuoDoAnel(int k) {
      if (s == 0) return 0;
      if (!redondo) return k == 0 ? 0 : d;
      return d * (1 - math.cos(k / s * math.pi / 2));
    }

    double zDoAnel(int k) {
      if (s == 0) return h;
      if (!redondo) return k == 0 ? h - bz : h;
      return h - bz + bz * math.sin(k / s * math.pi / 2);
    }

    // A normal do perfil no anel k, como (para fora, em z).
    (double, double) perfil(int k) {
      double pr, pz;
      if (!redondo) {
        pr = bz;
        pz = d;
      } else {
        final a = k / s * math.pi / 2;
        pr = bz * math.cos(a);
        pz = d * math.sin(a);
      }
      final l = math.sqrt(pr * pr + pz * pz);
      return l < 1e-12 ? (1.0, 0.0) : (pr / l, pz / l);
    }

    final tamanho = t.tamanho <= 0 ? 1.0 : t.tamanho;
    final aneis = [for (final c in ilha.contornos) _Anel(c)];
    final zTopo = h - bz;

    for (final a in aneis) {
      final col = _Colunas(a);
      final m = col.quantas;
      // LATERAL: do anel 0 da frente ao anel 0 de tras.
      final topo = lateral.pos.length ~/ 3;
      for (var c = 0; c < m; c++) {
        final v = col.vertice[c];
        lateral.vertice(
          a.p[2 * v],
          a.p[2 * v + 1],
          zTopo,
          col.nx[c],
          col.ny[c],
          0,
          col.u[c] / tamanho,
          1,
        );
      }
      final baixo = lateral.pos.length ~/ 3;
      for (var c = 0; c < m; c++) {
        final v = col.vertice[c];
        lateral.vertice(
          a.p[2 * v],
          a.p[2 * v + 1],
          -zTopo,
          col.nx[c],
          col.ny[c],
          0,
          col.u[c] / tamanho,
          0,
        );
      }
      for (var i = 0; i < a.n; i++) {
        final j = (i + 1) % a.n;
        final ti = topo + col.saida[i], tj = topo + col.entrada[j];
        final bi = baixo + col.saida[i], bj = baixo + col.entrada[j];
        lateral.triangulo(ti, bj, tj, doRascunho);
        lateral.triangulo(ti, bi, bj, doRascunho);
      }
      // CHANFRO: s faixas na frente e o espelho delas atras.
      if (s > 0) {
        for (final lado in const [1.0, -1.0]) {
          final inicios = <int>[];
          for (var k = 0; k <= s; k++) {
            inicios.add(chanfro.pos.length ~/ 3);
            final r = recuoDoAnel(k), z = zDoAnel(k) * lado;
            final (pr, pz) = perfil(k);
            for (var c = 0; c < m; c++) {
              final v = col.vertice[c];
              chanfro.vertice(
                a.p[2 * v] - r * a.mx[v],
                a.p[2 * v + 1] - r * a.my[v],
                z,
                col.nx[c] * pr,
                col.ny[c] * pr,
                pz * lado,
                col.u[c] / tamanho,
                k / s,
              );
            }
          }
          for (var k = 0; k < s; k++) {
            final base = inicios[k], cima = inicios[k + 1];
            for (var i = 0; i < a.n; i++) {
              final j = (i + 1) % a.n;
              final ti = cima + col.saida[i], tj = cima + col.entrada[j];
              final bi = base + col.saida[i], bj = base + col.entrada[j];
              if (lado > 0) {
                chanfro.triangulo(ti, bj, tj, doRascunho);
                chanfro.triangulo(ti, bi, bj, doRascunho);
              } else {
                chanfro.triangulo(ti, tj, bj, doRascunho);
                chanfro.triangulo(ti, bj, bi, doRascunho);
              }
            }
          }
        }
      }
    }

    // TAMPAS: o anel mais recuado, triangulado com os furos, na frente e
    // (com a ordem invertida) atras.
    final r = recuoDoAnel(s);
    final plano = <double>[];
    final furos = <int>[];
    for (var ci = 0; ci < aneis.length; ci++) {
      final a = aneis[ci];
      if (ci > 0) furos.add(plano.length ~/ 2);
      for (var i = 0; i < a.n; i++) {
        plano
          ..add(a.p[2 * i] - r * a.mx[i])
          ..add(a.p[2 * i + 1] - r * a.my[i]);
      }
    }
    final tris = triangularComFuros(Float64List.fromList(plano), furos);
    final zFrente = zDoAnel(s);
    final daFrente = frente.pos.length ~/ 3;
    for (var i = 0; i < plano.length; i += 2) {
      frente.vertice(
        plano[i],
        plano[i + 1],
        zFrente,
        0,
        0,
        1,
        plano[i] / tamanho,
        plano[i + 1] / tamanho,
      );
    }
    final deTras = frente.pos.length ~/ 3;
    for (var i = 0; i < plano.length; i += 2) {
      frente.vertice(
        plano[i],
        plano[i + 1],
        -zFrente,
        0,
        0,
        -1,
        1 - plano[i] / tamanho,
        plano[i + 1] / tamanho,
      );
    }
    for (var i = 0; i + 2 < tris.length; i += 3) {
      frente.triangulo(
        daFrente + tris[i],
        daFrente + tris[i + 1],
        daFrente + tris[i + 2],
        doRascunho,
      );
      frente.triangulo(
        deTras + tris[i + 2],
        deTras + tris[i + 1],
        deTras + tris[i],
        doRascunho,
      );
    }
  }
}

double _area(Float64List p) {
  final n = p.length ~/ 2;
  var soma = 0.0;
  for (var i = 0; i < n; i++) {
    final j = (i + 1) % n;
    soma += p[2 * i] * p[2 * j + 1] - p[2 * j] * p[2 * i + 1];
  }
  return soma / 2;
}

/// Tira pontos colados e pontos alinhados.
///
/// Ponto ALINHADO tem de sair ANTES dos aneis: a triangulacao da tampa
/// descarta ponto alinhado por conta propria, e se o anel do chanfro
/// ficasse com ele, a borda da tampa e a do chanfro deixariam de casar
/// aresta por aresta — uma rachadura de um pixel que pisca na GPU.
Float64List? _limpar(Float64List p, double eps) {
  final xs = <double>[], ys = <double>[];
  for (var i = 0; i + 1 < p.length; i += 2) {
    if (xs.isNotEmpty &&
        (xs.last - p[i]).abs() <= eps &&
        (ys.last - p[i + 1]).abs() <= eps) {
      continue;
    }
    xs.add(p[i]);
    ys.add(p[i + 1]);
  }
  while (xs.length > 1 &&
      (xs.first - xs.last).abs() <= eps &&
      (ys.first - ys.last).abs() <= eps) {
    xs.removeLast();
    ys.removeLast();
  }
  var mudou = true;
  while (mudou && xs.length >= 3) {
    mudou = false;
    var i = 0;
    while (i < xs.length && xs.length >= 3) {
      final n = xs.length;
      final a = (i - 1 + n) % n, b = (i + 1) % n;
      final abx = xs[b] - xs[a], aby = ys[b] - ys[a];
      final cruz = (xs[i] - xs[a]) * aby - (ys[i] - ys[a]) * abx;
      final base = math.sqrt(abx * abx + aby * aby);
      final colado =
          (xs[i] - xs[b]).abs() <= eps && (ys[i] - ys[b]).abs() <= eps;
      if (colado || cruz.abs() <= eps * math.max(base, 1e-12)) {
        xs.removeAt(i);
        ys.removeAt(i);
        mudou = true;
      } else {
        i++;
      }
    }
  }
  if (xs.length < 3) return null;
  final out = Float64List(xs.length * 2);
  for (var i = 0; i < xs.length; i++) {
    out[2 * i] = xs[i];
    out[2 * i + 1] = ys[i];
  }
  if (_area(out).abs() <= eps * eps) return null;
  return out;
}

/// [dentro] esta dentro de [fora]? Tres pontos de [dentro] votam: um
/// contorno que encosta no outro num vertice nao engana o teste.
bool _contido(Float64List dentro, Float64List fora) {
  final n = dentro.length ~/ 2;
  var sim = 0, nao = 0;
  for (final k in [0, n ~/ 3, (2 * n) ~/ 3]) {
    final j = (k + 1) % n;
    final x = (dentro[2 * k] + dentro[2 * j]) / 2;
    final y = (dentro[2 * k + 1] + dentro[2 * j + 1]) / 2;
    if (_pontoDentro(x, y, fora)) {
      sim++;
    } else {
      nao++;
    }
  }
  return sim > nao;
}

bool _pontoDentro(double x, double y, Float64List p) {
  final n = p.length ~/ 2;
  var dentro = false;
  for (var i = 0, j = n - 1; i < n; j = i++) {
    final xi = p[2 * i], yi = p[2 * i + 1];
    final xj = p[2 * j], yj = p[2 * j + 1];
    if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) {
      dentro = !dentro;
    }
  }
  return dentro;
}

// ----------------------------------------------------------- earcut

/// TRIANGULACAO COM FUROS (o algoritmo "earcut", da Mapbox).
///
/// O corte de orelha que ja existia (`earClip`) nao aceita furo: o "O"
/// sairia com o miolo tapado. Aqui cada furo e costurado ao contorno de
/// fora por uma ponte ate o vertice visivel mais proximo, e o poligono
/// que sobra (um so, sem furo) e cortado orelha por orelha, com tres
/// passes de socorro para contorno imperfeito.
///
/// [coords] e [x0, y0, x1, y1, ...]: o contorno de fora primeiro, depois
/// os furos; [inicioDosFuros] diz em que ponto cada furo comeca. Devolve
/// trincas de indices de ponto, ANTI-HORARIAS com Y para cima.
List<int> triangularComFuros(Float64List coords, List<int> inicioDosFuros) {
  final total = coords.length ~/ 2;
  final fimDoExterno = inicioDosFuros.isEmpty ? total : inicioDosFuros.first;
  var externo = _listaLigada(coords, 0, fimDoExterno, true);
  final out = <int>[];
  if (externo == null || identical(externo.next, externo.prev)) return out;
  if (inicioDosFuros.isNotEmpty) {
    externo = _eliminarFuros(coords, inicioDosFuros, externo, total);
  }
  _cortar(externo, out, 0);
  return out;
}

class _No {
  _No(this.i, this.x, this.y);
  final int i;
  final double x;
  final double y;
  late _No prev;
  late _No next;
  bool steiner = false;
}

double _areaSinal(Float64List d, int inicio, int fim) {
  var soma = 0.0;
  for (var i = inicio, j = fim - 1; i < fim; j = i++) {
    soma += (d[2 * j] - d[2 * i]) * (d[2 * i + 1] + d[2 * j + 1]);
  }
  return soma;
}

_No? _listaLigada(Float64List d, int inicio, int fim, bool horario) {
  _No? ultimo;
  if (horario == (_areaSinal(d, inicio, fim) > 0)) {
    for (var i = inicio; i < fim; i++) {
      ultimo = _inserir(i, d[2 * i], d[2 * i + 1], ultimo);
    }
  } else {
    for (var i = fim - 1; i >= inicio; i--) {
      ultimo = _inserir(i, d[2 * i], d[2 * i + 1], ultimo);
    }
  }
  if (ultimo != null && _iguais(ultimo, ultimo.next)) {
    _remover(ultimo);
    ultimo = ultimo.next;
  }
  return ultimo;
}

_No _inserir(int i, double x, double y, _No? ultimo) {
  final p = _No(i, x, y);
  if (ultimo == null) {
    p.prev = p;
    p.next = p;
  } else {
    p.next = ultimo.next;
    p.prev = ultimo;
    ultimo.next.prev = p;
    ultimo.next = p;
  }
  return p;
}

void _remover(_No p) {
  p.next.prev = p.prev;
  p.prev.next = p.next;
}

bool _iguais(_No a, _No b) => a.x == b.x && a.y == b.y;

double _areaTri(_No p, _No q, _No r) =>
    (q.y - p.y) * (r.x - q.x) - (q.x - p.x) * (r.y - q.y);

_No? _filtrar(_No? inicio, [_No? fim]) {
  if (inicio == null) return null;
  fim ??= inicio;
  var p = inicio;
  bool denovo;
  do {
    denovo = false;
    if (!p.steiner &&
        (_iguais(p, p.next) || _areaTri(p.prev, p, p.next) == 0)) {
      _remover(p);
      p = fim = p.prev;
      if (identical(p, p.next)) break;
      denovo = true;
    } else {
      p = p.next;
    }
  } while (denovo || !identical(p, fim));
  return fim;
}

void _cortar(_No? orelha, List<int> out, int passe) {
  if (orelha == null) return;
  var ear = orelha;
  var parada = ear;
  while (!identical(ear.prev, ear.next)) {
    final prev = ear.prev, next = ear.next;
    if (_ehOrelha(ear)) {
      out
        ..add(prev.i)
        ..add(ear.i)
        ..add(next.i);
      _remover(ear);
      // Pular o proximo vertice da menos triangulo fino.
      ear = next.next;
      parada = next.next;
      continue;
    }
    ear = next;
    if (identical(ear, parada)) {
      if (passe == 0) {
        _cortar(_filtrar(ear), out, 1);
      } else if (passe == 1) {
        final curado = _curarCruzamentos(_filtrar(ear)!, out);
        _cortar(curado, out, 2);
      } else if (passe == 2) {
        _dividirECortar(ear, out);
      }
      break;
    }
  }
}

bool _ehOrelha(_No ear) {
  final a = ear.prev, b = ear, c = ear.next;
  if (_areaTri(a, b, c) >= 0) return false; // canto reflexo
  final x0 = math.min(a.x, math.min(b.x, c.x));
  final y0 = math.min(a.y, math.min(b.y, c.y));
  final x1 = math.max(a.x, math.max(b.x, c.x));
  final y1 = math.max(a.y, math.max(b.y, c.y));
  var p = c.next;
  while (!identical(p, a)) {
    if (p.x >= x0 &&
        p.x <= x1 &&
        p.y >= y0 &&
        p.y <= y1 &&
        _pontoNoTriangulo(a.x, a.y, b.x, b.y, c.x, c.y, p.x, p.y) &&
        _areaTri(p.prev, p, p.next) >= 0) {
      return false;
    }
    p = p.next;
  }
  return true;
}

_No? _curarCruzamentos(_No inicio, List<int> out) {
  var comeco = inicio;
  var p = inicio;
  do {
    final a = p.prev, b = p.next.next;
    if (!_iguais(a, b) &&
        _cruzam(a, p, p.next, b) &&
        _localmenteDentro(a, b) &&
        _localmenteDentro(b, a)) {
      out
        ..add(a.i)
        ..add(p.i)
        ..add(b.i);
      _remover(p);
      _remover(p.next);
      p = comeco = b;
    }
    p = p.next;
  } while (!identical(p, comeco));
  return _filtrar(p);
}

void _dividirECortar(_No inicio, List<int> out) {
  var a = inicio;
  do {
    var b = a.next.next;
    while (!identical(b, a.prev)) {
      if (a.i != b.i && _diagonalValida(a, b)) {
        var c = _dividir(a, b);
        final a2 = _filtrar(a, a.next);
        c = _filtrar(c, c.next)!;
        _cortar(a2, out, 0);
        _cortar(c, out, 0);
        return;
      }
      b = b.next;
    }
    a = a.next;
  } while (!identical(a, inicio));
}

_No _eliminarFuros(Float64List d, List<int> inicios, _No externo, int total) {
  final fila = <_No>[];
  for (var k = 0; k < inicios.length; k++) {
    final inicio = inicios[k];
    final fim = k < inicios.length - 1 ? inicios[k + 1] : total;
    final lista = _listaLigada(d, inicio, fim, false);
    if (lista == null) continue;
    if (identical(lista, lista.next)) lista.steiner = true;
    fila.add(_maisAEsquerda(lista));
  }
  fila.sort((a, b) => a.x.compareTo(b.x));
  var saida = externo;
  for (final furo in fila) {
    saida = _eliminarFuro(furo, saida);
  }
  return saida;
}

_No _eliminarFuro(_No furo, _No externo) {
  final ponte = _acharPonte(furo, externo);
  if (ponte == null) return externo;
  final reversa = _dividir(ponte, furo);
  _filtrar(reversa, reversa.next);
  return _filtrar(ponte, ponte.next)!;
}

_No? _acharPonte(_No furo, _No externo) {
  var p = externo;
  final hx = furo.x, hy = furo.y;
  var qx = double.negativeInfinity;
  _No? m;
  do {
    if (hy <= p.y && hy >= p.next.y && p.next.y != p.y) {
      final x = p.x + (hy - p.y) * (p.next.x - p.x) / (p.next.y - p.y);
      if (x <= hx && x > qx) {
        qx = x;
        m = p.x < p.next.x ? p : p.next;
        if (x == hx) return m;
      }
    }
    p = p.next;
  } while (!identical(p, externo));
  if (m == null) return null;
  final parada = m;
  final mx = m.x, my = m.y;
  var tanMin = double.infinity;
  p = m;
  do {
    if (hx >= p.x &&
        p.x >= mx &&
        hx != p.x &&
        _pontoNoTriangulo(
          hy < my ? hx : qx,
          hy,
          mx,
          my,
          hy < my ? qx : hx,
          hy,
          p.x,
          p.y,
        )) {
      final tan = (hy - p.y).abs() / (hx - p.x);
      if (_localmenteDentro(p, furo) &&
          (tan < tanMin ||
              (tan == tanMin &&
                  (p.x > m!.x || (p.x == m.x && _setorContemSetor(m, p)))))) {
        m = p;
        tanMin = tan;
      }
    }
    p = p.next;
  } while (!identical(p, parada));
  return m;
}

bool _setorContemSetor(_No m, _No p) =>
    _areaTri(m.prev, m, p.prev) < 0 && _areaTri(p.next, m, m.next) < 0;

_No _maisAEsquerda(_No inicio) {
  var p = inicio, esquerda = inicio;
  do {
    if (p.x < esquerda.x || (p.x == esquerda.x && p.y < esquerda.y)) {
      esquerda = p;
    }
    p = p.next;
  } while (!identical(p, inicio));
  return esquerda;
}

bool _pontoNoTriangulo(
  double ax,
  double ay,
  double bx,
  double by,
  double cx,
  double cy,
  double px,
  double py,
) =>
    (cx - px) * (ay - py) >= (ax - px) * (cy - py) &&
    (ax - px) * (by - py) >= (bx - px) * (ay - py) &&
    (bx - px) * (cy - py) >= (cx - px) * (by - py);

bool _diagonalValida(_No a, _No b) =>
    a.next.i != b.i &&
    a.prev.i != b.i &&
    !_cruzaPoligono(a, b) &&
    ((_localmenteDentro(a, b) &&
            _localmenteDentro(b, a) &&
            _meioDentro(a, b) &&
            (_areaTri(a.prev, a, b.prev) != 0 ||
                _areaTri(a, b.prev, b) != 0)) ||
        (_iguais(a, b) &&
            _areaTri(a.prev, a, a.next) > 0 &&
            _areaTri(b.prev, b, b.next) > 0));

int _sinal(double v) => v > 0 ? 1 : (v < 0 ? -1 : 0);

bool _cruzam(_No p1, _No q1, _No p2, _No q2) {
  final o1 = _sinal(_areaTri(p1, q1, p2));
  final o2 = _sinal(_areaTri(p1, q1, q2));
  final o3 = _sinal(_areaTri(p2, q2, p1));
  final o4 = _sinal(_areaTri(p2, q2, q1));
  if (o1 != o2 && o3 != o4) return true;
  if (o1 == 0 && _noSegmento(p1, p2, q1)) return true;
  if (o2 == 0 && _noSegmento(p1, q2, q1)) return true;
  if (o3 == 0 && _noSegmento(p2, p1, q2)) return true;
  if (o4 == 0 && _noSegmento(p2, q1, q2)) return true;
  return false;
}

bool _noSegmento(_No p, _No q, _No r) =>
    q.x <= math.max(p.x, r.x) &&
    q.x >= math.min(p.x, r.x) &&
    q.y <= math.max(p.y, r.y) &&
    q.y >= math.min(p.y, r.y);

bool _cruzaPoligono(_No a, _No b) {
  var p = a;
  do {
    if (p.i != a.i &&
        p.next.i != a.i &&
        p.i != b.i &&
        p.next.i != b.i &&
        _cruzam(p, p.next, a, b)) {
      return true;
    }
    p = p.next;
  } while (!identical(p, a));
  return false;
}

bool _localmenteDentro(_No a, _No b) => _areaTri(a.prev, a, a.next) < 0
    ? _areaTri(a, b, a.next) >= 0 && _areaTri(a, a.prev, b) >= 0
    : _areaTri(a, b, a.prev) < 0 || _areaTri(a, a.next, b) < 0;

bool _meioDentro(_No a, _No b) {
  var p = a;
  var dentro = false;
  final px = (a.x + b.x) / 2, py = (a.y + b.y) / 2;
  do {
    if ((p.y > py) != (p.next.y > py) &&
        p.next.y != p.y &&
        px < (p.next.x - p.x) * (py - p.y) / (p.next.y - p.y) + p.x) {
      dentro = !dentro;
    }
    p = p.next;
  } while (!identical(p, a));
  return dentro;
}

_No _dividir(_No a, _No b) {
  final a2 = _No(a.i, a.x, a.y), b2 = _No(b.i, b.x, b.y);
  final an = a.next, bp = b.prev;
  a.next = b;
  b.prev = a;
  a2.next = an;
  an.prev = a2;
  b2.next = a2;
  a2.prev = b2;
  bp.next = b2;
  b2.prev = bp;
  return b2;
}

// ------------------------------------------------- glifos guardados

/// OS GLIFOS DO TEXTO, GUARDADOS NO PROJETO.
///
/// O projeto e levado de um aparelho para outro, e a fonte importada nao
/// vai junto. Guardando o contorno das letras que o texto usa (algumas
/// centenas de numeros), o texto reabre identico em qualquer lugar — e a
/// malha e refeita na abertura, com normais e UV, sem depender do blob
/// de malha que perde as normais.
///
/// Editar o texto depois precisa da fonte de verdade; o que esta aqui so
/// redesenha o que ja foi escrito.
class GlifosGuardados implements FonteDeGlifos {
  GlifosGuardados({
    required this.unidadesPorEm,
    required this.ascendente,
    required this.descendente,
    required this.entreLinhas,
    required this.glifos,
    required this.ausentes,
    required this.kerning,
  });

  /// Copia de [fonte] o que [texto] precisa.
  factory GlifosGuardados.capturar(FonteDeGlifos fonte, String texto) {
    final glifos = <int, GlifoDaFonte>{};
    final ausentes = <int>{};
    final kerning = <(int, int), double>{};
    for (final linha in texto.split('\n')) {
      int? anterior;
      for (final c in linha.runes) {
        final g = fonte.glifoDoCaractere(c);
        if (g == null) {
          ausentes.add(c);
          anterior = null;
          continue;
        }
        glifos[c] = g;
        if (anterior != null) {
          final k = fonte.kerningEntre(anterior, c);
          if (k != 0) kerning[(anterior, c)] = k;
        }
        anterior = c;
      }
    }
    return GlifosGuardados(
      unidadesPorEm: fonte.unidadesPorEm,
      ascendente: fonte.ascendente,
      descendente: fonte.descendente,
      entreLinhas: fonte.entreLinhas,
      glifos: glifos,
      ausentes: ausentes,
      kerning: kerning,
    );
  }

  @override
  final double unidadesPorEm;
  @override
  final double ascendente;
  @override
  final double descendente;
  @override
  final double entreLinhas;
  final Map<int, GlifoDaFonte> glifos;
  final Set<int> ausentes;
  final Map<(int, int), double> kerning;

  /// Tudo o que [texto] pede foi guardado (ou se sabe que falta)?
  bool cobre(String texto) {
    for (final c in texto.runes) {
      if (c == 0x0A || c == 0x0D) continue;
      if (!glifos.containsKey(c) && !ausentes.contains(c)) return false;
    }
    return true;
  }

  @override
  GlifoDaFonte? glifoDoCaractere(int codigo) => glifos[codigo];

  @override
  double kerningEntre(int esquerdo, int direito) =>
      kerning[(esquerdo, direito)] ?? 0;

  static double _r(double v) => (v * 1000).roundToDouble() / 1000;

  Map<String, Object> toJson() => {
    'upem': unidadesPorEm,
    'asc': ascendente,
    'desc': descendente,
    'gap': entreLinhas,
    'g': {
      for (final e in glifos.entries)
        '${e.key}': {
          'a': _r(e.value.avanco),
          'c': [
            for (final c in e.value.contornos)
              [
                _r(c.inicioX),
                _r(c.inicioY),
                for (final t in c.trechos)
                  ...(t.ehCurva
                      ? [1, _r(t.cx!), _r(t.cy!), _r(t.x), _r(t.y)]
                      : [0, _r(t.x), _r(t.y)]),
              ],
          ],
        },
    },
    if (ausentes.isNotEmpty) 'sem': ausentes.toList(),
    if (kerning.isNotEmpty)
      'k': {
        for (final e in kerning.entries) '${e.key.$1},${e.key.$2}': e.value,
      },
  };

  /// Leitura tolerante: um glifo com numero faltando e pulado, e nao
  /// derruba o projeto.
  static GlifosGuardados? fromJson(Object? bruto) {
    if (bruto is! Map) return null;
    double numero(Object? v, double padrao) =>
        v is num && v.isFinite ? v.toDouble() : padrao;
    final upem = numero(bruto['upem'], 0);
    if (upem <= 0) return null;
    final glifos = <int, GlifoDaFonte>{};
    final g = bruto['g'];
    if (g is Map) {
      for (final e in g.entries) {
        final codigo = int.tryParse('${e.key}');
        final dados = e.value;
        if (codigo == null || dados is! Map) continue;
        try {
          final contornos = <ContornoDoGlifo>[];
          for (final bruta in (dados['c'] as List? ?? const [])) {
            final v = [for (final x in bruta as List) (x as num).toDouble()];
            if (v.length < 2) continue;
            final trechos = <TrechoDoContorno>[];
            var i = 2;
            while (i < v.length) {
              if (v[i] == 1 && i + 4 < v.length) {
                trechos.add(
                  TrechoDoContorno.curva(
                    v[i + 1],
                    v[i + 2],
                    v[i + 3],
                    v[i + 4],
                  ),
                );
                i += 5;
              } else if (v[i] == 0 && i + 2 < v.length) {
                trechos.add(TrechoDoContorno.reta(v[i + 1], v[i + 2]));
                i += 3;
              } else {
                break;
              }
            }
            contornos.add(ContornoDoGlifo(v[0], v[1], trechos));
          }
          glifos[codigo] = GlifoDaFonte(
            contornos: contornos,
            avanco: numero(dados['a'], upem * 0.5),
          );
        } catch (_) {
          continue;
        }
      }
    }
    final kerning = <(int, int), double>{};
    final k = bruto['k'];
    if (k is Map) {
      for (final e in k.entries) {
        final partes = '${e.key}'.split(',');
        if (partes.length != 2) continue;
        final a = int.tryParse(partes[0]), b = int.tryParse(partes[1]);
        if (a == null || b == null || e.value is! num) continue;
        kerning[(a, b)] = (e.value as num).toDouble();
      }
    }
    return GlifosGuardados(
      unidadesPorEm: upem,
      ascendente: numero(bruto['asc'], upem * 0.8),
      descendente: numero(bruto['desc'], -upem * 0.2),
      entreLinhas: numero(bruto['gap'], 0),
      glifos: glifos,
      ausentes: {
        for (final c in (bruto['sem'] as List? ?? const []))
          if (c is int) c,
      },
      kerning: kerning,
    );
  }
}
