import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../domain/post_da_comunidade.dart';
import 'conta_da_comunidade.dart';

/// A COMUNIDADE, do servidor ate a tela.
///
/// COMO ISTO FUNCIONA: o app le e escreve num servidor proprio (o codigo
/// dele esta em `servidor/comunidade`). Publicar e direto — sem e-mail,
/// sem ninguem no meio.
///
/// QUEM ASSINA O POST E O CODIGO DE ACESSO, e nao um nome no corpo da
/// requisicao. O aplicativo manda o codigo no cabecalho; o servidor olha
/// de quem ele e e escreve o autor. Enquanto o autor vinha no corpo,
/// qualquer um que montasse a chamada a mao postava com o nome de outra
/// pessoa.
///
/// O FILTRO EXISTE DOS DOIS LADOS de proposito. O daqui e cortesia: diz
/// a pessoa o que esta errado enquanto ela escreve. O de la e o que vale,
/// porque quem chama o endereco direto nao passa por este.
///
/// O POST APARECE NA HORA PARA QUEM ESCREVEU, mesmo que o servidor leve
/// ate um minuto para publica-lo para os outros (o armazenamento e
/// distribuido e propaga nesse ritmo). A copia local some sozinha quando
/// a do servidor chega.
class ComunidadeService {
  ComunidadeService({HttpClient? http, this.endereco = enderecoPadrao})
    : _http = http ?? (HttpClient()..connectionTimeout = _tempoLimite);

  static const _tempoLimite = Duration(seconds: 15);

  /// ARQUIVO TEM OUTRO RELOGIO. Um video de trinta megabytes numa rede de
  /// celular nao cabe em quinze segundos, e cortar no meio faria a pessoa
  /// mandar de novo — gastando a internet dela duas vezes.
  static const _tempoDeArquivo = Duration(minutes: 3);

  /// O SERVIDOR DO MURAL. O codigo dele esta em servidor/comunidade.
  static const enderecoPadrao = 'https://mural-do-aurea.aureaapp.workers.dev';

  static final instance = ComunidadeService();

  final HttpClient _http;
  final String endereco;

  /// Sobe quando o conteudo muda — a aba escuta.
  final ValueNotifier<int> revisao = ValueNotifier(0);

  List<PostDaComunidade>? _feed;
  List<PostDaComunidade>? _meus;

  /// A ultima falha de rede, em palavras. Nulo = deu certo.
  String? ultimoErro;

  // =========================================================== a conta

  Future<RespostaDaConta> criarConta(String apelido) =>
      _conta('POST', '/conta', corpo: {'apelido': apelido});

  Future<RespostaDaConta> entrarComCodigo(String codigo) =>
      _conta('POST', '/conta/entrar', codigo: codigo);

  Future<RespostaDaConta> trocarApelido(String codigo, String apelido) =>
      _conta('PATCH', '/conta', codigo: codigo, corpo: {'apelido': apelido});

  /// Total de contas criadas no Aurea. Nulo significa apenas que o numero
  /// nao pôde ser atualizado agora; nunca inventamos zero numa falha de rede.
  Future<int?> totalDeUsuarios() async {
    try {
      final req = await _http.getUrl(Uri.parse('$endereco/estatisticas'));
      final res = await req.close().timeout(_tempoLimite);
      final corpo = await res.transform(utf8.decoder).join();
      if (res.statusCode != 200) return null;
      final mapa = (jsonDecode(corpo) as Map).cast<String, dynamic>();
      final total = mapa['usuarios'];
      if (total is int && total >= 0) return total;
      if (total is num && total >= 0) return total.toInt();
      return null;
    } catch (_) {
      return null;
    }
  }

  Future<RespostaDaConta> _conta(
    String metodo,
    String caminho, {
    Map<String, Object?>? corpo,
    String? codigo,
  }) async {
    try {
      final req = await _http.openUrl(metodo, Uri.parse('$endereco$caminho'));
      if (codigo != null) req.headers.set('authorization', 'Bearer $codigo');
      if (corpo != null) {
        req.headers.set('content-type', 'application/json; charset=utf-8');
        req.add(utf8.encode(jsonEncode(corpo)));
      }
      final res = await req.close().timeout(_tempoLimite);
      final texto = await res.transform(utf8.decoder).join();
      return RespostaDaConta.lerOuFalhar(res.statusCode, texto);
    } catch (e) {
      return RespostaDaConta.falha(erroDeRede(e));
    }
  }

