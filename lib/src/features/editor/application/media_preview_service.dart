import 'dart:async';
import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../domain/audio_ops.dart';
import '../domain/streaming_waveform.dart';
import '../domain/peak_pyramid.dart';
import '../domain/waveform_cache.dart';

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

/// Em que pe esta a forma de onda de um arquivo.
enum EstadoDaOnda { pendente, pronta, semAudio, falhou }

class MediaPreviewService {
  MediaPreviewService._();
  static final instance = MediaPreviewService._();

  final Map<String, Float32List> _peaks = {};
  final Map<String, EstadoDaOnda> _estado = {};
  final Map<String, double> _ganho = {};

  /// Pronta, sem audio, falhou ou ainda nao analisada.
  EstadoDaOnda estadoDaOnda(String path) =>
      _estado[path] ?? EstadoDaOnda.pendente;

  /// Ganho de EXIBICAO da onda (fala baixa legivel), 1..12.
  double ganhoDaOnda(String path) => _ganho[path] ?? 1;

  /// PRE-AQUECE a onda de varios arquivos: chamado ao importar e ao abrir
  /// um projeto, para a onda ja estar la quando a barra aparecer — e nao
  /// so quando a linha rola para dentro da tela.
  void preparar(Iterable<String> paths) {
    for (final p in paths.toSet()) {
      if (p.isEmpty) continue;
      ensureWaveform(p).ignore();
    }
  }

  /// A SONORIDADE de cada arquivo, em LUFS. Medir custa uma decodificacao
  /// inteira; o numero nao muda enquanto o arquivo for o mesmo.
  final Map<String, double?> _lufs = {};

  /// A PIRAMIDE por arquivo — o que o desenho usa. Ampliar troca de
  /// nivel, nunca recalcula.
  final Map<String, PeakPyramid> _pyramids = {};
  final Map<String, List<ui.Image>> _strips = {};
  final Map<String, Future<void>> _emAndamento = {};
  Future<void> _waveQueue = Future.value();

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
    if (_emAndamento[k] != null) return _emAndamento[k]!;
    final work = _waveQueue.then((_) => _buildWaveform(path));
    _waveQueue = work.catchError((Object _) {});
    return _emAndamento[k] = work.whenComplete(() {
      _emAndamento.remove(k);
    });
  }

  Future<void> _buildWaveform(String path) async {
    try {
      final dir = await _cacheDir('waveforms');
      final cache = File('${dir.path}/${_key(path)}.wf2');
      if (cache.existsSync()) {
        final d = decodeWaveformCache(await cache.readAsBytes());
        if (d != null) {
          _aplicar(path, d);
          return;
        }
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
        '-v',
        'error',
        '-nostats',
        '-i',
        path,
        '-map',
        '0:a:0',
        '-vn',
        '-ac',
        '1',
        '-ar',
        '16000',
        '-f',
        's16le',
        '-acodec',
        'pcm_s16le',
        raw.path,
      ]);
      if (!ReturnCode.isSuccess(await session.getReturnCode()) ||
          !raw.existsSync()) {
        final log = (await session.getOutput()) ?? '';
        // SEM FAIXA DE AUDIO e um fato do arquivo: vai para o disco, e o
        // FFmpeg nao roda de novo a cada sessao. Outra falha (arquivo
        // sumiu, codec) nao e gravada — pode dar certo na proxima.
        if (log.contains('matches no streams') ||
            log.contains('does not contain any stream')) {
          final d = WaveformCacheData.semAudio();
          await _gravarAtomico(cache, encodeWaveformCache(d));
          _aplicar(path, d);
        } else {
          _peaks[path] = Float32List(0);
          _estado[path] = EstadoDaOnda.falhou;
          revision.value++;
        }
        return;
      }

      final scanned = await compute(scanMonoPcm, raw.path);
      await raw.delete();
      final d = WaveformCacheData(
        hasAudio: scanned.baseMax.isNotEmpty,
        sampleRate: 16000,
        samplesPerBucket: waveBaseBucket,
        lufs: scanned.lufs,
        peaksPerSecond: peaksPerSecond,
        peaks: scanned.peaks,
        baseMin: scanned.baseMin,
        baseMax: scanned.baseMax,
        baseRms: scanned.baseRms,
      );
      await _gravarAtomico(cache, encodeWaveformCache(d));
      // O cache v1 (so o envelope) fica obsoleto.
      final antigo = File('${dir.path}/${_key(path)}.pk');
      if (antigo.existsSync()) antigo.deleteSync();
      _aplicar(path, d);
    } catch (_) {
      _peaks[path] = Float32List(0);
      _estado[path] = EstadoDaOnda.falhou;
      revision.value++;
    }
  }

  void _aplicar(String path, WaveformCacheData d) {
    if (!d.hasAudio || d.baseMax.isEmpty) {
      _peaks[path] = Float32List(0);
      _estado[path] = EstadoDaOnda.semAudio;
      revision.value++;
      return;
    }
    _peaks[path] = d.peaks;
    _lufs[path] = d.lufs;
    _pyramids[path] = pyramidFromBase(
      d.baseMin,
      d.baseMax,
      d.baseRms,
      d.sampleRate,
    );
    _ganho[path] = waveformDisplayGain(d.peaks);
    _estado[path] = EstadoDaOnda.pronta;
    revision.value++;
  }

  /// Escrita atomica: `.part` e renomeia. Um app morto no meio da escrita
  /// nao deixa um cache truncado que seria lido como verdade.
  static Future<void> _gravarAtomico(File destino, Uint8List bytes) async {
    final parte = File('${destino.path}.part');
    await parte.writeAsBytes(bytes, flush: true);
    if (destino.existsSync()) destino.deleteSync();
    await parte.rename(destino.path);
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
        '-i',
        path,
        '-vn',
        '-ac',
        '1',
        '-ar',
        '16000',
        '-f',
        's16le',
        '-acodec',
        'pcm_s16le',
        raw.path,
      ]);
      if (!ReturnCode.isSuccess(await session.getReturnCode()) ||
          !raw.existsSync()) {
        return _bandas[k] = Float32List(0);
      }
      final bytes = await raw.readAsBytes();
      raw.deleteSync();
      final samples = Int16List.view(
        bytes.buffer,
        bytes.offsetInBytes,
        bytes.length ~/ 2,
      );
      return _bandas[k] = bandEnvelope(samples, 16000, peaksPerSecond, band);
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
    return _emAndamento[k] ??= _buildFilmstrip(path, duration).whenComplete(() {
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
          '-i',
          path,
          '-vf',
          'fps=${fps.toStringAsFixed(4)},scale=-1:96',
          '-vsync',
          '0',
          '-q:v',
          '5',
          '-frames:v',
          '$stripCount',
          '${sub.path}/%03d.jpg',
        ]);
        if (!ReturnCode.isSuccess(await session.getReturnCode())) {
          _strips[path] = const [];
          revision.value++;
          return;
        }
      }

      final files =
          sub
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
            targetHeight: 96,
          );
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
    _pyramids.clear();
    _lufs.clear();
    _bandas.clear();
    _estado.clear();
    _ganho.clear();
  }
}
