// A CURVA DE TEMPO COMO O DEDO A ENXERGA.
//
// O editor antigo expunha alcas de bezier: com o dedo, o segundo arrasto
// entortava a curva em vez de mover o ponto, e o ponto novo nascia com
// alca horizontal (velocidade zero logo depois dele). Aqui cada ponto tem
// um MODO — linear, suave, segurar ou livre — e as tangentes saem da
// conta, nunca da mao. O resultado continua sendo a mesma trilha de
// sempre (`AnimatedDouble` com `Easing` bezier), lida pelo nucleo em C++.
//
// Convencao: `t` e o tempo local do clipe; `v` sao os segundos de fonte
// RELATIVOS a `sourceOffset` (o mesmo contrato do parametro 'tempo' do
// efeito Time Remap).
import 'dart:math' as math;

import 'keyframe.dart';
import 'time_core.dart';

/// Como a curva passa pelo ponto.
enum ModoDoPonto {
  /// Velocidade constante dos dois lados (quinas permitidas).
  linear,

  /// Tangente automatica monotona (PCHIP): a velocidade muda sem tranco e
  /// a fonte nunca anda para tras sozinha.
  suave,

  /// O quadro fica parado ate o proximo ponto.
  segurar,

  /// Curva que o modo nao sabe desenhar (preset antigo, bezier a mao):
  /// o trecho que sai do ponto e guardado como veio.
  livre,
}

/// Um quadro de folga entre pontos vizinhos (30 fps).
const kFolgaEntrePontos = Duration(microseconds: 33334);

/// Um ponto da curva de tempo. Imutavel.
class PontoDeTempo {
  const PontoDeTempo({
    required this.t,
    required this.v,
    this.modo = ModoDoPonto.linear,
    this.easeLivre,
  });

  /// Tempo local do clipe.
  final Duration t;

  /// Segundos de fonte relativos a `sourceOffset`.
  final double v;
  final ModoDoPonto modo;

  /// So vale em [ModoDoPonto.livre]: o easing do trecho que sai do ponto.
  final Easing? easeLivre;

  PontoDeTempo copyWith({
    Duration? t,
    double? v,
    ModoDoPonto? modo,
    Easing? easeLivre,
    bool semEaseLivre = false,
  }) => PontoDeTempo(
    t: t ?? this.t,
    v: v ?? this.v,
    modo: modo ?? this.modo,
    easeLivre: semEaseLivre ? null : (easeLivre ?? this.easeLivre),
  );

  @override
  bool operator ==(Object other) =>
      other is PontoDeTempo &&
      other.t == t &&
      other.v == v &&
      other.modo == modo &&
      _mesmoEase(other.easeLivre, easeLivre);

  @override
  int get hashCode => Object.hash(t, v, modo, easeLivre?.type, easeLivre?.x1);

  @override
  String toString() =>
      'PontoDeTempo(${t.inMicroseconds} us, $v s, ${modo.name})';
}

bool _mesmoEase(Easing? a, Easing? b) {
  if (identical(a, b)) return true;
  if (a == null || b == null) return false;
  return a.type == b.type &&
      a.x1 == b.x1 &&
      a.y1 == b.y1 &&
      a.x2 == b.x2 &&
      a.y2 == b.y2 &&
      a.count == b.count &&
      a.smooth == b.smooth &&
      a.intensity == b.intensity &&
      a.response == b.response &&
      a.damping == b.damping &&
      a.initialVelocity == b.initialVelocity;
}

double _segundos(Duration d) => d.inMicroseconds / 1000000.0;

Duration _duracao(double segundos) =>
    Duration(microseconds: (segundos * 1000000).round());

/// Abaixo disto o trecho e plano: o easing nao muda nenhum valor.
const _plano = 1e-9;

/// Tolerancia das tangentes, nas unidades normalizadas do easing.
const _tolerancia = 1e-6;

