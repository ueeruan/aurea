import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';
import 'package:whisper_flutter_new/whisper_flutter_new.dart';

import '../../community/application/comunidade_service.dart';
import '../../community/application/conta_da_comunidade.dart';
import '../../settings/application/settings_controller.dart';
import '../domain/caption.dart';
import '../domain/modo_de_transcricao.dart';

export '../domain/modo_de_transcricao.dart';

/// A transcricao na nuvem nao esta disponivel agora: servidor fora, Groq
/// fora, cota esgotada. A tela oferece tentar de novo ou usar o aparelho.
class TranscricaoNaNuvemIndisponivel implements Exception {
  const TranscricaoNaNuvemIndisponivel(
    this.mensagem, {
    this.tenteEm,
    this.cota = false,
  });

  final String mensagem;

  /// Quando vale tentar de novo, se o servidor disse.
  final Duration? tenteEm;

  /// Foi a cota (do dia, ou da hora) — nao um defeito.
  final bool cota;

  @override
  String toString() => mensagem;
}

/// Sem internet, a nuvem nem e tentada.
class TranscricaoSemInternet implements Exception {
  const TranscricaoSemInternet();

  @override
  String toString() => 'Sem conexão com a internet.';
}

/// A nuvem exige a conta da comunidade: e o codigo dela que autoriza o
/// servidor a gastar a cota.
class TranscricaoPrecisaDeConta implements Exception {
  const TranscricaoPrecisaDeConta();

  @override
  String toString() =>
      'A transcrição na nuvem usa a sua conta da comunidade. '
      'Crie a conta na aba Comunidade, ou transcreva no aparelho.';
}

/// O que volta do servidor, ja no formato do app. O app NAO conhece a
/// Groq: conhece este formato, que nao muda quando o modelo muda.
class TranscricaoBruta {
  const TranscricaoBruta({
    required this.texto,
    required this.duracao,
    required this.segmentos,
    required this.palavras,
    this.idioma,
    this.modelo,
  });

  final String texto;
  final double duracao;
  final List<Cue> segmentos;
  final List<Cue> palavras;
  final String? idioma;
  final String? modelo;

  static TranscricaoBruta deJson(Map<String, dynamic> m) {
    Duration segundos(Object? v) =>
        Duration(microseconds: (((v as num?) ?? 0) * 1e6).round());
    List<Cue> lista(Object? bruto, {Duration minimo = Duration.zero}) {
      if (bruto is! List) return const [];
      final out = <Cue>[];
      for (final item in bruto) {
        if (item is! Map) continue;
        final texto = '${item['texto'] ?? ''}'.trim();
        if (texto.isEmpty) continue;
        final inicio = segundos(item['inicio']);
        var fim = segundos(item['fim']);
        if (fim < inicio + minimo) fim = inicio + minimo;
        out.add(Cue(start: inicio, end: fim, text: texto));
      }
      return out;
    }

    return TranscricaoBruta(
      texto: '${m['texto'] ?? ''}'.trim(),
      duracao: ((m['duracao'] as num?) ?? 0).toDouble(),
      idioma: m['idioma'] as String?,
      modelo: m['modelo'] as String?,
      segmentos: lista(m['segmentos']),
      // Uma palavra sem duracao (a Groq as vezes da inicio = fim) ainda
      // precisa aparecer: 80 ms e o minimo que se ve.
      palavras: lista(m['palavras'], minimo: const Duration(milliseconds: 80)),
    );
  }
}

/// LEGENDAS AUTOMATICAS: a mesma pergunta, dois motores.
///
/// [transcribeMedia] recebe uma midia e devolve as falas com tempo. Quem
/// chama nao sabe (nem precisa saber) se a resposta veio da nuvem ou do
/// aparelho:
///
///   - NA NUVEM: o FFmpeg tira do video um AAC mono de 16 kHz (nao o
///     video inteiro — so o audio, e pequeno), o arquivo sobe para o
///     servidor do Aurea com o codigo da conta, o servidor repassa a Groq
///     com a chave dele e devolve texto, segmentos e palavras com tempo.
///     O audio extraido e apagado logo depois de subir.
///   - NO APARELHO: o FFmpeg extrai PCM 16 kHz mono, o whisper.cpp
///     transcreve. Nenhum audio sai do aparelho; o modelo ggml e baixado
///     uma unica vez, com validacao.
///
/// O automatico decide pela internet. Quando a nuvem falha COM internet,
/// a resposta e um erro que a tela transforma em "tentar de novo" ou
/// "usar o aparelho" — e nao uma troca silenciosa que baixa 70 MB de
/// modelo sem avisar.
class TranscriptionService {
  TranscriptionService({
    this.endereco = ComunidadeService.enderecoPadrao,
    HttpClient? http,
    ModoDeTranscricao Function()? modoAtual,
    String? Function()? codigoDaConta,
    // Os tres abaixo existem para os testes: rede, FFmpeg e whisper.cpp
    // de mentira. Chamam-se temInternet, extrairAudio e
    // transcreverNoAparelho de fora.
    this._temInternet,
    this._extrairAudio,
    this._transcreverNoAparelho,
  }) : _http =
           http ??
           (HttpClient()..connectionTimeout = const Duration(seconds: 15)),
       _modoAtual = modoAtual ?? (() => ModoDeTranscricao.auto),
       _codigoDaConta = codigoDaConta ?? (() => null);

