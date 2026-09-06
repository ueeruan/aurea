import 'dart:async';
import 'dart:io';

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/ffprobe_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

/// PROXY DE MIDIA — a copia leve que o editor toca no lugar do arquivo
/// original.
///
/// O que trava o scrub num vídeo pesado nao e o tamanho do quadro: e o
/// GOP. Um arquivo de camera tem quadro-chave a cada 2 ou 5 segundos, e
/// buscar para o meio do GOP obriga o decodificador a voltar ao ultimo
/// quadro-chave e decodificar tudo ate la. Arrastar o playhead vira uma
/// sequencia de decodificacoes inteiras.
///
/// O proxy resolve pelo outro lado: resolucao menor E **quadro-chave a
/// cada 6 quadros**. Buscar passa a custar no maximo 6 quadros de
/// decodificacao, e o arrasto fica continuo.
///
/// O original nunca e alterado nem descartado — a exportacao continua
/// lendo dele.
class ProxyService {
  ProxyService._();
  static final instance = ProxyService._();

  /// Altura do proxy. 480 e o ponto em que o preview de celular ja nao
  /// distingue, e o arquivo cai para uma fracao.
  static const proxyHeight = 480;

  /// Quadros entre quadros-chave. Seis e curto o bastante para o scrub
  /// ficar continuo sem inchar demais o arquivo.
  static const gop = 6;

  final Map<String, String> _pronto = {};
  final Map<String, Future<void>> _emAndamento = {};

  /// Progresso por arquivo, 0..1. -1 = falhou.
  final Map<String, double> progress = {};

  final ValueNotifier<int> revision = ValueNotifier(0);

  /// Caminho do proxy de [source], ou null se ainda nao existe.
  String? proxyOf(String source) {
    final path = _pronto[source];
    if (path == null) return null;
    try {
      if (File(path).lengthSync() > 4096) return path;
    } catch (_) {}
    _pronto.remove(source);
    revision.value++;
    return null;
  }

  /// O que o player deve tocar: o proxy quando ha, o original quando
  /// nao ha. Nunca falha por falta de proxy.
  String playbackPath(String source) => proxyOf(source) ?? source;

  static String _key(String path) {
    var stamp = '';
    try {
      final s = File(path).statSync();
      stamp = '${s.size}_${s.modified.millisecondsSinceEpoch}';
    } catch (_) {}
    return '${path.hashCode.toRadixString(16)}_$stamp';
  }

  Future<Directory> _dir() async {
    final base = await getApplicationSupportDirectory();
    final d = Directory('${base.path}/proxies');
    if (!d.existsSync()) d.createSync(recursive: true);
    return d;
  }

  /// Gera o proxy se ainda nao houver. Seguro chamar varias vezes.
  ///
  /// [force] existe para operacoes que dependem semanticamente do proxy
  /// (reverso, por exemplo). O pre-cache comum continua podendo pular
  /// arquivos pequenos, onde uma copia leve nao traria beneficio.
  Future<void> ensureProxy(String source, {bool force = false}) async {
    if (proxyOf(source) != null) return;

    final running = _emAndamento[source];
    if (running != null) {
      await running;
      if (proxyOf(source) != null || !force) return;
      // Reentra pela verificacao do mapa: outro chamador que acordou do
      // mesmo Future pode ter iniciado a tentativa forcada primeiro.
      await ensureProxy(source, force: true);
      return;
    }

    late final Future<void> pending;
    pending = _build(source, force: force).whenComplete(() {
      if (identical(_emAndamento[source], pending)) {
        _emAndamento.remove(source);
      }
    });
    _emAndamento[source] = pending;
    await pending;
  }

  /// Garante uma copia apta a busca quadro a quadro e so devolve caminho
  /// depois do rename atomico. `null` significa falha real; o chamador
  /// nao deve habilitar reverso nesse caso.
  Future<String?> ensureReverseProxy(String source) async {
    await ensureProxy(source, force: true);
    final path = proxyOf(source);
    if (path == null) return null;
    try {
      return File(path).lengthSync() > 4096 ? path : null;
    } catch (_) {
      return null;
    }
  }

