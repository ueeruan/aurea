import 'package:aurea/src/features/export/domain/export_settings.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('os ajustes de exportar vão e voltam das prefs', () {
    const a = ExportSettings(
      size: ExportSize.p2160,
      fps: 60,
      quality: 'alta',
      bitrateMbps: 24.5,
      codec: ExportCodec.hevc,
      format: ExportFormat.mp4,
    );
    final volta = ExportSettings.fromJson(a.toJson());
    expect(volta.size, ExportSize.p2160);
    expect(volta.fps, 60);
    expect(volta.quality, 'alta');
    expect(volta.bitrateMbps, 24.5);
    expect(volta.codec, ExportCodec.hevc);
    expect(volta.format, ExportFormat.mp4);
  });

  test('lixo nas prefs cai no padrão em vez de estourar', () {
    expect(ExportSettings.fromJson(null).size, ExportSize.original);
    expect(ExportSettings.fromJson('lixo').codec, ExportCodec.h264);
    final torto = ExportSettings.fromJson({
      's': 999,
      'q': 'turbo',
      'c': -3,
      'fm': 999,
    });
    expect(torto.size, ExportSize.values.last);
    expect(torto.quality, 'media');
    expect(torto.codec, ExportCodec.values.first);
    expect(torto.fps, isNull);
    expect(torto.bitrateMbps, isNull);
  });

  test('o que não foi pedido não vai para o arquivo', () {
    const cru = ExportSettings();
    final m = cru.toJson();
    expect(m.containsKey('f'), isFalse);
    expect(m.containsKey('b'), isFalse);
  });
}
