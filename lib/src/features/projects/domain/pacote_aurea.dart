import 'dart:convert';
import 'dart:io';
import 'dart:typed_data';

import 'package:archive/archive.dart';

import '../../editor/domain/project_store.dart';
import '../../editor/domain/video_project.dart';

/// O PACOTE .aurea: o projeto e as midias dele num arquivo so.
///
/// O "exportar projeto" de antes gravava so o JSON — que abre no mesmo
/// aparelho e vira um monte de camada cinza em qualquer outro, porque os
/// caminhos das midias apontam para um celular que nao e o seu. O pacote
/// leva os arquivos junto: e o projeto que se manda por mensagem, troca
/// de aparelho e guarda de backup.
///
/// Dentro do zip: `projeto.json` (com os caminhos reescritos para
/// `midia/<n>-<nome>`) e a pasta `midia/`. Na volta, as midias saem para
/// uma pasta do app e os caminhos viram absolutos de novo.
class PacoteAurea {
  const PacoteAurea._();

  static const extensao = 'aurea';

  /// TODA STRING DO PROJETO QUE E UM ARQUIVO EXISTENTE vira midia do
  /// pacote. Andar pelo JSON pronto (em vez de conhecer camada por
  /// camada) e o que faz video, foto, audio e preenchimento por midia
  /// entrarem pelo mesmo cano — inclusive os que ainda nao existiam
  /// quando este arquivo foi escrito.
  static List<String> midiasDoProjeto(VideoProject projeto) {
    final vistos = <String>{};
    void anda(Object? no) {
      if (no is Map) {
        no.values.forEach(anda);
      } else if (no is List) {
        no.forEach(anda);
      } else if (no is String && _pareceArquivo(no)) {
        if (File(no).existsSync()) vistos.add(no);
      }
    }

    anda(projectToJson(projeto));
    final lista = vistos.toList()..sort();
    return lista;
  }

  static bool _pareceArquivo(String v) {
    if (v.length < 5 || v.length > 1024) return false;
    if (!v.contains('/') && !v.contains('\\')) return false;
    final ponto = v.lastIndexOf('.');
    if (ponto < 0 || v.length - ponto > 6) return false;
    return true;
  }

  /// MONTA o pacote. As midias entram SEM recomprimir (video ja e
  /// comprimido; deflate em cima so gasta bateria), o JSON entra
  /// comprimido.
  static Uint8List montar(VideoProject projeto) {
    final midias = midiasDoProjeto(projeto);
    final porCaminho = <String, String>{};
    final usados = <String>{};
    var n = 0;
    for (final caminho in midias) {
      var nome = caminho.split(RegExp(r'[\\/]+')).last;
      nome = nome.replaceAll(RegExp(r'[^A-Za-z0-9._-]'), '_');
      var entrada = 'midia/${n.toString().padLeft(2, '0')}-$nome';
      while (!usados.add(entrada)) {
        n++;
        entrada = 'midia/${n.toString().padLeft(2, '0')}-$nome';
      }
      n++;
      porCaminho[caminho] = entrada;
    }

    Object? reescreve(Object? no) => switch (no) {
      Map() => <String, dynamic>{
        for (final e in no.entries) e.key as String: reescreve(e.value),
      },
      List() => [for (final v in no) reescreve(v)],
      String() => porCaminho[no] ?? no,
      _ => no,
    };

    final zip = Archive();
    final json = utf8.encode(
      jsonEncode({
        'formato': 'aurea-pacote',
        'versao': 1,
        'projeto': reescreve(projectToJson(projeto)),
      }),
    );
    zip.addFile(ArchiveFile('projeto.json', json.length, json));
    for (final e in porCaminho.entries) {
      final bytes = File(e.key).readAsBytesSync();
      zip.addFile(ArchiveFile.noCompress(e.value, bytes.length, bytes));
    }
    return ZipEncoder().encodeBytes(zip);
  }

  /// ABRE um pacote: solta as midias em [pastaDasMidias] e devolve o
  /// projeto com os caminhos apontando para elas. Um pacote sem
  /// `projeto.json` nao e um pacote.
  static VideoProject abrir(Uint8List bytes, Directory pastaDasMidias) {
    final zip = ZipDecoder().decodeBytes(bytes);
    final entradaDoJson = zip.files
        .where((f) => f.isFile && f.name == 'projeto.json')
        .firstOrNull;
    if (entradaDoJson == null) {
      throw const FormatException('Esse arquivo não é um pacote da Aurea.');
    }
    final m = jsonDecode(utf8.decode(entradaDoJson.content as List<int>));
    if (m is! Map<String, dynamic> || m['projeto'] is! Map<String, dynamic>) {
      throw const FormatException('O pacote veio sem o projeto dentro.');
    }

    pastaDasMidias.createSync(recursive: true);
    final porEntrada = <String, String>{};
    for (final f in zip.files) {
      if (!f.isFile || !f.name.startsWith('midia/')) continue;
      // Nome de entrada nunca vira caminho para fora da pasta.
      final nome = f.name.substring('midia/'.length).replaceAll('..', '_');
      final destino = File('${pastaDasMidias.path}/$nome');
      destino.writeAsBytesSync(f.content as List<int>);
      porEntrada[f.name] = destino.path;
    }

    // Mapas reconstruidos PRECISAM sair como Map<String, dynamic>: la
    // dentro o leitor do projeto faz cast e um _Map<dynamic, ...> derruba.
    Object? reescreve(Object? no) => switch (no) {
      Map() => <String, dynamic>{
        for (final e in no.entries) e.key as String: reescreve(e.value),
      },
      List() => [for (final v in no) reescreve(v)],
      String() => porEntrada[no] ?? no,
      _ => no,
    };

    final projeto = projectFromJson(
      reescreve(m['projeto']) as Map<String, dynamic>,
    );
    return projeto.comIdNovo();
  }
}
