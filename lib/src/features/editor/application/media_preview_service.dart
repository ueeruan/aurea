import 'dart:async';
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../domain/audio_ops.dart';
import '../domain/loudness.dart';
import '../domain/peak_pyramid.dart';

/// FORMA DE ONDA e TIRA DE MINIATURAS.
///
/// Sem elas nao existe decupagem: a forma de onda e como se acha a
/// respiracao entre duas falas, e a tira de miniaturas e como se acha o
/// corte sem ficar arrastando o playhead no escuro.
///
/// As duas sao caras de calcular e baratas de guardar, entao vao para o
/// disco na primeira vez e sao lidas dali depois. A chave e o caminho do
/// arquivo mais o tamanho e a data — se a midia mudar, o cache cai
/// sozinho.
/// Envelope de PICO por balde de tempo, em 0..1.
///
/// O valor de cada balde e a amplitude MAXIMA do trecho, nao a media.
/// Media achata transiente — e transiente e exatamente o que a pessoa
/// procura quando corta no ritmo ou acha a respiracao entre duas falas.
Float32List computePeaks(Int16List samples, int rate, int perSecond) {
  if (samples.isEmpty || rate <= 0 || perSecond <= 0) {
    return Float32List(0);
  }
  final perBucket = (rate / perSecond).round().clamp(1, rate);
  final count = samples.length ~/ perBucket;
  final out = Float32List(count);
  for (var i = 0; i < count; i++) {
    var peak = 0;
    final start = i * perBucket;
    for (var j = 0; j < perBucket; j++) {
      final v = samples[start + j];
      final a = v < 0 ? -v : v;
      if (a > peak) peak = a;
    }
    out[i] = peak / 32768.0;
  }
  return out;
}

class MediaPreviewService {
  MediaPreviewService._();
  static final instance = MediaPreviewService._();

  final Map<String, Float32List> _peaks = {};

  /// A SONORIDADE de cada arquivo, em LUFS. Medir custa uma decodificacao
  /// inteira; o numero nao muda enquanto o arquivo for o mesmo.
  final Map<String, double?> _lufs = {};

  /// A PIRAMIDE por arquivo — o que o desenho usa. Ampliar troca de
  /// nivel, nunca recalcula.
  final Map<String, PeakPyramid> _pyramids = {};
  final Map<String, List<ui.Image>> _strips = {};
  final Map<String, Future<void>> _emAndamento = {};

  /// Avisa a interface quando algo novo ficou pronto.
  final ValueNotifier<int> revision = ValueNotifier(0);

  /// Picos ja calculados para [path], ou null se ainda nao ha.
  ///
  /// E o envelope simples de sempre (100 baldes por segundo), que os
  /// detectores de silencio e batida consomem.
  Float32List? peaksOf(String path) => _peaks[path];

  /// A SONORIDADE integrada de [path], em LUFS, ou null quando a faixa e
  /// muda ou ainda nao foi analisada.
  double? loudnessOf(String path) => _lufs[path];

  /// A PIRAMIDE de [path] — min, max e RMS em seis niveis de detalhe.
  PeakPyramid? pyramidOf(String path) => _pyramids[path];

  /// Miniaturas ja extraidas para [path], ou null.
  List<ui.Image>? stripOf(String path) => _strips[path];

  static String _key(String path) {
    final f = File(path);
    var stamp = '';
    try {
      final s = f.statSync();
      stamp = '${s.size}_${s.modified.millisecondsSinceEpoch}';
    } catch (_) {}
    final name = path.hashCode.toRadixString(16);
    return '${name}_$stamp';
  }

  Future<Directory> _cacheDir(String kind) async {
    final base = await getApplicationSupportDirectory();
    final d = Directory('${base.path}/$kind');
    if (!d.existsSync()) d.createSync(recursive: true);
    return d;
  }

  // ------------------------------------------------- forma de onda

  /// Quantos picos por segundo. 100 e o suficiente para enxergar
  /// silabas sem gerar arquivo grande.
  static const peaksPerSecond = 100;

  /// Calcula (ou le do cache) a forma de onda de [path].
  ///
  /// O valor de cada pico e a AMPLITUDE MAXIMA do trecho, em 0..1 — nao
  /// a media. Media achata transiente, e transiente e exatamente o que a
  /// pessoa procura quando corta no ritmo.
  Future<void> ensureWaveform(String path) {
    final k = 'wave:$path';
    if (_peaks.containsKey(path)) return Future.value();
    return _emAndamento[k] ??= _buildWaveform(path).whenComplete(() {
      _emAndamento.remove(k);
    });
  }

