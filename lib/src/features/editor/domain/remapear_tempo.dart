// REMAPEAR TEMPO — a leitura estilo After Effects da trilha 'tempo'.
//
// A trilha continua sendo o que sempre foi: keyframes (tempo local ->
// segundos de fonte) com uma bezier cubica por trecho, avaliados no nucleo
// C++ (aurea_timecore). O que este arquivo adiciona e o VOCABULARIO do AE
// por cima das alcas que ja existem:
//
//   velocidade  ds/dt no keyframe (1 = tempo real; 0 = congelado;
//               negativo = reverso), um valor por LADO (entrada/saida);
//   influencia  quanto do trecho a alca alcanca (0..1).
//
// A conta e exata, nao aproximada. Num trecho K0=(t0,s0) -> K1=(t1,s1) o
// valor e s0 + Δs·f(x), com f a bezier normalizada P0=(0,0), P1=(x1,y1),
// P2=(x2,y2), P3=(1,1). A alca de saida de K0 mora em (x1,y1):
//
//   x1 = influencia            y1 = velocidade · influencia · Δt/Δs
//
// e a de entrada de K1 em (x2,y2) espelhada da ponta. Como o nucleo ja
// resolve x(u)=x por Newton/bissecao, a curva desenhada, a derivada do
// grafo de velocidade e o quadro mostrado saem TODOS da mesma conta.
//
// Trecho com Δs = 0 e congelamento por construcao (o valor nao depende da
// alca); e por isso que Congelar e robusto: dois keyframes de mesmo valor.
import 'dart:math' as math;

import 'keyframe.dart';
import 'time_core.dart';

/// Influencia minima: alca em cima da ancora deixaria a velocidade sem
/// definicao (0/0). O AE usa 0,1%; aqui 0,5% ja segura a conta.
const _influenciaMinima = 0.005;

/// A suavidade de UM lado de um keyframe, no vocabulario do AE.
class SuavidadeTemporal {
  const SuavidadeTemporal({required this.velocidade, required this.influencia});

  /// Segundos de fonte por segundo de timeline (1 = tempo real).
  final double velocidade;

  /// Fracao do trecho que a alca alcanca (0..1).
  final double influencia;
}

/// O tipo que o ponto APARENTA — derivado das alcas, nao guardado.
enum TipoDoPontoDeTempo { manter, linear, bezier }

/// Um keyframe da trilha visto pelo estudio: tempo, valor e as duas
/// suavidades. [entrada]/[saida] nulas nas pontas (sem trecho daquele lado).
class PontoDeTempo {
  const PontoDeTempo({
    required this.indice,
    required this.tempo,
    required this.valor,
    required this.tipo,
    this.entrada,
    this.saida,
  });

  final int indice;
  final Duration tempo;

  /// Segundos de fonte.
  final double valor;
  final TipoDoPontoDeTempo tipo;
  final SuavidadeTemporal? entrada;
  final SuavidadeTemporal? saida;
}

double _segundos(Duration d) => d.inMicroseconds / 1000000.0;

/// A inclinacao media do trecho [a] -> [b] (segundos de fonte por segundo).
double _secante(Keyframe<double> a, Keyframe<double> b) {
  final dt = _segundos(b.time - a.time);
  return dt <= 0 ? 0 : (b.value - a.value) / dt;
}

/// Velocidade REAL na ponta de um trecho: a derivada da bezier (ou do
/// easing que estiver la) vezes a secante. `Easing.speedAt` e analitico
/// na bezier; nos outros tipos usa diferenca central — serve igual.
double _velocidadeNaPonta(Keyframe<double> a, Keyframe<double> b, double x) {
  final m = _secante(a, b);
  if (a.ease.type == EasingType.hold) return 0;
  final v = a.ease.speedAt(x) * m;
  return v.isFinite ? v : (v.isNegative ? -1e6 : 1e6) * (m == 0 ? 0 : 1);
}

