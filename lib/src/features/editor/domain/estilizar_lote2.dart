import 'dart:ui';

import 'effect.dart';
import 'jpeg_damage.dart';
import 'distorcao_ae.dart';
import 'vhs_damage.dart';
import 'tv_damage.dart';
import 'pixel_sort_sapphire.dart';
import 'auto_paint.dart';

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
};
