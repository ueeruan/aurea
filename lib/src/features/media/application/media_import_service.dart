import 'dart:io';
import 'dart:ui' as ui;

import 'package:file_picker/file_picker.dart';
import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/ffprobe_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
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
      return _ImportedFile(target.path, file.name.split(RegExp(r'[\\/]')).last);
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
      withData: false,
    );
    final f = result?.files.single;
    if (f == null || f.path == null) return null;
    return persist(XFile(f.path!, name: f.name));
  }

  /// Metadata only: opening an AVPlayer/ExoPlayer here allocates an unnecessary
  /// decoder while the timeline is already preparing playback and waveforms.
  Future<Duration> audioDuration(String path) async {
    final session = await FFprobeKit.getMediaInformation(path);
    final info = session.getMediaInformation();
    if (info == null || !info.getStreams().any((s) => s.getType() == 'audio')) {
      throw const FormatException(
        'O arquivo nao tem uma faixa de audio legivel.',
      );
    }
    final seconds = double.tryParse(info.getDuration() ?? '');
    if (seconds == null || !seconds.isFinite || seconds <= 0) {
      throw const FormatException('Nao foi possivel ler a duracao do audio.');
    }
    return Duration(microseconds: (seconds * 1000000).round());
  }

  Future<XFile?> pickAudioFromVideo() async {
    final video = await _picker.pickVideo(source: ImageSource.gallery);
    if (video == null) return null;
    await audioDuration(video.path);
    final root = await _directory();
    final folder = await Directory('${root.path}/imported_media')
        .create(recursive: true);
    final output = File('${folder.path}/${const Uuid().v4()}.m4a');
    try {
      final session = await FFmpegKit.executeWithArguments([
        '-v',
        'error',
        '-y',
        '-i',
        video.path,
        '-map',
        '0:a:0',
        '-vn',
        '-sn',
        '-dn',
        '-c:a',
        'aac',
        '-b:a',
        '192k',
        output.path,
      ]);
      if (!ReturnCode.isSuccess(await session.getReturnCode()) ||
          !await output.exists() ||
          await output.length() == 0) {
        throw const FormatException(
          'Nao foi possivel extrair o audio desse video.',
        );
      }
      return _ImportedFile(output.path, '${video.name} (audio)');
    } catch (_) {
      if (await output.exists()) await output.delete();
      rethrow;
    }
  }
}

// XFile on native platforms derives name from path and ignores its constructor
// name argument. Keep the readable label alongside the durable UUID path.
class _ImportedFile extends XFile {
  _ImportedFile(super.path, this.name);
  @override
  final String name;
}

final mediaImportServiceProvider = Provider<MediaImportService>(
  (ref) => MediaImportService(),
);


/// O QUE JA FOI IMPORTADO PARA ESTE APARELHO.
///
/// A aba Midia do seletor mostra miniaturas, e o unico carretel que a
/// Aurea pode ler sem pedir permissao ao sistema e o proprio: a pasta
/// imported_media, para onde `persist` copia tudo que entra. E
/// conteudo real do app — nada de galeria inventada.
///
/// Mais recentes primeiro, com teto: uma grade de dez linhas de
/// miniaturas dentro de um seletor de 300 px nao seria olhada, e ler a
/// pasta inteira num aparelho com centenas de arquivos custaria o tempo
/// de abrir o menu.
/// A PROPORCAO DE UMA FOTO (largura / altura) sem decodificar a imagem.
///
/// O descritor do Flutter ja entrega a medida com a orientacao EXIF
/// aplicada (o gerador do Skia troca largura e altura quando a foto foi
/// tirada em pe), que e a mesma que o `Image.file` mostra. Nulo quando o
/// arquivo nao abre.
Future<double?> proporcaoDaFoto(String path) async {
  ui.ImmutableBuffer? buffer;
  ui.ImageDescriptor? descritor;
  try {
    buffer = await ui.ImmutableBuffer.fromFilePath(path);
    descritor = await ui.ImageDescriptor.encoded(buffer);
    final w = descritor.width, h = descritor.height;
    return w > 0 && h > 0 ? w / h : null;
  } catch (_) {
    return null;
  } finally {
    descritor?.dispose();
    buffer?.dispose();
  }
}

Future<List<File>> midiaRecente({int teto = 20}) async {
  try {
    final root = await getApplicationDocumentsDirectory();
    final pasta = Directory('${root.path}/imported_media');
    if (!pasta.existsSync()) return const [];
    final arquivos = pasta.listSync().whereType<File>().toList()
      ..sort(
        (a, b) => b.statSync().modified.compareTo(a.statSync().modified),
      );
    return arquivos.take(teto).toList();
  } on Object {
    // Pasta inacessivel nao e defeito: o seletor mostra o vazio util e
    // os dois caminhos de importacao continuam la.
    return const [];
  }
}
