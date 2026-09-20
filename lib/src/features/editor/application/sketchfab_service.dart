import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../../../core/storage/prefs.dart';
import '../domain/analise_do_modelo.dart';
import '../domain/scene3d.dart';

/// O QUE DEU ERRADO COM O SKETCHFAB, com a frase pronta para a tela.
///
/// O servico nunca devolve codigo cru para a interface: quem le e o dono,
/// e "HTTP 429" nao diz o que fazer. Cada caso vira uma frase que termina
/// numa acao possivel (esperar, reconectar, escolher outro modelo).
class SketchfabException implements Exception {
  const SketchfabException(this.mensagem, {this.status = 0});

  final String mensagem;

  /// O codigo HTTP, quando veio de uma resposta. Zero = falha de rede ou
  /// resposta ilegivel. Serve a quem precisa reagir (401 pede reconectar).
  final int status;

  /// O token expirou, foi revogado ou nunca valeu.
  bool get precisaDeToken => status == 401 || status == 403;

  @override
  String toString() => 'SketchfabException($mensagem)';
}

/// O dono apertou "Cancelar" no meio do download.
class DownloadCancelado implements Exception {
  const DownloadCancelado();
  @override
  String toString() => 'DownloadCancelado';
}

/// O BOTAO DE CANCELAR, do lado de quem baixa.
///
/// O download e um `await for` sobre a resposta: fechar o `HttpClient` no
/// meio derruba TODOS os pedidos do servico (a busca que o dono deixou
/// rolando atras, por exemplo). Este sinalizador corta so este download, no
/// proximo pedaco de bytes que chegar.
class Cancelamento {
  bool _cancelado = false;
  bool get cancelado => _cancelado;
  void cancelar() => _cancelado = true;
}

// ==========================================================================
// O QUE A BUSCA DEVOLVE
// ==========================================================================

/// UM ARQUIVO PRONTO PARA BAIXAR (`glb`, `gltf`, `usdz`, `source`).
///
/// A BUSCA JA TRAZ ISTO. E por isso que a ficha do modelo — triangulos,
/// texturas, tamanho — aparece na grade e no detalhe sem nenhuma chamada a
/// mais, e antes de qualquer byte ser baixado.
class ArquivoDoSketchfab {
  const ArquivoDoSketchfab({
    required this.formato,
    this.bytes = 0,
    this.faces = 0,
    this.vertices = 0,
    this.texturas = 0,
    this.maiorTextura = 0,
  });

  final String formato;
  final int bytes;
  final int faces;
  final int vertices;
  final int texturas;

  /// Lado maior da maior textura do pacote, em px.
  final int maiorTextura;

  static ArquivoDoSketchfab? deJson(String formato, Object? bruto) {
    if (bruto is! Map) return null;
    return ArquivoDoSketchfab(
      formato: formato,
      bytes: _inteiro(bruto['size']),
      faces: _inteiro(bruto['faceCount']),
      vertices: _inteiro(bruto['vertexCount']),
      texturas: _inteiro(bruto['textureCount']),
      maiorTextura: _inteiro(bruto['textureMaxResolution']),
    );
  }
}

/// UM MODELO DO SKETCHFAB, do jeito que a busca o descreve.
class ModeloDoSketchfab {
  const ModeloDoSketchfab({
    required this.uid,
    required this.nome,
    this.autor = '',
    this.autorUrl,
    this.licenca = '',
    this.faces = 0,
    this.vertices = 0,
    this.animacoes = 0,
    this.viewerUrl,
    this.miniaturas = const [],
    this.arquivos = const {},
    this.baixavel = true,
  });

  final String uid;
  final String nome;
  final String autor;
  final String? autorUrl;

  /// O rotulo por extenso ("CC Attribution", "CC0 Public Domain"...).
  final String licenca;

  final int faces;
  final int vertices;
  final int animacoes;

  /// A pagina do modelo no Sketchfab — e ela que a atribuicao exige.
  final String? viewerUrl;

  /// Da menor para a maior.
  final List<MiniaturaDoSketchfab> miniaturas;

  final Map<String, ArquivoDoSketchfab> arquivos;
  final bool baixavel;

