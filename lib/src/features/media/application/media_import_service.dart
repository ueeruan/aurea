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

import 'seletor_do_sistema.dart';

/// A PASTA ONDE A MIDIA IMPORTADA MORA — UMA SO, PARA ESCREVER E PARA LER.
///
/// Eram DUAS: a importacao gravava em `getApplicationSupportDirectory` e a
/// aba Midia lia de `getApplicationDocumentsDirectory`. Nas duas existe
/// uma pasta com o mesmo nome, e sao pastas diferentes — o que entrava
/// nunca aparecia na galeria, e o defeito nao dava erro nenhum.
const subpastaDaMidia = 'imported_media';

/// E VIDEO? Pela extensao do nome — a mesma lista que a Inicio usa para
/// decidir entre camada de video e de imagem.
bool pareceVideo(String nome) {
  final ponto = nome.lastIndexOf('.');
  if (ponto < 0) return false;
  return const {
    'mp4',
    'mov',
    'm4v',
    'avi',
    'mkv',
    'webm',
    '3gp',
  }.contains(nome.substring(ponto + 1).toLowerCase());
}

/// Importacao de midia do dispositivo (galeria/camera/arquivos).
class MediaImportService {
  MediaImportService([
    ImagePicker? picker,
    Future<Directory> Function()? directory,
    SeletorDoSistema? seletor,
  ]) : _picker = picker ?? ImagePicker(),
       _directory = directory ?? getApplicationSupportDirectory,
       _seletor = seletor ?? const SeletorDoSistema();

