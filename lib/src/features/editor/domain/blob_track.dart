import 'dart:convert';
import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui';

import 'tracker2d.dart';

/// RASTREAR REGIOES — o visual de visao de maquina.
///
/// Nao existe no After Effects: e um visual do TouchDesigner que virou
/// plugin no DaVinci e ferramenta web, e chegou aos edits por ai. O que
/// esta aqui foi definido a partir da tecnica, nao copiado de lugar
/// nenhum.
///
/// O requisito que separa isto de um detector qualquer e a IDENTIDADE
/// ESTAVEL: o blob numero 3 tem que continuar sendo o 3 no quadro
/// seguinte. Sem isso as caixas piscam, os numeros dancam, e o efeito
/// vira ruido em vez de rastreio.

/// Uma regiao detectada, ja com identidade.
class Blob {
  const Blob({
    required this.id,
    required this.rect,
    required this.area,
    this.age = 0,
    this.missing = 0,
  });

  /// Identidade ESTAVEL entre quadros.
  final int id;

  /// Caixa em coordenadas do quadro analisado.
  final Rect rect;

  /// Quantos pixels a regiao ocupa (nao a area da caixa).
  final int area;

  /// Ha quantos quadros existe.
  final int age;

  /// Ha quantos quadros nao e detectado — o que a persistencia cobre.
  final int missing;

  Offset get center => rect.center;

  Blob copyWith({Rect? rect, int? area, int? age, int? missing}) => Blob(
        id: id,
        rect: rect ?? this.rect,
        area: area ?? this.area,
        age: age ?? this.age,
        missing: missing ?? this.missing,
      );
}

/// Como achar as regioes.
enum BlobDetectBy { motion, brightness, colorKey, edges }

/// Regioes conectadas de uma mascara binaria.
///
/// Rotulacao por varredura com pilha (flood fill iterativo): recursao
/// estoura a pilha numa regiao grande, e regiao grande e exatamente o
/// que se quer detectar.
List<Blob> labelRegions(
  Uint8List mask,
  int width,
  int height, {
  int minArea = 400,
  int maxArea = 0,
  int maxBlobs = 20,
}) {
  if (width <= 0 || height <= 0 || mask.length < width * height) {
    return const [];
  }
  final visto = Uint8List(width * height);
  final achados = <Blob>[];
  final pilha = <int>[];

  for (var i = 0; i < width * height; i++) {
    if (mask[i] == 0 || visto[i] != 0) continue;

    pilha
      ..clear()
      ..add(i);
    visto[i] = 1;
    var minX = width, minY = height, maxX = -1, maxY = -1, area = 0;

    while (pilha.isNotEmpty) {
      final p = pilha.removeLast();
      final x = p % width;
      final y = p ~/ width;
      area++;
      if (x < minX) minX = x;
      if (y < minY) minY = y;
      if (x > maxX) maxX = x;
      if (y > maxY) maxY = y;

      for (final (dx, dy) in const [(1, 0), (-1, 0), (0, 1), (0, -1)]) {
        final nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
        final np = ny * width + nx;
        if (mask[np] == 0 || visto[np] != 0) continue;
        visto[np] = 1;
        pilha.add(np);
      }
    }

    if (area < minArea) continue;
    if (maxArea > 0 && area > maxArea) continue;
    achados.add(Blob(
      id: -1,
      rect: Rect.fromLTRB(minX.toDouble(), minY.toDouble(),
          (maxX + 1).toDouble(), (maxY + 1).toDouble()),
      area: area,
    ));
  }

  // Os MAIORES primeiro: com limite de blobs, o que sobra tem de ser o
  // que importa, nao o que apareceu antes na varredura.
  achados.sort((a, b) => b.area.compareTo(a.area));
  return achados.take(maxBlobs.clamp(1, 200)).toList();
}

/// Funde blobs cujas caixas estao a menos de [distance] uma da outra.
///
/// Uma pessoa andando vira dois blobs (tronco e pernas) quando o
/// contraste falha no meio. Fundir o que esta perto devolve a pessoa
/// inteira, que e o que se queria rastrear.
List<Blob> mergeNearby(List<Blob> blobs, double distance) {
  if (distance <= 0 || blobs.length < 2) return blobs;
  final restantes = [...blobs];
  final saida = <Blob>[];

  while (restantes.isNotEmpty) {
    var atual = restantes.removeAt(0);
    var mudou = true;
    while (mudou) {
      mudou = false;
      for (var i = restantes.length - 1; i >= 0; i--) {
        final o = restantes[i];
        final inflada = atual.rect.inflate(distance);
        if (inflada.overlaps(o.rect)) {
          atual = Blob(
            id: -1,
            rect: atual.rect.expandToInclude(o.rect),
            area: atual.area + o.area,
          );
          restantes.removeAt(i);
          mudou = true;
        }
      }
    }
    saida.add(atual);
  }
  return saida;
}

