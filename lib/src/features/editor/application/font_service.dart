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
  Future<void>? _loading;

  /// Avisa a interface quando uma fonte nova ficou pronta.
  final ValueNotifier<int> revision = ValueNotifier(0);

  /// Familias importadas e a fonte empacotada dos motions.
  List<String> get families =>
      {..._familias.keys, 'Aurea Motion Sans'}.toList()..sort();

  bool has(String family) =>
      family == 'Aurea Motion Sans' || _familias.containsKey(family);

  bool isBundled(String family) => family == 'Aurea Motion Sans';

  /// O arquivo da fonte empacotada, dentro dos assets do aplicativo.
  static const assetDaFonteEmpacotada =
      'assets/templates/dnyx/AureaMotionSans.ttf';

  /// O CAMINHO DO ARQUIVO de uma familia importada, ou nulo.
  ///
  /// O registro do Flutter so desenha a fonte; o texto 3D precisa LER o
  /// contorno das letras, e para isso precisa do arquivo que foi copiado
  /// para dentro do aplicativo. A fonte empacotada nao tem caminho: ela
  /// mora nos assets (ver [bytesDaFonte]).
  Future<String?> caminhoDoArquivo(String familia) async {
    if (isBundled(familia)) return null;
    try {
      await loadAll();
      final arquivo = _familias[familia];
      // Familia registrada sem arquivo (a bancada de testes) nao tem o
      // que ler.
      if (arquivo == null || arquivo.isEmpty) return null;
      final dir = await _pasta();
      final f = File('${dir.path}/$arquivo');
      return f.existsSync() ? f.path : null;
    } catch (_) {
      return null;
    }
  }

  /// OS BYTES da fonte de [familia]: do asset, se for a empacotada; do
  /// arquivo copiado, se foi importada. Nulo quando nao ha o que ler.
  Future<Uint8List?> bytesDaFonte(String familia) async {
    try {
      if (isBundled(familia)) {
        final dados = await rootBundle.load(assetDaFonteEmpacotada);
        return dados.buffer.asUint8List(
          dados.offsetInBytes,
          dados.lengthInBytes,
        );
      }
      final caminho = await caminhoDoArquivo(familia);
      if (caminho == null) return null;
      return await File(caminho).readAsBytes();
    } catch (_) {
      return null;
    }
  }

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
  Future<void> loadAll() => _loading ??= _loadAll();

  Future<void> _loadAll() async {
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
      // O `loadAll` FICA DENTRO DO TRY.
      //
      // Ele estava de fora, e por isso uma falha ao abrir a pasta do
      // app — sem espaco, permissao negada, indice corrompido —
      // ESCAPAVA de `import` em vez de virar "nao deu". Quem chama
      // espera nulo, nao excecao: com o importador ligado a um botao,
      // essa excecao subiria ate a tela.
      await loadAll();
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

      // O NOME NAO PROVA NADA. Ate aqui a unica conferencia era a
      // extensao: um arquivo de texto renomeado para `.ttf` entrava na
      // lista como fonte, aparecia escolhida e nao mudava um glifo —
      // uma fonte instalada que nao desenha e pior que uma que faltou,
      // porque a primeira nao da sinal nenhum.
      //
      // A assinatura sfnt sao os quatro primeiros bytes, e so ha quatro
      // possibilidades.
      if (!_pareceFonte(await origem.openRead(0, 4).first)) return null;

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

  /// Import sequentially so registering and saving one font cannot race
  /// with another. A broken file does not discard the rest of the selection.
  Future<({List<String> imported, int failed})> importMany(
    Iterable<String> paths,
  ) async {
    final imported = <String>[];
    var failed = 0;
    for (final path in paths) {
      final family = await import(path);
      if (family == null) {
        failed++;
      } else {
        imported.add(family);
      }
    }
    return (imported: imported, failed: failed);
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

  /// Os quatro primeiros bytes sao de um arquivo sfnt?
  ///
  ///   0x00010000  TrueType
  ///   'OTTO'      OpenType com contornos CFF
  ///   'true'      TrueType do Mac antigo
  ///   'ttcf'      colecao (varias fontes num arquivo so)
  static bool _pareceFonte(List<int> b) {
    if (b.length < 4) return false;
    const assinaturas = [
      [0x00, 0x01, 0x00, 0x00],
      [0x4F, 0x54, 0x54, 0x4F],
      [0x74, 0x72, 0x75, 0x65],
      [0x74, 0x74, 0x63, 0x66],
    ];
    for (final a in assinaturas) {
      if (b[0] == a[0] && b[1] == a[1] && b[2] == a[2] && b[3] == a[3]) {
        return true;
      }
    }
    return false;
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
