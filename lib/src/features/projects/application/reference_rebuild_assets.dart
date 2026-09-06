import 'dart:io';

import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

import '../../editor/domain/video_project.dart';
import '../domain/reference_rebuild_template.dart';

/// Assets bundled with the app use durable device-local paths for export.
Future<VideoProject> prepareReferenceRebuild() async {
  final directory = await getApplicationDocumentsDirectory();
  final folder = Directory('${directory.path}/aurea_reference_rebuild');
  await folder.create(recursive: true);
  final audio = File('${folder.path}/reference-audio-v1.m4a');
  final data = await rootBundle.load(
    'assets/templates/reference-rebuild-audio.m4a',
  );
  if (!await audio.exists() || await audio.length() != data.lengthInBytes) {
    await audio.writeAsBytes(
      data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes),
      flush: true,
    );
  }
  return buildReferenceRebuildTemplate(audioPath: audio.path);
}