/// A MASCARA de deteccao, conforme o modo.
///
/// [previous] so e usado em `motion` — e a diferenca entre quadros que
/// revela o que se mexeu.
Uint8List detectionMask(
  GrayFrame frame, {
  GrayFrame? previous,
  BlobDetectBy by = BlobDetectBy.motion,
  double threshold = 35,
  double sensitivity = 50,
}) {
  final n = frame.width * frame.height;
  final out = Uint8List(n);
  // A sensibilidade abaixa o corte: sensibilidade alta pega movimento
  // sutil, e tambem mais ruido.
  final corte =
      (threshold * (1 - sensitivity.clamp(0.0, 100.0) / 200)).clamp(1.0, 255.0);

  switch (by) {
    case BlobDetectBy.motion:
      if (previous == null ||
          previous.width != frame.width ||
          previous.height != frame.height) {
        return out;
      }
      for (var i = 0; i < n; i++) {
        final d = (frame.pixels[i] - previous.pixels[i]).abs();
        out[i] = d >= corte ? 1 : 0;
      }
    case BlobDetectBy.brightness:
      for (var i = 0; i < n; i++) {
        out[i] = frame.pixels[i] >= corte ? 1 : 0;
      }
    case BlobDetectBy.colorKey:
      // Em tons de cinza a chave de cor vira faixa de luminancia: o
      // quadro analisado ja perdeu a cor. Fica registrado que aqui
      // mora a diferenca em relacao a uma chave de verdade.
      for (var i = 0; i < n; i++) {
        final v = frame.pixels[i];
        out[i] = (v - corte).abs() <= 30 ? 1 : 0;
      }
    case BlobDetectBy.edges:
      // Sobel simplificado: onde o brilho muda depressa.
      for (var y = 1; y < frame.height - 1; y++) {
        for (var x = 1; x < frame.width - 1; x++) {
          final gx = frame.at(x + 1, y) - frame.at(x - 1, y);
          final gy = frame.at(x, y + 1) - frame.at(x, y - 1);
          final g = math.sqrt(gx * gx + gy * gy.toDouble());
          out[y * frame.width + x] = g >= corte ? 1 : 0;
        }
      }
  }
  return out;
}

/// CASAMENTO ENTRE QUADROS — a peca que da identidade estavel.
///
/// Cada blob novo procura o antigo mais proximo que ainda nao foi
/// tomado. O que nao acha vira id novo; o antigo que ninguem tomou
/// sobrevive [persistence] quadros antes de morrer — e o que cobre a
/// falha curta de deteccao sem a caixa piscar.
///
/// [smoothing] (0..1) segura a caixa: a deteccao pura treme de quadro em
/// quadro, e caixa tremendo e o defeito que mais denuncia rastreio
/// caseiro.
class BlobMatcher {
  BlobMatcher({this.persistence = 8, this.smoothing = 0.4, int firstId = 1})
      : _proximoId = firstId;

  final int persistence;
  final double smoothing;

  int _proximoId;
  List<Blob> _vivos = const [];

  List<Blob> get current => _vivos;

  /// Passa um quadro de deteccoes cruas e devolve os blobs COM
  /// identidade.
  List<Blob> update(List<Blob> detectados) {
    final antigos = [..._vivos];
    final tomados = <int>{};
    final saida = <Blob>[];
    final s = smoothing.clamp(0.0, 1.0);

    for (final novo in detectados) {
      var melhor = -1;
      var melhorD = double.infinity;
      for (var i = 0; i < antigos.length; i++) {
        if (tomados.contains(i)) continue;
        final d = (antigos[i].center - novo.center).distance;
        // Longe demais nao e o mesmo objeto: e outro.
        final limite = math.max(
            40.0, math.max(antigos[i].rect.longestSide, novo.rect.longestSide));
        if (d < melhorD && d <= limite) {
          melhorD = d;
          melhor = i;
        }
      }

      if (melhor < 0) {
        saida.add(Blob(
            id: _proximoId++, rect: novo.rect, area: novo.area, age: 1));
        continue;
      }

      tomados.add(melhor);
      final velho = antigos[melhor];
      // Suavizacao: a caixa anda em direcao a nova, nao salta para ela.
      final r = Rect.fromLTRB(
        velho.rect.left + (novo.rect.left - velho.rect.left) * (1 - s),
        velho.rect.top + (novo.rect.top - velho.rect.top) * (1 - s),
        velho.rect.right + (novo.rect.right - velho.rect.right) * (1 - s),
        velho.rect.bottom + (novo.rect.bottom - velho.rect.bottom) * (1 - s),
      );
      saida.add(Blob(
        id: velho.id,
        rect: r,
        area: novo.area,
        age: velho.age + 1,
      ));
    }

    // PERSISTENCIA: quem nao foi casado neste quadro nao some na hora.
    for (var i = 0; i < antigos.length; i++) {
      if (tomados.contains(i)) continue;
      final v = antigos[i];
      if (v.missing + 1 <= persistence) {
        saida.add(v.copyWith(missing: v.missing + 1, age: v.age + 1));
      }
    }

    _vivos = saida;
    return saida;
  }
}

