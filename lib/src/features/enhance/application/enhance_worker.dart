import 'dart:async';
import 'dart:io';
import 'dart:isolate';
import 'dart:math' as math;
import 'dart:typed_data';

import 'package:image/image.dart' as img;

import '../domain/color_look.dart';
import 'native_enhancer.dart';

/// UM isolate e UM motor nativo por trabalho. O modelo carrega uma vez; a
/// inferencia (bloqueante) nunca roda na thread da interface.
class EnhanceWorker {
  EnhanceWorker._(this._isolate, this._port, this._messages);
  final Isolate _isolate;
  final SendPort _port;
  final StreamIterator<dynamic> _messages;

  static Future<EnhanceWorker> start() async {
    final receive = ReceivePort();
    final messages = StreamIterator<dynamic>(receive);
    final isolate = await Isolate.spawn(_workerMain, receive.sendPort);
    await messages.moveNext();
    return EnhanceWorker._(isolate, messages.current as SendPort, messages);
  }

  Future<dynamic> _ask(Object message) async {
    _port.send(message);
    if (!await _messages.moveNext()) throw StateError('Processamento interrompido');
    final reply = _messages.current;
    if (reply is String) throw StateError(reply);
    return reply;
  }

  /// Uma imagem (foto ou quadro de comparacao). [before], quando dado,
  /// recebe o original redimensionado convencionalmente ao MESMO tamanho da
  /// saida — antes/depois comparaveis.
  Future<(int, int)> frame(
    String input,
    String output,
    String modelDir,
    String cancelFile,
    EnhanceSettings settings, {
    bool video = false,
    String? before,
  }) async {
    final r = await _ask(('image', input, output, modelDir, cancelFile, settings, video, before));
    return r as (int, int);
  }

  /// VIDEO SEM PNG: le quadros RGB24 crus de [pipe] (o FFmpegKit escreve),
  /// processa um por vez e entrega RGBA a [onFrame]. So le o proximo quadro
  /// depois que [onFrame] termina — fila de um, memoria limitada.
  Future<int> video(
    String pipe,
    int width,
    int height,
    String modelDir,
    EnhanceSettings settings,
    int outW,
    int outH,
    Future<void> Function(Uint8List rgba, int w, int h) onFrame,
  ) async {
    _port.send(('video', pipe, width, height, modelDir, settings, outW, outH));
    var count = 0;
    while (await _messages.moveNext()) {
      final m = _messages.current;
      if (m is String) throw StateError(m);
      if (m is int) return m; // fim: quantidade de quadros
      final (TransferableTypedData data, int w, int h) = m as (TransferableTypedData, int, int);
      await onFrame(data.materialize().asUint8List(), w, h);
      count++;
      _port.send('ack');
    }
    throw StateError('Processamento interrompido depois de $count quadros');
  }

  void cancel() => _port.send('cancel');

  Future<void> close() async {
    _port.send(null);
    await _messages.moveNext();
    await _messages.cancel();
    _isolate.kill(priority: Isolate.immediate);
  }
}

Uint8List _rgbOf(img.Image image) {
  final out = Uint8List(image.width * image.height * 3);
  var i = 0;
  for (final p in image) {
    out[i++] = p.r.toInt();
    out[i++] = p.g.toInt();
    out[i++] = p.b.toInt();
  }
  return out;
}

img.Image _imageOf(Uint8List rgb, int w, int h) =>
    img.Image.fromBytes(width: w, height: h, bytes: rgb.buffer, numChannels: 3);

Uint8List _rgba(Uint8List rgb, int w, int h) {
  final out = Uint8List(w * h * 4);
  for (var s = 0, d = 0; s < rgb.length; s += 3, d += 4) {
    out[d] = rgb[s];
    out[d + 1] = rgb[s + 1];
    out[d + 2] = rgb[s + 2];
    out[d + 3] = 255;
  }
  return out;
}

