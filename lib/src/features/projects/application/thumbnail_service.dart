import 'dart:io';
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';
import 'package:path_provider/path_provider.dart';

/// MINIATURAS DOS PROJETOS: um quadro do palco, capturado ao sair do
/// editor, para a tela inicial mostrar o projeto de verdade — nao um
/// icone generico igual para todos.
///
/// A captura e assincrona e nunca derruba a navegacao: se falhar, a
/// tela inicial mostra o cartao sem imagem, como antes.
class ThumbnailService {
  ThumbnailService._();

  static final ThumbnailService instance = ThumbnailService._();

  /// Sobe a cada miniatura nova ou apagada; a tela inicial escuta.
  final ValueNotifier<int> revision = ValueNotifier<int>(0);

  Directory? _dir;
  bool _iniciando = false;

  Future<Directory> _pasta() async {
    if (_dir != null) return _dir!;
    final docs = await getApplicationDocumentsDirectory();
    final d = Directory('${docs.path}/thumbs');
    if (!d.existsSync()) d.createSync(recursive: true);
    return _dir = d;
  }

  /// Descobre a pasta cedo, para [fileFor] responder de forma sincrona.
  Future<void> init() async {
    if (_dir != null || _iniciando) return;
    _iniciando = true;
    try {
      await _pasta();
      revision.value++;
    } catch (e) {
      debugPrint('miniaturas: $e');
    } finally {
      _iniciando = false;
    }
  }

  static String _safe(String id) {
    final c = id.replaceAll(RegExp(r'[^A-Za-z0-9_-]'), '');
    return c.isEmpty ? 'projeto' : c;
  }

  /// O arquivo da miniatura, se existe (e a pasta ja e conhecida).
  File? fileFor(String id) {
    final d = _dir;
    if (d == null) return null;
    final f = File('${d.path}/${_safe(id)}.png');
    return f.existsSync() ? f : null;
  }

  /// Captura o RepaintBoundary de [key] e guarda como miniatura de
  /// [projectId]. Chamado no toque de voltar do editor, com a arvore
  /// ainda viva.
  Future<void> capture(GlobalKey key, String projectId) async {
    final obj = key.currentContext?.findRenderObject();
    if (obj is! RenderRepaintBoundary) return;
    if (obj.debugNeedsPaint) return;
    try {
      final size = obj.size;
      if (size.isEmpty) return;
      final ratio = (640 / size.longestSide).clamp(0.1, 2.0);
      final img = await obj.toImage(pixelRatio: ratio);
      final data = await img.toByteData(format: ui.ImageByteFormat.png);
      img.dispose();
      if (data == null) return;
      final d = await _pasta();
      final f = File('${d.path}/${_safe(projectId)}.png');
      await f.writeAsBytes(data.buffer.asUint8List(), flush: true);
      await FileImage(f).evict();
      revision.value++;
    } catch (e) {
      debugPrint('miniatura de $projectId: $e');
    }
  }

  Future<void> delete(String id) async {
    final f = fileFor(id);
    if (f == null) return;
    try {
      f.deleteSync();
    } catch (_) {}
    revision.value++;
  }
}