  // ============================================================== ler

  Future<Directory> _pasta() async {
    final base = await getApplicationSupportDirectory();
    final d = Directory('${base.path}/comunidade');
    if (!d.existsSync()) d.createSync(recursive: true);
    return d;
  }

  File _arquivo(Directory d, String nome) => File('${d.path}/$nome');

  /// O QUE APARECE NA ABA: os meus primeiro, depois o feed.
  ///
  /// Os meus vem na frente de proposito. Quem acabou de escrever quer ver
  /// o que escreveu; um post que some no meio de uma lista parece perdido.
  Future<List<PostDaComunidade>> carregar({bool daRede = true}) async {
    final meus = await meusPosts();
    if (daRede || _feed == null) {
      await _buscarFeed();
    }
    final feed = _feed ?? const <PostDaComunidade>[];
    // O MESMO POST NAO APARECE DUAS VEZES.
    //
    // Ao publicar, a copia local entra na hora e a do servidor chega ate
    // um minuto depois. Quem da o id e o servidor, e por isso o envio
    // troca o id da copia local pelo dele: sem isso, o mural mostraria o
    // post repetido nesse intervalo e quem escreveu acharia que publicou
    // duas vezes sem querer.
    final noServidor = {for (final p in feed) p.id};
    final pendentes = [
      for (final p in meus)
        if (!noServidor.contains(p.id)) p,
    ];
    if (pendentes.length != meus.length) {
      _meus = pendentes;
      await _gravarMeus();
    }
    return [...pendentes, ...feed];
  }

  Future<void> _buscarFeed() async {
    try {
      final req = await _http.getUrl(Uri.parse('$endereco/feed'));
      final res = await req.close().timeout(_tempoLimite);
      if (res.statusCode != 200) {
        throw HttpException('resposta ${res.statusCode}');
      }
      final corpo = await res.transform(utf8.decoder).join();
      final posts = lerFeed(corpo);
      _feed = posts;
      ultimoErro = null;
      // GRAVA O QUE CHEGOU. Comunidade que so existe com internet nao
      // serve para quem edita no onibus.
      try {
        final d = await _pasta();
        await _arquivo(d, 'feed.json').writeAsString(corpo, flush: true);
      } catch (_) {}
    } catch (e) {
      ultimoErro = e is SocketException || e is HttpException
          ? 'Sem conexão com a comunidade.'
          : 'Não consegui ler a comunidade agora.';
      // Cai para o que ja foi lido antes.
      if (_feed == null) {
        try {
          final f = _arquivo(await _pasta(), 'feed.json');
          if (f.existsSync()) _feed = lerFeed(await f.readAsString());
        } catch (_) {}
      }
    }
    revisao.value++;
  }

  /// AS RESPOSTAS DE UM POST, sob demanda.
  ///
  /// Elas nao vem junto com o feed de proposito: carregar as respostas de
  /// duzentos posts para mostrar uma lista onde quase nenhuma sera aberta
  /// seria pagar por tudo para ver quase nada.
  Future<List<PostDaComunidade>> respostas(String postId) async {
    try {
      final req = await _http.getUrl(
        Uri.parse('$endereco/respostas/${Uri.encodeComponent(postId)}'),
      );
      final res = await req.close().timeout(_tempoLimite);
      final corpo = await res.transform(utf8.decoder).join();
      if (res.statusCode != 200) return const [];
      // As respostas vem em ordem de chegada; lerFeed inverte para
      // mostrar a mais nova primeiro, que e o certo no mural. Numa
      // conversa, o certo e o contrario: le-se de cima para baixo.
      return lerFeed(corpo).reversed.toList();
    } catch (_) {
      return const [];
    }
  }

  // ========================================================= escrever

