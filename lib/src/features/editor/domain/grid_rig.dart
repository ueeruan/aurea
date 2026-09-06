import 'dart:math' as math;
import 'dart:ui';

import 'fx.dart';
import 'keyframe.dart';

/// Modulo Grid (spec AM2-modulo-grid): rig NATIVO que vive num Objeto
/// Nulo. O rig calcula a transform de cada asset; a transform propria da
/// camada e aplicada POR CIMA, como offset — editar uma camada nunca
/// quebra a grade.
///
/// Modos: 1 = Retangular, 2 = Radial, 3 = Esferico (espiral de
/// Fibonacci). O morph anima [transition] entre eles usando o SEGMENTO de
/// keyframe (caminho mais curto: de 1 a 3 direto, sem passar pelo 2).

class GridRig {
  GridRig({
    List<String>? assets,
    this.columns = 3,
    AnimatedDouble? spacingX,
    AnimatedDouble? spacingY,
    AnimatedDouble? radius,
    this.spread = 0,
    AnimatedDouble? gridRotationDeg,
    AnimatedDouble? twistDeg,
    AnimatedDouble? staggerDeg,
    AnimatedDouble? zDepth,
    AnimatedDouble? scaleFront,
    AnimatedDouble? scaleBack,
    this.globalOpacity = 1,
    AnimatedDouble? randomOffset,
    this.seed = 1,
    this.shuffle = false,
    AnimatedDouble? transition,
    this.proximity,
    this.controllerId,
  })  : assets = List.unmodifiable(assets ?? const <String>[]),
        spacingX = spacingX ?? AnimatedDouble(260),
        spacingY = spacingY ?? AnimatedDouble(260),
        radius = radius ?? AnimatedDouble(320),
        gridRotationDeg = gridRotationDeg ?? AnimatedDouble(0),
        twistDeg = twistDeg ?? AnimatedDouble(0),
        staggerDeg = staggerDeg ?? AnimatedDouble(0),
        zDepth = zDepth ?? AnimatedDouble(0),
        scaleFront = scaleFront ?? AnimatedDouble(1),
        scaleBack = scaleBack ?? AnimatedDouble(1),
        randomOffset = randomOffset ?? AnimatedDouble(0),
        transition = transition ?? AnimatedDouble(1);

  /// Ordem da lista = indice do asset.
  final List<String> assets;

  final int columns;

  /// TODO parametro numerico do rig e ANIMAVEL — cada um com sua trilha
  /// de keyframes propria (pedido explicito do usuario).
  final AnimatedDouble spacingX;
  final AnimatedDouble spacingY;
  final AnimatedDouble radius;

  /// Radial: 0 = spread automatico (= N). Valores > N deixam lacunas.
  final double spread;

  /// Gira a DISPOSICAO mantendo os assets em pe (diferente de girar o
  /// nulo, que gira tudo junto).
  final AnimatedDouble gridRotationDeg;

  /// Rotacao igual em todos + offset progressivo: o stagger e o valor que
  /// chega no ULTIMO asset (o primeiro fica em 0).
  final AnimatedDouble twistDeg;
  final AnimatedDouble staggerDeg;

  /// Diferenca de Z entre indices consecutivos.
  final AnimatedDouble zDepth;

  /// Escala do primeiro e do ultimo indice, interpolada no meio.
  final AnimatedDouble scaleFront;
  final AnimatedDouble scaleBack;

  final double globalOpacity;

  /// Deslocamento aleatorio puro por (seed, indice) — deterministico.
  final AnimatedDouble randomOffset;
  final int seed;
  final bool shuffle;

  /// 1 = Retangular, 2 = Radial, 3 = Esferico. ANIMAVEL: com keyframes,
  /// o morph interpola os LAYOUTS pelo segmento (caminho mais curto).
  final AnimatedDouble transition;

  final ProximityGroup? proximity;

  /// Nulo CONTROLADOR (alem do nulo dono): o transform dele modula os
  /// parametros da grade — escala multiplica espacamento/raio, rotacao Z
  /// soma na rotacao da grade, rotacao Y soma no twist. Editar/animar o
  /// nulo (com keyframes e curvas normais) edita a grade.
  final String? controllerId;