const _terco = 1.0 / 3;
const _doisTercos = 2.0 / 3;

/// Duracoes (s) e secantes (s de fonte por s de clipe) de cada trecho.
({List<double> h, List<double> d}) _secantes(List<PontoDeTempo> p) {
  final h = <double>[];
  final d = <double>[];
  for (var i = 0; i + 1 < p.length; i++) {
    final dt = _segundos(p[i + 1].t - p[i].t);
    h.add(dt);
    d.add(dt > 0 ? (p[i + 1].v - p[i].v) / dt : 0);
  }
  return (h: h, d: d);
}

/// TANGENTE AUTOMATICA (Fritsch-Butland).
///
/// Media harmonica ponderada das secantes vizinhas. Ela nunca passa de
/// tres vezes nenhuma das duas, e e esse teto que garante que a cubica
/// entre dois pontos suaves nao ultrapassa os valores deles: a fonte nao
/// volta para tras sozinha e o quadro nao "quica". Onde a curva vira
/// (secantes de sinais opostos) ou para (uma secante zero), a tangente e
/// zero. Nas pontas vale a secante do unico trecho vizinho.
double _pchip(List<double> h, List<double> d, int i) {
  final n = d.length + 1;
  if (n < 2) return 0;
  if (i <= 0) return d.first;
  if (i >= n - 1) return d.last;
  final d0 = d[i - 1], d1 = d[i];
  if (d0.abs() < 1e-12 || d1.abs() < 1e-12 || (d0 > 0) != (d1 > 0)) {
    return 0;
  }
  final h0 = h[i - 1], h1 = h[i];
  final w1 = 2 * h1 + h0;
  final w2 = h1 + 2 * h0;
  return (w1 + w2) / (w1 / d0 + w2 / d1);
}

/// Tangente com que o trecho [i] SAI do ponto [i], pelo modo dele.
double _tangenteDeSaida(
  List<double> h,
  List<double> d,
  int i,
  ModoDoPonto modo,
) => modo == ModoDoPonto.suave ? _pchip(h, d, i) : d[i];

/// Tangente com que o trecho [i - 1] CHEGA ao ponto [i]. Segurar e livre
/// chegam em linha reta: o que eles guardam e o trecho que sai.
double _tangenteDeChegada(
  List<double> h,
  List<double> d,
  int i,
  ModoDoPonto modo,
) => modo == ModoDoPonto.suave ? _pchip(h, d, i) : d[i - 1];

/// Bezier com x linear (x1 = 1/3, x2 = 2/3): y vira a cubica de Hermite
/// com as duas tangentes. Com as duas iguais a secante, e a reta.
Easing _hermite(double m0, double m1, double dt, double dv) {
  if (dv.abs() < _plano || dt <= 0) return Easing.linear;
  final y1 = m0 * dt / (3 * dv);
  final y2 = 1 - m1 * dt / (3 * dv);
  if ((y1 - _terco).abs() < 1e-9 && (y2 - _doisTercos).abs() < 1e-9) {
    return Easing.linear;
  }
  return Easing(x1: _terco, y1: y1, x2: _doisTercos, y2: y2);
}

/// As alcas normalizadas (y1, y2) de um easing na forma de Hermite, ou
/// nulo quando o easing nao cabe nela (outro tipo, outro x).
(double, double)? _formaDeHermite(Easing e) {
  if (e.type != EasingType.cubicBezier) return null;
  if (e.isLinear) return (_terco, _doisTercos);
  if ((e.x1 - _terco).abs() < 1e-9 && (e.x2 - _doisTercos).abs() < 1e-9) {
    return (e.y1, e.y2);
  }
  // Alcas em cima da diagonal: x(u) e y(u) sao o mesmo polinomio, reta.
  if ((e.x1 - e.y1).abs() < 1e-12 && (e.x2 - e.y2).abs() < 1e-12) {
    return (_terco, _doisTercos);
  }
  return null;
}

