import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:path_provider/path_provider.dart';

/// FONTES DA PESSOA.
///
/// A fonte e metade da identidade de um video. Trabalhar so com a fonte
/// do sistema obriga a fazer a arte em outro aplicativo e trazer como
/// imagem — e ai o texto deixa de ser texto: nao anima por letra, nao
/// muda depois, nao aceita o animador.
///
/// Aqui o arquivo .ttf/.otf e COPIADO para dentro do aplicativo. Ficar
/// apontando para o caminho original quebraria na primeira faxina da
/// pasta de Downloads, e o projeto abriria sem a fonte — que e o defeito
/// classico de editor que "importa" fonte por referencia.
class FontService {
  FontService._();
  static final instance = FontService._();

  static const _indice = 'fontes.json';

  final Map<String, String> _familias = {};
  bool _carregado = false;

  /// Avisa a interface quando uma fonte nova ficou pronta.
  final ValueNotifier<int> revision = ValueNotifier(0);

  /// Familias importadas e a fonte empacotada dos motions.
  List<String> get families =>
      {..._familias.keys, 'Aurea Motion Sans'}.toList()..sort();

  bool has(String family) =>
      family == 'Aurea Motion Sans' || _familias.containsKey(family);

  bool isBundled(String family) => family == 'Aurea Motion Sans';

  /// Para a bancada de render (testes): declara uma familia que ja foi
  /// carregada por fora (FontLoader), sem arquivo na pasta do app.
  @visibleForTesting
  void registrarSemArquivo(String familia) {
    _familias[familia] = '';
    revision.value++;
  }

  Future<Directory> _pasta() async {
    final base = await getApplicationSupportDirectory();
    final d = Directory('${base.path}/fontes');
    if (!d.existsSync()) d.createSync(recursive: true);
    return d;
  }

  /// Re-registra tudo o que ja foi importado. Roda uma vez, no comeco:
  /// o registro do Flutter vive so enquanto o processo vive.
  Future<void> loadAll() async {
    if (_carregado) return;
    _carregado = true;
    try {
      final dir = await _pasta();
      final idx = File('${dir.path}/$_indice');
      if (!idx.existsSync()) return;

      final mapa = (jsonDecode(idx.readAsStringSync()) as Map)
          .cast<String, dynamic>();
      for (final e in mapa.entries) {
        final arquivo = File('${dir.path}/${e.value}');
        if (!arquivo.existsSync()) continue;
        await _registrar(e.key, arquivo);
      }
      revision.value++;
    } catch (_) {
      // Indice corrompido nao pode impedir o aplicativo de abrir.
    }
  }

  Future<void> _registrar(String familia, File arquivo) async {
    final bytes = await arquivo.readAsBytes();
    final loader = FontLoader(familia)
      ..addFont(Future.value(ByteData.sublistView(bytes)));
    await loader.load();
    _familias[familia] = arquivo.uri.pathSegments.last;
  }

  /// Traz um .ttf/.otf para dentro e deixa pronto para uso.
  ///
  /// Devolve o nome da familia (que e o nome do arquivo sem extensao —
  /// e o que a pessoa reconhece na lista) ou null se nao deu.
  Future<String?> import(String caminhoOrigem) async {
    try {
      final origem = File(caminhoOrigem);
      if (!origem.existsSync()) return null;

      final nomeArquivo = origem.uri.pathSegments.last;
      final ext = nomeArquivo.contains('.')
          ? nomeArquivo.split('.').last.toLowerCase()
          : '';
      if (ext != 'ttf' && ext != 'otf') return null;

      final familia = nomeArquivo.substring(
        0,
        nomeArquivo.length - ext.length - 1,
      );
      if (familia.isEmpty) return null;

      final dir = await _pasta();
      final destino = File('${dir.path}/$nomeArquivo');
      // Copia para um temporario e renomeia: um arquivo pela metade no
      // indice viraria uma fonte que nao carrega em toda abertura.
      final parcial = File('${destino.path}.part');
      await origem.copy(parcial.path);
      if (destino.existsSync()) destino.deleteSync();
      await parcial.rename(destino.path);

      await _registrar(familia, destino);
      await _salvarIndice(dir);
      revision.value++;
      return familia;
    } catch (_) {
      return null;
    }
  }

  /// Tira a fonte da lista e apaga o arquivo. O registro no Flutter so
  /// cai de verdade na proxima abertura — nao ha como desregistrar.
  Future<void> remove(String familia) async {
    final arquivo = _familias.remove(familia);
    if (arquivo == null) return;
    try {
      final dir = await _pasta();
      final f = File('${dir.path}/$arquivo');
      if (f.existsSync()) f.deleteSync();
      await _salvarIndice(dir);
    } catch (_) {
      // Falhar em apagar o arquivo nao pode travar a lista.
    }
    revision.value++;
  }

  Future<void> _salvarIndice(Directory dir) async {
    final idx = File('${dir.path}/$_indice');
    await idx.writeAsString(jsonEncode(_familias));
  }
}

/// Nome da familia que a fonte importada usa, ou null para a do
/// aplicativo. Serve para o widget de texto nao precisar saber do
/// servico.
String? resolveFontFamily(String? family) {
  if (family == null || family.isEmpty) return null;
  return FontService.instance.has(family) ? family : null;
}