void _workerMain(SendPort reply) async {
  final commands = ReceivePort();
  reply.send(commands.sendPort);
  final inbox = StreamIterator<dynamic>(commands);
  NativeEnhancer? engine;
  String? engineDir;

  NativeEnhancer motor(String dir) {
    if (engine != null && engineDir == dir) return engine!;
    engine?.close();
    engine = NativeEnhancer.open(dir);
    engineDir = dir;
    return engine!;
  }

  try {
    while (await inbox.moveNext()) {
      final message = inbox.current;
      if (message == null) break;
      if (message == 'cancel' || message == 'ack') continue;
      try {
        final kind = (message as Record);
        if (kind is (String, String, String, String, String, EnhanceSettings, bool, String?)) {
          final (_, input, output, modelDir, cancel, settings, video, before) = kind;
          if (File(cancel).existsSync()) throw StateError('Cancelado');
          final decoded = img.decodeImage(File(input).readAsBytesSync());
          if (decoded == null) throw StateError('Formato de imagem não suportado');
          var image = img.bakeOrientation(decoded);
          final alpha = image.hasAlpha ? image : null;
          if (settings.ai) {
            final m = motor(modelDir);
            final s = settings.scale.clamp(1, 4);
            final rgb = _rgbOf(image);
            final out = m.process(rgb, image.width, image.height, scale: s, strength: settings.aiStrength);
            if (before != null) {
              final b = m.resize(rgb, image.width, image.height, image.width * s, image.height * s);
              File(before).writeAsBytesSync(img.encodePng(_imageOf(b, image.width * s, image.height * s), level: 1));
            }
            final big = _imageOf(out, image.width * s, image.height * s);
            if (alpha != null) {
              // Alfa nao passa pela rede: amplia-se o canal original.
              final a = img.copyResize(alpha, width: big.width, height: big.height, interpolation: img.Interpolation.cubic);
              final withAlpha = img.Image(width: big.width, height: big.height, numChannels: 4);
              for (final p in withAlpha) {
                final c = big.getPixel(p.x, p.y);
                p
                  ..r = c.r
                  ..g = c.g
                  ..b = c.b
                  ..a = a.getPixel(p.x, p.y).a;
              }
              image = withAlpha;
            } else {
              image = big;
            }
          } else if (before != null) {
            File(before).writeAsBytesSync(img.encodePng(image, level: 1));
          }
          image = applyEnhancement(image, settings);
          if (video && (image.width.isOdd || image.height.isOdd)) {
            image = img.copyResize(image, width: math.max(2, image.width ~/ 2 * 2), height: math.max(2, image.height ~/ 2 * 2));
          }
          File(output).writeAsBytesSync(img.encodePng(image, level: 1));
          reply.send((image.width, image.height));
        } else if (kind is (String, String, int, int, String, EnhanceSettings, int, int)) {
          final (_, pipe, w, h, modelDir, settings, outW, outH) = kind;
          final m = settings.ai ? motor(modelDir) : null;
          final s = settings.ai ? settings.scale.clamp(1, 4) : 1;
          final frameBytes = w * h * 3;
          final buffer = BytesBuilder(copy: false);
          var count = 0;
          var cancelled = false;
          await for (final chunk in File(pipe).openRead()) {
            buffer.add(chunk);
            while (buffer.length >= frameBytes) {
              final all = buffer.takeBytes();
              final frame = Uint8List.sublistView(all, 0, frameBytes);
              if (all.length > frameBytes) buffer.add(Uint8List.sublistView(all, frameBytes));
              var rgb = m == null ? Uint8List.fromList(frame) : m.process(frame, w, h, scale: s, strength: settings.aiStrength);
              var fw = w * s, fh = h * s;
              if (settings.look != ColorLook.natural || settings.detail > 0) {
                final done = applyEnhancement(_imageOf(rgb, fw, fh), settings);
                rgb = _rgbOf(done);
              }
              if (fw != outW || fh != outH) {
                final r = img.copyResize(_imageOf(rgb, fw, fh), width: outW, height: outH, interpolation: img.Interpolation.average);
                rgb = _rgbOf(r);
                fw = outW;
                fh = outH;
              }
              reply.send((TransferableTypedData.fromList([_rgba(rgb, fw, fh)]), fw, fh));
              count++;
              // Espera o encoder aceitar antes de ler o proximo quadro.
              while (await inbox.moveNext()) {
                final c = inbox.current;
                if (c == 'ack') break;
                if (c == 'cancel') {
                  cancelled = true;
                  break;
                }
              }
              if (cancelled) break;
            }
            if (cancelled) break;
          }
          if (cancelled) throw StateError('Cancelado');
          reply.send(count);
        } else {
          throw StateError('Comando desconhecido');
        }
      } catch (e) {
        reply.send(e.toString().replaceFirst('Bad state: ', ''));
      }
    }
  } finally {
    engine?.close();
    commands.close();
    reply.send(true);
  }
}

img.Image applyEnhancement(img.Image source, EnhanceSettings settings) {
  // NITIDEZ (unsharp) e so nitidez: nao ha mais "reduzir ruido" somado ao
  // mesmo filtro. A reducao de ruido/artefatos vem da rede neural.
  final amount = settings.detail.clamp(0.0, 1.0) + settings.look.clarity * settings.strength;
  final blur = amount > 0 ? img.gaussianBlur(img.Image.from(source), radius: 1) : null;
  for (final pixel in source) {
    var r = pixel.r.toDouble(), g = pixel.g.toDouble(), b = pixel.b.toDouble();
    if (blur != null) {
      final p = blur.getPixel(pixel.x, pixel.y);
      r += (r - p.r) * amount;
      g += (g - p.g) * amount;
      b += (b - p.b) * amount;
    }
    final c = settings.look.apply(r, g, b, settings.strength);
    final nx = (pixel.x + .5) / source.width * 2 - 1, ny = (pixel.y + .5) / source.height * 2 - 1;
    final edge = ((nx * nx + ny * ny) / 2).clamp(0.0, 1.0);
    final shade = 1 - settings.look.vignette * settings.strength * edge * edge;
    pixel
      ..r = c.$1 * shade
      ..g = c.$2 * shade
      ..b = c.$3 * shade;
  }
  return source;
}