/// DOS PONTOS PARA A TRILHA que o projeto guarda e o nucleo toca.
///
/// Monta a lista direto, sem `withKeyframe`: ele funde marcas a menos de
/// 8 ms, e uma curva lida do projeto pode ter pedacos mais juntos que isso.
AnimatedDouble curvaDosPontos(List<PontoDeTempo> pontos) {
  if (pontos.isEmpty) return AnimatedDouble(0);
  final p = [...pontos]..sort((a, b) => a.t.compareTo(b.t));
  final s = _secantes(p);
  final kfs = <Keyframe<double>>[];
  for (var i = 0; i < p.length; i++) {
    var ease = Easing.linear;
    if (i + 1 < p.length) {
      final a = p[i];
      if (a.modo == ModoDoPonto.segurar) {
        ease = const Easing(type: EasingType.hold);
      } else if (a.modo == ModoDoPonto.livre && a.easeLivre != null) {
        ease = a.easeLivre!;
      } else {
        final modoDeSaida = a.modo == ModoDoPonto.suave
            ? ModoDoPonto.suave
            : ModoDoPonto.linear;
        ease = _hermite(
          _tangenteDeSaida(s.h, s.d, i, modoDeSaida),
          _tangenteDeChegada(s.h, s.d, i + 1, p[i + 1].modo),
          s.h[i],
          p[i + 1].v - a.v,
        );
      }
    }
    kfs.add(Keyframe(time: p[i].t, value: p[i].v, ease: ease));
  }
  return AnimatedDouble(0, kfs);
}

enum _Trecho { segurar, livre, plano, hermite }