  /// MANDA O POST PARA O SERVIDOR.
  ///
  /// Devolve null se entrou, ou o motivo em portugues se nao entrou. O
  /// motivo vem do proprio servidor quando ele recusa (ofensa, dado
  /// pessoal, limite por hora), porque quem sabe a regra e ele — repetir
  /// a regra aqui so criaria duas versoes da verdade.
  Future<String?> enviar(
    PostDaComunidade post,
    String codigo, {
    String? respondeA,
    String? repostaDe,
  }) async {
    try {
      // A local gallery path is only usable on this phone. Upload the
      // attachment before sending its post, and keep the URL for retries.
      if (post.imagemLocal && post.imagem != null) {
        final path = post.imagem!;
        final extension = path.split('.').last.toLowerCase();
        final type = switch (extension) {
          'png' => 'image/png',
          'webp' => 'image/webp',
          'gif' => 'image/gif',
          'heic' || 'heif' => 'image/heic',
          'mov' => 'video/quicktime',
          'mp4' || 'm4v' => 'video/mp4',
          _ => post.temVideo ? 'video/mp4' : 'image/jpeg',
        };
        final uploaded = await subirArquivo(File(path), type, codigo);
        if (uploaded.erro != null) return uploaded.erro;
        if (uploaded.url == null) {
          return 'O mural não devolveu o endereço da mídia.';
        }
        post = post.copyWith(imagem: uploaded.url, imagemLocal: false);
        await publicar(post);
      }
      final req = await _http.postUrl(Uri.parse('$endereco/post'));
      req.headers.set('content-type', 'application/json; charset=utf-8');
      req.headers.set('authorization', 'Bearer $codigo');
      req.add(
        utf8.encode(
          jsonEncode({
            ...post.toJson(),
            'respondeA': ?respondeA,
            'repostaDe': ?repostaDe,
          }),
        ),
      );
      final res = await req.close().timeout(_tempoLimite);
      final corpo = await res.transform(utf8.decoder).join();
      if (res.statusCode == 201) {
        // O ID QUEM DA E O SERVIDOR. Guardar o dele aqui e o que faz a
        // copia local sumir quando a publicada chegar no feed.
        try {
          final m = (jsonDecode(corpo) as Map).cast<String, dynamic>();
          final publicado = PostDaComunidade.deJson(m['post']);
          if (publicado != null && publicado.id != post.id) {
            await _trocarId(post.id, publicado.id);
          }
        } catch (_) {}
        return null;
      }
      try {
        final m = (jsonDecode(corpo) as Map).cast<String, dynamic>();
        final erro = m['erro'];
        if (erro is String && erro.isNotEmpty) return erro;
      } catch (_) {}
      return 'O mural recusou o post (${res.statusCode}).';
    } on SocketException {
      return 'Sem conexão. O post ficou guardado aqui e você pode '
          'tentar de novo.';
    } catch (_) {
      return 'Não consegui falar com o mural agora. O post ficou '
          'guardado aqui.';
    }
  }

  /// APAGA NO SERVIDOR um post que e meu. Devolve o motivo, ou null.
  Future<String?> apagarNoServidor(String postId, String codigo) async {
    try {
      final req = await _http.deleteUrl(
        Uri.parse('$endereco/post/${Uri.encodeComponent(postId)}'),
      );
      req.headers.set('authorization', 'Bearer $codigo');
      final res = await req.close().timeout(_tempoLimite);
      await res.drain<void>();
      // 404 conta como sucesso: se ele ja nao esta la, o pedido esta
      // atendido, e dizer "nao achei" so confundiria quem apertou apagar.
      if (res.statusCode == 200 || res.statusCode == 404) {
        _feed = [
          for (final p in _feed ?? const <PostDaComunidade>[])
            if (p.id != postId) p,
        ];
        revisao.value++;
        return null;
      }
      if (res.statusCode == 401) return 'Esse post não é seu.';
      return 'Não consegui apagar agora (${res.statusCode}).';
    } catch (e) {
      return erroDeRede(e);
    }
  }