  static ModeloDoSketchfab? deJson(Object? bruto) {
    if (bruto is! Map) return null;
    final uid = '${bruto['uid'] ?? ''}'.trim();
    if (uid.isEmpty) return null;
    final user = bruto['user'];
    final license = bruto['license'];
    final miniaturas = <MiniaturaDoSketchfab>[];
    final imagens = (bruto['thumbnails'] is Map)
        ? (bruto['thumbnails'] as Map)['images']
        : null;
    for (final img in imagens is List ? imagens : const []) {
      final m = MiniaturaDoSketchfab.deJson(img);
      if (m != null) miniaturas.add(m);
    }
    miniaturas.sort((a, b) => a.largura.compareTo(b.largura));
    final arquivos = <String, ArquivoDoSketchfab>{};
    final archives = bruto['archives'];
    if (archives is Map) {
      for (final entrada in archives.entries) {
        final a = ArquivoDoSketchfab.deJson('${entrada.key}', entrada.value);
        if (a != null) arquivos['${entrada.key}'] = a;
      }
    }
    return ModeloDoSketchfab(
      uid: uid,
      nome: '${bruto['name'] ?? ''}'.trim(),
      autor: user is Map
          ? '${user['displayName'] ?? user['username'] ?? ''}'.trim()
          : '',
      autorUrl: user is Map ? _texto(user['profileUrl']) : null,
      licenca: license is Map ? '${license['label'] ?? ''}'.trim() : '',
      faces: _inteiro(bruto['faceCount']),
      vertices: _inteiro(bruto['vertexCount']),
      animacoes: _inteiro(bruto['animationCount']),
      viewerUrl: _texto(bruto['viewerUrl']),
      miniaturas: miniaturas,
      arquivos: arquivos,
      // Ausente = a busca ja filtrou por `downloadable=true`.
      baixavel: bruto['isDownloadable'] != false,
    );
  }

  /// O PACOTE QUE O APP PREFERE: `glb` (um arquivo so) antes de `gltf`
  /// (zip com `scene.gltf` + `scene.bin` + `textures/`). `usdz` e `source`
  /// ficam de fora — o importador do Aurea nao le nenhum dos dois.
  ArquivoDoSketchfab? get pacote => arquivos['glb'] ?? arquivos['gltf'];

  /// A FICHA, ANTES DE BAIXAR. E a mesma [AnaliseDoModelo] do aviso de
  /// modelo pesado, so que montada com o que a busca DECLARA — assim o
  /// dono ve o peso antes de gastar a rede, e nao depois.
  ///
  /// Materiais e ossos ficam em zero: a busca nao os informa, e chutar
  /// numero numa ficha que existe para informar seria pior do que omitir.
  AnaliseDoModelo get ficha {
    final p = pacote;
    return AnaliseDoModelo(
      triangulos: p?.faces ?? faces,
      vertices: p?.vertices ?? vertices,
      texturas: p?.texturas ?? 0,
      maiorTextura: p?.maiorTextura ?? 0,
      pixelsDasTexturas: p == null
          ? 0
          : p.texturas * p.maiorTextura * p.maiorTextura,
      animacoes: animacoes,
      arquivoBytes: p?.bytes ?? 0,
    );
  }

  /// A ATRIBUICAO QUE VAI PRESA AO NO DA CENA. As licencas Creative
  /// Commons pedem titulo, autor, origem e licenca em todo lugar onde a
  /// obra aparece — inclusive dentro do projeto de quem baixou.
  ModelCredit3D get credito => ModelCredit3D(
    author: autor.isEmpty ? null : autor,
    license: licenca.isEmpty ? null : licenca,
    url: viewerUrl,
    title: nome.isEmpty ? null : nome,
    source: 'Sketchfab',
    authorUrl: autorUrl,
  );

  /// A miniatura mais proxima de [largura] sem ficar menor que ela; se
  /// todas forem menores, a maior que houver.
  String? miniatura(int largura) {
    for (final m in miniaturas) {
      if (m.largura >= largura) return m.url;
    }
    return miniaturas.isEmpty ? null : miniaturas.last.url;
  }
}

class MiniaturaDoSketchfab {
  const MiniaturaDoSketchfab({
    required this.url,
    this.largura = 0,
    this.altura = 0,
  });

  final String url;
  final int largura;
  final int altura;

  static MiniaturaDoSketchfab? deJson(Object? bruto) {
    if (bruto is! Map) return null;
    final url = '${bruto['url'] ?? ''}'.trim();
    if (!url.startsWith('http')) return null;
    return MiniaturaDoSketchfab(
      url: url,
      largura: _inteiro(bruto['width']),
      altura: _inteiro(bruto['height']),
    );
  }
}