/// DA TRILHA PARA OS PONTOS: descobre o modo de cada marca.
///
/// Da direita para a esquerda, cada ponto fica com o modo que REPRODUZ o
/// trecho que sai dele junto com o modo (ja decidido) do ponto seguinte.
/// Quando nenhum modo reproduz, o ponto vira livre e guarda o easing como
/// veio — entao `curvaDosPontos(pontosDaCurva(t))` toca os mesmos quadros
/// que `t`, sempre.
///
/// Linear e suave coincidem quando as secantes dos dois lados sao iguais
/// (e sempre nas pontas). Esse empate vira suave se um vizinho for suave
/// de verdade, e linear no resto: o desenho e o mesmo, e o chip mostrado
/// fica coerente com a vizinhanca.
List<PontoDeTempo> pontosDaCurva(AnimatedDouble track) {
  final ks = track.keyframes;
  final n = ks.length;
  if (n == 0) return const [];
  final brutos = [for (final k in ks) PontoDeTempo(t: k.time, v: k.value)];
  if (n == 1) return brutos;
  final s = _secantes(brutos);

  final tipos = <_Trecho>[];
  final alcas = <(double, double)?>[];
  for (var i = 0; i + 1 < n; i++) {
    final e = ks[i].ease;
    final dv = ks[i + 1].value - ks[i].value;
    final forma = _formaDeHermite(e);
    alcas.add(forma);
    if (e.type == EasingType.hold) {
      tipos.add(_Trecho.segurar);
    } else if (dv.abs() < _plano || s.h[i] <= 0) {
      tipos.add(_Trecho.plano);
    } else if (forma == null) {
      tipos.add(_Trecho.livre);
    } else {
      tipos.add(_Trecho.hermite);
    }
  }

  final modos = List<ModoDoPonto>.filled(n, ModoDoPonto.linear);
  final eases = List<Easing?>.filled(n, null);
  final empate = List<bool>.filled(n, false);

  bool reproduzSaida(int i, ModoDoPonto c) {
    if (i + 1 >= n || tipos[i] != _Trecho.hermite) return true;
    final (y1, y2) = alcas[i]!;
    final dv = brutos[i + 1].v - brutos[i].v;
    final m0 = _tangenteDeSaida(s.h, s.d, i, c);
    final m1 = _tangenteDeChegada(s.h, s.d, i + 1, modos[i + 1]);
    final e1 = m0 * s.h[i] / (3 * dv);
    final e2 = 1 - m1 * s.h[i] / (3 * dv);
    return (y1 - e1).abs() < _tolerancia && (y2 - e2).abs() < _tolerancia;
  }

  bool casaChegada(int i, ModoDoPonto c) {
    if (i == 0 || tipos[i - 1] != _Trecho.hermite) return true;
    final (_, y2) = alcas[i - 1]!;
    final dv = brutos[i].v - brutos[i - 1].v;
    final m1 = _tangenteDeChegada(s.h, s.d, i, c);
    final e2 = 1 - m1 * s.h[i - 1] / (3 * dv);
    return (y2 - e2).abs() < _tolerancia;
  }

  for (var i = n - 1; i >= 0; i--) {
    if (i + 1 < n && tipos[i] == _Trecho.segurar) {
      modos[i] = ModoDoPonto.segurar;
      continue;
    }
    if (i + 1 < n && tipos[i] == _Trecho.livre) {
      modos[i] = ModoDoPonto.livre;
      eases[i] = ks[i].ease;
      continue;
    }
    const candidatos = [ModoDoPonto.linear, ModoDoPonto.suave];
    final saida = [for (final c in candidatos) reproduzSaida(i, c)];
    final tudo = [
      for (var k = 0; k < 2; k++) saida[k] && casaChegada(i, candidatos[k]),
    ];
    if (tudo[0] && tudo[1]) {
      empate[i] = true;
    } else if (tudo[0] || tudo[1]) {
      modos[i] = tudo[0] ? ModoDoPonto.linear : ModoDoPonto.suave;
    } else if (saida[0] && saida[1]) {
      empate[i] = true;
    } else if (saida[0] || saida[1]) {
      modos[i] = saida[0] ? ModoDoPonto.linear : ModoDoPonto.suave;
    } else {
      modos[i] = ModoDoPonto.livre;
      eases[i] = ks[i].ease;
    }
  }

  // Empates: suave ao lado de um suave de verdade.
  final decididos = [...modos];
  for (var i = 0; i < n; i++) {
    if (!empate[i]) continue;
    final vizinhoSuave =
        (i > 0 && !empate[i - 1] && decididos[i - 1] == ModoDoPonto.suave) ||
        (i + 1 < n && !empate[i + 1] && decididos[i + 1] == ModoDoPonto.suave);
    modos[i] = vizinhoSuave ? ModoDoPonto.suave : ModoDoPonto.linear;
  }

  return [
    for (var i = 0; i < n; i++)
      PontoDeTempo(
        t: brutos[i].t,
        v: brutos[i].v,
        modo: modos[i],
        easeLivre: eases[i],
      ),
  ];
}

/// AS PONTAS NO LUGAR: primeiro ponto em zero, ultimo no fim do clipe.
///
/// Uma trilha lida do projeto pode comecar depois do zero ou passar do
/// fim (o nucleo extrapola em linha reta fora das marcas). O ponto que
/// falta nasce com o valor que a curva JA tem ali, entao nenhum quadro
/// dentro do clipe muda; o que passa do fim sai.
List<PontoDeTempo> ajustarPontasAoClipe(
  List<PontoDeTempo> pontos,
  Duration duracao,
) {
  if (duracao <= Duration.zero) return pontos;
  if (pontos.isEmpty) return curvaReta(duracao);
  final curva = curvaDosPontos(pontos);
  const tol = Duration(milliseconds: 1);
  PontoDeTempo? inicio;
  PontoDeTempo? fim;
  final meio = <PontoDeTempo>[];
  for (final p in pontos) {
    if (p.t.abs() <= tol) {
      inicio ??= p.copyWith(t: Duration.zero);
    } else if ((p.t - duracao).abs() <= tol) {
      fim = p.copyWith(t: duracao);
    } else if (p.t > Duration.zero && p.t < duracao) {
      meio.add(p);
    }
  }
  inicio ??= PontoDeTempo(
    t: Duration.zero,
    v: coreValue(curva, Duration.zero, linear: true),
  );
  fim ??= PontoDeTempo(t: duracao, v: coreValue(curva, duracao, linear: true));
  return [inicio, ...meio, fim];
}

