import 'dart:io';

import 'package:aurea_core/aurea_core.dart';
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

  /// O fluxo aberto esta no NUCLEO C++ (Android): quadros por FFI, sem
  /// canal de plataforma, conversao de cor e codificacao numa thread
  /// nativa enquanto o Dart ja desenha o proximo quadro. Quando o nucleo
  /// nao abre (codificador recusou a configuracao), o fluxo vai pelo
  /// caminho antigo, em Kotlin — a exportacao nunca fica sem codificador.
  static bool _noNucleo = false;

  /// A CHAVE A/B DO NUCLEO. Desligada por padrao: o codificador em C++ so
  /// entra na exportacao depois de validado em aparelho (no emulador de
  /// 16/09 o Codec2 de software recusou a configuracao pelo NDK e o fluxo
  /// voltou ao Kotlin, como deve). A bancada e os testes ligam.
  static bool nucleoLigado = false;

  /// Se o fluxo aberto agora esta no nucleo C++ (para o teste e o relatorio).
  static bool get noNucleo => _noNucleo;

  /// Se o aparelho tem o codificador. Consultado uma vez.
  static Future<bool> get available async {
    // `true` e estavel durante a sessao. `false` nao: a primeira consulta
    // pode acontecer enquanto o Flutter ainda registra o plugin nativo.
    // Guardar esse falso para sempre desativava a exportacao ate fechar o
    // app — e tambem contaminava a tentativa seguinte depois de um erro.
    if (_available == true) return true;
    try {
      final ok = await _channel.invokeMethod<bool>('available') ?? false;
      if (ok) _available = true;
      return ok;
    } on MissingPluginException {
      return false;
    } catch (_) {
      return false;
    }
  }

  /// Abre o fluxo. [bitrate] em bits por segundo.
  static Future<void> start({
    required String path,
    required int width,
    required int height,
    required int fps,
    required int bitrate,
    bool hevc = false,

    /// Quem chama vai mandar os quadros por [frameRgba] (e nao por PNG):
    /// so assim o fluxo pode ir para o nucleo.
    bool pelaMemoria = false,
  }) async {
    _noNucleo =
        nucleoLigado &&
        pelaMemoria &&
        Platform.isAndroid &&
        CodificadorNativo.disponivel &&
        CodificadorNativo.abrir(
          caminho: path,
          largura: width,
          altura: height,
          fps: fps,
          bitrate: bitrate,
          hevc: hevc,
        );
    if (_noNucleo) return;
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
  ///
  /// Caminho ANTIGO, mantido so para o modo de reserva. Ver [frameRgba]:
  /// passar por PNG e disco custava mais que codificar.
  static Future<void> frame(String path) async {
    await _channel.invokeMethod<bool>('frame', {'path': path});
  }

  /// CODIFICA UM QUADRO DIRETO DA MEMORIA.
  ///
  /// Este metodo e a correcao central da exportacao. O caminho anterior,
  /// por quadro, era:
  ///
  ///   GPU → CPU  →  codificar PNG (zlib)  →  gravar no disco
  ///                        ... e depois, numa segunda passada ...
  ///   ler do disco  →  decodificar PNG  →  buffer  →  codificador
  ///
  /// O PNG nao existia por nenhum motivo de imagem: era so o jeito de os
  /// pixels irem do Dart ao codificador nativo. Custava de longe a maior
  /// parte do tempo de exportacao (o zlib de um quadro 1080p sozinho e
  /// dezenas a centenas de milissegundos) e obrigava a guardar o filme
  /// inteiro descomprimido em disco — dezenas de gigabytes num filme de
  /// dez minutos, que e por que a exportacao as vezes simplesmente nao
  /// terminava.
  ///
  /// Agora os bytes crus atravessam a ponte uma vez e entram no
  /// codificador. Sem compressao, sem disco, sem segunda passada.
  static Future<void> frameRgba(Uint8List rgba, int width, int height) async {
    if (_noNucleo) {
      if (!CodificadorNativo.quadro(rgba, width, height)) {
        throw PlatformException(
          code: 'nucleo',
          message: 'O codificador do nucleo recusou o quadro.',
        );
      }
      return;
    }
    await _channel.invokeMethod<bool>('frameRgba', {
      'bytes': rgba,
      'width': width,
      'height': height,
    });
  }

  /// Codifica um LOTE de quadros numa chamada so. Atravessar a ponte por
  /// quadro custa mais que codificar em muitos aparelhos.
  static Future<int> frames(List<String> paths) async {
    final n = await _channel.invokeMethod<int>('frames', {'paths': paths});
    return n ?? 0;
  }

  /// ESPACO LIVRE no disco onde a exportacao vai escrever, em bytes.
  /// Zero quando o sistema nao responde — nesse caso nao se bloqueia
  /// nada: e melhor tentar do que impedir por falta de informacao.
  static Future<int> espacoLivre(String path) async {
    try {
      final v = await _channel.invokeMethod<int>('freeBytes', {'path': path});
      return v ?? 0;
    } catch (_) {
      return 0;
    }
  }

  static Future<bool> finish() async {
    if (_noNucleo) {
      _noNucleo = false;
      return CodificadorNativo.terminar();
    }
    return await _channel.invokeMethod<bool>('finish') ?? false;
  }

  static Future<void> cancel() async {
    if (_noNucleo) {
      _noNucleo = false;
      CodificadorNativo.cancelar();
      return;
    }
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

  // A CONTA DE TAXA DE BITS SAIU DAQUI.
  //
  // Havia duas, iguais menos por um detalhe: esta ignorava o codec, e a
  // de `ExportSettings.bitrateFor` desconta os 35% que o HEVC pede a
  // menos. Duas contas para a mesma pergunta e uma delas ser escolhida
  // por um `if` era o que fazia "alta" ser silenciosamente ignorado.
  // Agora so existe a dos ajustes.
}
