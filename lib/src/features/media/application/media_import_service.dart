import 'dart:io';
import 'dart:ui' as ui;

import 'package:file_picker/file_picker.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:image_picker/image_picker.dart';
import 'package:path_provider/path_provider.dart';
import 'package:uuid/uuid.dart';

/// Importacao de midia do dispositivo (galeria/camera/arquivos).
class MediaImportService {
  MediaImportService([
    ImagePicker? picker,
    Future<Directory> Function()? directory,
  ]) : _picker = picker ?? ImagePicker(),
       _directory = directory ?? getApplicationSupportDirectory;

  final ImagePicker _picker;
  final Future<Directory> Function() _directory;

  Future<XFile?> pickVideoFromGallery() async {
    final file = await _picker.pickVideo(source: ImageSource.gallery);
    return file == null ? null : persist(file);
  }

  Future<XFile?> pickImageFromGallery() async {
    final file = await _picker.pickImage(
      source: ImageSource.gallery,
      requestFullMetadata: false,
    );
    return file == null ? null : persist(file, image: true);
  }

  /// Copy out of picker/cloud caches before publishing a layer. A failed or
  /// cancelled import never leaves an unreadable layer in the project.
  Future<XFile> persist(XFile file, {bool image = false}) async {
    final root = await _directory();
    final folder = await Directory('${root.path}/imported_media')
        .create(recursive: true);
    final match = RegExp(r'\.[a-zA-Z0-9]{1,8}$').firstMatch(file.path);
    final target = File(
      '${folder.path}/${const Uuid().v4()}${match?.group(0) ?? ''}',
    );
    try {
      await file.saveTo(target.path);
      if (await target.length() == 0) {
        throw const FormatException('Arquivo vazio');
      }
      if (image) {
        final buffer = await ui.ImmutableBuffer.fromFilePath(target.path);
        ui.ImageDescriptor? descriptor;
        ui.Codec? codec;
        try {
          descriptor = await ui.ImageDescriptor.encoded(buffer);
          codec = await descriptor.instantiateCodec(
            targetWidth: 1,
            targetHeight: 1,
          );
          final frame = await codec.getNextFrame();
          frame.image.dispose();
        } finally {
          codec?.dispose();
          descriptor?.dispose();
          buffer.dispose();
        }
      }
      return XFile(target.path, name: file.name);
    } catch (_) {
      if (await target.exists()) await target.delete();
      rethrow;
    }
  }

  Future<XFile?> recordVideo() {
    return _picker.pickVideo(source: ImageSource.camera);
  }

  /// Audio via seletor de ARQUIVOS do sistema (galeria nao lista audio).
  Future<XFile?> pickAudioFile() async {
    final result = await FilePicker.platform.pickFiles(
      type: FileType.audio,
      allowMultiple: false,
    );
    final f = result?.files.single;
    if (f == null || f.path == null) return null;
    return XFile(f.path!, name: f.name);
  }
}

final mediaImportServiceProvider = Provider<MediaImportService>(
  (ref) => MediaImportService(),
);