/// Resultado de [inserirNaCurva].
typedef Insercao = ({List<PontoDeTempo> pontos, int indice, bool criou});

/// CRIA UM PONTO EM [t] SEM MUDAR O QUE JA TOCA.
///
/// O valor e o da curva ali. Num trecho linear ou segurado nenhum quadro
/// muda; num trecho livre de bezier o trecho e dividido (de Casteljau) e
/// as duas metades ficam com o desenho exato. A menos de um quadro de um
/// ponto que ja existe nao cria nada: devolve o indice dele.
Insercao inserirNaCurva(List<PontoDeTempo> pontos, Duration t, {int fps = 30}) {
  if (pontos.isEmpty) {
    return (pontos: [PontoDeTempo(t: t, v: 0)], indice: 0, criou: true);
  }
  final quadro = Duration(microseconds: (1000000 / math.max(1, fps)).round());
  var alvo = t;
  if (alvo < pontos.first.t) alvo = pontos.first.t;
  if (alvo > pontos.last.t) alvo = pontos.last.t;

  var existente = -1;
  var menor = quadro;
  for (var i = 0; i < pontos.length; i++) {
    final d = (pontos[i].t - alvo).abs();
    if (d < menor || (existente < 0 && d == Duration.zero)) {
      menor = d;
      existente = i;
    }
  }
  if (existente >= 0) {
    return (pontos: pontos, indice: existente, criou: false);
  }

  final k = pontos.lastIndexWhere((p) => p.t < alvo);
  final a = pontos[k];
  final b = pontos[k + 1];
  final valor = coreValue(curvaDosPontos(pontos), alvo, linear: true);
  var anterior = a;
  PontoDeTempo novo;
  if (a.modo == ModoDoPonto.segurar) {
    novo = PontoDeTempo(t: alvo, v: a.v, modo: ModoDoPonto.segurar);
  } else if (a.modo == ModoDoPonto.livre && a.easeLivre != null) {
    final f = (alvo - a.t).inMicroseconds / (b.t - a.t).inMicroseconds;
    final partes = _dividirBezier(a.easeLivre!, f);
    if (partes != null) {
      anterior = a.copyWith(easeLivre: partes.$1);
      novo = PontoDeTempo(
        t: alvo,
        v: valor,
        modo: ModoDoPonto.livre,
        easeLivre: partes.$2,
      );
    } else {
      novo = PontoDeTempo(t: alvo, v: valor);
    }
  } else {
    final suave = a.modo == ModoDoPonto.suave || b.modo == ModoDoPonto.suave;
    novo = PontoDeTempo(
      t: alvo,
      v: valor,
      modo: suave ? ModoDoPonto.suave : ModoDoPonto.linear,
    );
  }
  final saida = [
    for (var i = 0; i <= k; i++) i == k ? anterior : pontos[i],
    novo,
    for (var i = k + 1; i < pontos.length; i++) pontos[i],
  ];
  return (pontos: saida, indice: k + 1, criou: true);
}

/// Parametro u da bezier em que x(u) = [x] (x1, x2 em 0..1: x e monotona).
double _parametroDoX(double x1, double x2, double x) {
  if (x <= 0) return 0;
  if (x >= 1) return 1;
  var lo = 0.0, hi = 1.0;
  for (var i = 0; i < 60; i++) {
    final u = (lo + hi) / 2;
    final w = 1 - u;
    final xu = 3 * w * w * u * x1 + 3 * w * u * u * x2 + u * u * u;
    if (xu < x) {
      lo = u;
    } else {
      hi = u;
    }
  }
  return (lo + hi) / 2;
}

