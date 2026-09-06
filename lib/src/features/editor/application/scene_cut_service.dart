import 'dart:async';

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';

/// DECUPAR SOZINHO: onde a cena muda.
///
/// O FFmpeg compara cada quadro com o anterior e da uma nota de 0 a 1
/// para "quanto mudou"; acima do limiar e um corte. E o mesmo detector
/// que o Premiere usa por baixo do "Scene Edit Detection" — aqui roda
/// numa versao encolhida do video (160 px de largura), porque para
/// perceber que a cena mudou nao precisa de pixel nenhum a mais.
///
/// Os tempos voltam RELATIVOS AO INICIO DO TRECHO analisado: o `-ss`
/// antes do `-i` zera o relogio no ponto de entrada.
class SceneCutService {
  SceneCutService._();

  static final SceneCutService instance = SceneCutService._();

  final Map<String, List<Duration>> _cache = {};
  final Map<String, Future<List<Duration>>> _emAndamento = {};

  /// Cortes dentro de [path], do instante [start] por [duration], com
  /// [threshold] entre ~0.15 (sensivel) e ~0.65 (so cortes secos).
  Future<List<Duration>> detect(
    String path, {
    required Duration start,
    required Duration duration,
    double threshold = 0.35,
  }) {
    final k = '$path|${start.inMilliseconds}|${duration.inMilliseconds}|'
        '${threshold.toStringAsFixed(2)}';
    final pronto = _cache[k];
    if (pronto != null) return Future.value(pronto);
    return _emAndamento[k] ??=
        _run(path, start, duration, threshold).then((v) {
      _cache[k] = v;
      return v;
    }).whenComplete(() => _emAndamento.remove(k));
  }

  Future<List<Duration>> _run(
      String path, Duration start, Duration duration, double threshold) async {
    String seg(Duration d) => (d.inMicroseconds / 1e6).toStringAsFixed(3);
    final session = await FFmpegKit.executeWithArguments([
      '-hide_banner',
      '-nostats',
      '-ss', seg(start),
      '-t', seg(duration),
      '-i', path,
      '-an',
      '-sn',
      '-vf',
      "scale=160:-2,select='gt(scene,${threshold.toStringAsFixed(3)})',showinfo",
      '-vsync', 'vfr',
      '-f', 'null',
      '-',
    ]);
    if (!ReturnCode.isSuccess(await session.getReturnCode())) {
      return const [];
    }
    final log = await session.getAllLogsAsString() ?? '';
    return parseSceneCuts(log);
  }
}

/// Le os `pts_time:` que o `showinfo` imprime para os quadros
/// selecionados. Um corte no primeiro instante nao e corte, e dois
/// cortes a menos de [minGap] um do outro sao o mesmo corte (fade,
/// flash, tremida de camera).
List<Duration> parseSceneCuts(
  String log, {
  Duration minGap = const Duration(milliseconds: 300),
}) {
  final re = RegExp(r'pts_time:\s*(-?[0-9]+(?:\.[0-9]+)?)');
  final out = <Duration>[];
  Duration? ultimo;
  for (final m in re.allMatches(log)) {
    final s = double.tryParse(m.group(1)!);
    if (s == null || s.isNaN) continue;
    final t = Duration(microseconds: (s * 1e6).round());
    if (t < minGap) continue;
    if (ultimo != null && t - ultimo < minGap) continue;
    out.add(t);
    ultimo = t;
  }
  return out;
}
