import 'package:flutter_test/flutter_test.dart';

import 'package:aurea/src/features/export/domain/export_settings.dart';

void main() {
  group('Tamanho de saida', () {
    test('original nao mexe', () {
      expect(const ExportSettings().resolve(1920, 1080), (1920, 1080));
    });

    // A ALTURA pedida manda: 1080p num projeto vertical tem de dar 1080
    // de altura, nao de largura.
    test('vertical: 1080p da 1080 de altura', () {
      const s = ExportSettings(size: ExportSize.p1080);
      expect(s.resolve(1080, 1920), (608, 1080));
    });

    test('horizontal: 720p mantem 16:9', () {
      const s = ExportSettings(size: ExportSize.p720);
      expect(s.resolve(1920, 1080), (1280, 720));
    });

    // O H.264 rejeita dimensao impar — errar isso e exportacao que
    // falha no fim, depois de dez minutos rendendo.
    test('tudo sai par', () {
      const s = ExportSettings(size: ExportSize.p1080);
      final (w, h) = s.resolve(1001, 1333);
      expect(w.isEven, isTrue);
      expect(h.isEven, isTrue);
    });

    test('4K amplia', () {
      const s = ExportSettings(size: ExportSize.p2160);
      expect(s.resolve(1920, 1080), (3840, 2160));
    });

    test('projeto com altura zero nao quebra', () {
      const s = ExportSettings(size: ExportSize.p1080);
      expect(s.resolve(1920, 0).$2.isEven, isTrue);
    });

    test('todo tamanho tem rotulo', () {
      for (final t in ExportSize.values) {
        expect(exportSizeLabel(t).trim(), isNotEmpty);
      }
    });
  });

  group('Quadros por segundo', () {
    test('sem escolha, vale o do projeto', () {
      expect(const ExportSettings().resolveFps(24), 24);
    });

    test('escolhido manda', () {
      expect(const ExportSettings(fps: 60).resolveFps(24), 60);
    });

    test('zero cai em 30', () {
      expect(const ExportSettings().resolveFps(0), 30);
    });
  });

  group('Taxa de bits', () {
    test('alta e maior que media, media maior que baixa', () {
      const alta = ExportSettings(quality: 'alta');
      const media = ExportSettings();
      const baixa = ExportSettings(quality: 'baixa');
      final a = alta.bitrateFor(1920, 1080, 30);
      final m = media.bitrateFor(1920, 1080, 30);
      final b = baixa.bitrateFor(1920, 1080, 30);
      expect(a, greaterThan(m));
      expect(m, greaterThan(b));
    });

    // A conta e por pixel: e o que mantem a mesma aparencia ao trocar
    // de resolucao, em vez de um numero fixo generoso em 720p e pobre
    // em 4K.
    test('4K pede mais que 1080p', () {
      const s = ExportSettings();
      expect(s.bitrateFor(3840, 2160, 30),
          greaterThan(s.bitrateFor(1920, 1080, 30)));
    });

    test('HEVC pede menos para o mesmo tamanho', () {
      const h264 = ExportSettings();
      const hevc = ExportSettings(codec: ExportCodec.hevc);
      expect(hevc.bitrateFor(1920, 1080, 30),
          lessThan(h264.bitrateFor(1920, 1080, 30)));
    });

    test('escolhida na mao manda', () {
      const s = ExportSettings(bitrateMbps: 20);
      expect(s.bitrateFor(640, 360, 24), 20000000);
    });

    test('valor absurdo e aparado', () {
      const s = ExportSettings(bitrateMbps: 99999);
      expect(s.bitrateFor(1920, 1080, 30), lessThanOrEqualTo(200000000));
      const zero = ExportSettings(bitrateMbps: 0);
      expect(zero.bitrateFor(1920, 1080, 30), greaterThan(0));
    });
  });

  group('Previsao de tamanho', () {
    test('o dobro da duracao e o dobro do arquivo', () {
      const s = ExportSettings();
      final a = s.estimatedMegabytes(
          1920, 1080, 30, const Duration(seconds: 10));
      final b = s.estimatedMegabytes(
          1920, 1080, 30, const Duration(seconds: 20));
      expect(b, closeTo(a * 2, 0.01));
    });

    test('duracao zero nao ocupa nada', () {
      expect(
          const ExportSettings()
              .estimatedMegabytes(1920, 1080, 30, Duration.zero),
          0);
    });
  });

  group('Formato', () {
    test('so a sequencia guarda transparencia', () {
      expect(const ExportSettings().keepsAlpha, isFalse);
      expect(
          const ExportSettings(format: ExportFormat.pngSequence).keepsAlpha,
          isTrue);
    });

    test('copiar troca so o que foi pedido', () {
      const base = ExportSettings(quality: 'alta', fps: 60);
      final c = base.copyWith(size: ExportSize.p720);
      expect(c.quality, 'alta');
      expect(c.fps, 60);
      expect(c.size, ExportSize.p720);
    });

    test('limpar volta ao padrao', () {
      const base = ExportSettings(fps: 60, bitrateMbps: 30);
      expect(base.copyWith(clearFps: true).fps, isNull);
      expect(base.copyWith(clearBitrate: true).bitrateMbps, isNull);
    });

    test('todo codec e formato tem rotulo', () {
      for (final c in ExportCodec.values) {
        expect(exportCodecLabel(c).trim(), isNotEmpty);
      }
      for (final f in ExportFormat.values) {
        expect(exportFormatLabel(f).trim(), isNotEmpty);
      }
    });
  });
}