/// Divide o easing bezier no progresso [f]: (antes, depois), cada metade
/// renormalizada para o proprio trecho. Nulo quando uma metade nao muda
/// de valor (nao ha como normalizar) ou o easing nao e bezier.
(Easing, Easing)? _dividirBezier(Easing e, double f) {
  if (e.type != EasingType.cubicBezier || f <= 0 || f >= 1) return null;
  final ax = e.x1.clamp(0.0, 1.0), bx = e.x2.clamp(0.0, 1.0);
  final u = _parametroDoX(ax, bx, f);
  double l(double p, double q) => p + (q - p) * u;
  final p01x = l(0, ax), p01y = l(0, e.y1);
  final p12x = l(ax, bx), p12y = l(e.y1, e.y2);
  final p23x = l(bx, 1), p23y = l(e.y2, 1);
  final p012x = l(p01x, p12x), p012y = l(p01y, p12y);
  final p123x = l(p12x, p23x), p123y = l(p12y, p23y);
  final mx = l(p012x, p123x), my = l(p012y, p123y);
  if (mx <= 1e-9 || mx >= 1 - 1e-9) return null;
  if (my.abs() < 1e-9 || (1 - my).abs() < 1e-9) return null;
  final antes = Easing(
    x1: p01x / mx,
    y1: p01y / my,
    x2: p012x / mx,
    y2: p012y / my,
  );
  final depois = Easing(
    x1: (p123x - mx) / (1 - mx),
    y1: (p123y - my) / (1 - my),
    x2: (p23x - mx) / (1 - mx),
    y2: (p23y - my) / (1 - my),
  );
  return (antes, depois);
}

/// MOVE O PONTO [i] respeitando pontas, vizinhos e a fonte.
///
/// O primeiro ponto fica no zero e o ultimo no fim do clipe: so sobem e
/// descem. Os do meio andam entre os vizinhos com [folga] de cada lado —
/// dois pontos no mesmo quadro nao dizem nada e se fundiriam no projeto.
/// O valor fica entre [vMin] (inicio do arquivo) e [vMax] (fim dele).
List<PontoDeTempo> moverPonto(
  List<PontoDeTempo> pontos,
  int i, {
  Duration? t,
  double? v,
  required Duration duracao,
  required double vMin,
  required double vMax,
  Duration folga = kFolgaEntrePontos,
}) {
  if (i < 0 || i >= pontos.length) return pontos;
  final p = pontos[i];
  final baixo = math.min(vMin, vMax), alto = math.max(vMin, vMax);
  var novoV = v ?? p.v;
  if (!novoV.isFinite) novoV = p.v;
  novoV = novoV.clamp(baixo, alto).toDouble();
  final Duration novoT;
  if (i == 0) {
    novoT = Duration.zero;
  } else if (i == pontos.length - 1) {
    novoT = duracao;
  } else {
    final minimo = pontos[i - 1].t + folga;
    final maximo = pontos[i + 1].t - folga;
    final pedido = t ?? p.t;
    if (minimo > maximo) {
      novoT = p.t;
    } else if (pedido < minimo) {
      novoT = minimo;
    } else if (pedido > maximo) {
      novoT = maximo;
    } else {
      novoT = pedido;
    }
  }
  return [
    for (var k = 0; k < pontos.length; k++)
      k == i ? p.copyWith(t: novoT, v: novoV) : pontos[k],
  ];
}

/// Onde o valor encaixou.
enum ImaDoValor {
  /// Mesmo valor do ponto anterior: o quadro para entre os dois.
  valorDoAnterior,

  /// Mesmo valor do proximo ponto.
  valorDoProximo,

  /// Na reta de velocidade normal (1x) que sai do ponto anterior.
  normalDesdeOAnterior,

