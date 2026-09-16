import 'dart:async';
import 'dart:convert';
import 'dart:io';
import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit_config.dart';
import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_session.dart';
import 'package:ffmpeg_kit_flutter_new_full/ffprobe_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:path_provider/path_provider.dart';

import '../../editor/domain/aprimoramento_ia.dart';
import '../../export/application/aprimoramento_export.dart';
import '../../export/application/platform_encoder.dart';
import '../domain/color_look.dart';
import 'enhance_worker.dart';

class EnhanceProgress {
  const EnhanceProgress(this.label, this.fraction);
  final String label;
  final double fraction;
}

/// Owns only its temporary directory and encoder session. Originals are read-only.
class EnhancementJob {
  EnhancementJob({Future<ByteData> Function(String asset)? loadAsset})
    : _loadAsset = loadAsset ?? rootBundle.load;
  final Future<ByteData> Function(String asset) _loadAsset;

  /// Os modelos de cada perfil (os mesmos da exportacao do editor).
  static const modelosDoPerfil = AprimoradorIa.modelosDoPerfil;
  final progress = ValueNotifier(const EnhanceProgress('Pronto', 0));
  Directory? _directory;
  EnhanceWorker? _worker;
  bool _cancelled = false, _encoding = false, _running = false;
  int? _ffmpegSession;
  String get _cancelPath => '${_directory!.path}/cancel';
  PerfilDoAprimoramento _perfil = PerfilDoAprimoramento.videoReal;
  String get _modelDir => '${_directory!.path}/model-${_perfil.name}';