SuavidadeTemporal _saidaDe(Keyframe<double> a, Keyframe<double> b) {
  final e = a.ease;
  final influencia = e.type == EasingType.cubicBezier
      ? e.x1.clamp(_influenciaMinima, 1.0)
      : 1 / 3;
  return SuavidadeTemporal(
    velocidade: _velocidadeNaPonta(a, b, 0),
    influencia: e.isLinear ? 1 / 3 : influencia,
  );
}

SuavidadeTemporal _entradaDe(Keyframe<double> a, Keyframe<double> b) {
  final e = a.ease;
  final influencia = e.type == EasingType.cubicBezier
      ? (1 - e.x2).clamp(_influenciaMinima, 1.0)
      : 1 / 3;
  return SuavidadeTemporal(
    velocidade: _velocidadeNaPonta(a, b, 1),
    influencia: e.isLinear ? 1 / 3 : influencia,
  );
}

TipoDoPontoDeTempo _tipoDe(AnimatedDouble track, int i) {
  final ks = track.keyframes;
  final saida = i < ks.length - 1 ? ks[i].ease : null;
  if (saida?.type == EasingType.hold) return TipoDoPontoDeTempo.manter;
  final entrada = i > 0 ? ks[i - 1].ease : null;
  final saidaLinear = saida == null || saida.isLinear;
  final entradaLinear =
      entrada == null ||
      entrada.isLinear ||
      // A alca de entrada deste ponto e a METADE de la do trecho anterior:
      // (x2,y2) na ancora = reta chegando, mesmo que a saida de la curve.
      (entrada.type == EasingType.cubicBezier &&
          entrada.x2 == 1 &&
          entrada.y2 == 1);
  return saidaLinear && entradaLinear
      ? TipoDoPontoDeTempo.linear
      : TipoDoPontoDeTempo.bezier;
}

/// Todos os keyframes da trilha, ja no vocabulario do estudio.
List<PontoDeTempo> pontosDaTrilha(AnimatedDouble track) {
  final ks = track.keyframes;
  return [
    for (var i = 0; i < ks.length; i++)
      PontoDeTempo(
        indice: i,
        tempo: ks[i].time,
        valor: ks[i].value,
        tipo: _tipoDe(track, i),
        entrada: i > 0 ? _entradaDe(ks[i - 1], ks[i]) : null,
        saida: i < ks.length - 1 ? _saidaDe(ks[i], ks[i + 1]) : null,
      ),
  ];
}

/// A alca normalizada de um lado, a partir de velocidade e influencia.
/// Δs = 0 (congelado) deixa y = 0: a curva e chata de qualquer jeito.
(double, double) _alca(SuavidadeTemporal s, double dt, double ds) {
  final x = s.influencia.clamp(_influenciaMinima, 1.0);
  final y = ds.abs() < 1e-12 ? 0.0 : s.velocidade * x * dt / ds;
  return (x, y);
}

Easing _easeComLados(
  Easing atual,
  double dt,
  double ds, {
  SuavidadeTemporal? saida,
  SuavidadeTemporal? entrada,
}) {
  // Escrever uma alca converte o trecho para bezier; a outra ponta
  // permanece onde estava (reta = alca na ancora oposta).
  var e = atual.type == EasingType.cubicBezier
      ? atual
      : const Easing(); // linear
  if (saida != null) {
    final (x1, y1) = _alca(saida, dt, ds);
    e = e.copyWith(type: EasingType.cubicBezier, x1: x1, y1: y1);
  }
  if (entrada != null) {
    final (x, y) = _alca(entrada, dt, ds);
    e = e.copyWith(type: EasingType.cubicBezier, x2: 1 - x, y2: 1 - y);
  }
  return e;
}

AnimatedDouble _comEaseDoTrecho(AnimatedDouble track, int trecho, Easing e) =>
    AnimatedDouble(track.base, [
      for (var i = 0; i < track.keyframes.length; i++)
        i == trecho ? track.keyframes[i].copyWith(ease: e) : track.keyframes[i],
    ], track.loop, track.expression);

