import 'dart:ui';

import 'effect.dart';
import 'layer.dart';
import 'mask.dart';
import 'video_project.dart';

/// Arquitetura de MARCHAS (spec AUREA-arquitetura-de-marchas): um editor
/// 2-em-1 e um motor que sabe quando NAO precisa compor.
///
/// PR-G1: o classificador e uma funcao PURA cena -> marcha, com o motivo
/// em texto, recalculada quando a CENA muda — nunca por frame. No
/// Flutter, a marcha alimenta duas coisas hoje: o overlay de diagnostico
/// (§9) e o portao de recomposicao (cena estatica nao recompoe por tick,
/// o analogo Flutter de "nao esta compondo — esta tocando um video").
/// O caminho de overlay de hardware (M1/M2 nativas com SurfaceView)
/// depende de platform view em Kotlin e fica para a fase nativa.
enum PreviewGear { m1, m2, m3, m4 }

String gearLabel(PreviewGear g) => switch (g) {
      PreviewGear.m1 => 'M1 · player',
      PreviewGear.m2 => 'M2 · player+grafismos',
      PreviewGear.m3 => 'M3 · GL direto',
      PreviewGear.m4 => 'M4 · compositor',
    };

class GearDecision {
  const GearDecision(this.gear, this.reason);

  final PreviewGear gear;

  /// Motivo TEXTUAL (§9): em M4, diz o que forcou; nas outras, o que a
  /// cena tem. Cada reclamacao de lentidao vira um relatorio util.
  final String reason;
}

bool _isGraphic(Layer l) =>
    l is TextLayer ||
    l is ShapeLayer ||
    l is ImageLayer ||
    l is CaptionLayer ||
    l is Element3DLayer;

/// Camadas que PINTAM pixels (nulos e audio ficam de fora da conta).
List<Layer> _paintable(List<Layer> layers) => [
      for (final l in layers)
        if (l is! NullLayer && l is! AudioLayer) l,
    ];

bool _identityOpacity(Layer l) =>
    !l.opacity.isAnimated && (l.opacity.valueAt(Duration.zero) - 1).abs() < 1e-6;

bool _simpleTransform(Layer l) =>
    !l.rotation.isAnimated &&
    l.rotation.valueAt(Duration.zero).abs() < 1e-6 &&
    !l.rotationX.isAnimated &&
    l.rotationX.valueAt(Duration.zero).abs() < 1e-6 &&
    !l.rotationY.isAnimated &&
    l.rotationY.valueAt(Duration.zero).abs() < 1e-6 &&
    !l.skewX.isAnimated &&
    l.skewX.valueAt(Duration.zero).abs() < 1e-6 &&
    !l.skewY.isAnimated &&
    l.skewY.valueAt(Duration.zero).abs() < 1e-6 &&
    !l.positionZ.isAnimated &&
    l.positionZ.valueAt(Duration.zero).abs() < 1e-6;

/// O que forca M4 nesta camada (null = nada). Segue a lista da spec §3:
/// qualquer efeito, mascara, matte, ajuste, blend != Normal, 3D usado,
/// precomp, particulas.
String? _m4Trigger(Layer l) {
  if (l is AdjustmentLayer) return 'camada de ajuste "${l.name}"';
  if (l is ParticlesLayer) return 'particulas "${l.name}"';
  if (l is GroupLayer) {
    for (final c in l.children) {
      final t = _m4Trigger(c);
      if (t != null) return t;
    }
    return 'precomp "${l.name}"';
  }
  if (l.effects.any((e) => e.enabled)) return 'efeito em "${l.name}"';
  if (l.masks.isNotEmpty) return 'mascara em "${l.name}"';
  if (l.matteMode != MatteMode.none) return 'matte em "${l.name}"';
  if (l.blendMode != BlendMode.srcOver) {
    return 'blend em "${l.name}"';
  }
  final uses3D = (l.rotationX.isAnimated ||
          l.rotationX.valueAt(Duration.zero).abs() > 1e-6) ||
      (l.rotationY.isAnimated ||
          l.rotationY.valueAt(Duration.zero).abs() > 1e-6) ||
      (l.positionZ.isAnimated ||
          l.positionZ.valueAt(Duration.zero).abs() > 1e-6);
  if (uses3D) return '3D em "${l.name}"';
  return null;
}

