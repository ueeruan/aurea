import 'dart:math' as math;
import 'dart:ui';

import 'mask.dart';

/// EDICAO DE NO A NO.
///
/// Ate aqui um caminho so podia ser escolhido pronto (retangulo, elipse,
/// estrela) ou deformado por operadores. Desenhar a mascara em volta de
/// uma pessoa, ou consertar UM canto de uma forma, exigia comecar de
/// novo com outra forma pronta — o que na pratica queria dizer "nao da".
///
/// Aqui ficam as contas: mover ancora, mover alca, inserir no em cima da
/// curva sem mudar a forma, tirar no, e alternar canto/curva. Sao
/// funcoes puras sobre [BezierPath] — a tela so traduz dedo em chamada.

/// Qual alca de um no.
enum Handle { entrada, saida }

/// Onde um ponto caiu na curva.
class PathHit {
  const PathHit({
    required this.segment,
    required this.t,
    required this.point,
    required this.distance,
  });

  /// Indice do vertice em que o segmento COMECA.
  final int segment;

  /// Parametro dentro do segmento, 0..1.
  final double t;

  /// O ponto na curva.
  final Offset point;

  /// Distancia ate o ponto procurado.
  final double distance;
}

int _segmentCount(BezierPath p) {
  if (p.vertices.length < 2) return 0;
  return p.closed ? p.vertices.length : p.vertices.length - 1;
}

(Offset, Offset, Offset, Offset) _controlPoints(BezierPath p, int i) {
  final a = p.vertices[i];
  final b = p.vertices[(i + 1) % p.vertices.length];
  return (a.p, a.p + a.outT, b.p + b.inT, b.p);
}

Offset _cubic(Offset p0, Offset p1, Offset p2, Offset p3, double t) {
  final u = 1 - t;
  final a = u * u * u;
  final b = 3 * u * u * t;
  final c = 3 * u * t * t;
  final d = t * t * t;
  return Offset(
    a * p0.dx + b * p1.dx + c * p2.dx + d * p3.dx,
    a * p0.dy + b * p1.dy + c * p2.dy + d * p3.dy,
  );
}

/// MOVER A ANCORA. As alcas andam junto — sao relativas ao no, entao a
/// curva nao se retorce quando o no viaja.
BezierPath moveVertex(BezierPath path, int index, Offset to) {
  if (index < 0 || index >= path.vertices.length) return path;
  final v = path.vertices[index];
  final out = [...path.vertices];
  out[index] = PathVertex(p: to, inT: v.inT, outT: v.outT, corner: v.corner);
  return BezierPath(vertices: out, closed: path.closed);
}

/// MOVER A ALCA, em coordenada absoluta (e onde o dedo esta).
///
/// Num no de CURVA as duas alcas ficam opostas: puxar uma empurra a
/// outra, mantendo o comprimento dela. E o que impede o "bico" aparecer
/// no meio de uma curva lisa. Num no de CANTO cada alca anda sozinha.
BezierPath moveHandle(
  BezierPath path,
  int index,
  Handle which,
  Offset to, {
  bool alcasIguais = false,
}) {
  if (index < 0 || index >= path.vertices.length) return path;
  final v = path.vertices[index];
  final rel = to - v.p;

  // Ponto suave: a alca oposta segue a direcao. Com [alcasIguais], segue
  // tambem o tamanho — as duas alcas espelhadas por inteiro.
  Offset oposta(Offset antiga) => alcasIguais ? -rel : _oposta(rel, antiga);
  var inT = v.inT;
  var outT = v.outT;
  if (which == Handle.entrada) {
    inT = rel;
    if (!v.corner) outT = oposta(v.outT);
  } else {
    outT = rel;
    if (!v.corner) inT = oposta(v.inT);
  }

  final out = [...path.vertices];
  out[index] = PathVertex(p: v.p, inT: inT, outT: outT, corner: v.corner);
  return BezierPath(vertices: out, closed: path.closed);
}

/// MOVER O CONTORNO INTEIRO: todos os pontos (e as alcas, que sao
/// relativas) andam [delta].
BezierPath moverTodosOsPontos(BezierPath path, Offset delta) => BezierPath(
  closed: path.closed,
  vertices: [
    for (final v in path.vertices)
      PathVertex(p: v.p + delta, inT: v.inT, outT: v.outT, corner: v.corner),
  ],
);

