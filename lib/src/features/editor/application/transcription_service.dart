import 'dart:io';

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';
import 'package:whisper_flutter_new/whisper_flutter_new.dart';

import '../domain/caption.dart';

/// Legendas automaticas on-device (spec AM2-modo-edicao-e-legendas §6):
/// FFmpeg extrai PCM 16 kHz mono do video/audio, whisper.cpp transcreve.
/// Nenhum audio sai do aparelho; o modelo ggml e baixado uma unica vez.
///
/// O download do modelo e feito AQUI (nao pelo plugin): com progresso,
/// escrita atomica (.part -> rename) e validacao de tamanho. O plugin so
/// checa se o arquivo existe — um download interrompido deixava um modelo
/// corrompido que derrubava o whisper.cpp nativo (o app fechava).
class TranscriptionService {
  static const _minModelBytes = <WhisperModel, int>{
    WhisperModel.tiny: 70 * 1024 * 1024,
    WhisperModel.base: 135 * 1024 * 1024,
    WhisperModel.small: 450 * 1024 * 1024,
    WhisperModel.medium: 1400 * 1024 * 1024,
  };

  Future<List<Cue>> transcribeMedia(
    String mediaPath, {
    WhisperModel model = WhisperModel.tiny,
    String language = 'pt',
    CaptionMode mode = CaptionMode.frases,
    void Function(String status)? onStatus,
  }) async {
    // 1. Pipeline de audio: decode -> downmix mono -> resample 16 kHz.
    onStatus?.call('Extraindo audio...');
    final tmp = await getTemporaryDirectory();
    final wav = '${tmp.path}/whisper_input.wav';
    // ARGUMENTOS EM LISTA, nunca uma linha de comando montada com
    // aspas: um nome de arquivo com aspas viraria argumento extra do
    // FFmpeg, e argumento de FFmpeg escreve arquivo.
    final session = await FFmpegKit.executeWithArguments([
      '-y',
      '-i', mediaPath,
      '-vn',
      '-ac', '1',
      '-ar', '16000',
      '-c:a', 'pcm_s16le',
      wav,
    ]);
    if (!ReturnCode.isSuccess(await session.getReturnCode())) {
      throw Exception('Falha ao extrair o audio da midia.');
    }

    // 2. Garante o modelo integro no disco antes de tocar no codigo nativo.
    final modelDir = await _ensureModel(model, onStatus);

    onStatus?.call('Transcrevendo no aparelho...');
    final whisper = Whisper(model: model, modelDir: modelDir);
    // Palavra/curtas: timestamps POR TOKEN (max_len=1 no whisper.cpp) —
    // cada segmento vira ~uma palavra, que o modo depois agrupa ou nao.
    final response = await whisper.transcribe(
      transcribeRequest: TranscribeRequest(
        audio: wav,
        language: language,
        threads: 4,
        splitOnWord: mode != CaptionMode.frases,
      ),
    );

    // 3. Segmentos -> cues, com descarte de repeticao em laco
    //    (mitigacao de alucinacao do Whisper em silencio).
    final cues = <Cue>[];
    for (final segment in response.segments ?? <WhisperTranscribeSegment>[]) {
      final text = segment.text.trim();
      if (text.isEmpty) continue;
      if (cues.length >= 2 &&
          cues[cues.length - 1].text == text &&
          cues[cues.length - 2].text == text) {
        continue;
      }
      if (segment.toTs <= segment.fromTs) continue;
      cues.add(Cue(start: segment.fromTs, end: segment.toTs, text: text));
    }
    // Cada modo tem seu pos-processamento: normalizar FRASES uniria as
    // palavras de volta (duracao minima de 0,8s), entao palavra/curtas
    // nao passam pelo merge.
    switch (mode) {
      case CaptionMode.frases:
        return normalizeCues(cues);
      case CaptionMode.curtas:
        return groupWordCues(cues);
      case CaptionMode.palavra:
        return cues;
    }
  }

  /// Baixa o modelo ggml se preciso e devolve o diretorio onde ele mora.
  Future<String> _ensureModel(
    WhisperModel model,
    void Function(String status)? onStatus,
  ) async {
    final dir = await getApplicationSupportDirectory();
    final path = '${dir.path}/ggml-${model.modelName}.bin';
    final file = File(path);
    final minBytes = _minModelBytes[model] ?? 10 * 1024 * 1024;

    if (file.existsSync()) {
      if (file.lengthSync() >= minBytes && _hasGgmlMagic(file)) {
        return dir.path;
      }
      // Sobra de download interrompido ou arquivo invalido (ex.: pagina
      // de erro salva como .bin): apaga e baixa de novo.
      file.deleteSync();
    }

    final part = File('$path.part');
    if (part.existsSync()) part.deleteSync();

    onStatus?.call('Baixando modelo de voz (0%)...');
    final client = HttpClient();
    try {
      final request = await client.getUrl(Uri.parse(
        'https://huggingface.co/ggerganov/whisper.cpp/resolve/main/'
        'ggml-${model.modelName}.bin',
      ));
      final response = await request.close();
      if (response.statusCode != 200) {
        throw Exception(
            'Falha ao baixar o modelo (HTTP ${response.statusCode}). '
            'Verifique a conexao.');
      }
      final total = response.contentLength;
      var received = 0;
      var lastPct = -1;
      final sink = part.openWrite();
      try {
        await for (final chunk in response) {
          sink.add(chunk);
          received += chunk.length;
          if (total > 0) {
            final pct = received * 100 ~/ total;
            if (pct != lastPct) {
              lastPct = pct;
              onStatus?.call('Baixando modelo de voz ($pct%)...');
            }
          }
        }
      } finally {
        await sink.close();
      }
      if (received < minBytes) {
        throw Exception('Download do modelo veio incompleto. Tente de novo.');
      }
      if (!_hasGgmlMagic(part)) {
        throw Exception(
            'O arquivo baixado nao e um modelo de voz valido. Tente de novo.');
      }
      part.renameSync(path);
    } catch (e) {
      if (part.existsSync()) part.deleteSync();
      rethrow;
    } finally {
      client.close();
    }
    return dir.path;
  }

  /// Primeiros 4 bytes do modelo ggml: magic 0x67676d6c ("ggml") em
  /// little-endian = bytes 6C 6D 67 67. Um modelo sem esse cabecalho
  /// nunca deve chegar ao codigo nativo.
  static bool _hasGgmlMagic(File f) {
    try {
      final raf = f.openSync();
      try {
        final b = raf.readSync(4);
        return b.length == 4 &&
            b[0] == 0x6C &&
            b[1] == 0x6D &&
            b[2] == 0x67 &&
            b[3] == 0x67;
      } finally {
        raf.closeSync();
      }
    } catch (_) {
      return false;
    }
  }
}

final transcriptionServiceProvider =
    Provider<TranscriptionService>((ref) => TranscriptionService());
