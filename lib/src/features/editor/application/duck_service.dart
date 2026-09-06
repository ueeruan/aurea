import 'dart:typed_data';

import '../domain/audio_mix.dart';
import '../domain/layer.dart';
import 'media_preview_service.dart';

/// QUEM MONTA OS ENVELOPES DE ABAIXAMENTO DO PROJETO.
///
/// Um lugar so, chamado pelo preview e pela exportacao com a mesma
/// entrada — e o que garante que os dois abaixem a musica na mesma hora
/// e na mesma medida.
///
/// Recebe a busca de picos por parametro em vez de ir no servico: assim
/// da para testar a conta sem arquivo nenhum no disco.
Map<String, DuckEnvelope> buildProjectDuckEnvelopes(
  List<Layer> layers,
  Float32List? Function(String path) peaksOf, {
  int perSecond = MediaPreviewService.peaksPerSecond,
}) {
  final porId = {for (final l in layers) l.id: l};
  final out = <String, DuckEnvelope>{};

  for (final l in layers) {
    final spec = audioSpecOf(l);
    final alvo = spec?.duckAgainstId;
    if (spec == null || alvo == null || spec.duckAmount <= 0) continue;

    final voz = porId[alvo];
    if (voz == null) continue;
    final caminho = switch (voz) {
      AudioLayer a => a.sourcePath,
      VideoLayer v => v.sourcePath,
      _ => null,
    };
    if (caminho == null) continue;
    final picos = peaksOf(caminho);
    if (picos == null || picos.isEmpty) continue;

    out[l.id] = buildDuckEnvelope(
      voicePeaksInTimeline(voz, picos, perSecond: perSecond),
      amount: spec.duckAmount,
      offset: voz.startTime,
    );
  }
  return out;
}

/// OS PICOS DA VOZ NO TEMPO DA LINHA, nao no do arquivo.
///
/// Os picos vem do arquivo inteiro; a camada usa um pedaco dele, comeca
/// num ponto da linha e pode estar acelerada. Sem essa traducao a musica
/// abaixaria no instante errado toda vez que a locucao fosse cortada —
/// que e sempre.
Float32List voicePeaksInTimeline(
  Layer voz,
  Float32List picosDaFonte, {
  int perSecond = MediaPreviewService.peaksPerSecond,
}) {
  final n = (voz.duration.inMicroseconds * perSecond / 1000000).round();
  if (n <= 0 || picosDaFonte.isEmpty) return Float32List(0);

  final (offset, velocidade) = switch (voz) {
    AudioLayer a => (a.sourceOffset, a.speed),
    VideoLayer v => (v.sourceOffset, 1.0),
    _ => (Duration.zero, 1.0),
  };
  final base = offset.inMicroseconds * perSecond / 1000000.0;

  final out = Float32List(n);
  for (var i = 0; i < n; i++) {
    final idx = (base + i * velocidade).round();
    if (idx >= 0 && idx < picosDaFonte.length) out[i] = picosDaFonte[idx];
  }
  return out;
}