/// A alca oposta: mesma direcao invertida, comprimento preservado.
Offset _oposta(Offset movida, Offset antiga) {
  final d = movida.distance;
  if (d < 0.0001) return antiga;
  final comprimento = antiga.distance;
  // Alca que estava zerada nao ganha tamanho do nada — o no continua
  // com um lado reto ate a pessoa puxar aquele lado.
  if (comprimento < 0.0001) return Offset.zero;
  return -movida / d * comprimento;
}

/// TIRAR UM NO.
///
/// O PISO DEPENDE DE O CAMINHO SER FECHADO, e antes nao dependia.
///
/// Num caminho FECHADO, tres nos e o minimo: com dois so ha uma ida e
/// volta, nao ha area, e o "poligono" deixa de existir. Num caminho
/// ABERTO dois nos ja sao um traco — e o traco e o caso de uso (o
/// contorno que vira animacao, o risco que vira morph). O piso unico de
/// tres proibia apagar o terceiro no de um traco, sem nada acontecendo na
/// tela: apertar a lixeira e a curva nao mudar e o que parece travamento.
BezierPath removeVertex(BezierPath path, int index) {
  if (index < 0 || index >= path.vertices.length) return path;
  final piso = path.closed ? 3 : 2;
  if (path.vertices.length <= piso) return path;
  final out = [...path.vertices]..removeAt(index);
  return BezierPath(vertices: out, closed: path.closed);
}

/// INSERIR UM NO em cima da curva, no ponto [t] do segmento [segment].
///
/// A forma NAO muda: e a divisao de de Casteljau, que reescreve os dois
/// pedacos com as mesmas curvas. Inserir e depois tirar o mesmo no volta
/// ao desenho de antes.
BezierPath insertVertex(BezierPath path, int segment, double t) {
  final n = _segmentCount(path);
  if (segment < 0 || segment >= n) return path;
  final tt = t.clamp(0.0001, 0.9999);

  final (p0, p1, p2, p3) = _controlPoints(path, segment);
  final m01 = Offset.lerp(p0, p1, tt)!;
  final m12 = Offset.lerp(p1, p2, tt)!;
  final m23 = Offset.lerp(p2, p3, tt)!;
  final m012 = Offset.lerp(m01, m12, tt)!;
  final m123 = Offset.lerp(m12, m23, tt)!;
  final meio = Offset.lerp(m012, m123, tt)!;

  final j = (segment + 1) % path.vertices.length;
  final a = path.vertices[segment];
  final b = path.vertices[j];

  final out = [...path.vertices];
  out[segment] = PathVertex(
    p: a.p,
    inT: a.inT,
    outT: m01 - p0,
    corner: a.corner,
  );
  out[j] = PathVertex(p: b.p, inT: m23 - p3, outT: b.outT, corner: b.corner);
  out.insert(
    segment + 1,
    PathVertex(p: meio, inT: m012 - meio, outT: m123 - meio, corner: false),
  );
  return BezierPath(vertices: out, closed: path.closed);
}

/// ALTERNAR CANTO / CURVA.
///
/// Virar canto zera as alcas: o no vira bico, que e o que a pessoa
/// espera ao pedir canto. Virar curva calcula as alcas a partir dos
/// VIZINHOS (um terco da distancia, na direcao da corda) — a curva sai
/// lisa de primeira, sem obrigar a puxar alca na mao.
BezierPath toggleCorner(BezierPath path, int index) {
  if (index < 0 || index >= path.vertices.length) return path;
  final v = path.vertices[index];
  final out = [...path.vertices];

  if (!v.corner) {
    out[index] = PathVertex(
      p: v.p,
      inT: Offset.zero,
      outT: Offset.zero,
      corner: true,
    );
    return BezierPath(vertices: out, closed: path.closed);
  }

  // OS VIZINHOS DE UM CAMINHO ABERTO NAO DÃO A VOLTA.
  //
  // Com o `% n` dos dois lados, a PONTA de um traco aberto usava o outro
  // extremo como vizinho: a corda atravessava o desenho inteiro de ponta
  // a ponta, e a alca nascia apontando para o lado errado — no sentido
  // contrario ao traco. O `% n` so vale no caminho fechado, onde o
  // primeiro e o ultimo sao vizinhos de verdade.
  final n = path.vertices.length;
  final int iAntes, iDepois;
  if (path.closed) {
    iAntes = (index - 1 + n) % n;
    iDepois = (index + 1) % n;
  } else {
    iAntes = index > 0 ? index - 1 : 0;
    iDepois = index < n - 1 ? index + 1 : n - 1;
  }
  final antes = path.vertices[iAntes].p;
  final depois = path.vertices[iDepois].p;
  // A PONTA tem um vizinho so: a corda e o proprio trecho ate ele, e a
  // alca sai na direcao dele (antes saia em diagonal, porque o "antes"
  // era o outro extremo).
  if (!path.closed && n >= 2 && (index == 0 || index == n - 1)) {
    final corda = index == 0 ? depois - v.p : v.p - antes;
    final t = corda / 3;
    out[index] = PathVertex(p: v.p, inT: -t, outT: t, corner: false);
    return BezierPath(vertices: out, closed: path.closed);
  }
  final corda = depois - antes;
  final t = corda / 6;
  out[index] = PathVertex(p: v.p, inT: -t, outT: t, corner: false);
  return BezierPath(vertices: out, closed: path.closed);
}