/// UMA PAGINA DE RESULTADOS. [proxima] e a URL COMPLETA da pagina
/// seguinte, como o Sketchfab a devolve (ela ja carrega o cursor); `null`
/// quando acabou.
class PaginaDoSketchfab {
  const PaginaDoSketchfab({this.itens = const [], this.proxima});
  final List<ModeloDoSketchfab> itens;
  final String? proxima;
}

/// A CONTA CONECTADA, do `GET /v3/me`.
class ContaDoSketchfab {
  const ContaDoSketchfab({required this.nome, this.usuario = ''});
  final String nome;
  final String usuario;
}

/// O LINK TEMPORARIO DE DOWNLOAD.
///
/// VALE ~300 SEGUNDOS E NAO SE GUARDA. Por isso o servico nao o cacheia e
/// a tela pede um novo imediatamente antes de baixar — um link pedido na
/// hora de abrir o detalhe ja teria vencido quando o dono decidisse.
class LinkDeDownload {
  const LinkDeDownload({
    required this.url,
    required this.formato,
    this.bytes = 0,
    this.expiraEm = 300,
  });

  final String url;

  /// `glb` ou `gltf`. Decide a extensao do arquivo baixado, e com ela o
  /// caminho que o importador toma (arquivo direto ou zip).
  final String formato;
  final int bytes;
  final int expiraEm;

  /// `.glb` para o arquivo unico; `.zip` para o pacote glTF, que e o que o
  /// Sketchfab entrega em `gltf` (`scene.gltf` + `scene.bin` + texturas).
  String get extensao => formato == 'glb' ? '.glb' : '.zip';
}

// ==========================================================================
// O COFRE DO TOKEN
// ==========================================================================

/// ONDE O TOKEN DO SKETCHFAB FICA GUARDADO.
///
/// E uma interface, e nao uma funcao direta, por dois motivos: o teste
/// injeta um cofre de mentira sem tocar em plugin nenhum, e a troca da
/// guarda provisoria (preferencias, texto puro na caixa de areia do app)
/// pelo chaveiro do sistema acontece AQUI, sem uma linha de tela mudar.
abstract interface class CofreDoToken {
  Future<String?> ler();
  Future<void> gravar(String token);
  Future<void> apagar();
}

/// GUARDA PROVISORIA, em `SharedPreferences`.
///
/// O token fica em texto puro na caixa de areia do app: so quem ja tem o
/// aparelho desbloqueado (ou root) o alcanca. O certo para credencial e o
/// chaveiro do sistema (`flutter_secure_storage`), que e plugin nativo
/// novo — decisao do dono, e por isso esta como pendencia.
///
/// O QUE NUNCA ACONTECE, aqui ou em qualquer lugar: o token ir para uma
/// URL, para um log, para o projeto salvo ou para o relatorio de erro.
class CofreEmPreferencias implements CofreDoToken {
  const CofreEmPreferencias(this._prefs);

  final SharedPreferences _prefs;

  static const chave = 'sketchfab.token';

  @override
  Future<String?> ler() async {
    final t = _prefs.getString(chave)?.trim();
    return (t == null || t.isEmpty) ? null : t;
  }

  @override
  Future<void> gravar(String token) async {
    final t = token.trim();
    if (t.isEmpty) return apagar();
    await _prefs.setString(chave, t);
  }

  @override
  Future<void> apagar() => _prefs.remove(chave).then((_) {});
}

/// COFRE DE MEMORIA: para teste e para quando nao ha preferencias.
class CofreNaMemoria implements CofreDoToken {
  CofreNaMemoria([this._token]);

  String? _token;

  @override
  Future<String?> ler() async => _token;

  @override
  Future<void> gravar(String token) async =>
      _token = token.trim().isEmpty ? null : token.trim();

  @override
  Future<void> apagar() async => _token = null;
}

// ==========================================================================
// O SERVICO
// ==========================================================================

/// A DATA API v3 DO SKETCHFAB, so o que o app usa.
///
/// A BUSCA E PUBLICA e o DOWNLOAD NAO E: quem nao conectou a conta navega
/// pelo acervo inteiro e so esbarra no token na hora de baixar. Foi de
/// proposito — pedir credencial antes de a pessoa ver o que ha do outro
/// lado e o jeito mais rapido de ela desistir.
class SketchfabService {
  SketchfabService({
    HttpClient? http,
    this.base = 'https://api.sketchfab.com/v3',
    Future<Directory> Function()? pastaDeDownload,
  }) : _http =
           http ??
           (HttpClient()..connectionTimeout = const Duration(seconds: 15)),
       _pasta = pastaDeDownload ?? _pastaDoCache;

  final HttpClient _http;