/// PoE a suavidade pedida nos lados do ponto [indice]. Cada lado mexe so
/// na metade da alca que lhe pertence: a saida vive no ease do proprio
/// ponto, a entrada no ease do ponto anterior.
AnimatedDouble trilhaComSuavidade(
  AnimatedDouble track,
  int indice, {
  SuavidadeTemporal? entrada,
  SuavidadeTemporal? saida,
}) {
  final ks = track.keyframes;
  if (indice < 0 || indice >= ks.length) return track;
  var out = track;
  if (saida != null && indice < ks.length - 1) {
    final a = ks[indice], b = ks[indice + 1];
    out = _comEaseDoTrecho(
      out,
      indice,
      _easeComLados(
        a.ease,
        _segundos(b.time - a.time),
        b.value - a.value,
        saida: saida,
      ),
    );
  }
  if (entrada != null && indice > 0) {
    final a = out.keyframes[indice - 1], b = out.keyframes[indice];
    out = _comEaseDoTrecho(
      out,
      indice - 1,
      _easeComLados(
        a.ease,
        _segundos(b.time - a.time),
        b.value - a.value,
        entrada: entrada,
      ),
    );
  }
  return out;
}

/// Manter / Linear no ponto [indice]. Manter congela o trecho de SAIDA
/// (convencao do AE); Linear endireita as duas metades de alca do ponto.
AnimatedDouble trilhaComTipo(
  AnimatedDouble track,
  int indice,
  TipoDoPontoDeTempo tipo,
) {
  final ks = track.keyframes;
  if (indice < 0 || indice >= ks.length) return track;
  switch (tipo) {
    case TipoDoPontoDeTempo.manter:
      if (indice >= ks.length - 1) return track;
      return _comEaseDoTrecho(
        track,
        indice,
        const Easing(type: EasingType.hold),
      );
    case TipoDoPontoDeTempo.linear:
      var out = track;
      if (indice < ks.length - 1) {
        final e = ks[indice].ease;
        out = _comEaseDoTrecho(
          out,
          indice,
          e.type == EasingType.cubicBezier
              ? e.copyWith(x1: 0, y1: 0)
              : const Easing(),
        );
      }
      if (indice > 0) {
        final e = out.keyframes[indice - 1].ease;
        out = _comEaseDoTrecho(
          out,
          indice - 1,
          e.type == EasingType.cubicBezier
              ? e.copyWith(x2: 1, y2: 1)
              : const Easing(),
        );
      }
      return out;
    case TipoDoPontoDeTempo.bezier:
      return trilhaAutoBezier(track, indice);
  }
}

/// EASY EASE do AE: velocidade 0 e influencia 1/3 nos lados pedidos.
AnimatedDouble trilhaSuavizada(
  AnimatedDouble track,
  int indice, {
  bool entrada = true,
  bool saida = true,
}) => trilhaComSuavidade(
  track,
  indice,
  entrada: entrada
      ? const SuavidadeTemporal(velocidade: 0, influencia: 1 / 3)
      : null,
  saida: saida
      ? const SuavidadeTemporal(velocidade: 0, influencia: 1 / 3)
      : null,
);

/// AUTO BEZIER: tangente continua pelos vizinhos (secante deles, como a
/// Catmull-Rom) com influencia 1/3 — o "suave por conta propria" do AE.
AnimatedDouble trilhaAutoBezier(AnimatedDouble track, int indice) {
  final ks = track.keyframes;
  if (indice < 0 || indice >= ks.length) return track;
  final antes = indice > 0 ? ks[indice - 1] : null;
  final depois = indice < ks.length - 1 ? ks[indice + 1] : null;
  final double m;
  if (antes != null && depois != null) {
    final dt = _segundos(depois.time - antes.time);
    m = dt <= 0 ? 0 : (depois.value - antes.value) / dt;
  } else if (depois != null) {
    m = _secante(ks[indice], depois);
  } else if (antes != null) {
    m = _secante(antes, ks[indice]);
  } else {
    return track;
  }
  final s = SuavidadeTemporal(velocidade: m, influencia: 1 / 3);
  return trilhaComSuavidade(track, indice, entrada: s, saida: s);
}

