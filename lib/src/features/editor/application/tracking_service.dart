import 'dart:io';
import 'dart:ui' as ui;

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../domain/tracker2d.dart';

/// RASTREIO EM CIMA DO ARQUIVO.
///
/// O rastreador e uma conta pura sobre quadros em tons de cinza; quem
/// arranja os quadros e este servico. Eles saem em resolucao BAIXA de
/// proposito: rastrear em 1080p custa 30 vezes mais e nao acha nada que
/// 240p nao ache — o movimento e o mesmo, so muda a escala, e no fim a
/// conta e multiplicada de volta.
class TrackingService {
  TrackingService._();
  static final instance = TrackingService._();

  /// Largura em que o rastreio acontece.
  static const larguraAnalise = 240;

  /// Progresso 0..1 enquanto roda.
  final ValueNotifier<double> progress = ValueNotifier(0);

  Future<Directory> _pasta() async {
    final base = await getApplicationSupportDirectory();
    final d = Directory('${base.path}/track');
    if (d.existsSync()) d.deleteSync(recursive: true);
    d.createSync(recursive: true);
    return d;
  }

  /// Extrai os quadros do trecho usado, em cinza e pequenos.
  Future<List<GrayFrame>> grayFrames(
    String path, {
    required Duration start,
    required Duration duration,
    int fps = 12,
    int maxFrames = 300,
  }) async {
    final dir = await _pasta();
    final segundos = duration.inMilliseconds / 1000.0;
    if (segundos <= 0) return const [];

    final session = await FFmpegKit.executeWithArguments([
      '-y',
      '-ss', (start.inMilliseconds / 1000.0).toStringAsFixed(3),
      '-t', segundos.toStringAsFixed(3),
      '-i', path,
      '-vf', 'fps=$fps,scale=$larguraAnalise:-2,format=gray',
      '-frames:v', '$maxFrames',
      '-start_number', '0',
      '${dir.path}/%05d.png',
    ]);
    if (!ReturnCode.isSuccess(await session.getReturnCode())) {
      return const [];
    }

    final arquivos = dir
        .listSync()
        .whereType<File>()
        .where((f) => f.path.endsWith('.png'))
        .toList()
      ..sort((a, b) => a.path.compareTo(b.path));

    final out = <GrayFrame>[];
    for (var i = 0; i < arquivos.length; i++) {
      final g = await _paraCinza(arquivos[i]);
      if (g != null) out.add(g);
      progress.value = (i + 1) / arquivos.length;
    }
    return out;
  }

  Future<GrayFrame?> _paraCinza(File f) async {
    try {
      final codec = await ui.instantiateImageCodec(await f.readAsBytes());
      final frame = await codec.getNextFrame();
      final img = frame.image;
      final bytes =
          await img.toByteData(format: ui.ImageByteFormat.rawRgba);
      if (bytes == null) return null;
      final w = img.width, h = img.height;
      final px = Uint8List(w * h);
      final raw = bytes.buffer.asUint8List();
      for (var i = 0; i < w * h; i++) {
        // Luminancia Rec. 601 — a mesma que o olho usa para "claro".
        final r = raw[i * 4];
        final g = raw[i * 4 + 1];
        final b = raw[i * 4 + 2];
        px[i] = (0.299 * r + 0.587 * g + 0.114 * b).round().clamp(0, 255);
      }
      img.dispose();
      return GrayFrame(px, w, h);
    } catch (_) {
      return null;
    }
  }
}