  GridRig copyWith({
    List<String>? assets,
    int? columns,
    AnimatedDouble? spacingX,
    AnimatedDouble? spacingY,
    AnimatedDouble? radius,
    double? spread,
    AnimatedDouble? gridRotationDeg,
    AnimatedDouble? twistDeg,
    AnimatedDouble? staggerDeg,
    AnimatedDouble? zDepth,
    AnimatedDouble? scaleFront,
    AnimatedDouble? scaleBack,
    double? globalOpacity,
    AnimatedDouble? randomOffset,
    int? seed,
    bool? shuffle,
    AnimatedDouble? transition,
    ProximityGroup? proximity,
    bool clearProximity = false,
    String? controllerId,
    bool clearController = false,
  }) {
    return GridRig(
      assets: assets ?? this.assets,
      columns: columns ?? this.columns,
      spacingX: spacingX ?? this.spacingX,
      spacingY: spacingY ?? this.spacingY,
      radius: radius ?? this.radius,
      spread: spread ?? this.spread,
      gridRotationDeg: gridRotationDeg ?? this.gridRotationDeg,
      twistDeg: twistDeg ?? this.twistDeg,
      staggerDeg: staggerDeg ?? this.staggerDeg,
      zDepth: zDepth ?? this.zDepth,
      scaleFront: scaleFront ?? this.scaleFront,
      scaleBack: scaleBack ?? this.scaleBack,
      globalOpacity: globalOpacity ?? this.globalOpacity,
      randomOffset: randomOffset ?? this.randomOffset,
      seed: seed ?? this.seed,
      shuffle: shuffle ?? this.shuffle,
      transition: transition ?? this.transition,
      proximity:
          clearProximity ? null : (proximity ?? this.proximity),
      controllerId:
          clearController ? null : (controllerId ?? this.controllerId),
    );
  }
}

/// Trilha animavel da grade por nome — usada pelo controller e pelo
/// editor de curvas ('transition' e o morph).
AnimatedDouble? gridTrackOf(GridRig g, String key) => switch (key) {
      'spacingX' => g.spacingX,
      'spacingY' => g.spacingY,
      'radius' => g.radius,
      'rotation' => g.gridRotationDeg,
      'twist' => g.twistDeg,
      'stagger' => g.staggerDeg,
      'zDepth' => g.zDepth,
      'scaleFront' => g.scaleFront,
      'scaleBack' => g.scaleBack,
      'randomOffset' => g.randomOffset,
      'transition' => g.transition,
      _ => null,
    };

/// Substitui uma trilha da grade por nome (par do [gridTrackOf]).
GridRig gridWithTrack(GridRig g, String key, AnimatedDouble v) =>
    switch (key) {
      'spacingX' => g.copyWith(spacingX: v),
      'spacingY' => g.copyWith(spacingY: v),
      'radius' => g.copyWith(radius: v),
      'rotation' => g.copyWith(gridRotationDeg: v),
      'twist' => g.copyWith(twistDeg: v),
      'stagger' => g.copyWith(staggerDeg: v),
      'zDepth' => g.copyWith(zDepth: v),
      'scaleFront' => g.copyWith(scaleFront: v),
      'scaleBack' => g.copyWith(scaleBack: v),
      'randomOffset' => g.copyWith(randomOffset: v),
      'transition' => g.copyWith(transition: v),
      _ => g,
    };

/// Grupo de proximidade: o effector e uma ESFERA em 3D (raio de 200 =
/// tambem 200 de profundidade); o asset reage ao effector mais proximo —
/// aqui, v1 com um effector, arrastavel por keyframe.
class ProximityGroup {
  ProximityGroup({
    this.enabled = true,
    AnimatedOffset? effector,
    AnimatedDouble? effectorZ,
    AnimatedDouble? radius,
    AnimatedDouble? falloff,
    this.scaleMin = 1,
    this.scaleMax = 1.8,
    this.opacityMin = 1,
    this.opacityMax = 1,
    AnimatedDouble? attract,
  })  : effector = effector ?? AnimatedOffset(Offset.zero),
        effectorZ = effectorZ ?? AnimatedDouble(0),
        radius = radius ?? AnimatedDouble(220),
        falloff = falloff ?? AnimatedDouble(220),
        attract = attract ?? AnimatedDouble(0);

  final bool enabled;

  /// Posicao do effector RELATIVA ao nulo controlador.
  final AnimatedOffset effector;
  final AnimatedDouble effectorZ;
  final AnimatedDouble radius;
  final AnimatedDouble falloff;

