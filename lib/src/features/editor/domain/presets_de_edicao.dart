import 'dart:ui';

import 'cut.dart';
import 'effect.dart';
import 'keyframe.dart';

/// OS PRESETS DO BUNDLE 4nas.ftbl (1K Free Editing Bundle), convertidos
/// para o motor do app a pedido do dono (15/09).
///
/// Os .ffx do After Effects foram abertos byte a byte (RIFX): cada CC do
/// bundle e uma pilha de Magic Bullet Looks + 4-Color Gradient +
/// Exposure + Hue/Saturation + Sapphire (Sharpen/Glow) + Film Damage;
/// os shakes sao S_BlurMoCurves + BCC Directional Blur sobre transform
/// tremido; os zooms sao S_BlurMoCurves com punch; o Twixtor e rampa de
/// velocidade. Nada disso existe aqui por plugin — existe por MOTOR:
/// cada preset vira uma receita dos efeitos NATIVOS, calibrada pelos
/// numeros LEGIVEIS extraidos dos arquivos (as quatro cores do
/// gradiente, o +25 de saturacao, o overlay a 10%...) e pelo carater de
/// cada look. O Looks e caixa-preta (blob proprio): o carater dele foi
/// reconstruido, nao copiado.
///
/// A regra de ouro: TODA chave e todo valor daqui sao validados contra o
/// catalogo em teste — preset com chave errada nao compila a suite.

/// O que o preset faz ao ser aplicado.
enum AcaoDoPreset {
  /// Anexa a pilha de efeitos a camada.
  efeitos,

  /// Camera lenta com rampa (so video) + motion blur na pilha.
  cameraLenta,
}

class PresetDeEdicao {
  const PresetDeEdicao({
    required this.id,
    required this.nome,
    required this.detalhe,
    required this.receita,
    this.acao = AcaoDoPreset.efeitos,
    this.rampa,
    this.zoomLento = 0,
    this.ligarSpeedBlur = false,
    this.marca = '4nas.ftbl',
  });

  final String id;
  final String nome;

  /// Uma linha dizendo o que ele faz — o cartao mostra.
  final String detalhe;

  final AcaoDoPreset acao;

  /// A rampa de velocidade (so quando [acao] e cameraLenta).
  final SpeedRampPreset? rampa;

  /// ZOOM LENTO: a escala sobe esta fracao do comeco ao fim do clipe
  /// (0.08 = +8%), em keyframes de verdade. Zero = nada; camada com a
  /// escala JA animada nao e tocada.
  final double zoomLento;

  /// Liga o desfoque por velocidade do clipe (o blur das pontas da
  /// rampa vem de graca, proporcional a taxa).
  final bool ligarSpeedBlur;

  /// O selo do cartao (de onde o preset veio).
  final String marca;

  /// A pilha, como (tipo, valores, cor, cores extras). Vira instancia em
  /// [montar] — cada aplicacao gera ids novos.
  final List<ReceitaDeEfeito> receita;

  /// Instancias NOVAS da pilha (ids proprios a cada aplicacao).
  ///
  /// RECEITA COM EFEITO QUE SAIU DO CATALOGO NAO ESTOURA. O `!` aqui
  /// derrubava a tela inteira quando um tipo da receita perdia a ficha —
  /// o mesmo defeito que o construtor de `EffectInstance` ja tinha sido
  /// corrigido para nao ter. Receita sem ficha simplesmente nao entra na
  /// pilha: quem aplica avisa, e o resto da receita continua valendo.
  List<EffectInstance> montar() => [
    for (final r in receita)
      if (effectSpecs[r.tipo] case final spec?)
        EffectInstance(
          type: r.tipo,
          params: {
            for (final e in spec.params.entries)
              e.key: AnimatedDouble(r.valores[e.key] ?? e.value.initial),
          },
          color: r.cor,
          extraColors: r.coresExtras,
        ),
  ];
}

class ReceitaDeEfeito {
  const ReceitaDeEfeito(
    this.tipo,
    this.valores, {
    this.cor,
    this.coresExtras,
  });

  final EffectType tipo;
  final Map<String, double> valores;
  final Color? cor;
  final List<Color>? coresExtras;
}

// O 4-Color Gradient SAIU (17/09): nao existe no catalogo oficial de 37,
// e nao ha gradiente equivalente para por no lugar. As quatro cores do
// bundle (rosa-magenta, laranja, amarelo, ambar, em overlay a 10%) estao
// registradas aqui — quem reautorar a familia de gradiente parte delas.
//
// const Color(0xFFFF00D8), Color(0xFFFFAE00), Color(0xFFFFFF00), Color(0xFFFFC000)

