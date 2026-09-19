import 'effect.dart';

/// RGB TIME WARP (19/09): cada canal de cor vem de um INSTANTE diferente
/// do video.
///
/// NAO E SEPARACAO RGB ESPACIAL. A Separacao RGB empurra o vermelho para
/// o lado; aqui o vermelho e o quadro de outro MOMENTO. Onde a imagem se
/// move, o rastro colorido sai do movimento de verdade — e nao de um
/// deslocamento fixo.
///
///   R = quadro(t + deslocamento do vermelho)
///   G = quadro(t + deslocamento do verde)
///   B = quadro(t + deslocamento do azul)
///
/// O DESLOCAMENTO E EM QUADROS, e nao em segundos: um quadro e a unidade
/// em que o video existe, e "tres quadros para tras" atravessa a mesma
/// quantidade de movimento em 24, 30 ou 60 fps. O tempo vem da timeline,
/// entao o mesmo instante da o mesmo resultado no cursor, na previa, na
/// exportacao e depois de reabrir o projeto.
///
/// SO VALE PARA VIDEO. Nao ha o que puxar de outro instante numa forma
/// vetorial ou num texto — o efeito passa por eles sem desenhar nada, e
/// nao inventa um segundo desenho.
///
/// QUEM DECODIFICA OS OUTROS INSTANTES e o resto do app, e nao este
/// arquivo: na previa e o `QuadrosDeVideo` (que extrai o trecho com o
/// FFmpeg e guarda os quadros num LRU), e na exportacao e o
/// `instantesDeOutroTempo`, que pede os quadros antes de desenhar. Sem
/// isso cada canal mostraria o mesmo quadro — e o efeito nao faria nada.
const efeitosRgbTimeWarp = <EffectType, EffectSpec>{
  EffectType.rgbTimeWarp: EffectSpec(
    id: 'rgb_time_warp',
    name: 'RGB Time Warp',
    category: 'Time',
    synonyms: [
      'rgb time warp', 'timewarp', 'time warp', 'sapphire', 's_timewarpgb',
      'deslocar canal no tempo', 'canal em outro instante', 'rastro colorido',
      'separacao rgb temporal', 'arco iris do movimento',
    ],
    params: {
      // EM QUADROS, com sinal: negativo pega o que ja passou (o rastro
      // atras do movimento) e positivo adianta (o que ainda vai chegar).
      'red_frames': EffectParam('Quadros do vermelho', 0, -120, 120, decimals: 2, dragStep: .1),
      'green_frames': EffectParam('Quadros do verde', 0, -120, 120, decimals: 2, dragStep: .1),
      'blue_frames': EffectParam('Quadros do azul', 0, -120, 120, decimals: 2, dragStep: .1),
      // LIMITAR CROMA: o freio de seguranca. Em 100% a separacao vira so
      // luminancia e o efeito deixa de colorir — e o que salva um clipe
      // cujo movimento e rapido demais para a distancia escolhida.
      'clamp_chroma': EffectParam('Limitar croma', 0, 0, 100, unit: '%', decimals: 1, dragStep: .2),
      'mix': EffectParam('Mistura', 100, 0, 100, unit: '%', decimals: 1, dragStep: .2),
    },
    montar: ['red_frames', 'blue_frames', 'mix'],
    presets: [
      // MEIO QUADRO DE DESLOCAMENTO e o rastro de um obturador aberto:
      // sutil, e o que quase sempre se quer.
      EffectPronto('Rastro sutil', {'red_frames': -.5, 'blue_frames': .5}),
      EffectPronto('Arco-íris', {'red_frames': -3, 'green_frames': 0, 'blue_frames': 3}),
      EffectPronto('Fantasma', {'red_frames': -8, 'green_frames': -4, 'blue_frames': 4, 'clamp_chroma': 40}),
      EffectPronto('Glitch temporal', {'red_frames': -14, 'green_frames': 6, 'blue_frames': 12, 'mix': 70}),
    ],
  ),
};

/// OS TRES DESLOCAMENTOS de uma camada, em QUADROS, no instante [local].
///
/// Devolve zero nos tres quando o efeito nao esta ligado — e zero em tudo
/// e o mesmo que nao ter o efeito, entao quem chama pode pular o trabalho
/// inteiro sem perguntar mais nada.
({double r, double g, double b}) deslocamentosDoTimeWarp(
  Iterable<EffectInstance> efeitos,
  Duration local,
) {
  for (final e in efeitos) {
    if (e.type != EffectType.rgbTimeWarp || !e.enabled) continue;
    final spec = efeitosRgbTimeWarp[EffectType.rgbTimeWarp]!.params;
    double v(String k) {
      final p = spec[k]!;
      final bruto = e.paramAt(k, local);
      if (!bruto.isFinite) return p.initial;
      return bruto.clamp(p.min, p.max).toDouble();
    }
    return (r: v('red_frames'), g: v('green_frames'), b: v('blue_frames'));
  }
  return (r: 0, g: 0, b: 0);
}

/// O efeito ligado e com algum canal fora de zero?
bool timeWarpAtivo(Iterable<EffectInstance> efeitos, Duration local) {
  final d = deslocamentosDoTimeWarp(efeitos, local);
  return d.r != 0 || d.g != 0 || d.b != 0;
}

/// Onde o rastro para de ser util: meio segundo de deslocamento ja e um
/// efeito de outra natureza, e um clipe curto nao tem de onde tirar isso.
const double kMaximoDeQuadrosDoTimeWarp = 120;