  final ImagePicker _picker;
  final Future<Directory> Function() _directory;
  final SeletorDoSistema _seletor;

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
    final folder = await Directory('${root.path}/$subpastaDaMidia')
        .create(recursive: true);
    // A EXTENSAO VEM DOS BYTES, e nao do caminho.
    //
    // O caminho do seletor nem sempre tem extensao — o Android devolve um
    // arquivo em cache, e alguns provedores o gravam sem sufixo. O
    // `Image.file` nao liga para isso (ele fareja o formato), mas duas
    // coisas ligam: o nome que a pessoa le na camada, e o motivo pelo qual
    // um arquivo sem sufixo vira um "arquivo estranho" no projeto. Ler os
    // primeiros bytes resolve os dois, e o `\.[a-zA-Z0-9]{1,8}$` do
    // caminho continua sendo o primeiro palpite.
    final doCaminho = RegExp(r'\.[a-zA-Z0-9]{1,8}$').firstMatch(file.path);
    var extensao = doCaminho?.group(0) ?? '';
    final target = File('${folder.path}/${const Uuid().v4()}$extensao');
    try {
      await file.saveTo(target.path);
      if (await target.length() == 0) {
        throw const FormatException('Arquivo vazio');
      }
      if (extensao.isEmpty && image) {
        final palpite = await _extensaoPelosBytes(target);
        if (palpite != null) {
          final renomeado = File('${folder.path}/${const Uuid().v4()}$palpite');
          await target.rename(renomeado.path);
          return _ImportedFile(
            renomeado.path,
            file.name.split(RegExp(r'[\\/]')).last,
          );
        }
      }
      if (image) {
        // DECODIFICAR DE VERDADE, e nao pedir 1x1.
        //
        // O `instantiateCodec(targetWidth: 1, targetHeight: 1)` era uma
        // PISTA de reducao, e nao uma prova de que o arquivo abre: em
        // varios PNGs (entrelacado, 16 bits, paleta) ela nao devolve
        // quadro nenhum e levanta — e a importacao inteira morria por
        // causa de um arquivo que o palco desenharia sem reclamar. A
        // prova honesta e decodificar o primeiro quadro.
        await _abreComoImagem(target.path);
      }
      return _ImportedFile(target.path, file.name.split(RegExp(r'[\\/]')).last);
    } catch (_) {
      if (await target.exists()) await target.delete();
      rethrow;
    }
  }

  /// O PRIMEIRO QUADRO DA IMAGEM, decodificado — ou uma excecao.
  ///
  /// E a prova de que o arquivo abre. Vale o custo de uma decodificacao
  /// por importacao: e ela que impede um arquivo quebrado de virar camada.
  static Future<void> _abreComoImagem(String caminho) async {
    ui.ImmutableBuffer? buffer;
    ui.ImageDescriptor? descritor;
    ui.Codec? codec;
    try {
      buffer = await ui.ImmutableBuffer.fromFilePath(caminho);
      descritor = await ui.ImageDescriptor.encoded(buffer);
      if (descritor.width <= 0 || descritor.height <= 0) {
        throw const FormatException('Imagem sem dimensao');
      }
      codec = await descritor.instantiateCodec();
      final quadro = await codec.getNextFrame();
      quadro.image.dispose();
    } finally {
      codec?.dispose();
      descritor?.dispose();
      buffer?.dispose();
    }
  }

  /// A EXTENSAO PELOS PRIMEIROS BYTES (o "numero magico" de cada formato).
  ///
  /// Nao e adivinhacao: sao os quatro ou doze primeiros bytes, que cada
  /// formato define. Nulo quando nao e nenhum dos que o app importa.
  static Future<String?> _extensaoPelosBytes(File arquivo) async {
    try {
      final raf = await arquivo.open();
      final cabeca = await raf.read(12);
      await raf.close();
      if (cabeca.length >= 8 &&
          cabeca[0] == 0x89 &&
          cabeca[1] == 0x50 &&
          cabeca[2] == 0x4E &&
          cabeca[3] == 0x47) {
        return '.png';
      }
      if (cabeca.length >= 3 &&
          cabeca[0] == 0xFF &&
          cabeca[1] == 0xD8 &&
          cabeca[2] == 0xFF) {
        return '.jpg';
      }
      if (cabeca.length >= 12 &&
          cabeca[8] == 0x57 &&
          cabeca[9] == 0x45 &&
          cabeca[10] == 0x42 &&
          cabeca[11] == 0x50) {
        return '.webp';
      }
      if (cabeca.length >= 6 &&
          cabeca[0] == 0x47 &&
          cabeca[1] == 0x49 &&
          cabeca[2] == 0x46) {
        return '.gif';
      }
      return null;
    } catch (_) {
      return null;
    }
  }

  Future<XFile?> recordVideo() {
    return _picker.pickVideo(source: ImageSource.camera);
  }

  /// Audio via seletor de ARQUIVOS do sistema (galeria nao lista audio).
  ///
  /// No Android o seletor reabre na pasta da ultima trilha escolhida (canal
  /// `aurea/seletor`); em qualquer erro, e no iOS, e o file_picker de antes.
  Future<XFile?> pickAudioFile() async {
    final f = await _seletor.escolher(
      TipoDeSeletor.audio,
      reserva: SeletorDoSistema.peloFilePicker(FileType.audio),
    );
    if (f == null) return null;
    return persist(XFile(f.caminho, name: f.nome));
  }

  /// IMAGEM pelo navegador de documentos: devolve o CAMINHO DURAVEL da
  /// copia dentro do app (a textura do solido 3D so precisa disso), ou nulo
  /// se a pessoa cancelou. No Android reabre na pasta da ultima imagem
  /// escolhida por aqui; levanta se o arquivo nao abre como imagem.
  Future<String?> pickImageFile() async {
    final f = await _seletor.escolher(
      TipoDeSeletor.imagem,
      reserva: SeletorDoSistema.peloFilePicker(FileType.image),
    );
    if (f == null) return null;
    return (await persist(XFile(f.caminho, name: f.nome), image: true)).path;
  }

  /// FOTO OU VIDEO pelo navegador de documentos, ja copiado para o app.
  ///
  /// E a porta do "Importar midia" da Inicio: la o caminho do seletor ia
  /// direto para a camada, e o seletor devolve um arquivo em CACHE — o
  /// Android limpa o cache quando quer e o projeto abria sem a midia.
  /// No Android reabre na pasta da ultima midia escolhida por aqui.
  Future<XFile?> pickMediaFile() async {
    final f = await _seletor.escolher(
      TipoDeSeletor.midia,
      reserva: SeletorDoSistema.peloFilePicker(FileType.media),
    );
    if (f == null) return null;
    return persist(
      XFile(f.caminho, name: f.nome),
      image: !pareceVideo(f.nome) && !pareceVideo(f.caminho),
    );
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
    // Pela constante, e nao pelo literal: era assim que as DUAS pastas
    // nasceram, e o audio extraido nao pode escapar da que o app le.
    final folder = await Directory('${root.path}/$subpastaDaMidia')
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

/// UM ARQUIVO QUE JA MORA EM `imported_media` (um recente reusado), com o
/// nome legivel de quando entrou. Nao copia nada: e so o embrulho que os
/// chamadores de importacao esperam receber.
XFile arquivoJaImportado(String caminho, String nome) =>
    _ImportedFile(caminho, nome);

final mediaImportServiceProvider = Provider<MediaImportService>(
  (ref) => MediaImportService(),
);


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