  /// Na reta de velocidade normal que chega ao proximo ponto.
  normalAteOProximo,
}

/// O IMA: perto de um valor que significa algo, o valor encaixa nele.
///
/// Parar o quadro e voltar para a velocidade normal sao as duas coisas
/// que mais se quer fazer num Time Remap, e acertar isso no olho, com o
/// dedo em cima do ponto, e impossivel. [tolerancia] vem em segundos de
/// fonte (o widget converte uns 10 px).
({double valor, ImaDoValor? ima}) imaDoValor(
  List<PontoDeTempo> pontos,
  int i, {
  required Duration t,
  required double v,
  required double tolerancia,
}) {
  final candidatos = <(double, ImaDoValor)>[
    if (i > 0) (pontos[i - 1].v, ImaDoValor.valorDoAnterior),
    if (i + 1 < pontos.length) (pontos[i + 1].v, ImaDoValor.valorDoProximo),
    if (i > 0)
      (
        pontos[i - 1].v + _segundos(t - pontos[i - 1].t),
        ImaDoValor.normalDesdeOAnterior,
      ),
    if (i + 1 < pontos.length)
      (
        pontos[i + 1].v - _segundos(pontos[i + 1].t - t),
        ImaDoValor.normalAteOProximo,
      ),
  ];
  (double, ImaDoValor)? melhor;
  var distancia = tolerancia;
  for (final c in candidatos) {
    final d = (c.$1 - v).abs();
    if (d <= distancia && (melhor == null || d < distancia)) {
      melhor = c;
      distancia = d;
    }
  }
  if (melhor == null) return (valor: v, ima: null);
  return (valor: melhor.$1, ima: melhor.$2);
}

/// Velocidade media do trecho [i] (do ponto i ao i + 1): 1 = normal,
/// negativa = de tras para frente. Segurar e 0. Nulo fora da curva.
double? velocidadeDoTrecho(List<PontoDeTempo> pontos, int i) {
  if (i < 0 || i + 1 >= pontos.length) return null;
  if (pontos[i].modo == ModoDoPonto.segurar) return 0;
  final dt = _segundos(pontos[i + 1].t - pontos[i].t);
  if (dt <= 0) return 0;
  return (pontos[i + 1].v - pontos[i].v) / dt;
}

/// Ate onde a fonte deixa o valor ir, relativo a `sourceOffset`.
///
/// Sem a duracao do arquivo (projeto antigo), o teto e o maior entre o
/// dobro do maior valor e a duracao do clipe. Um valor que ja passa dos
/// limites alarga o limite em vez de pular no primeiro toque.
({double vMin, double vMax}) limitesDaFonte({
  required Duration sourceOffset,
  required Duration duracao,
  Duration? sourceDuration,
  List<PontoDeTempo> pontos = const [],
}) {
  var maior = 0.0, menor = 0.0;
  for (final p in pontos) {
    maior = math.max(maior, p.v);
    menor = math.min(menor, p.v);
  }
  final inicio = -_segundos(sourceOffset);
  final fim = sourceDuration == null
      ? math.max(2 * maior, _segundos(duracao))
      : _segundos(sourceDuration - sourceOffset);
  return (vMin: math.min(inicio, menor), vMax: math.max(fim, maior));
}

/// RETA: o video na [velocidade] do comeco ao fim.
///
/// Se a fonte acaba antes (com [vMax]), a reta segue na mesma velocidade
/// ate o fim do video e o resto do clipe segura o ultimo quadro — "1x"
/// continua querendo dizer 1x.
List<PontoDeTempo> curvaReta(
  Duration duracao, {
  double velocidade = 1,
  double? vMax,
}) {
  final total = _segundos(duracao) * velocidade;
  if (vMax == null || total <= vMax || velocidade <= 0) {
    return [
      const PontoDeTempo(t: Duration.zero, v: 0),
      PontoDeTempo(t: duracao, v: total),
    ];
  }
  final teto = math.max(0.0, vMax);
  final encontro = _duracao(teto / velocidade);
  if (encontro <= kFolgaEntrePontos ||
      encontro >= duracao - kFolgaEntrePontos) {
    return [
      const PontoDeTempo(t: Duration.zero, v: 0),
      PontoDeTempo(t: duracao, v: teto),
    ];
  }
  return [
    const PontoDeTempo(t: Duration.zero, v: 0),
    PontoDeTempo(t: encontro, v: teto),
    PontoDeTempo(t: duracao, v: teto),
  ];
}