  /// O teto do servidor (e da Groq): acima disso nem sobe.
  static const audioMaximo = 25 * 1024 * 1024;

  /// A REVISAO EXATA dos modelos, e nao `main`.
  ///
  /// O download apontava para `resolve/main`: a ponta movel do repositorio.
  /// Dois riscos concretos, e nenhum e teorico neste app:
  ///
  ///   1. o conteudo pode mudar sem aviso, e nada aqui detectaria — o
  ///      arquivo vai direto para o motor nativo, que faz PARSING DE
  ///      FORMATO BINARIO. O patch em `packages/whisper_flutter_new/src/main.cpp`
  ///      existe justamente porque esse caminho ja derrubou o app com SIGSEGV;
  ///   2. duas instalacoes do mesmo app podiam rodar modelos DIFERENTES, o
  ///      que torna qualquer defeito de transcricao irreproduzivel.
  ///
  /// Fixar o commit resolve os dois: o conteudo daquela revisao nao muda.
  /// O repositorio dos modelos e MIT (o GPL-3.0 esta no pacote wrapper,
  /// que e outro assunto e outra correcao).
  static const _revisaoDoModelo = '5359861c739e955e79d9a303bcbc70fb988958b1';

  static const _minModelBytes = <WhisperModel, int>{
    WhisperModel.tiny: 70 * 1024 * 1024,
    WhisperModel.base: 135 * 1024 * 1024,
    WhisperModel.small: 450 * 1024 * 1024,
    WhisperModel.medium: 1400 * 1024 * 1024,
  };

  final String endereco;
  final HttpClient _http;
  final ModoDeTranscricao Function() _modoAtual;
  final String? Function() _codigoDaConta;
  final Future<bool> Function()? _temInternet;
  final Future<String> Function(String, void Function(String)?)? _extrairAudio;
  final Future<List<Cue>> Function(
    String, {
    required WhisperModel model,
    required String language,
    required CaptionMode mode,
    void Function(String status)? onStatus,
  })?
  _transcreverNoAparelho;

  Future<List<Cue>> transcribeMedia(
    String mediaPath, {
    WhisperModel model = WhisperModel.tiny,
    String language = 'pt',
    CaptionMode mode = CaptionMode.frases,
    void Function(String status)? onStatus,
    ModoDeTranscricao? modo,
  }) async {
    final escolhido = modo ?? _modoAtual();
    if (escolhido == ModoDeTranscricao.local) {
      return _local(
        mediaPath,
        model: model,
        language: language,
        mode: mode,
        onStatus: onStatus,
      );
    }
    if (!await temInternet()) {
      if (escolhido == ModoDeTranscricao.auto) {
        onStatus?.call('Sem internet: transcrevendo no aparelho...');
        return _local(
          mediaPath,
          model: model,
          language: language,
          mode: mode,
          onStatus: onStatus,
        );
      }
      throw const TranscricaoSemInternet();
    }
    final codigo = _codigoDaConta();
    if (codigo == null || codigo.trim().isEmpty) {
      throw const TranscricaoPrecisaDeConta();
    }
    return _nuvem(
      mediaPath,
      codigo: codigo,
      language: language,
      mode: mode,
      onStatus: onStatus,
    );
  }

  /// Ha rede ate o servidor? Uma consulta de nome com prazo curto — nao
  /// uma requisicao inteira, que demoraria o mesmo que a transcricao.
  Future<bool> temInternet() async {
    final custom = _temInternet;
    if (custom != null) return custom();
    try {
      final r = await InternetAddress.lookup(Uri.parse(endereco).host)
          .timeout(const Duration(seconds: 4));
      return r.isNotEmpty && r.first.rawAddress.isNotEmpty;
    } catch (_) {
      return false;
    }
  }

  // ------------------------------------------------------------ nuvem