  /// Canais: dentro do raio o asset vai para "max"; fora, "min".
  final double scaleMin;
  final double scaleMax;
  final double opacityMin;
  final double opacityMax;

  /// Positivo atrai o asset para o effector; negativo repele.
  final AnimatedDouble attract;

  ProximityGroup copyWith({
    bool? enabled,
    AnimatedOffset? effector,
    AnimatedDouble? effectorZ,
    AnimatedDouble? radius,
    AnimatedDouble? falloff,
    double? scaleMin,
    double? scaleMax,
    double? opacityMin,
    double? opacityMax,
    AnimatedDouble? attract,
  }) {
    return ProximityGroup(
      enabled: enabled ?? this.enabled,
      effector: effector ?? this.effector,
      effectorZ: effectorZ ?? this.effectorZ,
      radius: radius ?? this.radius,
      falloff: falloff ?? this.falloff,
      scaleMin: scaleMin ?? this.scaleMin,
      scaleMax: scaleMax ?? this.scaleMax,
      opacityMin: opacityMin ?? this.opacityMin,
      opacityMax: opacityMax ?? this.opacityMax,
      attract: attract ?? this.attract,
    );
  }
}

/// Transform que o rig calcula para um asset (no espaco do NULO — o
/// compositor aplica o nulo e a camada por cima).
class GridPlacement {
  const GridPlacement({
    required this.pos,
    required this.z,
    required this.rotationDeg,
    required this.scale,
    required this.opacity,
  });

  final Offset pos;
  final double z;
  final double rotationDeg;
  final double scale;
  final double opacity;
}

/// Valores do rig ja avaliados num instante.
typedef _RigValues = ({
  int columns,
  double sx,
  double sy,
  double radius,
  double spread,
});

/// Layout cru (sem parametros comuns) de um modo. Modo fora de 1..3 e
/// grampeado.
({Offset pos, double z}) _layoutAt(
    _RigValues v, double mode, int i, int n) {
  final m = mode.round().clamp(1, 3);
  switch (m) {
    case 1: // Retangular (§3.1)
      final c = math.max(1, v.columns);
      final rows = (n / c).ceil();
      final col = i % c;
      final row = i ~/ c;
      return (
        pos: Offset(
          (col - (c - 1) / 2) * v.sx,
          (row - (rows - 1) / 2) * v.sy,
        ),
        z: 0,
      );
    case 2: // Radial (§3.2): horario a partir do topo.
      final s = v.spread <= 0 ? n.toDouble() : v.spread;
      final theta = 2 * math.pi * i / s;
      return (
        pos: Offset(
            v.radius * math.sin(theta), -v.radius * math.cos(theta)),
        z: 0,
      );
    default: // Esferico (§3.4): espiral de Fibonacci.
      final phi = math.pi * (3 - math.sqrt(5));
      final y = 1 - (2 * i + 1) / n;
      final r = math.sqrt(math.max(0, 1 - y * y));
      final theta = i * phi;
      return (
        pos: Offset(r * math.cos(theta) * v.radius, y * v.radius),
        z: r * math.sin(theta) * v.radius,
      );
  }
}

/// Indice efetivo com shuffle deterministico (funcao pura de seed).
int gridEffectiveIndex(GridRig rig, int i, int n) {
  if (!rig.shuffle || n <= 1) return i;
  // Fisher-Yates com xorshift proprio (mesma do motor de texto).
  final order = List<int>.generate(n, (k) => k);
  var s = (rig.seed == 0 ? 0x9E3779B9 : rig.seed) & 0xFFFFFFFF;
  int next() {
    s ^= (s << 13) & 0xFFFFFFFF;
    s ^= s >> 17;
    s ^= (s << 5) & 0xFFFFFFFF;
    return s;
  }

  for (var k = n - 1; k > 0; k--) {
    final j = next() % (k + 1);
    final tmp = order[k];
    order[k] = order[j];
    order[j] = tmp;
  }
  return order[i];
}