/// Curva que nasce de velocidades por trecho: [fracoes] do clipe (0..1,
/// crescentes) e a velocidade de cada trecho entre elas. Os pontos
/// internos sao suaves; as pontas tambem, para o chip bater com o que a
/// leitura da trilha devolve. Passando de [vMax], a curva inteira encolhe
/// na mesma proporcao — o desenho fica, so fica mais lento.
List<PontoDeTempo> _pelasVelocidades(
  Duration duracao,
  List<double> fracoes,
  List<double> velocidades,
  double? vMax,
) {
  final total = _segundos(duracao);
  final valores = <double>[0];
  for (var i = 0; i < velocidades.length; i++) {
    valores.add(
      valores.last + (fracoes[i + 1] - fracoes[i]) * total * velocidades[i],
    );
  }
  final maior = valores.reduce(math.max);
  final escala = vMax != null && maior > vMax && maior > 0
      ? math.max(0.0, vMax) / maior
      : 1.0;
  return [
    for (var i = 0; i < fracoes.length; i++)
      PontoDeTempo(
        t: i == fracoes.length - 1
            ? duracao
            : Duration(
                microseconds: (duracao.inMicroseconds * fracoes[i]).round(),
              ),
        v: valores[i] * escala,
        modo: ModoDoPonto.suave,
      ),
  ];
}

/// 100% -> 30% -> 100%, com a desaceleracao e a volta suaves.
List<PontoDeTempo> curvaCameraLentaNoMeio(Duration duracao, {double? vMax}) =>
    _pelasVelocidades(duracao, const [0, .3, .7, 1], const [1, .3, 1], vMax);

/// Da metade da velocidade ao 1,5x; no fim, a mesma fonte que em 1x.
List<PontoDeTempo> curvaAcelerando(Duration duracao, {double? vMax}) =>
    _pelasVelocidades(
      duracao,
      const [0, 1 / 3, 2 / 3, 1],
      const [.5, 1, 1.5],
      vMax,
    );

/// De 1,5x a metade da velocidade; no fim, a mesma fonte que em 1x.
List<PontoDeTempo> curvaDesacelerando(Duration duracao, {double? vMax}) =>
    _pelasVelocidades(
      duracao,
      const [0, 1 / 3, 2 / 3, 1],
      const [1.5, 1, .5],
      vMax,
    );

/// v' = [eixo] - v em todos os pontos. Os modos e os easings livres
/// continuam valendo: o easing e normalizado pela diferenca de valor, e
/// espelhar troca o sinal dos dois lados da conta.
List<PontoDeTempo> espelharValores(List<PontoDeTempo> pontos, double eixo) => [
  for (final p in pontos) p.copyWith(v: eixo - p.v),
];

/// INVERTER: o clipe corre de tras para frente pelo MESMO trecho da fonte,
/// com os pontos nos mesmos tempos. Espelha dentro da propria faixa dos
/// pontos, entao nada sai dos limites que ja respeitava.
List<PontoDeTempo> inverterCurva(List<PontoDeTempo> pontos) {
  if (pontos.isEmpty) return pontos;
  var menor = pontos.first.v, maior = pontos.first.v;
  for (final p in pontos) {
    menor = math.min(menor, p.v);
    maior = math.max(maior, p.v);
  }
  return espelharValores(pontos, menor + maior);
}