  Future<List<Cue>> _nuvem(
    String mediaPath, {
    required String codigo,
    required String language,
    required CaptionMode mode,
    void Function(String status)? onStatus,
  }) async {
    onStatus?.call('Extraindo áudio...');
    final audio = await (_extrairAudio ?? _extrairAudioPadrao)(
      mediaPath,
      onStatus,
    );
    try {
      final arquivo = File(audio);
      final tamanho = await arquivo.length();
      if (tamanho > audioMaximo) {
        throw const TranscricaoNaNuvemIndisponivel(
          'O áudio passa de 25 MB. Corte o vídeo ou transcreva no aparelho.',
        );
      }
      final segundos = duracaoEstimada(tamanho);
      TranscricaoBruta bruta;
      try {
        bruta = await _enviar(
          arquivo,
          tamanho: tamanho,
          segundos: segundos,
          codigo: codigo,
          language: language,
          onStatus: onStatus,
        );
      } on TranscricaoNaNuvemIndisponivel {
        rethrow;
      } on TranscricaoPrecisaDeConta {
        rethrow;
      } on TimeoutException {
        throw const TranscricaoNaNuvemIndisponivel(
          'A transcrição na nuvem demorou demais. Tente de novo.',
        );
      } catch (e) {
        throw TranscricaoNaNuvemIndisponivel(
          'Não consegui falar com o servidor de transcrição. ($e)',
        );
      }
      return cuesDe(bruta, mode);
    } finally {
      // O AUDIO MORRE AQUI: foi extraido para subir, e subiu.
      try {
        File(audio).deleteSync();
      } catch (_) {}
    }
  }

  Future<TranscricaoBruta> _enviar(
    File arquivo, {
    required int tamanho,
    required double segundos,
    required String codigo,
    required String language,
    void Function(String status)? onStatus,
  }) async {
    onStatus?.call('Enviando áudio...');
    final req = await _http.postUrl(Uri.parse('$endereco/transcricao'));
    req.headers.set('authorization', 'Bearer $codigo');
    req.headers.set('content-type', 'audio/mp4');
    // A duracao e uma ESTIMATIVA para o servidor barrar antes de gastar;
    // a cota de verdade e cobrada pela duracao que a nuvem mede.
    req.headers.set('x-duracao', segundos.toStringAsFixed(1));
    if (RegExp(r'^[a-z]{2}$').hasMatch(language)) {
      req.headers.set('x-idioma', language);
    }
    req.contentLength = tamanho;
    var enviados = 0;
    var ultimoPct = -1;
    await for (final pedaco in arquivo.openRead()) {
      req.add(pedaco);
      await req.flush();
      enviados += pedaco.length;
      final pct = tamanho == 0 ? 100 : enviados * 100 ~/ tamanho;
      if (pct != ultimoPct && pct % 10 == 0) {
        ultimoPct = pct;
        onStatus?.call('Enviando áudio ($pct%)...');
      }
    }
    onStatus?.call('Transcrevendo na nuvem...');
    final res = await req.close().timeout(const Duration(minutes: 3));
    final corpo = await res.transform(utf8.decoder).join();
    Map<String, dynamic> m;
    try {
      m = (jsonDecode(corpo) as Map).cast<String, dynamic>();
    } catch (_) {
      throw TranscricaoNaNuvemIndisponivel(
        'O servidor devolveu algo ilegível (${res.statusCode}).',
      );
    }
    if (res.statusCode == 200) return TranscricaoBruta.deJson(m);
    if (res.statusCode == 401) throw const TranscricaoPrecisaDeConta();
    final tenteEm = m['tenteEm'];
    throw TranscricaoNaNuvemIndisponivel(
      '${m['erro'] ?? 'Transcrição na nuvem indisponível.'}',
      tenteEm: tenteEm is num ? Duration(seconds: tenteEm.round()) : null,
      cota: res.statusCode == 429,
    );
  }

  /// De segmentos e palavras para as falas do modo pedido, com o mesmo
  /// descarte de repeticao em laco do caminho local (alucinacao do
  /// Whisper em silencio).
  static List<Cue> cuesDe(TranscricaoBruta bruta, CaptionMode mode) {
    List<Cue> filtrar(List<Cue> lista) {
      final out = <Cue>[];
      for (final c in lista) {
        final t = c.text.trim();
        if (t.isEmpty || c.end <= c.start) continue;
        if (out.length >= 2 &&
            out[out.length - 1].text == t &&
            out[out.length - 2].text == t) {
          continue;
        }
        out.add(c);
      }
      return out;
    }

    final palavras = bruta.palavras.isNotEmpty
        ? bruta.palavras
        : bruta.segmentos;
    final segmentos = bruta.segmentos.isNotEmpty
        ? bruta.segmentos
        : bruta.palavras;
    switch (mode) {
      case CaptionMode.frases:
        return normalizeCues(filtrar(segmentos));
      case CaptionMode.curtas:
        return groupWordCues(filtrar(palavras));
      case CaptionMode.palavra:
        return filtrar(palavras);
    }
  }

