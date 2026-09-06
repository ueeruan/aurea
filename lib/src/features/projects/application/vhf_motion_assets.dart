import 'dart:io';

import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

import '../../editor/domain/video_project.dart';
import '../domain/vhf_motion_template.dart';

/// The native project needs only its audio. All visuals are editable vectors.
Future<VideoProject> prepareVhfMotion() async {
  final docs = await getApplicationDocumentsDirectory();
  final folder = Directory('${docs.path}/aurea_vhf_neon_v1');
  await folder.create(recursive: true);
  final data = await rootBundle.load('assets/templates/vhf/audio.m4a');
  final audio = File('${folder.path}/audio.m4a');
  if (!await audio.exists() || await audio.length() != data.lengthInBytes) {
    await audio.writeAsBytes(
      data.buffer.asUint8List(data.offsetInBytes, data.lengthInBytes),
      flush: true,
    );
  }
  return buildVhfMotionTemplate(audioPath: audio.path);
}
