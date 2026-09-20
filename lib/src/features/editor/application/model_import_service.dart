import 'dart:convert';
import 'dart:io';
import 'dart:isolate';
import 'dart:typed_data';

import 'package:archive/archive.dart';

import '../domain/analise_do_modelo.dart';
import '../domain/limites_de_importacao.dart';
import '../domain/malha_importada.dart';
import '../domain/model_asset3d.dart';
import '../domain/model_import3d.dart';
import '../domain/fbx_import3d.dart';
import '../domain/obj_import3d.dart';
import '../domain/texturas_importadas.dart';

/// O QUE FAZER QUANDO O MODELO E PESADO.
///
///  * [perguntar]: a leitura PARA e lanca [ModeloPesadoException] com a
///    ficha do modelo. Quem chamou mostra o aviso e chama de novo com uma
///    das outras duas. Reler o arquivo so acontece no caso pesado, e evita
///    atravessar uma malha gigante entre isolates so para esperar o dono;
///  * [otimizar]: reduz a malha para perto de `alvoDeTriangulos` e as
///    texturas para `ladoDaTextura`, no mesmo isolate da leitura;
///  * [original]: entra como veio (o comportamento de sempre).
enum PoliticaDePeso { perguntar, otimizar, original }

/// O modelo foi lido, e pesado, e a politica era [PoliticaDePeso.perguntar].
///
/// E UMA [ModelImportException] de proposito: quem ainda nao conhece o
/// aviso de modelo pesado mostra a mensagem em vez de cair no "nao consegui
/// ler" generico.
class ModeloPesadoException extends ModelImportException {
  const ModeloPesadoException(this.analise)
    : super(
        'Este modelo é pesado para o aparelho. Importe com a otimização '
        'automática ou escolha importar o original.',
      );

  final AnaliseDoModelo analise;
}

// Parsing and file access run off the UI isolate. Do not run the native
// analyzer here: it blocks the UI and its recommendations are not consumed
// by the active renderer.
Future<ModelAsset3D> readModel3DFiles(
  List<String> paths, {
  bool permitirModeloGrande = false,
  PoliticaDePeso politica = PoliticaDePeso.original,
  int alvoDeTriangulos = alvoDeTriangulosOtimizado,
  int ladoDaTextura = ladoDaTexturaOtimizada,
  LimitesDeModeloPesado limitesDePeso = limitesDeModeloPesado,
}) => Isolate.run(() async {
  // UM .ZIP E UMA PASTA: o modelo e as texturas dele juntos. Sozinho, um OBJ
  // ou FBX costuma chegar sem textura — elas moram em arquivos ao lado, e o
  // seletor do celular deixa escolher um arquivo so. Com o pacote, o que o
  // autor mandou chega inteiro.
  final pastas = <Directory>[];
  final abertos = <String>[];
  try {
    for (final caminho in paths) {
      if (caminho.toLowerCase().endsWith('.zip')) {
        final pasta = Directory.systemTemp.createTempSync('aurea_modelo_zip_');
        pastas.add(pasta);
        abertos.addAll(_abrirZip(caminho, pasta));
      } else {
        abertos.add(caminho);
      }
    }
    if (abertos.isEmpty) {
      modelFail('O .zip nao tem nenhum modelo (GLB, glTF, OBJ ou FBX).');
    }
    return await _lerEOtimizar(
      abertos,
      permitirModeloGrande,
      politica: politica,
      alvoDeTriangulos: alvoDeTriangulos,
      ladoDaTextura: ladoDaTextura,
      limitesDePeso: limitesDePeso,
    );
  } finally {
    // O modelo lido ja carrega as texturas por dentro (data: URI): a pasta
    // temporaria pode ir embora.
    for (final pasta in pastas) {
      try {
        pasta.deleteSync(recursive: true);
      } catch (_) {}
    }
  }
});

