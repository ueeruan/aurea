import 'effect.dart';

/// POSTERIZE TIME (19/09), o do After Effects.
///
/// A CAMADA ANDA EM DEGRAUS. Com a composicao a 60 fps e o efeito em 12
/// fps, a camada mostra 12 quadros por segundo e SEGURA cada um deles —
/// o visual de anime, de stop motion e do travado.
///
/// NAO E UM EFEITO DE PIXEL, e por isso ele nao tem shader. Ele quantiza o
/// TEMPO da camada, e o resto vem de graca: o quadro do video, os
/// keyframes, a transformacao e todos os efeitos desenham a partir do
/// mesmo instante quantizado. E o FPS da composicao nao muda — quem muda
/// e so esta camada.
///
/// A CONTA E A DE SEMPRE: `floor(t * taxa) / taxa`, tirada do tempo da
/// timeline e nao de um contador de quadros. Arrastar o cursor, dar play,
/// exportar e reabrir o projeto dao o mesmo degrau no mesmo instante.
///
/// Quem aplica a conta e `Layer.localTime` — um lugar so, por onde passa
/// todo mundo que pergunta "que horas sao nesta camada".
const efeitosPosterizeTime = <EffectType, EffectSpec>{
  EffectType.posterizeTime: EffectSpec(
    id: 'posterize_time',
    name: 'Posterize Time',
    category: 'Time',
    synonyms: [
      'posterize time', 'posterizar tempo', 'stop motion', 'anime',
      'quadros por segundo', 'fps', 'travado', 'degrau', 'hold',
    ],
    params: {
      // A TAXA E LIDA NO TEMPO CRU da camada, e nao no quantizado: se ela
      // fosse lida depois, a grade se moveria junto com ela mesma. Com
      // keyframe, a taxa muda no tempo e a grade acompanha — sem ciclo.
      'frame_rate': EffectParam('Taxa de quadros', 12, 1, 120, unit: 'fps', decimals: 2, dragStep: .1),
    },
    montar: ['frame_rate'],
    presets: [
      EffectPronto('Anime', {'frame_rate': 12}),
      EffectPronto('Stop motion', {'frame_rate': 8}),
      EffectPronto('Travado', {'frame_rate': 4}),
    ],
  ),
};

/// A TAXA de posterizacao de uma camada, em quadros por segundo, ou 0
/// quando nao ha Posterize Time ligado.
///
/// [emTempoCru] e o tempo local SEM quantizar — e o que a taxa pode ler
/// sem se enroscar na propria grade que ela define.
double taxaDePosterizacao(Iterable<EffectInstance> efeitos, Duration emTempoCru) {
  for (final e in efeitos) {
    if (e.type != EffectType.posterizeTime || !e.enabled) continue;
    final p = efeitosPosterizeTime[EffectType.posterizeTime]!.params['frame_rate']!;
    final bruto = e.paramAt('frame_rate', emTempoCru);
    if (!bruto.isFinite) return p.initial;
    return bruto.clamp(p.min, p.max).toDouble();
  }
  return 0;
}

/// O INSTANTE QUANTIZADO: o degrau de [taxa] quadros por segundo em que
/// [tempo] cai.
///
/// Taxa zero (ou invalida) devolve o proprio tempo — nenhum degrau.
Duration quantizarTempo(Duration tempo, double taxa) {
  if (!taxa.isFinite || taxa <= 0) return tempo;
  final us = tempo.inMicroseconds;
  if (us == 0) return tempo;
  // A CONTA DO PEDIDO: `floor(t * taxa) / taxa`, com t em segundos.
  //
  // O `+ 1e-9` E CONTRA O ERRO DE ARREDONDAMENTO na fronteira do degrau:
  // `t * taxa` de um instante que cai EXATAMENTE na divisa pode sair
  // 2,9999999999 em vez de 3, e o `floor` devolveria o degrau anterior —
  // a camada ficaria um quadro atrasada so nas divisas exatas.
  // Duration guarda microssegundos inteiros. Um degrau como 1/12 s
  // arredonda para 83333 us; requantiza-lo deve continuar no mesmo
  // degrau, nao voltar a zero. Meio microssegundo cobre so esse erro.
  final degrau = ((us + .5) / 1e6 * taxa + 1e-9).floorToDouble();
  final quantizado = (degrau / taxa * 1e6).round();
  // NEGATIVO (camada antes do proprio inicio) tambem tem degrau: o floor
  // ja o leva para tras, e nao para o zero.
  return Duration(microseconds: quantizado);
}
