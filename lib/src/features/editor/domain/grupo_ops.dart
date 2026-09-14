import 'dart:math' as math;
import 'dart:ui';

import 'keyframe.dart';
import 'layer.dart';

/// GRUPOS: as contas que o agrupar e o desagrupar precisam para nao pular.
///
/// Um grupo desenha os filhos numa caixa do tamanho da composicao, com a
/// transformacao dele aplicada por fora:
///
///     q -> posicao + pivo + R(rotacao) * S(escala) * (q - centro - pivo)
///
/// onde q e um ponto dos filhos em coordenadas da composicao e centro e o
/// centro da composicao. Desagrupar e passar essa conta para cada filho.

/// Onde o ponto [q] dos filhos aparece na tela, pela transformacao do grupo
/// [g] no instante LOCAL do grupo [tg].
Offset pontoPeloGrupo(GroupLayer g, Offset q, Duration tg, Offset centro) {
  final pos = g.position.valueAt(tg);
  final piv = g.pivot.valueAt(tg);
  final rot = g.rotation.valueAt(tg) * math.pi / 180;
  final sx = g.scaleX.valueAt(tg);
  final sy = g.scaleY.valueAt(tg);
  final v = q - centro - piv;
  final e = Offset(v.dx * sx, v.dy * sy);
  final c = math.cos(rot), s = math.sin(rot);
  return pos + piv + Offset(e.dx * c - e.dy * s, e.dx * s + e.dy * c);
}

/// O grupo tem transformacao que muda com o tempo?
bool grupoAnimado(GroupLayer g) =>
    g.position.isAnimated ||
    g.pivot.isAnimated ||
    g.rotation.isAnimated ||
    g.scaleX.isAnimated ||
    g.scaleY.isAnimated ||
    g.opacity.isAnimated;

/// OS FILHOS FORA DO GRUPO, sem mudar um pixel.
///
/// Cada filho recebe o tempo absoluto e a transformacao do grupo somada a
/// dele: posicao pelo mapa acima (com o pivo do filho no lugar certo),
/// rotacao somada, escala e opacidade multiplicadas.
///
/// * GRUPO PARADO: a conta e afim, entao cada keyframe do filho e levado
///   sozinho e a curva de cada um se mantem;
/// * GRUPO ANIMADO: as duas animacoes se misturam — os filhos sao
///   reamostrados nos keyframes dos dois e a cada dois quadros entre eles.
///
/// O que nao cabe num filho (efeitos, mascaras, mescla e remapeamento de
/// tempo do grupo; inclinacao e 3D do grupo) e descartado, e o aviso diz.
({List<Layer> filhos, List<String> avisos}) filhosDesagrupados(
  GroupLayer g, {
  required Offset centro,
  int fps = 30,
}) {
  final avisos = <String>[
    if (g.effects.any((e) => e.enabled)) 'Os efeitos do grupo foram descartados.',
    if (g.masks.isNotEmpty) 'As mascaras do grupo foram descartadas.',
    if (g.timeRemap != null)
      'O remapeamento de tempo do grupo foi descartado.',
    if (g.blendMode != BlendMode.srcOver || g.customBlend != null)
      'A mescla do grupo foi descartada.',
    if (g.skewX.isAnimated ||
        g.skewY.isAnimated ||
        g.skewX.base != 0 ||
        g.skewY.base != 0 ||
        g.is3D ||
        g.rotationX.isAnimated ||
        g.rotationY.isAnimated ||
        g.rotationX.base != 0 ||
        g.rotationY.base != 0)
      'A inclinacao e o giro 3D do grupo nao passam para os filhos.',
  ];
  final animado = grupoAnimado(g);
  return (
    filhos: [
      for (final c in g.children)
        animado
            ? _reamostrado(g, c, centro, fps)
            : _levadoParado(g, c, centro),
    ],
    avisos: avisos,
  );
}

Layer _levadoParado(GroupLayer g, Layer c, Offset centro) {
  const tg = Duration.zero;
  final rotG = g.rotation.valueAt(tg);
  final sxG = g.scaleX.valueAt(tg);
  final syG = g.scaleY.valueAt(tg);
  final opG = g.opacity.valueAt(tg);
  final neutro =
      g.position.valueAt(tg) == centro &&
      g.pivot.valueAt(tg) == Offset.zero &&
      rotG == 0 &&
      sxG == 1 &&
      syG == 1 &&
      opG == 1;
  if (neutro) return c.copyLayer(startTime: g.startTime + c.startTime);

  // O pivo do filho entra na conta: e em volta dele que o filho gira, e
  // e ele que tem de cair no mesmo ponto da tela.
  Offset leva(Offset p, Offset pivoDoFilho) =>
      pontoPeloGrupo(g, p + pivoDoFilho, tg, centro) - pivoDoFilho;

  final AnimatedOffset posicao;
  if (!c.pivot.isAnimated) {
    final piv = c.pivot.base;
    posicao = AnimatedOffset(
      leva(c.position.base, piv),
      [
        for (final k in c.position.keyframes)
          Keyframe(time: k.time, value: leva(k.value, piv), ease: k.ease),
      ],
      c.position.loop,
    );
  } else {
    // Pivo animado: a posicao depende dele a cada instante.
    final tempos = <Duration>{
      Duration.zero,
      for (final k in c.position.keyframes) k.time,
      for (final k in c.pivot.keyframes) k.time,
    }.toList()..sort();
    posicao = AnimatedOffset(leva(c.position.valueAt(Duration.zero), c.pivot.valueAt(Duration.zero)), [
      for (final t in tempos)
        Keyframe(time: t, value: leva(c.position.valueAt(t), c.pivot.valueAt(t))),
    ]);
  }

  AnimatedDouble mapa(AnimatedDouble a, double Function(double) f) =>
      AnimatedDouble(
        f(a.base),
        [
          for (final k in a.keyframes)
            Keyframe(time: k.time, value: f(k.value), ease: k.ease),
        ],
        a.loop,
        a.expression,
      );

  return c.copyLayer(
    startTime: g.startTime + c.startTime,
    position: posicao,
    rotation: mapa(c.rotation, (v) => v + rotG),
    scaleX: mapa(c.scaleX, (v) => v * sxG),
    scaleY: mapa(c.scaleY, (v) => v * syG),
    opacity: mapa(c.opacity, (v) => v * opG),
  );
}