  /// SO O AUDIO SOBE, e pequeno: AAC mono a 32 kbit/s e 16 kHz — que e o
  /// que o Whisper usa de qualquer jeito. Dez minutos dao uns 2,4 MB.
  Future<String> _extrairAudioPadrao(
    String mediaPath,
    void Function(String)? onStatus,
  ) async {
    final tmp = await getTemporaryDirectory();
    final saida =
        '${tmp.path}/transcricao_${DateTime.now().microsecondsSinceEpoch}.m4a';
    // ARGUMENTOS EM LISTA, nunca uma linha de comando montada com
    // aspas: um nome de arquivo com aspas viraria argumento extra do
    // FFmpeg, e argumento de FFmpeg escreve arquivo.
    final session = await FFmpegKit.executeWithArguments([
      '-y',
      '-i',
      mediaPath,
      '-vn',
      '-ac',
      '1',
      '-ar',
      '16000',
      '-c:a',
      'aac',
      '-b:a',
      '32k',
      '-movflags',
      '+faststart',
      saida,
    ]);
    if (!ReturnCode.isSuccess(await session.getReturnCode())) {
      throw Exception('Falha ao extrair o áudio da mídia.');
    }
    return saida;
  }

  /// A duracao pelo tamanho: o AAC sai a 32 kbit/s fixos, entao bytes
  /// viram segundos sem abrir o arquivo. E uma ESTIMATIVA para o servidor
  /// barrar antes de gastar; a cota de verdade e medida pela nuvem.
  static double duracaoEstimada(int bytes) => bytes * 8 / 32000;

  // ------------------------------------------------------------ local

  Future<List<Cue>> _local(
    String mediaPath, {
    required WhisperModel model,
    required String language,
    required CaptionMode mode,
    void Function(String status)? onStatus,
  }) {
    final custom = _transcreverNoAparelho;
    if (custom != null) {
      return custom(
        mediaPath,
        model: model,
        language: language,
        mode: mode,
        onStatus: onStatus,
      );
    }
    return _localPadrao(
      mediaPath,
      model: model,
      language: language,
      mode: mode,
      onStatus: onStatus,
    );
  }

  /// Legendas on-device (spec AM2-modo-edicao-e-legendas §6): FFmpeg
  /// extrai PCM 16 kHz mono, whisper.cpp transcreve. Nenhum audio sai do
  /// aparelho; o modelo ggml e baixado uma unica vez.
  Future<List<Cue>> _localPadrao(
    String mediaPath, {
    required WhisperModel model,
    required String language,
    required CaptionMode mode,
    void Function(String status)? onStatus,
  }) async {
    // 1. Pipeline de audio: decode -> downmix mono -> resample 16 kHz.
    onStatus?.call('Extraindo audio...');
    final tmp = await getTemporaryDirectory();
    final wav = '${tmp.path}/whisper_input.wav';
    final session = await FFmpegKit.executeWithArguments([
      '-y',
      '-i',
      mediaPath,
      '-vn',
      '-ac',
      '1',
      '-ar',
      '16000',
      '-c:a',
      'pcm_s16le',
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
  ///
  /// O download e feito AQUI (nao pelo plugin): com progresso, escrita
  /// atomica (.part -> rename) e validacao de tamanho. O plugin so checa
  /// se o arquivo existe — um download interrompido deixava um modelo
  /// corrompido que derrubava o whisper.cpp nativo (o app fechava).
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
      final request = await client.getUrl(
        Uri.parse(
          'https://huggingface.co/ggerganov/whisper.cpp/resolve/$_revisaoDoModelo/'
          'ggml-${model.modelName}.bin',
        ),
      );
      final response = await request.close();
      if (response.statusCode != 200) {
        throw Exception(
          'Falha ao baixar o modelo (HTTP ${response.statusCode}). '
          'Verifique a conexao.',
        );
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
          'O arquivo baixado nao e um modelo de voz valido. Tente de novo.',
        );
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

/// O servico do app: o modo vem dos Ajustes e o codigo, da conta da
/// comunidade — lidos NA HORA de transcrever, nao na criacao.
final transcriptionServiceProvider = Provider<TranscriptionService>(
  (ref) => TranscriptionService(
    modoAtual: () => ref.read(settingsControllerProvider).modoDeTranscricao,
    codigoDaConta: () => ref.read(contaDaComunidadeProvider)?.codigo,
  ),
);
