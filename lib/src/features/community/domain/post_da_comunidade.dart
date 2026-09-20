import 'dart:convert';

import 'moderacao.dart';

/// UM POST DA COMUNIDADE.
///
/// O que a comunidade de um app de edicao precisa carregar e pouco: quem
/// fez, o que escreveu, uma imagem do trabalho e quando. Tudo o mais
/// (curtidas, comentarios, seguidores) e infraestrutura social que so
/// faz sentido depois que ha gente postando — e ainda nao ha.
class PostDaComunidade {
  const PostDaComunidade({
    required this.id,
    required this.autor,
    required this.texto,
    required this.quando,
    this.imagem,
    this.imagemLocal = false,
    this.tipoDeMidia = TipoDeMidia.imagem,
    this.duracaoDaMidia,
    this.autorId,
    this.respondeA,
    this.repostaDe,
    this.original,
    this.nomeDoProjeto,
    this.link,
    this.etiquetas = const [],
    this.estado = EstadoDoPost.publicado,
    this.perfil,
    this.curtidas = 0,
    this.curtiu = false,
  });

  final String id;
  final Map<String, dynamic>? perfil;
  final int curtidas;
  final bool curtiu;
  final String autor;
  final String texto;
  final DateTime quando;

  /// Endereco da imagem: uma URL quando veio do feed, um caminho de
  /// arquivo quando e um post ainda nao publicado.
  final String? imagem;
  final bool imagemLocal;

  /// Imagem ou video. O campo do endereco continua sendo [imagem] para
  /// que um feed escrito antes disto continue valendo: la, tudo era
  /// imagem, e e isso que o padrao diz.
  final TipoDeMidia tipoDeMidia;

  /// Duracao do video, quando se sabe. Mostrada no canto da miniatura —
  /// e o que decide se alguem toca no play.
  final Duration? duracaoDaMidia;

  bool get temVideo => imagem != null && tipoDeMidia == TipoDeMidia.video;

  bool get temProjeto => imagem != null && tipoDeMidia == TipoDeMidia.projeto;

  /// Quem escreveu, do jeito que o SERVIDOR sabe. O apelido pode mudar;
  /// este nao.
  final String? autorId;

  /// De qual post isto e resposta.
  final String? respondeA;

  /// Qual post isto reposta.
  final String? repostaDe;

  /// UMA COPIA DO ORIGINAL, guardada no momento do repost.
  ///
  /// O feed devolve duzentos posts; o original pode ser mais antigo que
  /// isso, e ai o cartao apareceria vazio. Com a copia, o repost continua
  /// legivel para sempre — inclusive se o original for apagado depois,
  /// que e o que se espera de uma citacao.
  final PostDaComunidade? original;

  /// O nome do projeto anexado, para o cartao dizer o que se vai abrir.
  final String? nomeDoProjeto;

  bool get ehResposta => respondeA != null;
  bool get ehRepost => repostaDe != null;

  /// Link opcional (um video publicado, um perfil).
  final String? link;

  final List<String> etiquetas;
  final EstadoDoPost estado;

  bool get meu => estado != EstadoDoPost.publicado;

  /// "agora", "há 3 h", "há 2 d" — data cheia so acima de uma semana.
  ///
  /// Quem abre uma comunidade quer saber se a coisa esta viva, e "há 3 h"
  /// responde isso; "07/09/2026 14:12" faz a pessoa calcular.
  String quandoEmPalavras([DateTime? agora]) {
    final d = (agora ?? DateTime.now()).difference(quando);
    if (d.inMinutes < 1) return 'agora';
    if (d.inMinutes < 60) return 'há ${d.inMinutes} min';
    if (d.inHours < 24) return 'há ${d.inHours} h';
    if (d.inDays < 7) return 'há ${d.inDays} d';
    final dd = quando.day.toString().padLeft(2, '0');
    final mm = quando.month.toString().padLeft(2, '0');
    return '$dd/$mm/${quando.year}';
  }

  Map<String, dynamic> toJson() => {
    'id': id,
    if (perfil != null) 'perfil': perfil,
    'curtidas': curtidas,
    'curtiu': curtiu,
    'autor': autor,
    'texto': texto,
    'quando': quando.toUtc().toIso8601String(),
    if (imagem != null) 'imagem': imagem,
    if (imagemLocal) 'imagemLocal': true,
    if (tipoDeMidia != TipoDeMidia.imagem) 'midia': tipoDeMidia.name,
    if (duracaoDaMidia != null) 'duracao': duracaoDaMidia!.inMilliseconds,
    if (link != null) 'link': link,
    if (etiquetas.isNotEmpty) 'etiquetas': etiquetas,
    if (estado != EstadoDoPost.publicado) 'estado': estado.name,
    if (autorId != null) 'autorId': autorId,
    if (respondeA != null) 'respondeA': respondeA,
    if (repostaDe != null) 'repostaDe': repostaDe,
    if (original != null) 'original': original!.toJson(),
    if (nomeDoProjeto != null) 'nomeDoProjeto': nomeDoProjeto,
  };