  Future<void> _prepare({
    required bool ai,
    PerfilDoAprimoramento perfil = PerfilDoAprimoramento.videoReal,
  }) async {
    _perfil = perfil;
    _cancelled = false;
    _directory ??= await (await getTemporaryDirectory()).createTemp(
      'aurea-enhance-',
    );
    final flag = File(_cancelPath);
    if (await flag.exists()) await flag.delete();
    if (ai) {
      progress.value = const EnhanceProgress('Preparando modelo de IA…', 0);
      await Directory(_modelDir).create(recursive: true);
      for (final e in modelosDoPerfil[perfil]!.arquivos.entries) {
        final f = File('$_modelDir/${e.key}');
        if (await f.exists()) continue;
        final data = await _loadAsset(e.value);
        await f.writeAsBytes(
          data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes),
        );
      }
    }
    _worker ??= await EnhanceWorker.start();
  }

  void _check() {
    if (_cancelled) throw StateError('Cancelado');
  }

  Future<void> cancel() async {
    _cancelled = true;
    _worker?.cancel();
    if (_directory != null) await File(_cancelPath).writeAsString('cancel');
    final session = _ffmpegSession;
    if (session != null) await FFmpegKit.cancel(session);
  }

  Future<void> _ffmpeg(List<String> args) async {
    _check();
    final done = Completer<FFmpegSession>();
    final session = await FFmpegKit.executeWithArgumentsAsync(
      args,
      done.complete,
    );
    _ffmpegSession = session.getSessionId();
    if (_cancelled) await FFmpegKit.cancel(_ffmpegSession);
    try {
      final result = await done.future;
      _check();
      if (!ReturnCode.isSuccess(await result.getReturnCode())) {
        throw StateError(
          'Não foi possível processar este arquivo. Tente outro formato.',
        );
      }
    } finally {
      _ffmpegSession = null;
    }
  }

  Future<String> _imageSource(String source) async {
    final extension = source.split('.').last.toLowerCase();
    if (!['heic', 'heif', 'avif'].contains(extension)) return source;
    final converted =
        '${_directory!.path}/source-${DateTime.now().microsecondsSinceEpoch}.png';
    await _ffmpeg(['-y', '-i', source, '-frames:v', '1', converted]);
    return converted;
  }

  Future<(String, String)> preview(
    String source,
    bool video,
    EnhanceSettings settings,
  ) async {
    if (_running) throw StateError('Já existe um processamento em andamento');
    _running = true;
    try {
      await _prepare(ai: settings.ai, perfil: settings.perfil);
      _check();
      progress.value = const EnhanceProgress('Preparando comparação…', 0);
      var input = source;
      if (video) {
        input =
            '${_directory!.path}/before-${DateTime.now().microsecondsSinceEpoch}.png';
        await _ffmpeg(['-y', '-i', source, '-frames:v', '1', input]);
      } else {
        input = await _imageSource(source);
      }
      final stamp = DateTime.now().microsecondsSinceEpoch;
      final output = '${_directory!.path}/preview-$stamp.png';
      // O ANTES e o original ampliado de forma convencional ao MESMO
      // tamanho do depois: a comparacao mostra so o que a IA mudou.
      final before = '${_directory!.path}/before-scaled-$stamp.png';
      await _worker!.frame(input, output, _modelDir, _cancelPath, settings, before: before);
      _check();
      progress.value = const EnhanceProgress('Comparação pronta', 1);
      return (before, output);
    } finally {
      _running = false;
    }
  }

  Future<File> process(
    String source,
    bool video,
    EnhanceSettings settings,
  ) async {
    if (_running) throw StateError('Já existe um processamento em andamento');
    _running = true;
    try {
      await _prepare(ai: settings.ai, perfil: settings.perfil);
      _check();
      progress.value = const EnhanceProgress('Preparando arquivo…', 0);
      final result = File(
        '${_directory!.path}/result.${video ? 'mp4' : 'png'}',
      );
      if (video) {
        try {
          await _video(source, result.path, settings);
        } on PlatformException {
          await PlatformEncoder.cancel();
          _encoding = false;
          _check();
          progress.value = const EnhanceProgress(
            'Usando codificação compatível…',
            0,
          );
          await _video(source, result.path, settings, software: true);
        }
      } else {
        final input = await _imageSource(source);
        await _worker!.frame(
          input,
          result.path,
          _modelDir,
          _cancelPath,
          settings,
        );
      }
      _check();
      final dir = Directory(
        '${(await getApplicationDocumentsDirectory()).path}/aurea-enhanced',
      );
      await dir.create(recursive: true);
      final saved = await result.copy(
        '${dir.path}/Aurea-${DateTime.now().microsecondsSinceEpoch}.${video ? 'mp4' : 'png'}',
      );
      progress.value = const EnhanceProgress('Concluído', 1);
      return saved;
    } finally {
      if (_encoding) {
        await PlatformEncoder.cancel();
        _encoding = false;
      }
      _running = false;
    }
  }

  Future<void> _video(
    String source,
    String target,
    EnhanceSettings settings, {
    bool software = false,
  }) async {
    if (!software && !await PlatformEncoder.available) {
      return _video(source, target, settings, software: true);
    }
    final info = await _probe(source);
    final seconds = info.seconds;
    // Entrada da IA: o quadro inteiro, reduzido so se a saida passar de
    // 3840 px no lado maior (limite do encoder).
    final s = settings.ai ? settings.scale.clamp(1, 4) : 1;
    var inW = info.width, inH = info.height;
    final longest = math.max(inW, inH) * s;
    if (longest > 3840) {
      final k = 3840 / longest;
      inW = (inW * k).floor();
      inH = (inH * k).floor();
    }
    inW = math.max(2, inW ~/ 2 * 2);
    inH = math.max(2, inH ~/ 2 * 2);
    final outW = math.max(2, inW * s ~/ 2 * 2);
    final outH = math.max(2, inH * s ~/ 2 * 2);
    // Relogio: a taxa MEDIA real da fonte (antes era 30 fixo). Fonte VFR sai
    // em taxa constante com a mesma duracao, sem perder o sincronismo.
    final fps = info.fps;
    final silent = '${_directory!.path}/silent.mp4';
    final pipe = await FFmpegKitConfig.registerNewFFmpegPipe();
    if (pipe == null) {
      throw StateError('Não foi possível abrir o canal de quadros');
    }
    String? outPipe;
    FFmpegSession? encoderSession;
    Future<FFmpegSession>? encoderDone;
    IOSink? outSink;
    try {
      // QUADROS CRUS POR PIPE: nada de PNG em disco.
      final decodeDone = Completer<FFmpegSession>();
      final decoder = await FFmpegKit.executeWithArgumentsAsync([
        '-y', '-i', source, '-an',
        '-vf', 'fps=$fps,scale=$inW:$inH:flags=bicubic',
        '-f', 'rawvideo', '-pix_fmt', 'rgb24', pipe,
      ], decodeDone.complete);
      _ffmpegSession = decoder.getSessionId();
      if (software) {
        outPipe = await FFmpegKitConfig.registerNewFFmpegPipe();
        if (outPipe == null) {
          throw StateError('Não foi possível abrir o canal do codificador');
        }
        final done = Completer<FFmpegSession>();
        encoderSession = await FFmpegKit.executeWithArgumentsAsync([
          '-y', '-f', 'rawvideo', '-pix_fmt', 'rgba', '-s', '${outW}x$outH',
          '-r', '$fps', '-i', outPipe,
          '-c:v', 'mpeg4', '-q:v', '2', '-pix_fmt', 'yuv420p', silent,
        ], done.complete);
        encoderDone = done.future;
        outSink = File(outPipe).openWrite();
      } else {
        await PlatformEncoder.start(
          path: silent,
          width: outW,
          height: outH,
          fps: fps,
          bitrate: (outW * outH * fps * .2).round().clamp(4000000, 80000000),
          pelaMemoria: true,
        );
        _encoding = true;
      }
      final total = math.max(1, (seconds * fps).ceil());
      var feitos = 0;
      final sink = outSink;
      final count = await _worker!.video(
        pipe, inW, inH, _modelDir, settings, outW, outH,
        (rgba, w, h) async {
          _check();
          if (sink != null) {
            sink.add(rgba);
            await sink.flush();
          } else {
            await PlatformEncoder.frameRgba(rgba, w, h);
          }
          feitos++;
          progress.value = EnhanceProgress(
            'Melhorando quadro $feitos de $total',
            math.min(.95, feitos / total * .95),
          );
        },
      );
      final decoded = await decodeDone.future;
      _ffmpegSession = null;
      _check();
      if (!ReturnCode.isSuccess(await decoded.getReturnCode()) || count == 0) {
        throw StateError('O vídeo não contém quadros legíveis');
      }
      if (sink != null) {
        await sink.close();
        outSink = null;
        final enc = await encoderDone!;
        if (!ReturnCode.isSuccess(await enc.getReturnCode())) {
          throw StateError('Não foi possível codificar o vídeo');
        }
      } else if (!await PlatformEncoder.finish()) {
        throw PlatformException(
          code: 'encode_finish',
          message: 'Não foi possível finalizar o vídeo',
        );
      }
      _encoding = false;
    } finally {
      await outSink?.close();
      if (encoderSession != null && _cancelled) {
        await FFmpegKit.cancel(encoderSession.getSessionId());
      }
      await FFmpegKitConfig.closeFFmpegPipe(pipe);
      if (outPipe != null) await FFmpegKitConfig.closeFFmpegPipe(outPipe);
    }
    _check();
    progress.value = const EnhanceProgress('Preservando o áudio…', .97);
    await _ffmpeg([
      '-y', '-i', silent, '-i', source,
      '-map', '0:v:0', '-map', '1:a?',
      '-c:v', 'copy', '-c:a', 'aac', '-b:a', '192k',
      '-t', seconds.toStringAsFixed(9),
      '-movflags', '+faststart', target,
    ]);
  }

  /// Tamanho ja ROTACIONADO, taxa media e duracao, via ffprobe em JSON.
  Future<({int width, int height, int fps, double seconds})> _probe(
    String source,
  ) async {
    final session = await FFprobeKit.executeWithArguments([
      '-v', 'error', '-select_streams', 'v:0',
      '-show_entries',
      'stream=width,height,avg_frame_rate,r_frame_rate:stream_side_data=rotation:stream_tags=rotate:format=duration',
      '-of', 'json', source,
    ]);
    final text = await session.getOutput() ?? '';
    return parseProbe(text);
  }

  /// Separado para teste: interpreta a saida JSON do ffprobe.
  static ({int width, int height, int fps, double seconds}) parseProbe(
    String text,
  ) {
    final json = jsonDecode(text) as Map<String, dynamic>;
    final streams = ((json['streams'] as List?) ?? const [])
        .cast<Map<String, dynamic>>();
    if (streams.isEmpty) throw StateError('Nenhuma faixa de vídeo encontrada');
    final st = streams.first;
    var w = (st['width'] as num?)?.toInt() ?? 0;
    var h = (st['height'] as num?)?.toInt() ?? 0;
    var rot = 0;
    for (final sd in ((st['side_data_list'] as List?) ?? const [])
        .cast<Map<String, dynamic>>()) {
      rot = (sd['rotation'] as num?)?.toInt() ?? rot;
    }
    rot = int.tryParse('${(st['tags'] as Map?)?['rotate'] ?? ''}') ?? rot;
    if (rot.abs() % 180 == 90) {
      final t = w;
      w = h;
      h = t;
    }
    double rate(String? r) {
      final p = (r ?? '').split('/');
      if (p.length != 2) return double.tryParse(r ?? '') ?? 0;
      final d = double.tryParse(p[1]) ?? 0;
      return d == 0 ? 0 : (double.tryParse(p[0]) ?? 0) / d;
    }

    var f = rate(st['avg_frame_rate'] as String?);
    if (!(f > 1 && f < 241)) f = rate(st['r_frame_rate'] as String?);
    final fps = (f > 1 && f < 241) ? f.round() : 30;
    final seconds =
        double.tryParse('${(json['format'] as Map?)?['duration'] ?? ''}') ?? 0;
    if (w <= 0 || h <= 0 || !seconds.isFinite || seconds <= 0) {
      throw StateError('Não foi possível ler este vídeo');
    }
    return (width: w, height: h, fps: fps, seconds: seconds);
  }

  Future<void> close() async {
    await cancel();
    // Call only after preview/process completes, so native resources are released safely.
    if (_running) return;
    await _worker?.close();
    _worker = null;
    if (_directory != null && await _directory!.exists()) {
      await _directory!.delete(recursive: true);
    }
    _directory = null;
    progress.dispose();
  }
}
