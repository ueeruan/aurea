import 'package:flutter/services.dart';

/// CODIFICADOR DA PLATAFORMA — a ponte para o MediaCodec (Android) e o
/// AVAssetWriter (iOS).
///
/// Por que trocar o x264 do FFmpeg por isto:
///
///   LICENCA   x264 e GPL. Num app comercial isso obrigaria a abrir o
///             codigo inteiro. O codificador do sistema nao contamina, e
///             a exposicao de patente passa a ser do fabricante.
///   VELOCIDADE  e hardware; x264 e software.
///   MANUTENCAO  vem com o sistema, nao e dependencia aposentada.
///
/// O FFmpeg continua no app para DECODIFICAR e para juntar o audio — usos
/// que nao precisam de codec GPL.
class PlatformEncoder {
  PlatformEncoder._();

  static const _channel = MethodChannel('aurea/encoder');

  static bool? _available;

  /// Se o aparelho tem o codificador. Consultado uma vez.
  static Future<bool> get available async {
    if (_available != null) return _available!;
    try {
      _available = await _channel.invokeMethod<bool>('available') ?? false;
    } on MissingPluginException {
      _available = false;
    } catch (_) {
      _available = false;
    }
    return _available!;
  }

  /// Abre o fluxo. [bitrate] em bits por segundo.
  static Future<void> start({
    required String path,
    required int width,
    required int height,
    required int fps,
    required int bitrate,
    bool hevc = false,
  }) async {
    await _channel.invokeMethod<bool>('start', {
      'path': path,
      'width': width,
      'height': height,
      'fps': fps,
      'bitrate': bitrate,
      'hevc': hevc,
    });
  }

  /// Codifica um quadro a partir de um PNG no disco.
  static Future<void> frame(String path) async {
    await _channel.invokeMethod<bool>('frame', {'path': path});
  }

  /// Codifica um LOTE de quadros numa chamada so. Atravessar a ponte por
  /// quadro custa mais que codificar em muitos aparelhos.
  static Future<int> frames(List<String> paths) async {
    final n = await _channel.invokeMethod<int>('frames', {'paths': paths});
    return n ?? 0;
  }

  static Future<bool> finish() async =>
      await _channel.invokeMethod<bool>('finish') ?? false;

  static Future<void> cancel() async {
    try {
      await _channel.invokeMethod<bool>('cancel');
    } catch (_) {}
  }

  /// REMUX: copia as trilhas sem recodificar. Corte puro deixa de custar
  /// um render inteiro — sai quase instantaneo.
  static Future<bool> remux({
    required String source,
    required String target,
    Duration start = Duration.zero,
    Duration? end,
  }) async {
    try {
      return await _channel.invokeMethod<bool>('remux', {
            'source': source,
            'target': target,
            'startUs': start.inMicroseconds,
            'endUs': end?.inMicroseconds ?? 0,
          }) ??
          false;
    } catch (_) {
      return false;
    }
  }

  /// Taxa de bits sugerida para [w]x[h] a [fps], pela qualidade pedida.
  ///
  /// A conta e bits por pixel por quadro: e o que mantem a mesma
  /// aparencia quando muda a resolucao, em vez de um numero fixo que fica
  /// generoso em 720p e pobre em 4K.
  static int bitrateFor(int w, int h, int fps, String quality) {
    final bpp = switch (quality) {
      'alta' => 0.20,
      'baixa' => 0.07,
      _ => 0.12,
    };
    final v = (w * h * fps * bpp).round();
    return v.clamp(1000000, 120000000);
  }
}