/// BEZIER CONTINUO: as duas velocidades viram a media (a tangente nao
/// quebra no ponto); as influencias ficam como estao.
AnimatedDouble trilhaContinua(AnimatedDouble track, int indice) {
  final pontos = pontosDaTrilha(track);
  if (indice < 0 || indice >= pontos.length) return track;
  final p = pontos[indice];
  final e = p.entrada, s = p.saida;
  if (e == null || s == null) return track;
  final m = (e.velocidade + s.velocidade) / 2;
  return trilhaComSuavidade(
    track,
    indice,
    entrada: SuavidadeTemporal(velocidade: m, influencia: e.influencia),
    saida: SuavidadeTemporal(velocidade: m, influencia: s.influencia),
  );
}

/// O valor da curva (segundos de fonte) — a MESMA conta do nucleo que
/// escolhe o quadro, entao o grafico nunca mente.
double valorDaCurva(AnimatedDouble track, Duration t) => coreValue(track, t);

/// A velocidade ds/dt (grafo de velocidade). 1 = 100%.
double velocidadeDaCurva(AnimatedDouble track, Duration t) =>
    coreSlope(track, t);

/// Faixa real de valores (com overshoot), para enquadrar o grafico.
(double, double) faixaDaCurva(AnimatedDouble track) => coreRange(track);

/// A curva DE TRAS PARA FRENTE: v'(t) = span - v(t). Espelhar os valores
/// mantendo as alcas espelha a curva inteira (o easing age na fracao, e a
/// fracao nao muda). E a mesma conta do assar de reverso de sempre.
AnimatedDouble curvaEspelhada(AnimatedDouble track, double spanSeconds) =>
    AnimatedDouble(track.base, [
      for (final k in track.keyframes) k.copyWith(value: spanSeconds - k.value),
    ], track.loop, track.expression);

/// A identidade de um clipe: dois keyframes reproduzindo normal.
AnimatedDouble curvaIdentidade(Duration duration, double spanSeconds) =>
    AnimatedDouble(0)
        .withKeyframe(Duration.zero, 0)
        .withKeyframe(duration, spanSeconds);

/// REVERSO A PARTIR DE [t]: o que ja passou fica; dali em diante a curva
/// espelha em torno do valor atual (v' = 2·v(t) − v), presa ao intervalo
/// [0, span] — voltar alem do comeco seguraria no quadro zero de qualquer
/// jeito, e o grafico deve mostrar isso.
AnimatedDouble reversoAPartirDe(
  AnimatedDouble track,
  Duration t,
  double spanSeconds,
) {
  final pivo = coreValue(track, t);
  double espelha(double v) => (2 * pivo - v).clamp(0.0, spanSeconds);
  final antes = [
    for (final k in track.keyframes)
      if (k.time < t - kToleranciaDoKeyframe) k,
  ];
  final depois = [
    for (final k in track.keyframes)
      if (k.time > t + kToleranciaDoKeyframe)
        k.copyWith(value: espelha(k.value)),
  ];
  return AnimatedDouble(track.base, [
    ...antes,
    Keyframe(time: t, value: pivo, ease: Easing.linear),
    ...depois,
  ], track.loop, track.expression);
}

/// SNAP DO ESTUDIO: gruda [t] na grade de quadros e, mais forte, nas
/// [guias] (cabecote, batidas, marcadores) quando a distancia cabe em
/// [tolerancia]. Devolve tambem SE grudou numa guia (para o clique hapt.).
({Duration tempo, bool naGuia}) ajustarTempoComGrade(
  Duration t, {
  required int fps,
  List<Duration> guias = const [],
  Duration tolerancia = const Duration(milliseconds: 40),
}) {
  for (final g in guias) {
    if ((t - g).abs() <= tolerancia) return (tempo: g, naGuia: true);
  }
  final quadroUs = 1000000 / math.max(1, fps);
  final us = (t.inMicroseconds / quadroUs).round() * quadroUs;
  return (tempo: Duration(microseconds: us.round()), naGuia: false);
}
