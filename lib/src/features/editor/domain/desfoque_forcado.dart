import 'effect.dart';

/// A JANELA DE EXPOSICAO DO DESFOQUE DE MOVIMENTO.
///
/// DOIS PONTOS, para o dia em que o efeito voltar a ter ficha: o motor
/// (`_forceMotionBlur`, no palco) existe desde 14/09 e funciona; o que
/// saiu foi a FICHA do catalogo, a pedido do dono — e sem ficha o efeito
/// nao aparece na galeria nem tem como ser ajustado. Os parametros lidos
/// do plugin, para nao se perderem: Motion Blur Samples 8, Override
/// Shutter Settings 1, Shutter Angle 180, Shutter Phase 0, Native Motion
/// Blur 2. O `Override Shutter Settings` fica de fora (aqui o desfoque da
/// composicao e ajuste do projeto, separado do efeito).
///
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