/// O ponto da CURVA mais perto de [alvo].
///
/// Serve para duas coisas: saber se o dedo caiu na linha (e ai inserir
/// um no ali) e desenhar o realce do segmento sob o dedo. Amostra cada
/// segmento e refina em volta do melhor — mais barato que resolver a
/// equacao, e a precisao de um pixel e de sobra para o dedo.
PathHit? nearestOnPath(BezierPath path, Offset alvo, {int samples = 24}) {
  final n = _segmentCount(path);
  if (n == 0) return null;

  var melhorSeg = 0;
  var melhorT = 0.0;
  var melhorD = double.infinity;
  var melhorP = Offset.zero;

  for (var s = 0; s < n; s++) {
    final (p0, p1, p2, p3) = _controlPoints(path, s);
    for (var k = 0; k <= samples; k++) {
      final t = k / samples;
      final p = _cubic(p0, p1, p2, p3, t);
      final d = (p - alvo).distance;
      if (d < melhorD) {
        melhorD = d;
        melhorSeg = s;
        melhorT = t;
        melhorP = p;
      }
    }
  }

  // Refina em volta do melhor palpite.
  final (q0, q1, q2, q3) = _controlPoints(path, melhorSeg);
  var passo = 1.0 / samples;
  for (var it = 0; it < 4; it++) {
    passo /= 2;
    for (final cand in [melhorT - passo, melhorT + passo]) {
      if (cand < 0 || cand > 1) continue;
      final p = _cubic(q0, q1, q2, q3, cand);
      final d = (p - alvo).distance;
      if (d < melhorD) {
        melhorD = d;
        melhorT = cand;
        melhorP = p;
      }
    }
  }

  return PathHit(
    segment: melhorSeg,
    t: melhorT,
    point: melhorP,
    distance: melhorD,
  );
}

/// Qual no esta debaixo do dedo, dentro de [raio]. Devolve null se
/// nenhum.
int? vertexAt(BezierPath path, Offset alvo, double raio) {
  var melhor = -1;
  var melhorD = raio;
  for (var i = 0; i < path.vertices.length; i++) {
    final d = (path.vertices[i].p - alvo).distance;
    if (d <= melhorD) {
      melhorD = d;
      melhor = i;
    }
  }
  return melhor < 0 ? null : melhor;
}

/// Qual ALCA esta debaixo do dedo. So as alcas do no selecionado contam
/// — mostrar todas de uma vez vira um monte de bolinha sobreposta.
(int, Handle)? handleAt(
  BezierPath path,
  int? selecionado,
  Offset alvo,
  double raio,
) {
  if (selecionado == null ||
      selecionado < 0 ||
      selecionado >= path.vertices.length) {
    return null;
  }
  final v = path.vertices[selecionado];
  final dEntrada = (v.p + v.inT - alvo).distance;
  final dSaida = (v.p + v.outT - alvo).distance;
  if (dEntrada <= raio && dEntrada <= dSaida) {
    return (selecionado, Handle.entrada);
  }
  if (dSaida <= raio) return (selecionado, Handle.saida);
  return null;
}

/// Caixa que envolve o caminho (ancoras e alcas), para enquadrar a
/// vista ao abrir o editor.
Rect pathBounds(BezierPath path) {
  if (path.vertices.isEmpty) return Rect.zero;
  var minX = double.infinity, minY = double.infinity;
  var maxX = -double.infinity, maxY = -double.infinity;
  void conta(Offset p) {
    minX = math.min(minX, p.dx);
    minY = math.min(minY, p.dy);
    maxX = math.max(maxX, p.dx);
    maxY = math.max(maxY, p.dy);
  }

  for (final v in path.vertices) {
    conta(v.p);
    conta(v.p + v.inT);
    conta(v.p + v.outT);
  }
  return Rect.fromLTRB(minX, minY, maxX, maxY);
}
