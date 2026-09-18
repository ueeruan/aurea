import 'effect.dart';

/// CC FORCE MOTION BLUR — O DESFOQUE DE MOVIMENTO DO PROPRIO AFTER.
///
/// ==========================================================================
/// ELE JA ESTAVA FEITO, E NAO TINHA FICHA
/// ==========================================================================
///
/// O motor deste efeito esta no palco desde 14/09 (`_forceMotionBlur`): a
/// camada e montada varias vezes dentro da JANELA DE EXPOSICAO e as copias
/// sao mediadas. Como cada amostra e a camada no seu instante, o desfoque
/// vale para o que se move DE VERDADE — inclusive o conteudo de um video,
/// que e a diferenca entre este efeito e o desfoque da composicao.
///
/// O que faltava era a ficha, e sem ficha o efeito nao aparecia na galeria
/// nem tinha como ser ajustado. O mesmo defeito que o Time Slice tinha.
///
/// ==========================================================================
/// E O RSMB?
/// ==========================================================================
///
/// O RE:Vision RSMB NAO ESTA INSTALADO, e nao ha o que medir dele. Este
/// efeito e o do After (`CC Force Motion Blur`), que e o parente proximo
/// e o que existe na maquina do dono.
///
/// A DIFERENCA, escrita para nao se perder: o RSMB estima o FLUXO entre
/// quadros e acumula ao longo do movimento estimado. Isso da a ele duas
/// coisas que este nao faz — borrar a partir de UM quadro (sintetizar o
/// movimento que nao foi filmado) e nao fantasmear onde o movimento nao e
/// uniforme (um objeto que passa na frente de outro). Aqui cada amostra e
/// um quadro de verdade: com poucas amostras o risco e a escada, e nao o
/// fantasma, e um quadro parado continua parado — nao ha de onde tirar o
/// movimento.
///
/// ==========================================================================
/// OS PARAMETROS SAO DO PLUGIN
/// ==========================================================================
///
/// Lidos do AE:
///
///   * Motion Blur Samples      = 8
///   * Override Shutter Settings = 1
///   * Shutter Angle            = 180
///   * Shutter Phase            = 0
///   * Native Motion Blur       = 2
///
/// O `Override Shutter Settings` NAO ENTROU: ele decide se o efeito usa o
/// obturador da composicao ou o proprio, e aqui o desfoque da composicao e
/// um ajuste do projeto, separado do efeito — nao ha o que sobrepor. Um
/// interruptor com o nome dele nao mudaria nada na tela.
///
/// O `Native Motion Blur` entrou como escolha: no AE, 2 e "Only" — quem
/// borra e a composicao, e o efeito nao faz nada.
const efeitosDesfoqueForcado = <EffectType, EffectSpec>{
  EffectType.forceMotionBlur: EffectSpec(
    id: 'cc_force_motion_blur',
    name: 'Desfoque de movimento',
    category: 'Blur & Sharpen',
    synonyms: [
      'desfoque de movimento',
      'force motion blur',
      'motion blur',
      'borrao de movimento',
      'arrasto',
      'rsmb',
    ],
    params: {
      // A ORDEM E A DO EFEITO NO AE (1 a 5).
      'samples': EffectParam('Amostras', 8, 2, 64, decimals: 0),
      'shutter_angle': EffectParam(
        'Ângulo do obturador',
        180,
        0,
        720,
        unit: '°',
        decimals: 1,
      ),
      'shutter_phase': EffectParam(
        'Fase do obturador',
        0,
        -360,
        360,
        unit: '°',
        decimals: 1,
      ),
      // O PADRAO DO AE E 2, e este e o unico parametro em que a gente
      // nao segue o valor de fabrica: no AE o 2 e "quem borra e a
      // composicao", ou seja, o efeito entra sem fazer nada. Quem pede um
      // desfoque de movimento quer o desfoque, entao aqui o padrao e
      // Aplicar — e o outro valor continua a um toque de distancia.
      'native_motion_blur': EffectParam(
        'Desfoque nativo',
        0,
        0,
        1,
        kind: ParamKind.choice,
        options: ['Aplicar', 'Só a composição'],
      ),
    },
    montar: ['samples', 'shutter_angle', 'shutter_phase'],
    presets: [
      // 180 graus e 8 amostras sao os valores de fabrica do plugin.
      EffectPronto('Padrão', {'samples': 8, 'shutter_angle': 180}),
      EffectPronto('Meia exposição', {'samples': 6, 'shutter_angle': 90}),
      EffectPronto('Longa', {'samples': 16, 'shutter_angle': 360}),
      EffectPronto('Arrastando', {
        'samples': 12,
        'shutter_angle': 180,
        'shutter_phase': 90,
      }),
      EffectPronto('Chegando', {
        'samples': 12,
        'shutter_angle': 180,
        'shutter_phase': -90,
      }),
    ],
  ),
};

/// A JANELA DE EXPOSICAO de um quadro, em instantes da composicao.
///
/// A FASE E O QUE DECIDE PARA ONDE O ARRASTO CAI, e a convencao e a da
/// propria composicao (`MotionBlurSpec.exposureWindow`), para nao
/// existirem duas:
///
///   * fase 0 (o padrao do plugin) comeca NO quadro e arrasta para
///     frente: pega o que ainda vai acontecer;
///   * fase -90 centra o desfoque no quadro, metade para cada lado — e o
///     que a composicao deste app usa;
///   * fase -180 pega o que ja passou.
///
/// O angulo de 180 graus e meia exposicao, que e o padrao do cinema.
({Duration inicio, Duration fim}) janelaDaExposicao({
  required Duration t,
  required double angulo,
  required double fase,
  required int fps,
}) {
  final f = fps < 1 ? 30 : fps;
  final quadroUs = 1000000 / f;
  final a = angulo.isFinite ? angulo.clamp(0.0, 720.0) : 0.0;
  final phi = fase.isFinite ? fase.clamp(-360.0, 360.0) : 0.0;
  Duration em(double voltas) =>
      t + Duration(microseconds: (voltas * quadroUs).round());
  return (inicio: em(phi / 360), fim: em((phi + a) / 360));
}

/// O INSTANTE DA AMOSTRA [i] DE [n] dentro da janela.
///
/// A primeira amostra cai no comeco da janela e a ultima no fim: repartir
/// a janela em n pontos e o que faz a media corrente virar desfoque em vez
/// de um borrao puxado para um lado.
({Duration inicio, Duration fim}) janelaDoEfeito(
  EffectInstance efeito,
  Duration local,
  Duration t,
  int fps,
) => janelaDaExposicao(
  t: t,
  angulo: efeito.paramAt('shutter_angle', local),
  fase: efeito.paramAt('shutter_phase', local),
  fps: fps,
);

/// O instante da amostra [i] de [n] dentro da janela.
Duration instanteDaAmostra(
  ({Duration inicio, Duration fim}) janela,
  int i,
  int n,
) {
  if (n <= 1) return janela.inicio;
  final total = (janela.fim - janela.inicio).inMicroseconds;
  return janela.inicio + Duration(microseconds: (total * i / (n - 1)).round());
}