/// Classificador cena -> marcha (§4.1). Estrutural: olha o PROJETO, nao
/// o instante — por isso e cacheavel e so muda quando o usuario edita.
GearDecision classifyGear(VideoProject project) {
  final paint = _paintable(project.layers);
  if (paint.isEmpty) {
    return const GearDecision(PreviewGear.m1, 'cena vazia');
  }

  // Nulos com grade movem outras camadas, mas quem pinta sao os assets —
  // um rig com trilha animada ja forca recomposicao pelo portao de clock;
  // para a marcha, o que importa e o que pinta.
  for (final l in project.layers) {
    if (l is NullLayer && l.grid != null && l.grid!.assets.isNotEmpty) {
      return GearDecision(
          PreviewGear.m4, 'forcada por: modulo grade em "${l.name}"');
    }
  }
  for (final l in paint) {
    final t = _m4Trigger(l);
    if (t != null) {
      return GearDecision(PreviewGear.m4, 'forcada por: $t');
    }
  }
  if (paint.length > 4) {
    return GearDecision(
        PreviewGear.m4, 'forcada por: ${paint.length} camadas (>4)');
  }

  final videos = paint.whereType<VideoLayer>().toList();
  final graphics = paint.where(_isGraphic).toList();

  // M1: uma unica camada de video ou imagem, identidade total.
  if (paint.length == 1 &&
      (paint.single is VideoLayer || paint.single is ImageLayer) &&
      _identityOpacity(paint.single) &&
      _simpleTransform(paint.single)) {
    return GearDecision(PreviewGear.m1,
        paint.single is VideoLayer ? '1 video, nada mais' : '1 imagem, nada mais');
  }

  // M2: 1 video na BASE + so grafismos em Normal acima, nenhum lendo o
  // video (ajuste/matte/blend ja forcaram M4 acima).
  if (videos.length == 1 &&
      identical(paint.last, videos.single) &&
      _identityOpacity(videos.single) &&
      _simpleTransform(videos.single) &&
      graphics.length == paint.length - 1) {
    final texts = graphics.whereType<TextLayer>().length +
        graphics.whereType<CaptionLayer>().length;
    return GearDecision(PreviewGear.m2,
        '1 video + ${graphics.length} grafismo(s)${texts > 0 ? ' ($texts texto)' : ''}');
  }

  // M3: video com transform/opacidade nao trivial, ou 2-4 camadas simples.
  if (videos.length == 1 && paint.length == 1) {
    return const GearDecision(
        PreviewGear.m3, 'video com transform/opacidade');
  }
  if (paint.length <= 4) {
    return GearDecision(
        PreviewGear.m3, '${paint.length} camadas simples');
  }
  return const GearDecision(PreviewGear.m4, 'forcada por: cena complexa');
}

/// Portao de recomposicao: a cena precisa de rebuild POR TICK do clock?
/// Falso = nada na cena evolui com o tempo por conta propria (video
/// atualiza via Texture sem rebuild; legenda troca por ASSINATURA de cue,
/// nao por tick). E o analogo Flutter de "o player nao compoe".
bool projectNeedsClockRebuild(VideoProject project) {
  bool needs(Layer l) {
    if (l.hasAnimation) return true;
    if (l is ParticlesLayer) return true;
    // Animadores de texto evoluem com o tempo mesmo sem keyframe
    // (wiggly/fase); efeitos de ruido idem (fase integrada).
    if (l is TextLayer && (l.anims.isNotEmpty || l.animators.isNotEmpty)) {
      return true;
    }
    for (final e in l.effects) {
      if (!e.enabled) continue;
      if (e.type == EffectType.tremor ||
          e.type == EffectType.glitch ||
          e.type == EffectType.echo ||
          e.type == EffectType.spatialEcho) {
        return true;
      }
    }
    if (l is GroupLayer) return l.children.any(needs);
    return false;
  }

  return project.layers.any(needs);
}

/// Assinatura do instante: o que, alem de animacao, muda o RESULTADO da
/// composicao em [t] — entrada/saida de camadas e o cue ativo de cada
/// legenda. Assinatura igual = pode reusar a arvore composta.
String compositionSignature(VideoProject project, Duration t) {
  final sb = StringBuffer();
  for (final l in project.layers) {
    sb.write(l.activeAt(t) ? '1' : '0');
    if (l is CaptionLayer) {
      sb
        ..write(':')
        ..write(l.cueAt(l.localTime(t))?.id ?? '-');
    }
  }
  return sb.toString();
}
