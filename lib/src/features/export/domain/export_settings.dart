/// AJUSTES DA EXPORTACAO.
///
/// "Media / Alta / Baixa" resolve o caso comum e trava todo o resto:
/// quem precisa entregar em 4K, quem precisa de HEVC para caber no
/// limite de upload, quem precisa da sequencia PNG com transparencia
/// para levar a arte para outro programa — nenhum desses cabia num
/// botao so.
///
/// Aqui os ajustes viram DADO, com o mesmo cuidado do resto: a conta de
/// taxa de bits e por pixel (nao um numero fixo que fica generoso em
/// 720p e pobre em 4K), e a resolucao mantem a proporcao do projeto em
/// vez de esticar.
library;

enum ExportFormat {
  /// Video em MP4 — o caminho normal.
  mp4,

  /// Sequencia de PNG. Grande, mas guarda TRANSPARENCIA e nao perde
  /// nada: e o que se leva para outro programa.
  pngSequence,
}

enum ExportCodec {
  /// Toca em qualquer lugar.
  h264,

  /// Metade do tamanho na mesma qualidade — mas nem todo aparelho
  /// antigo reproduz, e algumas redes recodificam.
  hevc,
}

/// Altura de saida. `original` = o tamanho do projeto.
enum ExportSize { original, p2160, p1440, p1080, p720, p480 }

int? exportSizeHeight(ExportSize s) => switch (s) {
      ExportSize.original => null,
      ExportSize.p2160 => 2160,
      ExportSize.p1440 => 1440,
      ExportSize.p1080 => 1080,
      ExportSize.p720 => 720,
      ExportSize.p480 => 480,
    };

String exportSizeLabel(ExportSize s) => switch (s) {
      ExportSize.original => 'Original',
      ExportSize.p2160 => '4K',
      ExportSize.p1440 => '1440p',
      ExportSize.p1080 => '1080p',
      ExportSize.p720 => '720p',
      ExportSize.p480 => '480p',
    };

String exportCodecLabel(ExportCodec c) =>
    c == ExportCodec.hevc ? 'HEVC (H.265)' : 'H.264';

String exportFormatLabel(ExportFormat f) =>
    f == ExportFormat.pngSequence ? 'Sequencia PNG' : 'MP4';

class ExportSettings {
  const ExportSettings({
    this.size = ExportSize.original,
    this.fps,
    this.quality = 'media',
    this.bitrateMbps,
    this.codec = ExportCodec.h264,
    this.format = ExportFormat.mp4,
  });

  final ExportSize size;

  /// Nulo = o fps do projeto.
  final int? fps;

  /// 'baixa' | 'media' | 'alta'. Ignorado quando ha [bitrateMbps].
  final String quality;

  /// Taxa de bits escolhida na mao, em megabits por segundo. Nulo = pela
  /// qualidade.
  final double? bitrateMbps;

  final ExportCodec codec;
  final ExportFormat format;

  /// A sequencia PNG e o unico caminho com transparencia de verdade: MP4
  /// com alfa so toca em um punhado de programas.
  bool get keepsAlpha => format == ExportFormat.pngSequence;

  ExportSettings copyWith({
    ExportSize? size,
    int? fps,
    bool clearFps = false,
    String? quality,
    double? bitrateMbps,
    bool clearBitrate = false,
    ExportCodec? codec,
    ExportFormat? format,
  }) =>
      ExportSettings(
        size: size ?? this.size,
        fps: clearFps ? null : (fps ?? this.fps),
        quality: quality ?? this.quality,
        bitrateMbps:
            clearBitrate ? null : (bitrateMbps ?? this.bitrateMbps),
        codec: codec ?? this.codec,
        format: format ?? this.format,
      );

  /// O tamanho de saida para um projeto de [w]x[h].
  ///
  /// A altura pedida manda e a largura acompanha a PROPORCAO — pedir
  /// 1080p num projeto vertical tem de dar 1080 de altura, nao de
  /// largura. E tudo sai par, porque o H.264 exige.
  (int, int) resolve(int w, int h) {
    final alvo = exportSizeHeight(size);
    if (alvo == null || h <= 0) return (_par(w), _par(h));
    final escala = alvo / h;
    return (_par((w * escala).round()), _par(alvo));
  }

  int resolveFps(int projectFps) {
    final f = fps ?? projectFps;
    return f < 1 ? 30 : f;
  }

  static int _par(int v) {
    final x = v < 2 ? 2 : v;
    return x.isOdd ? x + 1 : x;
  }

  /// Taxa de bits final, em bits por segundo.
  ///
  /// Escolhida na mao, e a escolhida. Se nao, a conta por pixel — que e
  /// o que mantem a mesma aparencia ao trocar de resolucao. HEVC pede
  /// uns 35% a menos para o mesmo resultado.
  int bitrateFor(int w, int h, int fps) {
    final manual = bitrateMbps;
    if (manual != null && manual > 0) {
      return (manual * 1000000).round().clamp(200000, 200000000);
    }
    final bpp = switch (quality) {
      'alta' => 0.20,
      'baixa' => 0.07,
      _ => 0.12,
    };
    final fator = codec == ExportCodec.hevc ? 0.65 : 1.0;
    final v = (w * h * fps * bpp * fator).round();
    return v.clamp(1000000, 120000000);
  }

  /// Quantos megabytes o video deve ocupar, aproximadamente. Serve para
  /// a tela avisar ANTES de gastar dez minutos rendendo.
  double estimatedMegabytes(int w, int h, int fps, Duration duration) {
    final bits = bitrateFor(w, h, fps) *
        (duration.inMilliseconds / 1000.0);
    return bits / 8 / 1024 / 1024;
  }
}