  /// A raiz da API. Existe para o teste apontar para outro lugar.
  final String base;

  /// ONDE O PACOTE BAIXADO CAI: o cache, e nao os Documentos. O modelo ja
  /// e copiado para dentro do projeto na importacao, entao o zip nao
  /// precisa sobreviver — e o sistema pode recolher a pasta quando
  /// precisar de espaco. Injetavel para o teste, que nao tem o
  /// `path_provider`.
  final Future<Directory> Function() _pasta;

  /// O maximo que a API aceita por pagina.
  static const porPagina = 24;

  /// A pagina de onde o dono copia o token dele.
  static const paginaDoToken = 'https://sketchfab.com/settings/password';

  /// BUSCA. Sem [proxima], comeca do inicio com [termo]; com ela, continua
  /// de onde parou (a URL ja vem pronta do servidor, com o cursor dentro).
  ///
  /// [token] e OPCIONAL de proposito: a busca funciona sem conta.
  Future<PaginaDoSketchfab> buscar({
    String termo = '',
    int quantidade = porPagina,
    int? maxFaces,
    String? proxima,
    String? token,
  }) async {
    final url = proxima != null
        ? Uri.parse(proxima)
        : Uri.parse('$base/search').replace(
            queryParameters: <String, String>{
              'type': 'models',
              'downloadable': 'true',
              'count': '${quantidade.clamp(1, porPagina)}',
              if (termo.trim().isNotEmpty) 'q': termo.trim(),
              if (maxFaces != null) 'max_face_count': '$maxFaces',
            },
          );
    final doc = await _json(url, token: token);
    final resultados = doc['results'];
    final itens = <ModeloDoSketchfab>[];
    for (final bruto in resultados is List ? resultados : const []) {
      final m = ModeloDoSketchfab.deJson(bruto);
      if (m != null) itens.add(m);
    }
    final next = _texto(doc['next']);
    return PaginaDoSketchfab(itens: itens, proxima: next);
  }

  /// "TESTAR": confere o token contra a propria API e devolve de quem ele e.
  Future<ContaDoSketchfab> eu(String token) async {
    final doc = await _json(Uri.parse('$base/me'), token: token);
    final usuario = '${doc['username'] ?? ''}'.trim();
    final nome = '${doc['displayName'] ?? ''}'.trim();
    return ContaDoSketchfab(
      nome: nome.isEmpty ? usuario : nome,
      usuario: usuario,
    );
  }

  /// O LINK TEMPORARIO. Pedir IMEDIATAMENTE antes de baixar — ele vence em
  /// cerca de 300 s.
  Future<LinkDeDownload> linkDeDownload(
    String uid, {
    required String token,
  }) async {
    final doc = await _json(
      Uri.parse('$base/models/$uid/download'),
      token: token,
    );
    // A doc so promete `gltf` e `usdz`; na pratica o `glb` costuma vir, e
    // ele e um arquivo so — preferivel ao zip sempre que existir.
    for (final formato in const ['glb', 'gltf']) {
      final entrada = doc[formato];
      if (entrada is! Map) continue;
      final url = _texto(entrada['url']);
      if (url == null) continue;
      return LinkDeDownload(
        url: url,
        formato: formato,
        bytes: _inteiro(entrada['size']),
        expiraEm: _inteiro(entrada['expires']),
      );
    }
    throw const SketchfabException(
      'Este modelo não tem um pacote glTF para baixar.',
    );
  }

  /// A PASTA DOS DOWNLOADS, criada se ainda nao existir.
  Future<Directory> pastaDeDownload() async {
    final pasta = await _pasta();
    if (!pasta.existsSync()) pasta.createSync(recursive: true);
    return pasta;
  }

  static Future<Directory> _pastaDoCache() async {
    Directory raiz;
    try {
      raiz = await getTemporaryDirectory();
    } catch (_) {
      // Sem o plugin (teste, desktop sem suporte): a temporaria do
      // sistema serve — o arquivo e apagado logo depois de importado.
      raiz = Directory.systemTemp;
    }
    return Directory('${raiz.path}${Platform.pathSeparator}sketchfab');
  }