  /// Um post do feed. Devolve null quando falta o essencial — um item
  /// estragado no meio do arquivo nao pode derrubar o feed inteiro.
  static PostDaComunidade? deJson(Object? bruto) {
    if (bruto is! Map) return null;
    final m = bruto.cast<String, dynamic>();
    final texto = m['texto'];
    if (texto is! String) return null;
    // TEXTO VAZIO SO VALE NO REPOST. Repostar sem comentar e o uso
    // normal; em qualquer outro caso, um post sem texto e lixo de
    // arquivo estragado e nao deve aparecer.
    if (texto.trim().isEmpty && m['repostaDe'] is! String) return null;
    DateTime quando;
    try {
      quando = DateTime.parse('${m['quando']}').toLocal();
    } catch (_) {
      quando = DateTime.now();
    }
    return PostDaComunidade(
      id: '${m['id'] ?? quando.microsecondsSinceEpoch}',
      perfil: (m['perfil'] as Map?)?.cast<String, dynamic>(),
      curtidas: (m['curtidas'] as num?)?.toInt() ?? 0,
      curtiu: m['curtiu'] == true,
      autor: '${m['autor'] ?? 'Anônimo'}',
      texto: texto,
      quando: quando,
      imagem: m['imagem'] as String?,
      imagemLocal: m['imagemLocal'] == true,
      tipoDeMidia: TipoDeMidia.values.firstWhere(
        (t) => t.name == m['midia'],
        orElse: () => TipoDeMidia.imagem,
      ),
      duracaoDaMidia: m['duracao'] is num
          ? Duration(milliseconds: (m['duracao'] as num).toInt())
          : null,
      link: m['link'] as String?,
      etiquetas: [for (final e in (m['etiquetas'] as List? ?? const [])) '$e'],
      estado: EstadoDoPost.values.firstWhere(
        (e) => e.name == m['estado'],
        orElse: () => EstadoDoPost.publicado,
      ),
      autorId: m['autorId'] as String?,
      respondeA: m['respondeA'] as String?,
      repostaDe: m['repostaDe'] as String?,
      original: m['original'] == null
          ? null
          : PostDaComunidade.deJson(m['original']),
      nomeDoProjeto: m['nomeDoProjeto'] as String?,
    );
  }

  PostDaComunidade copyWith({
    EstadoDoPost? estado,
    String? id,
    String? imagem,
    bool? imagemLocal,
  }) => PostDaComunidade(
    id: id ?? this.id,
    perfil: perfil,
    curtidas: curtidas,
    curtiu: curtiu,
    autor: autor,
    texto: texto,
    quando: quando,
    imagem: imagem ?? this.imagem,
    imagemLocal: imagemLocal ?? this.imagemLocal,
    tipoDeMidia: tipoDeMidia,
    duracaoDaMidia: duracaoDaMidia,
    link: link,
    etiquetas: etiquetas,
    estado: estado ?? this.estado,
    autorId: autorId,
    respondeA: respondeA,
    repostaDe: repostaDe,
    original: original,
    nomeDoProjeto: nomeDoProjeto,
  );
}

/// O que esta anexado ao post.
///
/// Um post carrega NO MAXIMO UMA midia. Nao e limitacao tecnica: um
/// mural de trabalho e sobre mostrar UMA coisa bem feita, e uma galeria
/// dentro do cartao rouba a leitura do que a pessoa escreveu.
enum TipoDeMidia { imagem, video, projeto }

enum EstadoDoPost {
  /// Esta no feed que todo mundo ve.
  publicado,

  /// Escrito aqui e ainda nao enviado.
  rascunho,

  /// Enviado para publicacao, esperando entrar no feed.
  enviado,
}

/// LE UM FEED INTEIRO.
///
/// Aceita as duas formas que um arquivo de feed costuma ter: a lista
/// direta e o objeto com a lista dentro de `posts`. Custa tres linhas e
/// evita que uma escolha de formato do servidor quebre o app.
List<PostDaComunidade> lerFeed(String fonte) {
  try {
    final bruto = jsonDecode(fonte);
    final lista = bruto is List
        ? bruto
        : (bruto is Map ? bruto['posts'] as List? ?? const [] : const []);
    final posts = <PostDaComunidade>[];
    for (final item in lista) {
      final p = PostDaComunidade.deJson(item);
      if (p == null) continue;
      // O FILTRO TAMBEM VALE PARA O QUE CHEGA.
      //
      // O de antes de publicar protege o mural de quem escreve daqui.
      // Este protege quem le: o feed vem de fora, e um dia vem com coisa
      // que nao passou por este app. Post reprovado nao aparece, e
      // ninguem precisa saber que ele existiu.
      if (!podeMostrar(p.texto, p.autor, permiteVazio: p.ehRepost)) continue;
      // No repost, o que aparece grande e o ORIGINAL: filtrar so o
      // comentario deixaria passar exatamente o que se quer barrar.
      final o = p.original;
      if (o != null && !podeMostrar(o.texto, o.autor)) continue;
      posts.add(p);
    }
    // O mais novo primeiro: e a ordem que uma comunidade tem.
    posts.sort((a, b) => b.quando.compareTo(a.quando));
    return posts;
  } catch (_) {
    return const [];
  }
}

String escreverFeed(List<PostDaComunidade> posts) => jsonEncode({
  'posts': [for (final p in posts) p.toJson()],
});