/// O RESULTADO DA ANALISE: as caixas de cada quadro, ja gravadas.
///
/// Rastreio depende do quadro anterior, e isso brigaria com o seek
/// instantaneo: pular para o segundo 40 exigiria processar os 1200
/// quadros anteriores. Analisando uma vez e guardando, desenhar vira
/// consulta — deterministico e barato.
class BlobTrackData {
  const BlobTrackData({
    required this.fps,
    required this.width,
    required this.height,
    required this.frames,
  });

  /// Taxa em que a analise foi feita.
  final int fps;

  /// Tamanho do quadro ANALISADO (menor que o do video).
  final int width;
  final int height;

  /// Por quadro, as caixas com identidade.
  final List<List<Blob>> frames;

  bool get isEmpty => frames.isEmpty;

  /// As caixas no instante [t]. Fora do intervalo, vazio — e melhor nao
  /// desenhar nada do que desenhar a caixa errada.
  List<Blob> at(Duration t) {
    if (frames.isEmpty || fps <= 0) return const [];
    final i = (t.inMicroseconds / 1000000.0 * fps).floor();
    if (i < 0 || i >= frames.length) return const [];
    return frames[i];
  }

  Map<String, dynamic> toJson() => {
        'fps': fps,
        'w': width,
        'h': height,
        'f': [
          for (final quadro in frames)
            [
              for (final b in quadro)
                [
                  b.id,
                  b.rect.left.round(),
                  b.rect.top.round(),
                  b.rect.width.round(),
                  b.rect.height.round(),
                  b.area,
                ]
            ]
        ],
      };

  static BlobTrackData? decode(String source) {
    try {
      final m = (jsonDecode(source) as Map).cast<String, dynamic>();
      return BlobTrackData(
        fps: (m['fps'] as num).toInt(),
        width: (m['w'] as num).toInt(),
        height: (m['h'] as num).toInt(),
        frames: [
          for (final q in (m['f'] as List))
            [
              for (final b in (q as List))
                Blob(
                  id: ((b as List)[0] as num).toInt(),
                  rect: Rect.fromLTWH(
                    (b[1] as num).toDouble(),
                    (b[2] as num).toDouble(),
                    (b[3] as num).toDouble(),
                    (b[4] as num).toDouble(),
                  ),
                  area: (b[5] as num).toInt(),
                )
            ]
        ],
      );
    } catch (_) {
      return null;
    }
  }
}

/// Roda a analise inteira sobre uma sequencia ja em tons de cinza.
BlobTrackData analyzeBlobs(
  List<GrayFrame> frames, {
  required int fps,
  BlobDetectBy by = BlobDetectBy.motion,
  double threshold = 35,
  double sensitivity = 50,
  int minArea = 400,
  int maxArea = 0,
  int maxBlobs = 20,
  double mergeDistance = 20,
  int persistence = 8,
  double smoothing = 0.4,
}) {
  if (frames.isEmpty) {
    return const BlobTrackData(fps: 1, width: 0, height: 0, frames: []);
  }
  final matcher =
      BlobMatcher(persistence: persistence, smoothing: smoothing);
  final saida = <List<Blob>>[];

  for (var i = 0; i < frames.length; i++) {
    final mask = detectionMask(
      frames[i],
      previous: i == 0 ? null : frames[i - 1],
      by: by,
      threshold: threshold,
      sensitivity: sensitivity,
    );
    final crus = mergeNearby(
      labelRegions(mask, frames[i].width, frames[i].height,
          minArea: minArea, maxArea: maxArea, maxBlobs: maxBlobs),
      mergeDistance,
    );
    saida.add(matcher.update(crus));
  }

  return BlobTrackData(
    fps: fps,
    width: frames.first.width,
    height: frames.first.height,
    frames: saida,
  );
}
