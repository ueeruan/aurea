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
import 'preto_e_branco.dart';

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
    this.usaOrcamentoDeAmostras = false,
  });

  final String asset;
  final List<double> Function(EffectInstance e, Duration local) valores;
  final int passadas;

  /// Anda sozinho no tempo (ruido, rolagem, sorteio por quadro).
  final bool usaTempo;

  /// Manda a cor principal e as extras para c0, c1.
  final bool cores;

  /// O shader tem o uniforme de orcamento de amostras (float 80).
  ///
  /// So o `luz.frag` tem: o kernel do brilho e o unico do lote que custa
  /// centenas de leituras de textura por pixel. Os outros shaders NAO
  /// declaram o uniforme, e escrever o float 80 neles cairia no meio de
  /// outra coisa — por isso o opt-in, e nao um `setFloat` geral.
  final bool usaOrcamentoDeAmostras;

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
      usaOrcamentoDeAmostras: true,
    ),
  EffectType.sGlintRainbow: ReceitaSapphire(
    asset: 'shaders/luz.frag',
    valores: valoresLuz,
    usaTempo: false,
    usaOrcamentoDeAmostras: true,
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
  // BLACK & WHITE: cor por faixa, e nao `rgb -> luminancia`. Um shader
  // proprio porque a ficha dele tem doze numeros, e nao os quatro de uma
  // operacao da passada de cor fundida.
  EffectType.pretoEBranco: ReceitaSapphire(
    asset: 'shaders/preto_e_branco.frag',
    valores: valoresPretoEBranco,
    usaTempo: false,
    cores: true,
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