  Future<void> _build(String source, {bool force = false}) async {
    try {
      final dir = await _dir();
      final target = File('${dir.path}/${_key(source)}.mp4');

      if (target.existsSync() && target.lengthSync() > 4096) {
        _pronto[source] = target.path;
        progress[source] = 1;
        revision.value++;
        return;
      }
      if (target.existsSync()) target.deleteSync();

      // NAO VALE A PENA: se a fonte ja e pequena ou baixa, o proxy so
      // gasta disco e tempo — e chega a ficar MAIOR que o original,
      // porque GOP curto custa taxa. O proxy existe para arquivo
      // grande, que e onde o scrub trava.
      if (!force && !await _valeAPena(source)) {
        progress[source] = 1;
        revision.value++;
        return;
      }

      progress[source] = 0;
      revision.value++;

      final parcial = File('${target.path}.part');
      if (parcial.existsSync()) parcial.deleteSync();

      final session = await FFmpegKit.executeWithArguments([
        '-y',
        '-i', source,
        // Altura fixa, largura par derivada — o H.264 exige par.
        '-vf', 'scale=-2:$proxyHeight',
        // MPEG-4 parte 2, nao x264: o x264 e GPL e saiu do aplicativo
        // de proposito. Para um cache de scrub isso nao custa nada —
        // arquivo um pouco maior, decodificacao rapida, e o que importa
        // aqui e o GOP, nao a taxa.
        '-c:v', 'mpeg4',
        '-q:v', '6',
        // O que resolve o scrub: quadro-chave a cada 6 quadros, sem
        // deteccao de cena mexendo nisso.
        '-g', '$gop',
        '-keyint_min', '$gop',
        '-sc_threshold', '0',
        '-pix_fmt', 'yuv420p',
        '-c:a', 'aac',
        '-b:a', '128k',
        '-movflags', '+faststart',
        // O arquivo sai como ".mp4.part" para a escrita ser atomica, e
        // ai o FFmpeg nao consegue deduzir o conteiner pela extensao.
        // Declarar resolve — e e o que a propria mensagem de erro pede.
        '-f', 'mp4',
        parcial.path,
      ]);

      final rc = await session.getReturnCode();
      if (!ReturnCode.isSuccess(rc) || !parcial.existsSync()) {
        if (parcial.existsSync()) parcial.deleteSync();
        progress[source] = -1;
        revision.value++;
        return;
      }

      // Renomeia so no fim: proxy pela metade seria pior que nenhum.
      parcial.renameSync(target.path);
      _pronto[source] = target.path;
      progress[source] = 1;
      revision.value++;
    } catch (_) {
      progress[source] = -1;
      revision.value++;
    }
  }

  /// O proxy so compensa acima de certo tamanho e certa altura.
  ///
  /// Abaixo disso o arquivo inteiro ja cabe no cache do sistema e o
  /// scrub anda sozinho — gerar proxy ali e trabalho jogado fora.
  Future<bool> _valeAPena(String source) async {
    try {
      final tamanho = File(source).lengthSync();
      if (tamanho < 12 * 1024 * 1024) return false;
    } catch (_) {
      return false;
    }
    try {
      final info = await FFprobeKit.getMediaInformation(source);
      final streams = info.getMediaInformation()?.getStreams() ?? [];
      for (final st in streams) {
        if (st.getType() != 'video') continue;
        final h = st.getHeight();
        // Ja esta na altura do proxy: nao ha o que reduzir.
        if (h != null && h <= proxyHeight * 1.2) return false;
      }
    } catch (_) {
      // Sem informacao, segue em frente: o tamanho ja disse que e grande.
    }
    return true;
  }

  /// Tamanho total do cache de proxies, em bytes.
  Future<int> cacheSize() async {
    try {
      final dir = await _dir();
      var total = 0;
      for (final f in dir.listSync()) {
        if (f is File) total += f.lengthSync();
      }
      return total;
    } catch (_) {
      return 0;
    }
  }

  Future<void> clearCache() async {
    try {
      final dir = await _dir();
      if (dir.existsSync()) dir.deleteSync(recursive: true);
    } catch (_) {}
    _pronto.clear();
    progress.clear();
    revision.value++;
  }
}