  Future<void> _buildWaveform(String path) async {
    try {
      final dir = await _cacheDir('waveforms');
      final cache = File('${dir.path}/${_key(path)}.pk');
      if (cache.existsSync()) {
        final bytes = await cache.readAsBytes();
        final env = Float32List.view(
            bytes.buffer, bytes.offsetInBytes, bytes.length ~/ 4);
        _peaks[path] = env;
        // A piramide se remonta do envelope guardado: cada balde do
        // envelope vira uma "amostra". Perde o detalhe abaixo de 10 ms,
        // que e menor que um pixel em qualquer zoom da linha.
        final comoAmostras = Int16List(env.length);
        for (var i = 0; i < env.length; i++) {
          comoAmostras[i] = (env[i].clamp(0.0, 1.0) * 32767).round();
        }
        _pyramids[path] =
            buildPeakPyramid(comoAmostras, peaksPerSecond);
        revision.value++;
        return;
      }

      // Decodifica para PCM cru mono a 16 kHz. Oito bastava para
      // desenhar, mas 16 e a taxa que o reconhecimento de fala usa — o
      // mesmo PCM serve para os dois, e decodificar duas vezes o mesmo
      // arquivo seria trabalho jogado fora.
      final tmp = await getTemporaryDirectory();
      final raw = File('${tmp.path}/wave_${path.hashCode}.pcm');
      if (raw.existsSync()) raw.deleteSync();

      final session = await FFmpegKit.executeWithArguments([
        '-y',
        '-i', path,
        '-vn',
        '-ac', '1',
        '-ar', '16000',
        '-f', 's16le',
        '-acodec', 'pcm_s16le',
        raw.path,
      ]);
      if (!ReturnCode.isSuccess(await session.getReturnCode()) ||
          !raw.existsSync()) {
        _peaks[path] = Float32List(0);
        revision.value++;
        return;
      }

      final bytes = await raw.readAsBytes();
      raw.deleteSync();
      final samples = Int16List.view(
          bytes.buffer, bytes.offsetInBytes, bytes.length ~/ 2);

      final out = computePeaks(samples, 16000, peaksPerSecond);
      _pyramids[path] = buildPeakPyramid(samples, 16000);
      // A SONORIDADE sai da mesma decodificacao. Medir depois obrigaria a
      // decodificar o arquivo de novo so para isso.
      final flutuante = Float32List(samples.length);
      for (var i = 0; i < samples.length; i++) {
        flutuante[i] = samples[i] / 32768.0;
      }
      _lufs[path] = integratedLufs(flutuante, 16000);

      await cache.writeAsBytes(out.buffer.asUint8List(), flush: true);
      _peaks[path] = out;
      revision.value++;
    } catch (_) {
      _peaks[path] = Float32List(0);
      revision.value++;
    }
  }

  /// Envelopes por FAIXA DE FREQUENCIA, so quando alguem pede.
  ///
  /// A forma de onda guardada em disco e de banda inteira — filtrar
  /// depois dela nao separaria nada, porque o pico ja misturou tudo. Por
  /// isso aqui o arquivo e decodificado de novo. E caro, e roda uma vez,
  /// sob comando explicito: analisar batidas.
  final Map<String, Float32List> _bandas = {};

  Future<Float32List> bandEnvelopeOf(String path, BeatBand band) async {
    final k = '${band.name}:$path';
    final pronto = _bandas[k];
    if (pronto != null) return pronto;
    try {
      final tmp = await getTemporaryDirectory();
      final raw = File('${tmp.path}/band_${path.hashCode}.pcm');
      if (raw.existsSync()) raw.deleteSync();

      final session = await FFmpegKit.executeWithArguments([
        '-y',
        '-i', path,
        '-vn',
        '-ac', '1',
        '-ar', '16000',
        '-f', 's16le',
        '-acodec', 'pcm_s16le',
        raw.path,
      ]);
      if (!ReturnCode.isSuccess(await session.getReturnCode()) ||
          !raw.existsSync()) {
        return _bandas[k] = Float32List(0);
      }
      final bytes = await raw.readAsBytes();
      raw.deleteSync();
      final samples = Int16List.view(
          bytes.buffer, bytes.offsetInBytes, bytes.length ~/ 2);
      return _bandas[k] =
          bandEnvelope(samples, 16000, peaksPerSecond, band);
    } catch (_) {
      return _bandas[k] = Float32List(0);
    }
  }

  // ------------------------------------------- tira de miniaturas

  /// Quantas miniaturas por clipe. Mais que isso vira memoria sem virar
  /// informacao — a barra tem poucos pixels de altura.
  static const stripCount = 12;

  Future<void> ensureFilmstrip(String path, Duration duration) {
    final k = 'strip:$path';
    if (_strips.containsKey(path)) return Future.value();
    return _emAndamento[k] ??=
        _buildFilmstrip(path, duration).whenComplete(() {
      _emAndamento.remove(k);
    });
  }

  Future<void> _buildFilmstrip(String path, Duration duration) async {
    try {
      final dir = await _cacheDir('filmstrips');
      final sub = Directory('${dir.path}/${_key(path)}');
      final total = duration.inMilliseconds / 1000.0;
      if (total <= 0) {
        _strips[path] = const [];
        return;
      }

      if (!sub.existsSync()) {
        sub.createSync(recursive: true);
        // Uma miniatura a cada fatia igual do clipe, com 96 px de altura
        // — o suficiente para reconhecer a cena numa barra fina.
        final fps = stripCount / total;
        final session = await FFmpegKit.executeWithArguments([
          '-y',
          '-i', path,
          '-vf', 'fps=${fps.toStringAsFixed(4)},scale=-1:96',
          '-vsync', '0',
          '-q:v', '5',
          '-frames:v', '$stripCount',
          '${sub.path}/%03d.jpg',
        ]);
        if (!ReturnCode.isSuccess(await session.getReturnCode())) {
          _strips[path] = const [];
          revision.value++;
          return;
        }
      }

      final files = sub
          .listSync()
          .whereType<File>()
          .where((f) => f.path.endsWith('.jpg'))
          .toList()
        ..sort((a, b) => a.path.compareTo(b.path));

      final images = <ui.Image>[];
      for (final f in files) {
        try {
          final codec = await ui.instantiateImageCodec(
              await f.readAsBytes(),
              targetHeight: 96);
          final frame = await codec.getNextFrame();
          codec.dispose();
          images.add(frame.image);
        } catch (_) {}
      }
      _strips[path] = images;
      revision.value++;
    } catch (_) {
      _strips[path] = const [];
      revision.value++;
    }
  }

  /// Apaga tudo que esta em memoria (nao o cache em disco).
  void clearMemory() {
    for (final list in _strips.values) {
      for (final img in list) {
        img.dispose();
      }
    }
    _strips.clear();
    _peaks.clear();
  }
}
