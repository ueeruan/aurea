import 'dart:ui';

import 'effect.dart';
import 'jpeg_damage.dart';
import 'distorcao_ae.dart';
import 'shake.dart';
import 'glitch_distorcao.dart';
import 'vhs_damage.dart';
import 'tv_damage.dart';
import 'pixel_sort_sapphire.dart';
import 'auto_paint.dart';
import 'luz_e_diversos.dart';

/// O QUE O PALCO PRECISA SABER DE UM EFEITO SAPPHIRE DO LOTE 2 de
/// Estilizar: qual shader, quantas passadas, e como os numeros viram
/// uniformes (ABI comum em `passe_de_cor.dart`, `MotorSapphire`).
class ReceitaSapphire {
  const ReceitaSapphire({
    required this.asset,
    required this.valores,
    this.passadas = 1,
    this.usaTempo = true,
    this.cores = false,
  });

  final String asset;
  final List<double> Function(EffectInstance e, Duration local) valores;
  final int passadas;

  /// Anda sozinho no tempo (ruido, rolagem, sorteio por quadro).
  final bool usaTempo;

  /// Manda a cor principal e as extras para c0, c1.
  final bool cores;

  List<Color> coresDe(EffectInstance e) =>
      cores ? [e.color, ...e.extraColors] : const [];
}

final receitasSapphire = <EffectType, ReceitaSapphire>{
  // ABA 4 DIVERSOS e ABA 5 GLOW E LUZ (beta 89).
  EffectType.chromaKeyPro: ReceitaSapphire(
    asset: 'shaders/chroma_key.frag',
    valores: valoresChromaKey,
    usaTempo: false,
    cores: true,
  ),
  for (final t in [
    EffectType.sRays,
    EffectType.deepGlow,
    EffectType.brilho,
    EffectType.sSpotLight,
    EffectType.sGlint,
    EffectType.sGlowRings,
    EffectType.sEdgeRays,
    EffectType.sGlowAura,
    EffectType.sGlowDarks,
  ])
    t: ReceitaSapphire(
      asset: 'shaders/luz.frag',
      valores: valoresLuz,
      usaTempo: false,
      cores: true,
    ),
  EffectType.sGlintRainbow: ReceitaSapphire(
    asset: 'shaders/luz.frag',
    valores: valoresLuz,
    usaTempo: false,
  ),
  EffectType.jpegDamage: ReceitaSapphire(
    asset: 'shaders/jpeg_damage.frag',
    valores: valoresJpegDamage,
    passadas: 2,
  ),
  EffectType.autoPaint: ReceitaSapphire(
    asset: 'shaders/auto_paint.frag',
    valores: valoresAutoPaint,
  ),
  EffectType.pixelSort: ReceitaSapphire(
    asset: 'shaders/pixel_sort.frag',
    valores: valoresPixelSort,
  ),
  EffectType.tvDamage: ReceitaSapphire(
    asset: 'shaders/tv_damage.frag',
    valores: valoresTvDamage,
    cores: true,
  ),
  EffectType.vhsDamage: ReceitaSapphire(
    asset: 'shaders/vhs_damage.frag',
    valores: valoresVhsDamage,
  ),
  EffectType.ccLens: ReceitaSapphire(
    asset: 'shaders/distorcao_ae.frag',
    valores: valoresCcLens,
    usaTempo: false,
  ),
  EffectType.opticsCompensation: ReceitaSapphire(
    asset: 'shaders/distorcao_ae.frag',
    valores: valoresOpticsCompensation,
    usaTempo: false,
  ),
  EffectType.turbulentDisplace: ReceitaSapphire(
    asset: 'shaders/distorcao_ae.frag',
    valores: valoresTurbulentDisplace,
    usaTempo: false,
  ),
  EffectType.tremor: ReceitaSapphire(
    asset: 'shaders/shake.frag',
    valores: valoresShake,
  ),
  EffectType.dissolveShake: ReceitaSapphire(
    asset: 'shaders/shake.frag',
    valores: valoresDissolveShake,
  ),
  EffectType.glitchify: ReceitaSapphire(
    asset: 'shaders/glitchify.frag',
    valores: valoresGlitchify,
  ),
  EffectType.twitch: ReceitaSapphire(
    asset: 'shaders/twitch.frag',
    valores: valoresTwitch,
  ),
  EffectType.crossGlitch: ReceitaSapphire(
    asset: 'shaders/cross_glitch.frag',
    valores: valoresCrossGlitch,
    cores: true,
  ),
};


/// OS SHADERS QUE ESTE LOTE USA, para aquecer de uma vez.
///
/// Sem esta lista, quem aquecia escolhia os arquivos a mao — e nao
/// escolhia nenhum destes: `MotorSapphire.warmUp` existia e nao tinha
/// chamador. O preco aparecia no primeiro uso de cada efeito (um engasgo)
/// e, na exportacao, em QUADROS GRAVADOS SEM O EFEITO: o laco exporta um
/// quadro por vez, e o shader que ainda nao chegou devolve a camada crua.
Set<String> get assetsDosShadersSapphire => {
  for (final r in receitasSapphire.values) r.asset,
};