/// DESEMPACOTA SO O QUE SERVE, em UMA pasta plana.
///
/// PLANA DE PROPOSITO: o `.mtl` e o FBX procuram a textura pelo NOME do
/// arquivo, e os pacotes da internet trazem `textures/`, `source/` e pastas
/// com o nome do autor. Achatar faz `map_Kd textures/pedra.png` encontrar
/// `pedra.png` sem depender de como o autor arrumou o zip.
///
/// O CAMINHO DE DENTRO DO ZIP NUNCA VIRA CAMINHO DE DISCO: so o nome final
/// e usado, entao um `../../` malicioso nao escreve fora da pasta.
List<String> _abrirZip(String caminho, Directory pasta) {
  const modelos = {'glb', 'gltf', 'obj', 'fbx'};
  const apoio = {'bin', 'mtl', 'png', 'jpg', 'jpeg', 'webp', 'bmp', 'tga'};
  final arquivo = ZipDecoder().decodeBytes(File(caminho).readAsBytesSync());
  final saida = <String>[];
  var total = 0;
  for (final f in arquivo.files) {
    if (!f.isFile) continue;
    final nome = f.name.replaceAll(r'\', '/').split('/').last;
    if (nome.isEmpty || nome.startsWith('.')) continue;
    final ponto = nome.lastIndexOf('.');
    final ext = ponto < 0 ? '' : nome.substring(ponto + 1).toLowerCase();
    if (!modelos.contains(ext) && !apoio.contains(ext)) continue;
    total += f.size;
    // 1 GB desempacotado: um zip-bomba nao enche o disco do aparelho.
    if (total > 1024 * 1024 * 1024) {
      modelFail('O .zip e grande demais depois de aberto.');
    }
    final destino = File('${pasta.path}/$nome');
    destino.writeAsBytesSync(f.content as List<int>);
    saida.add(destino.path);
  }
  // O MODELO VAI NA FRENTE: e o primeiro caminho que decide o formato.
  int peso(String p) {
    final e = p.substring(p.lastIndexOf('.') + 1).toLowerCase();
    return modelos.contains(e) ? 0 : 1;
  }

  saida.sort((a, b) => peso(a).compareTo(peso(b)));
  if (saida.isEmpty || peso(saida.first) != 0) return const [];
  return saida;
}

Future<ModelAsset3D> _lerEOtimizar(
  List<String> paths,
  bool permitirModeloGrande, {
  required PoliticaDePeso politica,
  required int alvoDeTriangulos,
  required int ladoDaTextura,
  required LimitesDeModeloPesado limitesDePeso,
}) async {
  final asset = await _read(paths, conferirLimites: !permitirModeloGrande);
  // O MODELO LIDO ANTES DE CUSTAR MAIS: as contagens reais (o FBX so
  // se conhece depois de lido) e as texturas pelo cabecalho, antes do
  // LOD em C++ e antes de alguma delas ser decodificada inteira.
  if (!permitirModeloGrande) conferirModeloLido(asset.data);
  var noDisco = 0;
  if (politica == PoliticaDePeso.perguntar) {
    for (final p in paths) {
      try {
        noDisco += File(p).lengthSync();
      } catch (_) {}
    }
  }
  return prepararModeloLido(
    asset,
    politica: politica,
    alvoDeTriangulos: alvoDeTriangulos,
    ladoDaTextura: ladoDaTextura,
    limitesDePeso: limitesDePeso,
    arquivoBytes: noDisco,
  );
}

/// O POS-LEITURA, separado da leitura para o teste rodar sem arquivo e sem
/// isolate: decide pelo peso, e so entao faz o trabalho caro em C++.
///
/// A PERGUNTA VEM ANTES DA SOLDA E DOS NIVEIS DE DETALHE: num modelo de um
/// milhao de triangulos eles custam segundos, e seriam jogados fora se o
/// dono escolhesse otimizar (a reducao roda no meio deles) ou cancelar.
ModelAsset3D prepararModeloLido(
  ModelAsset3D asset, {
  PoliticaDePeso politica = PoliticaDePeso.original,
  int alvoDeTriangulos = alvoDeTriangulosOtimizado,
  int ladoDaTextura = ladoDaTexturaOtimizada,
  LimitesDeModeloPesado limitesDePeso = limitesDeModeloPesado,
  int arquivoBytes = 0,
}) {
  if (politica == PoliticaDePeso.perguntar) {
    final analise = AnaliseDoModelo.doModelo(asset, arquivoBytes: arquivoBytes);
    if (analise.pesado(limitesDePeso)) throw ModeloPesadoException(analise);
  }
  final otimizar = politica == PoliticaDePeso.otimizar;
  // Solda, cache de vertices, busca e niveis de detalhe em C++ — aqui,
  // no isolate da importacao, uma vez so (ver malha_importada.dart).
  otimizarMalhasImportadas(
    asset.data,
    alvoDeTriangulos: otimizar ? alvoDeTriangulos : null,
  );
  if (otimizar) reduzirTexturasDoModelo(asset.data, lado: ladoDaTextura);
  // UM EMBRULHO NOVO sobre os mesmos dados: `triangleCount` e
  // `estimatedBytes` sao contas guardadas na primeira leitura, e a ficha de
  // peso as leu ANTES da solda e da reducao. Devolver o embrulho antigo
  // levaria ao orcamento de qualidade a memoria de um modelo que nao
  // existe mais.
  return ModelAsset3D(asset.data);
}

/// Malha e texturas do modelo ja lido, contra [limitesDeImportacao].
///
/// OS CINCO MAPAS ENTRAM NA SOMA. So a textura de cor era conferida, e um
/// material com relevo, metal e oclusao em 8K passava inteiro pelo teto de
/// pixels — que existe justamente para a memoria das imagens.
void conferirModeloLido(Map<String, dynamic> data) {
  final c = contarModelo(data);
  conferirMalha(vertices: c.vertices, triangulos: c.triangulos);
  var pixels = 0;
  final vistas = <String>{};
  final materiais = data['materials'];
  for (final m in materiais is List ? materiais : const []) {
    if (m is! Map) continue;
    for (final chave in mapasDoMaterial) {
      final uri = m[chave];
      if (uri is! String || !uri.startsWith('data:') || !vistas.add(uri)) {
        continue;
      }
      final bytes = bytesDoDataUri(uri);
      if (bytes == null) continue;
      pixels += conferirImagem('${m['name'] ?? 'do material'}', bytes);
    }
  }
  conferirPixelsDasImagens(pixels);
}

Future<ModelAsset3D> _read(
  List<String> paths, {
  bool conferirLimites = true,
}) async {
  final models = paths
      .where(
        (p) =>
            RegExp(r'\.(glb|gltf|obj|fbx)$', caseSensitive: false).hasMatch(p),
      )
      .toList();
  if (models.length != 1) {
    modelFail(
      'Selecione um modelo GLB, glTF, OBJ ou FBX e seus arquivos complementares.',
    );
  }
  final file = File(models.single);
  final root = await file.parent.resolveSymbolicLinks();
  final resources = <String, Uint8List>{};
  // O TAMANHO ANTES DA LEITURA: um arquivo grande demais nao chega a
  // entrar na memoria. O FBX de texto tem teto proprio (o leitor o separa
  // inteiro em palavras), e so o cabecalho diz qual e.
  final nomeDoModelo = file.uri.pathSegments.last;
  var fbxDeTexto = false;
  if (nomeDoModelo.toLowerCase().endsWith('.fbx')) {
    final raf = await file.open();
    try {
      fbxDeTexto = !fbxBinario(await raf.read(23));
    } finally {
      await raf.close();
    }
  }
  if (conferirLimites) {
    conferirArquivoDoModelo(
      nomeDoModelo,
      await file.length(),
      fbxDeTexto: fbxDeTexto,
    );
  }
  final tamanhosDosRecursos = <String, int>{};
  Future<Uint8List> read(File f) async {
    if (f.path != file.path) {
      tamanhosDosRecursos[f.uri.pathSegments.last] = await f.length();
      if (conferirLimites) conferirRecursos(tamanhosDosRecursos);
    }
    return f.readAsBytes();
  }

  final bytes = await read(file);
  Future<Uint8List> resolve(String uri, {Directory? relativeTo}) async {
    if (resources.containsKey(uri)) return resources[uri]!;
    final normalized = Uri.decodeComponent(uri).replaceAll('\\', '/');
    final parsed = Uri.tryParse(normalized);
    if (parsed == null ||
        parsed.hasScheme ||
        normalized.startsWith('/') ||
        normalized.split('/').contains('..')) {
      modelFail(
        'Caminho externo recusado: $uri. Coloque os recursos na pasta do modelo.',
      );
    }
    File candidate = File.fromUri(
      (relativeTo ?? file.parent).uri.resolve(normalized),
    );
    if (!await candidate.exists()) {
      final matching = paths
          .where(
            (p) =>
                p.replaceAll('\\', '/').split('/').last ==
                normalized.split('/').last,
          )
          .toList();
      if (matching.length != 1) {
        modelFail(
          'Arquivo complementar ausente: $uri. Selecione o recurso junto com o modelo.',
        );
      }
      candidate = File(matching.single);
    }
    final resolved = await candidate.resolveSymbolicLinks();
    final isSelected = paths.any(
      (p) => File(p).absolute.path == candidate.absolute.path,
    );
    if (!isSelected &&
        !resolved.toLowerCase().startsWith(
          '${root.toLowerCase()}${Platform.pathSeparator}',
        )) {
      modelFail(
        'Recurso fora da pasta do modelo. Selecione esse arquivo explicitamente.',
      );
    }
    return resources[uri] = await read(candidate);
  }

  final lower = file.path.toLowerCase();
  if (lower.endsWith('.fbx')) {
    // As imagens selecionadas junto entram pelo nome: e assim que o FBX
    // acha a textura base.
    for (final p in paths) {
      if (!RegExp(
        r'.(png|jpe?g|webp|bmp)$',
        caseSensitive: false,
      ).hasMatch(p)) {
        continue;
      }
      final f = File(p);
      resources[f.uri.pathSegments.last] = await read(f);
    }
    return importFbx3D(
      bytes,
      name: file.uri.pathSegments.last,
      resources: resources,
      maxTriangles: conferirLimites ? limitesDeImportacao.triangulos : 1 << 30,
    );
  }
  if (lower.endsWith('.obj')) {
    final contagem = contarObj(bytes);
    if (conferirLimites) {
      conferirMalha(
        vertices: contagem.vertices,
        triangulos: contagem.triangulos,
      );
    }
    // Texto que nao e UTF-8 valido (nome de material em Latin-1, comum em
    // exportadores antigos) nao pode recusar a geometria inteira.
    final source = utf8.decode(bytes, allowMalformed: true);
    // MTL E TEXTURA SAO OPCIONAIS: o que faltar vira aviso no importador,
    // e a geometria entra com material padrao.
    Future<Uint8List?> talvez(String uri, {Directory? relativeTo}) async {
      try {
        return await resolve(uri, relativeTo: relativeTo);
      } on ModelImportException {
        return null;
      }
    }

    for (final line in const LineSplitter().convert(source)) {
      if (!line.trimLeft().startsWith('mtllib ')) continue;
      final mtlName = line.trim().substring(7).trim();
      final mtlBytes = await talvez(mtlName);
      if (mtlBytes == null) continue;
      final mtl = utf8.decode(mtlBytes, allowMalformed: true);
      for (final entry in const LineSplitter().convert(mtl)) {
        if (!entry.trimLeft().startsWith('map_Kd ')) continue;
        final name = texturaDoMapKd(
          entry.trim().substring(7).trim().split(RegExp(r'\s+')),
        );
        if (name == null) continue;
        final mtlDirectory = Directory.fromUri(
          file.parent.uri.resolve(mtlName).resolve('.'),
        );
        await talvez(name, relativeTo: mtlDirectory);
      }
    }
    return importObj3D(
      source,
      name: file.uri.pathSegments.last,
      resources: resources,
    );
  }
  Map<String, dynamic>? doc;
  if (lower.endsWith('.gltf')) {
    doc = (jsonDecode(utf8.decode(bytes)) as Map).cast<String, dynamic>();
  } else if (bytes.length >= 20) {
    final data = ByteData.sublistView(bytes);
    final length = data.getUint32(12, Endian.little);
    if (20 + length <= bytes.length &&
        data.getUint32(16, Endian.little) == 0x4e4f534a) {
      doc = (jsonDecode(utf8.decode(bytes.sublist(20, 20 + length))) as Map)
          .cast<String, dynamic>();
    }
  }
  if (doc != null) {
    final declarado = contarGltf(doc);
    if (conferirLimites) {
      conferirMalha(
        vertices: declarado.vertices,
        triangulos: declarado.triangulos,
      );
    }
    for (final entry in [
      ...doc['buffers'] as List? ?? [],
      ...doc['images'] as List? ?? [],
    ]) {
      final uri = entry['uri'] as String?;
      if (uri != null && !uri.startsWith('data:')) await resolve(uri);
    }
  }
  final asset = importGltf3D(
    bytes,
    binary: lower.endsWith('.glb'),
    resources: resources,
  );
  // O NOME DO ARQUIVO quando a cena nao tem nome de verdade: quase todo
  // exportador chama a cena de "Scene", e a camada nascia com esse nome.
  final nome = (asset.data['name'] as String? ?? '').trim();
  if (nome.isEmpty || nome == 'Scene' || nome == 'Modelo glTF') {
    final arquivo = file.uri.pathSegments.last;
    final ponto = arquivo.lastIndexOf('.');
    asset.data['name'] = ponto > 0 ? arquivo.substring(0, ponto) : arquivo;
  }
  return asset;
}
