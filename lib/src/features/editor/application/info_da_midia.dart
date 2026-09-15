import 'dart:io';

import 'package:ffmpeg_kit_flutter_new_full/ffmpeg_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/ffprobe_kit.dart';
import 'package:ffmpeg_kit_flutter_new_full/return_code.dart';
import 'package:path_provider/path_provider.dart';
import 'package:uuid/uuid.dart';

/// O QUE SE SABE DE UM ARQUIVO DE MIDIA (menu da camada › Informacoes).
///
/// Cada campo e opcional: um arquivo que o ffprobe nao le ainda mostra
/// nome, formato e tamanho — e o que falta aparece como "—", nunca como
/// um erro que esconde o resto.
class InfoDaMidia {
  const InfoDaMidia({
    required this.nome,
    this.formato,
    this.bytes,
    this.largura,
    this.altura,
    this.quadrosPorSegundo,
    this.duracao,
    this.taxaDeAmostragem,
  });

  final String nome;
  final String? formato;
  final int? bytes;
  final int? largura;
  final int? altura;
  final double? quadrosPorSegundo;
  final Duration? duracao;
  final int? taxaDeAmostragem;

  /// As linhas da ficha, na ordem em que aparecem.
  List<(String, String)> get linhas => [
    ('Nome', nome),
    (
      'Dimensões',
      largura != null && altura != null ? '$largura × $altura' : '—',
    ),
    (
      'Quadros por segundo',
      quadrosPorSegundo == null ? '—' : _fps(quadrosPorSegundo!),
    ),
    ('Duração', duracao == null ? '—' : _duracao(duracao!)),
    ('Formato', formato ?? '—'),
    ('Tamanho', bytes == null ? '—' : tamanhoLegivel(bytes!)),
    (
      'Taxa de amostragem',
      taxaDeAmostragem == null ? '—' : '$taxaDeAmostragem Hz',
    ),
  ];
}

String _fps(double v) {
  final inteiro = v.roundToDouble();
  return (v - inteiro).abs() < 0.005
      ? '${inteiro.toInt()} fps'
      : '${v.toStringAsFixed(2)} fps';
}

String _duracao(Duration d) {
  final s = d.inMilliseconds / 1000;
  final m = d.inMinutes;
  if (m == 0) return '${s.toStringAsFixed(2)} s';
  final resto = (s - m * 60).toStringAsFixed(2).padLeft(5, '0');
  return '$m:$resto';
}

/// "12,4 MB", "830 KB", "3 B".
String tamanhoLegivel(int bytes) {
  const unidades = ['B', 'KB', 'MB', 'GB'];
  var v = bytes.toDouble();
  var i = 0;
  while (v >= 1024 && i < unidades.length - 1) {
    v /= 1024;
    i++;
  }
  final texto = i == 0 || v >= 100
      ? v.toStringAsFixed(0)
      : v.toStringAsFixed(1);
  return '${texto.replaceAll('.', ',')} ${unidades[i]}';
}

/// "30000/1001" -> 29,97; "25" -> 25; lixo -> nulo.
double? quadrosDaFracao(String? texto) {
  if (texto == null || texto.trim().isEmpty) return null;
  final partes = texto.split('/');
  final a = double.tryParse(partes.first.trim());
  if (a == null) return null;
  if (partes.length == 1) return a > 0 ? a : null;
  final b = double.tryParse(partes[1].trim());
  if (b == null || b == 0) return null;
  final v = a / b;
  return v.isFinite && v > 0 && v < 1000 ? v : null;
}

/// Le a ficha do arquivo. Sem plugin (testes) ou sem permissao, devolve
/// o que o sistema de arquivos sabe.
Future<InfoDaMidia> lerInfoDaMidia(String caminho, {String? nome}) async {
  final arquivo = File(caminho);
  final nomeDoArquivo = nome ?? caminho.split(RegExp(r'[\\/]')).last;
  final ponto = nomeDoArquivo.lastIndexOf('.');
  final extensao = ponto > 0
      ? nomeDoArquivo.substring(ponto + 1).toUpperCase()
      : null;
  int? bytes;
  try {
    if (arquivo.existsSync()) bytes = arquivo.lengthSync();
  } catch (_) {}

  int? largura;
  int? altura;
  double? fps;
  Duration? duracao;
  int? amostragem;
  String? formato;
  try {
    final sessao = await FFprobeKit.getMediaInformation(caminho);
    final info = sessao.getMediaInformation();
    if (info != null) {
      final segundos = double.tryParse(info.getDuration() ?? '');
      if (segundos != null && segundos.isFinite && segundos > 0) {
        duracao = Duration(microseconds: (segundos * 1e6).round());
      }
      formato = info.getFormat();
      for (final s in info.getStreams()) {
        if (s.getType() == 'video' && largura == null) {
          largura = s.getWidth();
          altura = s.getHeight();
          fps = quadrosDaFracao(s.getAverageFrameRate()) ??
              quadrosDaFracao(s.getRealFrameRate());
        }
        if (s.getType() == 'audio' && amostragem == null) {
          amostragem = int.tryParse(s.getSampleRate() ?? '');
        }
      }
    }
  } catch (_) {
    // Sem ffprobe (teste, arquivo sumido): fica o que o disco sabe.
  }

  return InfoDaMidia(
    nome: nomeDoArquivo,
    formato: extensao ?? formato,
    bytes: bytes,
    largura: largura,
    altura: altura,
    quadrosPorSegundo: fps,
    duracao: duracao,
    taxaDeAmostragem: amostragem,
  );
}

/// EXTRAIR O AUDIO de um arquivo de video para um .m4a na pasta de
/// midias importadas. Nulo quando o video nao tem som ou a conversao
/// falha — quem chamou avisa a pessoa.
Future<String?> extrairAudioDoArquivo(String caminhoDoVideo) async {
  try {
    final raiz = await getApplicationDocumentsDirectory();
    final pasta = await Directory(
      '${raiz.path}/imported_media',
    ).create(recursive: true);
    final saida = File('${pasta.path}/${const Uuid().v4()}.m4a');
    final sessao = await FFmpegKit.executeWithArguments([
      '-v',
      'error',
      '-y',
      '-i',
      caminhoDoVideo,
      '-map',
      '0:a:0',
      '-vn',
      '-sn',
      '-dn',
      '-c:a',
      'aac',
      '-b:a',
      '192k',
      saida.path,
    ]);
    final ok =
        ReturnCode.isSuccess(await sessao.getReturnCode()) &&
        await saida.exists() &&
        await saida.length() > 0;
    if (!ok) {
      if (await saida.exists()) await saida.delete();
      return null;
    }
    return saida.path;
  } catch (_) {
    return null;
  }
}
