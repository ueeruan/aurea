import 'dart:io';

import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

import '../../editor/domain/video_project.dart';
import '../domain/dnyx_remix_template.dart';

/// Durable copies allow both preview and the native iOS encoder to read media.
Future<VideoProject> prepareDnyxRemix() async {
  final docs = await getApplicationDocumentsDirectory();
  final folder = Directory('${docs.path}/aurea_dnyx_rmk_v1');
  await folder.create(recursive: true);
  final paths = <String, String>{};
  for (final name in dnyxAssetNames) {
    final bytes = await rootBundle.load('assets/templates/dnyx/$name');
    final file = File('${folder.path}/$name');
    if (!await file.exists() || await file.length() != bytes.lengthInBytes) {
      await file.writeAsBytes(
        bytes.buffer.asUint8List(bytes.offsetInBytes, bytes.lengthInBytes),
        flush: true,
      );
    }
    paths[name] = file.path;
  }
  return buildDnyxRemixTemplate(paths);
}