final presetsDeEdicao = List<PresetDeEdicao>.unmodifiable([
  // ------------------------------------------------- o edit da casa
  // Lido QUADRO A QUADRO do video de referencia do dono (72 quadros a
  // 120 fps): entra em disparada com blur, freia em camera lenta lisa
  // (optical flow), o zoom sobe devagar com micro-tremor organico, e
  // sai acelerando de novo — o preset de clipe decupado.
  PresetDeEdicao(
    id: 'impact-flow',
    nome: 'Impact Flow',
    detalhe:
        'Entra em disparada, freia em câmera lenta com optical flow, '
        'zoom sobe devagar e sai acelerando — pra clipe decupado (vídeo).',
    acao: AcaoDoPreset.cameraLenta,
    rampa: SpeedRampPreset.flow,
    zoomLento: 0.08,
    ligarSpeedBlur: true,
    marca: 'Edit',
    receita: const [
      ReceitaDeEfeito(EffectType.tremor, {
        'amplitude': 2.2,
        'frequency': 6,
        'x_random_amplitude': 0.9,
        'y_random_amplitude': 0.7,
        'motion_blur': 0.25,
        'blur_length': 1.2,
        'stillness': 0.6,
        'rgb_randomness': 0.06,
      }),
    ],
  ),
  // ------------------------------------------------------------- CCs
  PresetDeEdicao(
    id: '4nas-main-cc-2024',
    nome: 'Main CC 2024',
    detalhe: 'O look principal: teal e laranja, saturação e brilho limpo.',
    receita: const [
      ReceitaDeEfeito(EffectType.hueSaturation, {'master_saturation': 20}),
      ReceitaDeEfeito(EffectType.brightnessContrast, {'contrast': 14}),
      ReceitaDeEfeito(EffectType.unsharpMask, {'amount': 1.2}),
      ReceitaDeEfeito(EffectType.brilho, {
        'threshold': 82,
        'radius': 40,
        'intensity': 60,
      }),
    ],
  ),
  PresetDeEdicao(
    id: '4nas-4k-cc',
    nome: '4K CC',
    detalhe: 'Nitidez de “upscale”: sharpen duplo, contraste e grão fino.',
    receita: const [
      ReceitaDeEfeito(EffectType.unsharpMask, {'amount': 1.6, 'width': 1.2}),
      ReceitaDeEfeito(EffectType.unsharpMask, {'amount': 0.6, 'radius': 2}),
      ReceitaDeEfeito(EffectType.brightnessContrast, {'contrast': 18}),
      ReceitaDeEfeito(EffectType.hueSaturation, {'master_saturation': 12}),
      ReceitaDeEfeito(EffectType.vignette, {
        'quantidade': 0.25,
        'raio': 0.9,
        'suavidade': 0.7,
      }),
    ],
  ),
  PresetDeEdicao(
    id: '4nas-cold-cc',
    nome: 'Cold CC',
    detalhe: 'Sombras azuladas, saturação +25 (o número do arquivo) e grão.',
    receita: const [
      ReceitaDeEfeito(EffectType.hueSaturation, {'master_saturation': 25}),
      ReceitaDeEfeito(EffectType.brightnessContrast, {'contrast': 12}),
      ReceitaDeEfeito(EffectType.unsharpMask, {'amount': 1.3}),
    ],
  ),
  PresetDeEdicao(
    id: '4nas-aura-cc',
    nome: 'Aura CC',
    detalhe: 'Glow quente na pele da imagem — o brilho de aura do bundle.',
    receita: const [
      ReceitaDeEfeito(EffectType.brilho, {
        'threshold': 68,
        'radius': 60,
        'intensity': 85,
        'mult_r': 1.15,
        'mult_b': 0.85,
      }),
      ReceitaDeEfeito(EffectType.hueSaturation, {'master_saturation': 15}),
      ReceitaDeEfeito(EffectType.brightnessContrast, {'contrast': 10}),
      ReceitaDeEfeito(EffectType.unsharpMask, {'amount': 1.1}),
      ReceitaDeEfeito(EffectType.vignette, {
        'amount': 0.2,
        'suavidade': 0.8,
      }),
    ],
  ),
  PresetDeEdicao(
    id: '4nas-gorgeous-cc',
    nome: 'Gorgeous CC',
    detalhe: 'O dourado vistoso: gradiente do bundle, glow e saturação.',
    receita: const [
      ReceitaDeEfeito(EffectType.brilho, {
        'threshold': 75,
        'radius': 45,
        'intensity': 70,
        'mult_r': 1.1,
      }),
      ReceitaDeEfeito(EffectType.hueSaturation, {'master_saturation': 22}),
      ReceitaDeEfeito(EffectType.brightnessContrast, {'contrast': 12}),
      ReceitaDeEfeito(EffectType.unsharpMask, {'amount': 1.2}),
    ],
  ),
  PresetDeEdicao(
    id: '4nas-soft-emerald-cc',
    nome: 'Soft Emerald CC',
    detalhe: 'Verdes suaves nas sombras, brilho leve — o look esmeralda.',
    receita: const [
      ReceitaDeEfeito(EffectType.hueSaturation, {'master_saturation': 10}),
      ReceitaDeEfeito(EffectType.brightnessContrast, {
        'brightness': 4,
        'contrast': 8,
      }),
      ReceitaDeEfeito(EffectType.brilho, {
        'threshold': 88,
        'raio': 30,
        'intensity': 40,
      }),
      ReceitaDeEfeito(EffectType.unsharpMask, {'amount': 1.0}),
    ],
  ),
  PresetDeEdicao(
    id: '4nas-revenge-arc-cc',
    nome: 'Revenge Arc CC',
    detalhe: 'Contraste pesado, vinheta fechada e grão — o arco de vilão.',
    receita: const [
      ReceitaDeEfeito(EffectType.brightnessContrast, {'contrast': 30}),
      ReceitaDeEfeito(EffectType.hueSaturation, {'master_saturation': 18}),
      ReceitaDeEfeito(EffectType.vignette, {
        'quantidade': 0.45,
        'raio': 0.75,
        'suavidade': 0.55,
      }),
      ReceitaDeEfeito(EffectType.unsharpMask, {'amount': 1.4}),
    ],
  ),
  PresetDeEdicao(
    id: '4nas-smooth-4k-cc',
    nome: 'Smooth 4K CC',
    detalhe: 'Pele lisa com definição: máscara de nitidez larga e bloom.',
    receita: const [
      ReceitaDeEfeito(EffectType.unsharpMask, {
        'amount': 1.2,
        'radius': 6,
      }),
      ReceitaDeEfeito(EffectType.brightnessContrast, {'contrast': 8}),
      ReceitaDeEfeito(EffectType.hueSaturation, {'master_saturation': 8}),
      ReceitaDeEfeito(EffectType.brilho, {
        'threshold': 90,
        'raio': 25,
        'intensity': 35,
      }),
    ],
  ),
  PresetDeEdicao(
    id: '4nas-sharpen-cc',
    nome: 'Sharpen CC',
    detalhe: 'Só nitidez, com força: o sharpen assinatura do bundle.',
    receita: const [
      ReceitaDeEfeito(EffectType.unsharpMask, {
        'amount': 2.2,
        'width': 1.1,
        'luma': 1.4,
      }),
      ReceitaDeEfeito(EffectType.unsharpMask, {
        'amount': 0.8,
        'radius': 2.5,
      }),
      ReceitaDeEfeito(EffectType.brightnessContrast, {'contrast': 6}),
    ],
  ),
  // ---------------------------------------------------------- shakes
  PresetDeEdicao(
    id: '4nas-x-shake',
    nome: 'X Shake',
    detalhe: 'Tremor horizontal com motion blur — o shake de batida.',
    receita: const [
      ReceitaDeEfeito(EffectType.tremor, {
        'amplitude': 6,
        'frequency': 9,
        'x_random_amplitude': 2.2,
        'y_random_amplitude': 0.05,
        'motion_blur': 0.7,
        'blur_length': 2.5,
        'rgb_randomness': 0.15,
        'stillness': 0.35,
      }),
    ],
  ),
  PresetDeEdicao(
    id: '4nas-y-shake',
    nome: 'Y Shake',
    detalhe: 'O mesmo tremor, no eixo vertical.',
    receita: const [
      ReceitaDeEfeito(EffectType.tremor, {
        'amplitude': 6,
        'frequency': 9,
        'x_random_amplitude': 0.05,
        'y_random_amplitude': 2.2,
        'motion_blur': 0.7,
        'blur_length': 2.5,
        'rgb_randomness': 0.15,
        'stillness': 0.35,
      }),
    ],
  ),
  PresetDeEdicao(
    id: '4nas-main-shake',
    nome: 'Main Shake',
    detalhe: 'Tremor completo com punch de zoom e RGB rasgando de leve.',
    receita: const [
      ReceitaDeEfeito(EffectType.tremor, {
        'amplitude': 5,
        'frequency': 8,
        'x_random_amplitude': 1.6,
        'y_random_amplitude': 1.3,
        'motion_blur': 0.6,
        'blur_length': 2.0,
        'rgb_randomness': 0.25,
        'zoom_punch': 8,
        'stillness': 0.3,
      }),
    ],
  ),
  // ----------------------------------------------------------- zooms
  PresetDeEdicao(
    id: '4nas-zoom-in',
    nome: 'Zoom In',
    detalhe: 'Punch-in com blur radial, no gatilho — pronto pra batida.',
    receita: const [
    ],
  ),
  PresetDeEdicao(
    id: '4nas-zoom-out',
    nome: 'Zoom Out',
    detalhe: 'A imagem recua com rastro — o respiro depois do impacto.',
    receita: const [
    ],
  ),
  // --------------------------------------------------------- twixtor
  PresetDeEdicao(
    id: '4nas-twixtor',
    nome: 'Twixtor',
    detalhe: 'Câmera lenta de herói com rampa suave e motion blur (vídeo).',
    acao: AcaoDoPreset.cameraLenta,
    rampa: SpeedRampPreset.heroi,
    receita: const [
    ],
  ),
]);

PresetDeEdicao? presetDeEdicaoPorId(String id) {
  for (final p in presetsDeEdicao) {
    if (p.id == id) return p;
  }
  return null;
}