Layer _reamostrado(GroupLayer g, Layer c, Offset centro, int fps) {
  final passo = Duration(microseconds: (2 * 1000000 / (fps < 1 ? 30 : fps)).round());
  final tempos = <Duration>{Duration.zero, c.duration};
  void junta(Iterable<Duration> ts, Duration deslocamento) {
    for (final t in ts) {
      final local = t - deslocamento;
      if (local >= Duration.zero && local <= c.duration) tempos.add(local);
    }
  }

  // Keyframes do grupo estao no tempo do grupo; os do filho, no dele.
  junta([
    for (final a in [g.position.keyframes, g.pivot.keyframes])
      for (final k in a) k.time,
    for (final a in [
      g.rotation.keyframes,
      g.scaleX.keyframes,
      g.scaleY.keyframes,
      g.opacity.keyframes,
    ])
      for (final k in a) k.time,
  ], c.startTime);
  junta([
    for (final a in [c.position.keyframes, c.pivot.keyframes])
      for (final k in a) k.time,
    for (final a in [
      c.rotation.keyframes,
      c.scaleX.keyframes,
      c.scaleY.keyframes,
      c.opacity.keyframes,
    ])
      for (final k in a) k.time,
  ], Duration.zero);
  // Entre keyframes as duas curvas se misturam: a cada dois quadros, com
  // teto para uma camada longa nao virar milhares de keyframes.
  final quantos = c.duration.inMicroseconds ~/ math.max(1, passo.inMicroseconds);
  final pulo = math.max(1, (quantos / 400).ceil());
  for (var i = 0; i <= quantos; i += pulo) {
    tempos.add(passo * i);
  }
  final ordem = tempos.toList()..sort();

  Keyframe<T> kf<T>(Duration t, T v) => Keyframe(time: t, value: v);
  final pos = <Keyframe<Offset>>[];
  final rot = <Keyframe<double>>[];
  final sx = <Keyframe<double>>[];
  final sy = <Keyframe<double>>[];
  final op = <Keyframe<double>>[];
  for (final t in ordem) {
    final tg = c.startTime + t;
    final piv = c.pivot.valueAt(t);
    pos.add(kf(t, pontoPeloGrupo(g, c.position.valueAt(t) + piv, tg, centro) - piv));
    rot.add(kf(t, c.rotation.valueAt(t) + g.rotation.valueAt(tg)));
    sx.add(kf(t, c.scaleX.valueAt(t) * g.scaleX.valueAt(tg)));
    sy.add(kf(t, c.scaleY.valueAt(t) * g.scaleY.valueAt(tg)));
    op.add(kf(t, c.opacity.valueAt(t) * g.opacity.valueAt(tg)));
  }
  return c.copyLayer(
    startTime: g.startTime + c.startTime,
    position: AnimatedOffset(pos.first.value, pos),
    rotation: AnimatedDouble(rot.first.value, rot),
    scaleX: AnimatedDouble(sx.first.value, sx),
    scaleY: AnimatedDouble(sy.first.value, sy),
    opacity: AnimatedDouble(op.first.value, op),
  );
}

/// AS MIDIAS DE DENTRO DOS GRUPOS, com tempo absoluto.
///
/// Video e audio dentro de grupo nao tocavam: o gerenciador de tocadores,
/// a extracao de quadros da exportacao e a mixagem de som so olhavam o
/// nivel de cima, e o video virava o icone de filme — tambem no arquivo
/// exportado. Aqui as camadas de cima continuam como estao e cada midia
/// de dentro de um grupo entra de novo, com o inicio somado ao do grupo e
/// o fim cortado no fim dele (o grupo nao mostra nada depois do proprio
/// fim). O id nao muda: o palco acha o tocador pelo id do filho.
///
/// Sem grupo nenhum devolve a PROPRIA lista — quem compara por identidade
/// (o gerenciador de video decide assim se a cena mudou) nao perde nada.
List<Layer> midiasAchatadas(List<Layer> layers) {
  if (!layers.any((l) => l is GroupLayer)) return layers;
  final out = <Layer>[...layers];
  void abrir(List<Layer> lista, Duration deslocamento, Duration fim) {
    for (final l in lista) {
      if (l is GroupLayer) {
        // Conteudo remapeado nao anda junto com a linha do tempo: o
        // tocador nao tem como seguir a curva.
        if (l.timeRemap != null) continue;
        final ini = deslocamento + l.startTime;
        final f = ini + l.duration;
        abrir(l.children, ini, f < fim ? f : fim);
        continue;
      }
      if (l is! VideoLayer && l is! AudioLayer) continue;
      final ini = deslocamento + l.startTime;
      var dur = l.duration;
      if (ini + dur > fim) dur = fim - ini;
      if (dur <= Duration.zero) continue;
      out.add(l.copyLayer(startTime: ini, duration: dur));
    }
  }

  for (final l in layers) {
    if (l is GroupLayer && l.timeRemap == null) {
      abrir(l.children, l.startTime, l.startTime + l.duration);
    }
  }
  return out;
}