  /// SOBE UM ARQUIVO e devolve o endereco dele no mural.
  ///
  /// Imagem, video e projeto passam pelo mesmo caminho porque, para o
  /// servidor, os tres sao a mesma coisa: bytes com um tipo declarado. E
  /// ele quem decide onde cada um mora.
  Future<ArquivoNoMural> subirArquivo(
    File arquivo,
    String tipo,
    String codigo,
  ) async {
    try {
      final bytes = await arquivo.readAsBytes();
      final req = await _http.postUrl(Uri.parse('$endereco/midia'));
      req.headers.set('content-type', tipo);
      req.headers.set('authorization', 'Bearer $codigo');
      // O SERVIDOR RECUSA PELO TAMANHO ANTES DE LER. Sem este cabecalho
      // ele so descobriria que o arquivo e grande demais depois de
      // receber tudo — e a pessoa teria gastado a internet a toa.
      req.headers.contentLength = bytes.length;
      req.add(bytes);
      final res = await req.close().timeout(_tempoDeArquivo);
      final corpo = await res.transform(utf8.decoder).join();
      if (res.statusCode == 201) {
        final m = (jsonDecode(corpo) as Map).cast<String, dynamic>();
        final url = m['url'];
        if (url is String && url.startsWith('http')) {
          return ArquivoNoMural(url: url);
        }
        return const ArquivoNoMural(erro: 'O mural respondeu sem endereço.');
      }
      try {
        final m = (jsonDecode(corpo) as Map).cast<String, dynamic>();
        final erro = m['erro'];
        if (erro is String && erro.isNotEmpty) {
          return ArquivoNoMural(erro: erro);
        }
      } catch (_) {}
      return ArquivoNoMural(
        erro: 'O mural recusou o arquivo (${res.statusCode}).',
      );
    } catch (e) {
      return ArquivoNoMural(erro: erroDeRede(e));
    }
  }

  /// Traz um arquivo do mural para um caminho no aparelho. Usado para
  /// abrir um projeto que alguem publicou.
  Future<File?> baixarArquivo(String url, String nome) async {
    try {
      final req = await _http.getUrl(Uri.parse(url));
      final res = await req.close().timeout(_tempoDeArquivo);
      if (res.statusCode != 200) return null;
      final destino = _arquivo(await _pasta(), nome);
      final bytes = <int>[];
      await for (final parte in res) {
        bytes.addAll(parte);
      }
      await destino.writeAsBytes(bytes, flush: true);
      return destino;
    } catch (_) {
      return null;
    }
  }

  // =========================================================== os meus

  /// Os posts escritos neste aparelho.
  Future<List<PostDaComunidade>> meusPosts() async {
    if (_meus != null) return _meus!;
    try {
      final f = _arquivo(await _pasta(), 'meus.json');
      _meus = f.existsSync() ? lerFeed(await f.readAsString()) : [];
    } catch (_) {
      _meus = [];
    }
    return _meus!;
  }

  Future<void> _gravarMeus() async {
    try {
      final f = _arquivo(await _pasta(), 'meus.json');
      await f.writeAsString(escreverFeed(_meus ?? const []), flush: true);
    } catch (_) {}
    revisao.value++;
  }

  Future<void> _trocarId(String antigo, String novo) async {
    _meus = [
      for (final p in await meusPosts())
        p.id == antigo ? p.copyWith(id: novo, estado: EstadoDoPost.enviado) : p,
    ];
    await _gravarMeus();
  }

  Future<void> publicar(PostDaComunidade post) async {
    final lista = [...await meusPosts()];
    lista.removeWhere((p) => p.id == post.id);
    lista.insert(0, post);
    _meus = lista;
    await _gravarMeus();
  }

  Future<void> apagar(String id) async {
    final lista = [...await meusPosts()]..removeWhere((p) => p.id == id);
    _meus = lista;
    await _gravarMeus();
  }

  Future<void> marcarEnviado(String id) async {
    final lista = [
      for (final p in await meusPosts())
        p.id == id ? p.copyWith(estado: EstadoDoPost.enviado) : p,
    ];
    _meus = lista;
    await _gravarMeus();
  }

  /// O post no formato em que ele entra no feed — usado pelo caminho de
  /// reserva, quando o servidor nao responde.
  String comoJson(PostDaComunidade post) =>
      const JsonEncoder.withIndent('  ').convert(post.toJson());
}

/// O que volta de um envio de arquivo: o endereco, ou o motivo.
class ArquivoNoMural {
  const ArquivoNoMural({this.url, this.erro});

  final String? url;
  final String? erro;

  bool get deuCerto => url != null;
}
