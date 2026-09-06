import 'dart:convert';
import 'dart:io';
import 'dart:isolate';

import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

import '../../editor/application/model_import_service.dart';
import '../../editor/domain/video_project.dart';
import '../domain/campo_arvore_template.dart';

/// Loads only the tree and sky; all images become embedded in the project.
Future<VideoProject> prepareCampoArvore() async {
  final sky = await rootBundle.load('assets/templates/campo-sky.png');
  final skyUri =
      'data:image/png;base64,${base64Encode(sky.buffer.asUint8List())}';
  final docs = await getApplicationDocumentsDirectory();
  final dir = Directory('${docs.path}/modelos/campo');
  await dir.create(recursive: true);
  const names = ['arvore.obj', 'arvore.mtl', 'arvore.jpg'];
  for (final name in names) {
    final file = File('${dir.path}/$name');
    if (!await file.exists()) {
      final data = await rootBundle.load('assets/models/monolito/$name');
      await file.writeAsBytes(data.buffer.asUint8List(), flush: true);
    }
  }
  final tree = await readModel3DFiles([
    for (final name in names) '${dir.path}/$name',
  ]);
  return Isolate.run(
    () => buildCampoArvoreTemplate(skyTexture: skyUri, scannedTree: tree),
  );
}