  /// BAIXA PARA [destino], em pedacos.
  ///
  /// Grava num `.part` e so renomeia no fim: um download interrompido
  /// nunca deixa para tras um arquivo com a cara de pronto (e o
  /// importador, que so olha a extensao, tentaria ler o pedaco).
  ///
  /// [onProgresso] recebe (recebidos, total); total 0 = o servidor nao
  /// disse o tamanho, e a barra fica indeterminada.
  Future<File> baixar(
    LinkDeDownload link,
    String destino, {
    void Function(int recebidos, int total)? onProgresso,
    Cancelamento? cancelamento,
  }) async {
    final alvo = File(destino);
    alvo.parent.createSync(recursive: true);
    final parcial = File('$destino.part');
    if (parcial.existsSync()) parcial.deleteSync();
    final sink = parcial.openWrite();
    try {
      // O LINK NAO LEVA O TOKEN. Ele ja e assinado pelo proprio Sketchfab,
      // e mandar a credencial para um endereco de CDN seria entrega-la a
      // um terceiro sem motivo.
      final pedido = await _http.getUrl(Uri.parse(link.url));
      final resposta = await pedido.close();
      if (resposta.statusCode != 200) {
        throw _erroDoStatus(resposta.statusCode);
      }
      final total = resposta.contentLength > 0
          ? resposta.contentLength
          : link.bytes;
      var recebidos = 0;
      onProgresso?.call(0, total);
      await for (final pedaco in resposta) {
        if (cancelamento?.cancelado ?? false) {
          throw const DownloadCancelado();
        }
        sink.add(pedaco);
        recebidos += pedaco.length;
        onProgresso?.call(recebidos, total);
      }
      await sink.close();
      if (recebidos == 0) {
        throw const SketchfabException('O download veio vazio.');
      }
      if (alvo.existsSync()) alvo.deleteSync();
      parcial.renameSync(destino);
      return alvo;
    } catch (e) {
      try {
        await sink.close();
      } catch (_) {
        // Fechar duas vezes e inofensivo; o que importa e apagar o pedaco.
      }
      if (parcial.existsSync()) parcial.deleteSync();
      if (e is SketchfabException || e is DownloadCancelado) rethrow;
      throw SketchfabException(_frasePorFalhaDeRede(e));
    }
  }

  // ------------------------------------------------------------------

  Future<Map<String, dynamic>> _json(Uri url, {String? token}) async {
    HttpClientResponse resposta;
    try {
      final pedido = await _http.getUrl(url);
      // O TOKEN SO VIAJA NO CABECALHO. Em URL ele entraria em log de
      // servidor, em historico e no relatorio de erro.
      if (token != null && token.trim().isNotEmpty) {
        pedido.headers.set(HttpHeaders.authorizationHeader, 'Token ${token.trim()}');
      }
      resposta = await pedido.close();
    } catch (e) {
      throw SketchfabException(_frasePorFalhaDeRede(e));
    }
    if (resposta.statusCode != 200) {
      // O corpo do erro e drenado para a conexao poder ser reaproveitada.
      await resposta.drain<void>().catchError((_) {});
      throw _erroDoStatus(resposta.statusCode);
    }
    final texto = await resposta.transform(utf8.decoder).join();
    final doc = jsonDecode(texto);
    if (doc is! Map) {
      throw const SketchfabException(
        'O Sketchfab respondeu algo que não entendi.',
      );
    }
    return doc.cast<String, dynamic>();
  }

  SketchfabException _erroDoStatus(int status) {
    if (status == 401 || status == 403) {
      return SketchfabException(
        'O token do Sketchfab não foi aceito. Conecte a conta de novo.',
        status: status,
      );
    }
    if (status == 429) {
      return SketchfabException(
        'Muitos pedidos ao Sketchfab. Espere um minuto e tente de novo.',
        status: status,
      );
    }
    if (status == 404) {
      return SketchfabException(
        'Este modelo não está mais disponível no Sketchfab.',
        status: status,
      );
    }
    return SketchfabException(
      'O Sketchfab respondeu com erro ($status). Tente de novo.',
      status: status,
    );
  }

  String _frasePorFalhaDeRede(Object e) => e is SocketException
      ? 'Sem conexão com o Sketchfab. Confira a internet.'
      : 'Não consegui falar com o Sketchfab. Tente de novo.';
}

// ==========================================================================
// PROVEDORES
// ==========================================================================

final sketchfabServiceProvider = Provider<SketchfabService>(
  (ref) => SketchfabService(),
);

final cofreDoTokenProvider = Provider<CofreDoToken>(
  (ref) => CofreEmPreferencias(ref.read(sharedPreferencesProvider)),
);

// ==========================================================================

int _inteiro(Object? v) => v is num ? v.toInt() : int.tryParse('$v') ?? 0;

String? _texto(Object? v) {
  if (v is! String) return null;
  final t = v.trim();
  return t.isEmpty ? null : t;
}