/// Avalia o rig para o asset [i] de [n] no tempo [t] (§4 — ordem de
/// aplicacao). Tudo funcao pura: mesmo (rig, i, t) -> mesmo resultado.
/// [spacingMul]/[rotationAdd]/[twistAdd] vem do nulo CONTROLADOR
/// (escala/rotZ/rotY dele); nos padroes (1/0/0) nada muda.
GridPlacement gridPlacementAt(GridRig rig, int i, int n, Duration t,
    {double spacingMul = 1, double rotationAdd = 0, double twistAdd = 0}) {
  if (n <= 0) {
    return const GridPlacement(
        pos: Offset.zero, z: 0, rotationDeg: 0, scale: 1, opacity: 1);
  }
  final idx = gridEffectiveIndex(rig, i, n);

  // Cada parametro tem sua trilha de keyframes propria: avalia tudo
  // no instante [t].
  final vals = (
    columns: rig.columns,
    sx: rig.spacingX.valueAt(t) * spacingMul,
    sy: rig.spacingY.valueAt(t) * spacingMul,
    radius: rig.radius.valueAt(t) * spacingMul,
    spread: rig.spread,
  );

  // 2. posicao base — morph pelo SEGMENTO (caminho mais curto §3.5):
  // animando transition de 1 a 3, interpola os layouts 1 e 3 DIRETO.
  final seg = rig.transition.segmentAt(t);
  ({Offset pos, double z}) base;
  if (seg != null) {
    final a = _layoutAt(vals, seg.from, idx, n);
    final b = _layoutAt(vals, seg.to, idx, n);
    base = (
      pos: Offset.lerp(a.pos, b.pos, seg.fraction)!,
      z: a.z + (b.z - a.z) * seg.fraction,
    );
  } else {
    base = _layoutAt(vals, rig.transition.valueAt(t), idx, n);
  }

  var pos = base.pos;
  var z = base.z;

  // 3. Z depth por indice.
  z += idx * rig.zDepth.valueAt(t);

  // 4. random offset puro.
  final randomOffset = rig.randomOffset.valueAt(t);
  if (randomOffset > 0) {
    pos += Offset(
      fxNoiseSigned(rig.seed, 11, idx * 7.31) * randomOffset,
      fxNoiseSigned(rig.seed, 12, idx * 7.31) * randomOffset,
    );
  }

  // 5. grid rotation: gira a disposicao, assets em pe.
  final gridRot = rig.gridRotationDeg.valueAt(t) + rotationAdd;
  if (gridRot != 0) {
    final a = gridRot * math.pi / 180;
    final c = math.cos(a);
    final s = math.sin(a);
    pos = Offset(pos.dx * c - pos.dy * s, pos.dx * s + pos.dy * c);
  }

  // 6. twist + stagger (o stagger e o valor do ULTIMO asset).
  final frac = n <= 1 ? 0.0 : idx / (n - 1);
  final rot = rig.twistDeg.valueAt(t) +
      twistAdd +
      rig.staggerDeg.valueAt(t) * frac;

  // 7. escala por posicao (Off: t = i/(N-1)).
  final scaleFront = rig.scaleFront.valueAt(t);
  final scaleBack = rig.scaleBack.valueAt(t);
  var scale = scaleFront + (scaleBack - scaleFront) * frac;
  var opacity = rig.globalOpacity.clamp(0.0, 1.0);

  // 9. proximidade: esfera 3D, smoothstep decrescente no falloff.
  final prox = rig.proximity;
  if (prox != null && prox.enabled) {
    final e = prox.effector.valueAt(t);
    final ez = prox.effectorZ.valueAt(t);
    final radius = prox.radius.valueAt(t);
    final falloff = math.max(1.0, prox.falloff.valueAt(t));
    final delta = pos - e;
    final dz = z - ez;
    final d = math.sqrt(
        delta.dx * delta.dx + delta.dy * delta.dy + dz * dz);
    double w;
    if (d <= radius) {
      w = 1;
    } else if (d >= radius + falloff) {
      w = 0;
    } else {
      final u = (d - radius) / falloff;
      w = 1 - u * u * (3 - 2 * u); // smoothstep decrescente
    }
    if (w > 0) {
      scale *= prox.scaleMin + (prox.scaleMax - prox.scaleMin) * w;
      opacity *=
          prox.opacityMin + (prox.opacityMax - prox.opacityMin) * w;
      final attract = prox.attract.valueAt(t);
      if (attract != 0 && d > 1e-3) {
        final dir = Offset(-delta.dx / d, -delta.dy / d);
        pos += dir * attract * w;
      }
    } else {
      scale *= prox.scaleMin;
      opacity *= prox.opacityMin;
    }
  }

  return GridPlacement(
    pos: pos,
    z: z,
    rotationDeg: rot,
    scale: scale,
    opacity: opacity.clamp(0.0, 1.0),
  );
}
