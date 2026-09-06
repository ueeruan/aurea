import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../domain/blob_track.dart';
import 'tracking_service.dart';

/// A ANALISE DE BLOBS, feita UMA VEZ e guardada.
///
/// Rastreio depende do quadro anterior, e isso briga de frente com o
/// seek instantaneo: pular para o segundo 40 exigiria processar os 1200
/// quadros anteriores, toda vez.
///
/// Analisando sob comando e gravando as caixas por quadro, desenhar vira
/// consulta — deterministico, barato, e o mesmo quadro sai igual seja
/// renderizado direto ou depois de reproduzir do zero.
class BlobTrackService {
  BlobTrackService._();
  static final instance = BlobTrackService._();

  final Map<String, BlobTrackData> _cache = {};
  final Set<String> _emAndamento = {};

  /// Avisa a interface quando uma analise termina.
  final ValueNotifier<int> revision = ValueNotifier(0);

  /// Progresso 0..1 da analise em curso.
  final ValueNotifier<double> progress = ValueNotifier(0);

  BlobTrackData? dataFor(String effectId) => _cache[effectId];

  bool isRunning(String effectId) => _emAndamento.contains(effectId);

  Future<Directory> _pasta() async {
    final base = await getApplicationSupportDirectory();
    final d = Directory('${base.path}/blobs');
    if (!d.existsSync()) d.createSync(recursive: true);
    return d;
  }

  /// Le do disco o que ja foi analisado antes.
  Future<void> load(String effectId) async {
    if (_cache.containsKey(effectId)) return;
    try {
      final dir = await _pasta();
      final f = File('${dir.path}/$effectId.json');
      if (!f.existsSync()) return;
      final d = BlobTrackData.decode(await f.readAsString());
      if (d != null) {
        _cache[effectId] = d;
        revision.value++;
      }
    } catch (_) {
      // Analise antiga estragada nao pode impedir de fazer outra.
    }
  }

  /// Roda a analise e guarda o resultado.
  ///
  /// Devolve quantos quadros foram analisados, ou null se nao deu para
  /// ler o video.
  Future<int?> analyze({
    required String effectId,
    required String sourcePath,
    required Duration start,
    required Duration duration,
    BlobDetectBy by = BlobDetectBy.motion,
    double threshold = 35,
    double sensitivity = 50,
    double minBlobSize = 400,
    double maxBlobSize = 0,
    int maxBlobs = 20,
    double mergeDistance = 20,
    int persistence = 8,
    double smoothing = 0.4,
    int fps = 12,
  }) async {
    if (_emAndamento.contains(effectId)) return null;
    _emAndamento.add(effectId);
    progress.value = 0;
    try {
      final frames = await TrackingService.instance.grayFrames(
        sourcePath,
        start: start,
        duration: duration,
        fps: fps,
      );
      if (frames.isEmpty) return null;

      // A analise roda no tamanho REDUZIDO (240 px de largura). As
      // caixas saem nessa escala e sao convertidas na hora de desenhar —
      // guardar em pixel de tela quebraria ao trocar a resolucao do
      // projeto.
      final dados = analyzeBlobs(
        frames,
        fps: fps,
        by: by,
        threshold: threshold,
        sensitivity: sensitivity,
        minArea: minBlobSize.round(),
        maxArea: maxBlobSize.round(),
        maxBlobs: maxBlobs,
        mergeDistance: mergeDistance,
        persistence: persistence,
        smoothing: smoothing,
      );

      _cache[effectId] = dados;
      final dir = await _pasta();
      await File('${dir.path}/$effectId.json')
          .writeAsString(jsonEncode(dados.toJson()), flush: true);
      revision.value++;
      return dados.frames.length;
    } catch (_) {
      return null;
    } finally {
      _emAndamento.remove(effectId);
      progress.value = 1;
    }
  }

  /// Joga fora a analise — para refazer com outros ajustes.
  Future<void> clear(String effectId) async {
    _cache.remove(effectId);
    try {
      final dir = await _pasta();
      final f = File('${dir.path}/$effectId.json');
      if (f.existsSync()) f.deleteSync();
    } catch (_) {}
    revision.value++;
  }
}
