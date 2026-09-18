import 'keyframe.dart';
import 'layer.dart';
import 'time_core.dart';

/// A trilha de tempo do clipe, ou nulo quando ele anda em [speed] fixa.
///
/// Ate 17/09 isto procurava um `EffectInstance` de tipo `timeRemap` dentro
/// da lista de efeitos. O Time Remap saiu do catalogo e a trilha virou
/// campo da camada — onde o precomp ja a guardava. Nada mais aqui precisa
/// saber o que e um efeito.
AnimatedDouble? timeRemapTrackOf(VideoLayer layer) => layer.timeRemap;

bool hasTimeRemap(VideoLayer layer) => layer.timeRemap != null;

CoreLayer _core(VideoLayer layer) => (
  sourceOffsetUs: layer.sourceOffset.inMicroseconds,
  durationUs: layer.duration.inMicroseconds,
  speed: layer.speed,
  reverse: layer.reverse,
  track: layer.timeRemap,
);

/// Recorta a funcao fonte-tempo entre dois instantes locais. O resultado
/// fica normalizado para um novo sourceOffset e pode representar inclusive
/// um trecho reverso sem depender do sinal de [VideoLayer.speed].
///
/// O corte e EXATO no nucleo C++: um trecho bezier cortado no meio vira
/// uma sub-bezier (de Casteljau), em vez de herdar a curva inteira do
/// trecho original — o que antes deslocava o tempo no meio do corte.
({Duration sourceOffset, AnimatedDouble track}) sliceVideoTrack(
  VideoLayer layer,
  Duration from,
  Duration to,
) {
  final r = coreSlice(_core(layer), from.inMicroseconds, to.inMicroseconds);
  return (
    sourceOffset:
        layer.sourceOffset +
        Duration(microseconds: (r.minimum * 1000000).round()),
    track: r.track,
  );
}

/// Faixa relativa de fonte tocada pelo clipe. Para remap, vem dos extremos
/// reais da curva (com overshoot); para velocidade constante, da conta
/// duracao * speed.
Duration videoSourceSpan(VideoLayer layer) =>
    Duration(microseconds: coreLayerSpanUs(_core(layer)));

/// Instante RELATIVO a [VideoLayer.sourceOffset] que deve ser mostrado.
/// Esta funcao e pura, portanto scrub direto e reproducao desde o inicio
/// chegam sempre ao mesmo quadro.
Duration videoSourceTimeAt(VideoLayer layer, Duration local) =>
    Duration(microseconds: coreLayerSourceUs(_core(layer), local.inMicroseconds));

Duration videoAbsoluteSourceTimeAt(VideoLayer layer, Duration local) => Duration(
  microseconds: coreLayerAbsoluteUs(_core(layer), local.inMicroseconds),
);

/// Derivada local do Time Remap, em segundos de fonte por segundo de
/// timeline (analitica no nucleo). O sinal negativo identifica reverso.
double videoPlaybackRateAt(VideoLayer layer, Duration local) =>
    coreLayerRate(_core(layer), local.inMicroseconds);

/// Instante de CONTEUDO de uma camada qualquer com o efeito Time Remap
/// (texto, forma, precomp...): segura as pontas como valueAt e nunca e
/// negativo. Video usa [videoSourceTimeAt].
Duration remappedContentTime(AnimatedDouble track, Duration local, {Duration? bakeUntil}) {
  final us = (coreValue(track, local, bakeUntil: bakeUntil) * 1000000).round();
  return Duration(microseconds: us < 0 ? 0 : us);
}

/// O clipe com a trilha de tempo trocada. `null` LIMPA o remapeamento e
/// devolve o clipe a velocidade constante de [VideoLayer.speed].
///
/// Antes isto montava e devolvia uma LISTA de efeitos, porque a trilha
/// morava num deles. Agora e o que sempre deveria ter sido: uma copia da
/// camada com o campo trocado.
/// Nao mexe em `reverse` nem em `speed`: quem chama decide o que fazer
/// com eles. O nucleo ([coreLayerSourceUs]) ja sabe combinar os tres.
VideoLayer withTimeRemapTrack(VideoLayer layer, AnimatedDouble? track) =>
    layer.copyLayer(timeRemap: track, clearTimeRemap: track == null);


/// O TRECHO DO ARQUIVO que o clipe mostra de ponta a ponta (instantes
/// absolutos da fonte): o menor e o maior, varrendo a camada a 60 quadros
/// por segundo. Com velocidade constante e o [sourceOffset, +span]; com
/// reverso ou Time Remap, o que a curva realmente visita.
/// OS CORTES DE CENA NO TEMPO DA LINHA. A deteccao devolve instantes
/// RELATIVOS AO INICIO DO TRECHO DA FONTE analisado; na linha do tempo
/// eles caem esticados pela velocidade. So vale leitura simples (sem
/// reverso, sem Time Remap) — quem chama recusa o resto antes. Cortes
/// colados nas bordas saem: dividir a 40 ms da ponta so cria farelo.
List<Duration> temposGlobaisDosCortes(
  VideoLayer v,
  List<Duration> relativosAFonte, {
  Duration margem = const Duration(milliseconds: 200),
}) {
  final vel = v.speed <= 0 ? 1.0 : v.speed;
  final out = <Duration>[];
  for (final r in relativosAFonte) {
    final t =
        v.startTime + Duration(microseconds: (r.inMicroseconds / vel).round());
    if (t <= v.startTime + margem) continue;
    if (t >= v.endTime - margem) continue;
    out.add(t);
  }
  return out;
}

(Duration, Duration) trechoDaFonteMostrado(VideoLayer layer) {
  final passo = 1000000 ~/ 60;
  var menor = 1 << 62, maior = -(1 << 62);
  for (var us = 0; us <= layer.duration.inMicroseconds; us += passo) {
    final f = videoAbsoluteSourceTimeAt(layer, Duration(microseconds: us))
        .inMicroseconds;
    if (f < menor) menor = f;
    if (f > maior) maior = f;
  }
  final ultimo = videoAbsoluteSourceTimeAt(layer, layer.duration).inMicroseconds;
  if (ultimo < menor) menor = ultimo;
  if (ultimo > maior) maior = ultimo;
  if (maior <= menor) maior = menor + passo;
  return (Duration(microseconds: menor), Duration(microseconds: maior));
}
