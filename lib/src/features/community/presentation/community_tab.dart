import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:io';
import 'package:aurea/src/core/theme/aurea_colors.dart';

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:image_picker/image_picker.dart';
import 'package:path_provider/path_provider.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:uuid/uuid.dart';
import 'package:video_player/video_player.dart';

import '../../../core/theme/app_theme.dart';
import '../../../core/ui/snack.dart';
import '../../about/presentation/report_sheet.dart' show AureaAutor;
import '../../editor/application/editor_controller.dart';
import '../../editor/domain/template_pack.dart';
import '../../editor/domain/video_project.dart';
import '../../editor/presentation/editor_screen.dart';
import '../../projects/application/projects_controller.dart';
import '../application/comunidade_service.dart';
import '../application/conta_da_comunidade.dart';
import '../domain/moderacao.dart';
import '../domain/post_da_comunidade.dart';

/// A ABA COMUNIDADE — o mural do beta.
///
/// Tres coisas fazem um mural publico funcionar, e as tres estao aqui:
/// uma CONTA (quem assina), um FILTRO (o que entra) e MIDIA (o que se
/// mostra num app de video). Nada de curtida, seguidor ou notificacao:
/// isso e infraestrutura social, e so faz sentido depois que ha gente
/// postando.
class CommunityTab extends ConsumerStatefulWidget {
  const CommunityTab({super.key});

  @override
  ConsumerState<CommunityTab> createState() => _CommunityTabState();
}

class _CommunityTabState extends ConsumerState<CommunityTab> {
  List<PostDaComunidade> _posts = const [];
  bool _carregando = true;

  ComunidadeService get _s => ref.read(comunidadeServiceProvider);

  @override
  void initState() {
    super.initState();
    _atualizar(daRede: true);
  }

  Future<void> _atualizar({required bool daRede}) async {
    if (mounted) setState(() => _carregando = true);
    final posts = await _s.carregar(daRede: daRede);
    if (!mounted) return;
    setState(() {
      _posts = posts;
      _carregando = false;
    });
  }

  /// Sem conta nao se publica — e a conta se cria em um passo, aqui
  /// mesmo, em vez de mandar a pessoa procurar outra aba.
  Future<bool> _garantirConta() async {
    if (ref.read(contaDaComunidadeProvider) != null) return true;
    final criou = await showModalBottomSheet<bool>(
      context: context,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      builder: (_) => const _FolhaDaConta(),
    );
    return criou == true && ref.read(contaDaComunidadeProvider) != null;
  }

  Future<void> _compor({
    PostDaComunidade? responderA,
    PostDaComunidade? repostar,
  }) async {
    if (!await _garantirConta()) return;
    if (!mounted) return;
    final conta = ref.read(contaDaComunidadeProvider)!;
    final post = await showModalBottomSheet<PostDaComunidade>(
      context: context,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      builder: (_) =>
          _Compositor(conta: conta, responderA: responderA, repostar: repostar),
    );
    if (post == null) return;
    await _publicar(post, respondeA: responderA?.id, repostaDe: repostar?.id);
  }

  /// PUBLICA DE VERDADE: guarda aqui, sobe o arquivo, manda o post.
  ///
  /// GUARDA PRIMEIRO, MANDA DEPOIS. Nesta ordem o post nunca se perde: se
  /// a rede cair no meio, ele ficou aqui e da para tentar de novo. Na
  /// ordem contraria, uma falha apagaria o que a pessoa escreveu.
  Future<void> _publicar(
    PostDaComunidade post, {
    String? respondeA,
    String? repostaDe,
  }) async {
    final conta = ref.read(contaDaComunidadeProvider);
    if (conta == null) return;
    await _s.publicar(post);
    await _atualizar(daRede: false);
    if (!mounted) return;

    // O ARQUIVO SOBE ANTES DO POST, e nao junto.
    //
    // O post e um JSON de alguns kilobytes; a foto tem megabytes. Mandar
    // os dois na mesma requisicao faria o mural recusar o post inteiro
    // por causa do tamanho — e obrigaria a escrever tudo de novo.
    var paraOServidor = post;
    if (post.imagem != null && post.imagemLocal) {
      AureaSnack.show(
        context,
        post.temProjeto ? 'Enviando o projeto...' : 'Enviando o arquivo...',
        duration: const Duration(seconds: 20),
      );
      final r = await _s.subirArquivo(
        File(post.imagem!),
        tipoDoArquivo(post.imagem!, post.tipoDeMidia),
        conta.codigo,
      );
      if (!mounted) return;
      if (!r.deuCerto) {
        // O post CONTINUA AQUI como rascunho. Quando o arquivo nao sobe,
        // publicar so o texto entregaria um post que fala de uma imagem
        // que ninguem vai ver.
        AureaSnack.show(
          context,
          '${r.erro} O post ficou guardado — dá para tentar de novo.',
          duration: const Duration(seconds: 7),
        );
        return;
      }
      paraOServidor = post.copyWith(imagem: r.url, imagemLocal: false);
    }

    AureaSnack.show(context, 'Publicando...');
    final erro = await _s.enviar(
      paraOServidor,
      conta.codigo,
      respondeA: respondeA,
      repostaDe: repostaDe,
    );
    if (!mounted) return;
    if (erro != null) {
      // O servidor recusou (ofensa, dado pessoal, limite) ou nao
      // respondeu. Nos dois casos o post continua aqui, marcado, e o
      // motivo e dito com as palavras dele.
      AureaSnack.show(context, erro, duration: const Duration(seconds: 6));
      return;
    }
    await _s.marcarEnviado(post.id);
    await _atualizar(daRede: true);
    if (!mounted) return;
    AureaSnack.show(
      context,
      respondeA != null
          ? 'Respondido.'
          : 'No mural. Pode levar um minuto para aparecer para os outros.',
      duration: const Duration(seconds: 5),
    );
  }

  /// REPOSTAR sem escrever nada e o caso comum — por isso a folha abre
  /// com o texto vazio ja valendo como publicacao.
  Future<void> _repostar(PostDaComunidade post) =>
      _compor(repostar: post.ehRepost ? (post.original ?? post) : post);

  Future<void> _abrirConversa(PostDaComunidade post) async {
    await Navigator.of(context).push(
      MaterialPageRoute<void>(
        builder: (_) => _Conversa(
          post: post,
          aoResponder: () => _compor(responderA: post),
        ),
      ),
    );
    if (mounted) await _atualizar(daRede: true);
  }

  /// ABRE UM PROJETO PUBLICADO como projeto novo deste aparelho.
  ///
  /// Id novo, sempre. Sem isso, abrir o mesmo projeto do mural duas vezes
  /// sobrescreveria o que a pessoa fez na primeira — e ela perderia o
  /// trabalho sem entender por que.
  Future<void> _abrirProjeto(PostDaComunidade post) async {
    final url = post.imagem;
    if (url == null) return;
    AureaSnack.show(context, 'Baixando o projeto...');
    final arquivo = await _s.baixarArquivo(url, 'projeto-${post.id}.json');
    if (!mounted) return;
    if (arquivo == null) {
      AureaSnack.show(context, 'Não consegui baixar esse projeto.');
      return;
    }
    TemplatePack? pack;
    try {
      pack = TemplatePack.decode(await arquivo.readAsString());
    } catch (_) {
      pack = null;
    }
    if (!mounted) return;
    if (pack == null) {
      AureaSnack.show(context, 'Esse arquivo não é um projeto do Aurea.');
      return;
    }
    final novo = pack.project.copyWith(name: pack.name).comIdNovo();
    ref.read(projectsControllerProvider.notifier).add(novo);
    ref.read(editorControllerProvider.notifier).openProject(novo);
    await Navigator.of(context)
        .push(MaterialPageRoute<void>(builder: (_) => const EditorScreen()));
  }

  /// TENTAR DE NOVO um post que ficou para tras.
  ///
  /// Serve para os dois casos em que ele fica: a rede caiu na hora de
  /// publicar, ou o servidor recusou e a pessoa corrigiu o texto por
  /// fora. Se falhar outra vez, cai no e-mail — um post escrito e um
  /// trabalho, e trabalho nao se joga fora por falta de sinal.
  Future<void> _reenviar(PostDaComunidade post) async {
    final conta = ref.read(contaDaComunidadeProvider);
    if (conta == null) return;
    AureaSnack.show(context, 'Publicando...');
    final erro = await _s.enviar(post, conta.codigo);
    if (!mounted) return;
    if (erro == null) {
      await _s.marcarEnviado(post.id);
      await _atualizar(daRede: true);
      if (!mounted) return;
      AureaSnack.show(context, 'No mural.');
      return;
    }
    AureaSnack.show(
      context,
      '$erro Mandando por e-mail.',
      duration: const Duration(seconds: 5),
    );
    await _enviarPorEmail(post);
  }

  Future<void> _enviarPorEmail(PostDaComunidade post) async {
    final corpo = StringBuffer()
      ..writeln('Post para a comunidade do Aurea.')
      ..writeln()
      ..writeln(post.texto)
      ..writeln()
      ..writeln('--- para o feed ---')
      ..writeln(_s.comoJson(post));
    if (post.imagem != null && post.imagemLocal) {
      corpo
        ..writeln()
        ..writeln(
          post.temVideo
              ? 'O vídeo está no aparelho: ${post.imagem}'
              : 'A imagem está no aparelho: ${post.imagem}',
        )
        ..writeln('(anexe o arquivo ao e-mail)');
    }
    final uri = Uri(
      scheme: 'mailto',
      path: AureaAutor.email,
      queryParameters: {
        'subject': 'Comunidade Aurea · ${post.autor}',
        'body': corpo.toString(),
      },
    );
    var abriu = false;
    try {
      abriu = await launchUrl(uri, mode: LaunchMode.externalApplication);
    } catch (_) {
      abriu = false;
    }
    await _atualizar(daRede: false);
    if (!mounted) return;
    if (!abriu) {
      await Clipboard.setData(ClipboardData(text: corpo.toString()));
      if (!mounted) return;
      AureaSnack.show(
        context,
        'Sem app de e-mail. Copiei o post — mande para ${AureaAutor.email}',
        duration: const Duration(seconds: 6),
      );
      return;
    }
    AureaSnack.show(context, 'Abrindo seu app de e-mail');
  }

  /// APAGA nos dois lugares: aqui e, se ja tiver entrado, no mural.
  ///
  /// A copia local sai primeiro. Se a rede falhar, o post ja sumiu da
  /// tela de quem pediu para apagar — que e o que ele espera — e o mural
  /// continua com a versao dele ate a proxima tentativa.
  Future<void> _apagar(PostDaComunidade post) async {
    await _s.apagar(post.id);
    await _atualizar(daRede: false);
    final conta = ref.read(contaDaComunidadeProvider);
    if (conta == null || post.estado == EstadoDoPost.rascunho) return;
    final erro = await _s.apagarNoServidor(post.id, conta.codigo);
    if (!mounted) return;
    if (erro != null) {
      AureaSnack.show(context, 'Saiu daqui, mas não do mural: $erro');
      return;
    }
    await _atualizar(daRede: true);
  }

  /// Apagar um post que ja esta no mural, tocado no cartao de outra tela.
  Future<void> _apagarPublicado(PostDaComunidade post) async {
    final conta = ref.read(contaDaComunidadeProvider);
    if (conta == null) return;
    final erro = await _s.apagarNoServidor(post.id, conta.codigo);
    if (!mounted) return;
    if (erro != null) {
      AureaSnack.show(context, erro);
      return;
    }
    await _atualizar(daRede: true);
  }

  /// O post e meu quando a conta deste aparelho o assinou. O `autorId`
  /// vem do servidor: comparar apelidos deixaria qualquer um apagar o
  /// post de outro so trocando o proprio nome.
  bool _euEscrevi(PostDaComunidade post) {
    final conta = ref.read(contaDaComunidadeProvider);
    return conta != null && post.autorId != null && post.autorId == conta.id;
  }

  /// Autor que a fila de cima esta filtrando (id do servidor, ou o apelido
  /// quando o post nao tem id). Nulo = todo mundo.
  String? _filtroAutor;

  static String _chaveDoAutor(PostDaComunidade p) => p.autorId ?? p.autor;

  Future<void> _abrirConta() async {
    await showModalBottomSheet<bool>(
      context: context,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      builder: (_) => const _FolhaDaConta(),
    );
    if (mounted) setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    final conta = ref.watch(contaDaComunidadeProvider);
    // A FILA DE CRIADORES (como os stories): quem postou, uma vez cada,
    // na ordem do feed. Tocar filtra o feed por aquela pessoa.
    final autores = <String, PostDaComunidade>{};
    for (final p in _posts) {
      if (p.meu) continue;
      autores.putIfAbsent(_chaveDoAutor(p), () => p);
    }
    final filtro = _filtroAutor;
    final visiveis = filtro == null
        ? _posts
        : _posts.where((p) => _chaveDoAutor(p) == filtro).toList();

    return SafeArea(
      bottom: false,
      child: RefreshIndicator(
        onRefresh: () => _atualizar(daRede: true),
        color: AppColors.lime,
        backgroundColor: AppColors.surface,
        child: ListView(
          padding: const EdgeInsets.fromLTRB(0, 6, 0, 120),
          physics: const AlwaysScrollableScrollPhysics(),
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(20, 8, 12, 4),
              child: Row(
                children: [
                  Expanded(
                    child: AppText(
                      'Comunidade',
                      style: TextStyle(
                        fontSize: 28,
                        fontWeight: FontWeight.w800,
                        letterSpacing: -0.7,
                        color: AppColors.onDark,
                      ),
                    ),
                  ),
                  _BotaoRedondoDaComunidade(
                    chave: 'comunidade-publicar',
                    icone: CupertinoIcons.plus_app,
                    dica: translate(context, 'Publicar'),
                    onTap: _compor,
                  ),
                  GestureDetector(
                    key: const ValueKey('comunidade-conta'),
                    behavior: HitTestBehavior.opaque,
                    onTap: _abrirConta,
                    child: SizedBox(
                      width: 44,
                      height: 44,
                      child: Center(
                        child: _Avatar(
                          nome: conta?.apelido ?? '?',
                          arquivo: conta?.avatar,
                          raio: 16,
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
            SizedBox(
              height: 102,
              child: ListView(
                scrollDirection: Axis.horizontal,
                padding: const EdgeInsets.symmetric(horizontal: 14),
                children: [
                  _Criador(
                    nome: conta == null ? 'Criar conta' : 'Seu post',
                    avatarNome: conta?.apelido ?? '?',
                    arquivo: conta?.avatar,
                    mais: true,
                    onTap: conta == null ? _abrirConta : _compor,
                  ),
                  for (final e in autores.entries)
                    _Criador(
                      chave: 'criador-${e.key}',
                      nome: e.value.autor,
                      avatarNome: e.value.autor,
                      selecionado: filtro == e.key,
                      onTap: () => setState(
                        () => _filtroAutor = filtro == e.key ? null : e.key,
                      ),
                    ),
                ],
              ),
            ),
            if (filtro != null)
              Padding(
                padding: const EdgeInsets.fromLTRB(20, 0, 8, 4),
                child: Row(
                  children: [
                    Expanded(
                      child: Text(
                        '${translate(context, 'Posts de')} ${autores[filtro]?.autor ?? ''}',
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(fontSize: 13, color: AppColors.muted),
                      ),
                    ),
                    TextButton(
                      key: const ValueKey('comunidade-ver-todos'),
                      onPressed: () => setState(() => _filtroAutor = null),
                      child: const AppText('Ver todos'),
                    ),
                  ],
                ),
              ),
            Container(height: 0.5, color: AppColors.hairline),
            const SizedBox(height: 10),
            if (_s.ultimoErro != null) ...[
              Padding(
                padding: const EdgeInsets.symmetric(horizontal: 20),
                child: _Aviso(
                  icone: CupertinoIcons.wifi_slash,
                  texto:
                      '${_s.ultimoErro} O que já tinha sido lido continua aqui.',
                ),
              ),
              const SizedBox(height: 14),
            ],
            if (_carregando && _posts.isEmpty)
              const Padding(
                padding: EdgeInsets.symmetric(vertical: 40),
                child: Center(child: CupertinoActivityIndicator()),
              )
            else if (visiveis.isEmpty)
              const _Vazio()
            else
              for (final p in visiveis)
                Padding(
                  padding: const EdgeInsets.only(bottom: 22),
                  child: _Cartao(
                    post: p,
                    onEnviar: p.estado == EstadoDoPost.rascunho
                        ? () => _reenviar(p)
                        : null,
                    // Rascunho so apaga daqui; publicado tambem sai do
                    // mural, e so quem escreveu ve o botao.
                    onApagar: p.meu
                        ? () => _apagar(p)
                        : (_euEscrevi(p) ? () => _apagarPublicado(p) : null),
                    // O que ainda nao entrou no mural nao pode receber
                    // resposta nem repost: o post nao existe la.
                    onResponder: p.meu ? null : () => _compor(responderA: p),
                    onRepostar: p.meu ? null : () => _repostar(p),
                    onAbrir: p.meu ? null : () => _abrirConversa(p),
                    onProjeto: p.temProjeto && !p.imagemLocal
                        ? () => _abrirProjeto(p)
                        : null,
                  ),
                ),
          ],
        ),
      ),
    );
  }
}

/// Um botao redondo da barra de cima (publicar).
class _BotaoRedondoDaComunidade extends StatelessWidget {
  const _BotaoRedondoDaComunidade({
    required this.chave,
    required this.icone,
    required this.dica,
    required this.onTap,
  });

  final String chave;
  final IconData icone;
  final String dica;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Tooltip(
    message: dica,
    child: GestureDetector(
      key: ValueKey(chave),
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: SizedBox(
        width: 44,
        height: 44,
        child: Icon(icone, size: 25, color: AppColors.onDark),
      ),
    ),
  );
}

/// Uma bolinha da fila de criadores, com o anel colorido.
class _Criador extends StatelessWidget {
  const _Criador({
    this.chave,
    required this.nome,
    required this.avatarNome,
    this.arquivo,
    this.mais = false,
    this.selecionado = false,
    required this.onTap,
  });

  final String? chave;
  final String nome;
  final String avatarNome;
  final String? arquivo;
  final bool mais;
  final bool selecionado;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => GestureDetector(
    key: chave == null ? null : ValueKey<String>(chave!),
    behavior: HitTestBehavior.opaque,
    onTap: onTap,
    child: SizedBox(
      width: 76,
      child: Column(
        children: [
          const SizedBox(height: 6),
          Stack(
            clipBehavior: Clip.none,
            children: [
              Container(
                width: 64,
                height: 64,
                padding: const EdgeInsets.all(2.5),
                decoration: BoxDecoration(
                  shape: BoxShape.circle,
                  gradient: mais
                      ? null
                      : LinearGradient(
                          begin: Alignment.bottomLeft,
                          end: Alignment.topRight,
                          colors: selecionado
                              ? [AppColors.lime, AppColors.lime]
                              : [AppColors.lime, AppColors.violet],
                        ),
                  color: mais ? AppColors.surfaceHigh : null,
                ),
                child: Container(
                  padding: const EdgeInsets.all(2.5),
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: AppColors.background,
                  ),
                  child: _Avatar(nome: avatarNome, arquivo: arquivo, raio: 26),
                ),
              ),
              if (mais)
                Positioned(
                  right: -1,
                  bottom: -1,
                  child: Container(
                    width: 22,
                    height: 22,
                    decoration: BoxDecoration(
                      color: AppColors.lime,
                      shape: BoxShape.circle,
                      border: Border.all(color: AppColors.background, width: 2),
                    ),
                    child: const Icon(
                      CupertinoIcons.plus,
                      size: 12,
                      color: AureaColors.onAccent,
                    ),
                  ),
                ),
            ],
          ),
          const SizedBox(height: 6),
          // Apelido e da pessoa: Text. "Seu post"/"Criar conta" traduzem.
          mais
              ? AppText(
                  nome,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(fontSize: 11.5, color: AppColors.muted),
                )
              : Text(
                  nome,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: 11.5,
                    color: selecionado ? AppColors.lime : AppColors.onDark,
                  ),
                ),
        ],
      ),
    ),
  );
}

class _Avatar extends StatelessWidget {
  const _Avatar({required this.nome, this.arquivo, this.raio = 15});

  final String nome;
  final String? arquivo;
  final double raio;

  @override
  Widget build(BuildContext context) {
    final f = arquivo;
    if (f != null && File(f).existsSync()) {
      return CircleAvatar(radius: raio, backgroundImage: FileImage(File(f)));
    }
    return CircleAvatar(
      radius: raio,
      backgroundColor: AppColors.violet,
      child: AppText(
        nome.trim().isEmpty ? 'A' : nome.trim()[0].toUpperCase(),
        style: TextStyle(
          fontSize: raio * 0.85,
          fontWeight: FontWeight.w700,
          color: Colors.white,
        ),
      ),
    );
  }
}

class _Cartao extends StatelessWidget {
  const _Cartao({
    required this.post,
    this.onEnviar,
    this.onApagar,
    this.onResponder,
    this.onRepostar,
    this.onAbrir,
    this.onProjeto,
    this.dentroDaConversa = false,
  });

  final PostDaComunidade post;
  final VoidCallback? onEnviar;
  final VoidCallback? onApagar;
  final VoidCallback? onResponder;
  final VoidCallback? onRepostar;
  final VoidCallback? onAbrir;
  final VoidCallback? onProjeto;

  /// Na conversa o cartao ja esta aberto: repetir "abrir" ali levaria a
  /// mesma tela de novo, empilhada.
  final bool dentroDaConversa;

  Future<void> _menu(BuildContext context) async {
    final apagar = await showCupertinoModalPopup<bool>(
      context: context,
      builder: (c) => CupertinoActionSheet(
        actions: [
          CupertinoActionSheetAction(
            key: ValueKey('post-apagar-confirma-${post.id}'),
            isDestructiveAction: true,
            onPressed: () => Navigator.of(c).pop(true),
            child: const AppText('Apagar'),
          ),
        ],
        cancelButton: CupertinoActionSheetAction(
          onPressed: () => Navigator.of(c).pop(false),
          child: const AppText('Cancelar'),
        ),
      ),
    );
    if (apagar == true) onApagar?.call();
  }

  @override
  Widget build(BuildContext context) {
    // O POST INTEIRO ABRE A CONVERSA, e nao so um botao pequeno.
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onAbrir,
      child: _corpo(context),
    );
  }

  Widget _corpo(BuildContext context) {
    final temTexto = post.texto.trim().isNotEmpty;
    return Column(
      key: ValueKey('post-${post.id}'),
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        if (post.ehRepost)
          Padding(
            padding: const EdgeInsets.fromLTRB(20, 0, 20, 6),
            child: Row(
              children: [
                Icon(
                  CupertinoIcons.arrow_2_squarepath,
                  size: 13,
                  color: AppColors.muted,
                ),
                const SizedBox(width: 6),
                AppText(
                  'Repostou',
                  style: TextStyle(
                    fontSize: 11.5,
                    fontWeight: FontWeight.w600,
                    color: AppColors.muted,
                  ),
                ),
              ],
            ),
          ),
        // CABECALHO: foto, nome, quando, e o menu.
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 0, 4, 8),
          child: Row(
            children: [
              _Avatar(nome: post.autor, raio: 17),
              const SizedBox(width: 10),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      post.autor,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(
                        fontSize: 14,
                        fontWeight: FontWeight.w700,
                        color: AppColors.onDark,
                      ),
                    ),
                    Text(
                      post.quandoEmPalavras(),
                      style: TextStyle(fontSize: 11.5, color: AppColors.muted),
                    ),
                  ],
                ),
              ),
              if (post.meu) _Selo(estado: post.estado),
              if (onApagar != null)
                GestureDetector(
                  key: ValueKey('post-apagar-${post.id}'),
                  behavior: HitTestBehavior.opaque,
                  onTap: () => _menu(context),
                  // A UNICA ACAO do post e apagar: a lixeira a vista, e nao
                  // tres pontinhos escondendo um item so (a confirmacao
                  // continua na folha).
                  child: SizedBox(
                    width: 44,
                    height: 44,
                    child: Icon(
                      CupertinoIcons.trash,
                      size: 17,
                      color: AppColors.muted,
                    ),
                  ),
                )
              else
                const SizedBox(width: 12),
            ],
          ),
        ),
        // A MIDIA NA LARGURA INTEIRA, como num feed de fotos.
        if (post.temProjeto)
          _CartaoDeProjeto(post: post, onAbrir: onProjeto)
        else if (post.imagem != null)
          _Midia(post: post),
        if (post.original != null)
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 4, 16, 0),
            child: _Citacao(original: post.original!),
          ),
        // A BARRA DE ACOES: so icones, alvos de 44 px.
        if (onResponder != null || onRepostar != null)
          _BarraDoCartao(
            post: post,
            onResponder: onResponder,
            onRepostar: onRepostar,
            onAbrir: dentroDaConversa ? null : onAbrir,
          )
        else
          const SizedBox(height: 8),
        if (onEnviar != null)
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 0, 16, 8),
            child: _AcaoDoCartao(
              chave: 'post-enviar-${post.id}',
              icone: CupertinoIcons.paperplane,
              rotulo: 'Tentar publicar de novo',
              destaque: true,
              onTap: onEnviar!,
            ),
          ),
        // LEGENDA: o nome em negrito e o texto na mesma linha.
        if (temTexto)
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 0, 16, 0),
            child: Text.rich(
              TextSpan(
                children: [
                  TextSpan(
                    text: post.autor,
                    style: const TextStyle(fontWeight: FontWeight.w700),
                  ),
                  const TextSpan(text: '  '),
                  TextSpan(text: post.texto),
                ],
              ),
              style: TextStyle(
                fontSize: 14,
                height: 1.4,
                color: AppColors.onDark,
              ),
            ),
          ),
        if (post.etiquetas.isNotEmpty)
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 4, 16, 0),
            child: Wrap(
              spacing: 8,
              children: [
                for (final e in post.etiquetas)
                  Text(
                    '#$e',
                    style: TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                      color: AppColors.lime,
                    ),
                  ),
              ],
            ),
          ),
        if (post.link != null)
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 6, 16, 0),
            child: GestureDetector(
              onTap: () => launchUrl(
                Uri.parse(post.link!),
                mode: LaunchMode.externalApplication,
              ),
              child: Row(
                children: [
                  Icon(CupertinoIcons.link, size: 14, color: AppColors.lime),
                  const SizedBox(width: 5),
                  Expanded(
                    child: Text(
                      post.link!,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: TextStyle(fontSize: 12.5, color: AppColors.lime),
                    ),
                  ),
                ],
              ),
            ),
          ),
        if (onAbrir != null && !dentroDaConversa)
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 6, 16, 0),
            child: AppText(
              'Ver respostas',
              style: TextStyle(fontSize: 13, color: AppColors.muted),
            ),
          ),
      ],
    );
  }
}

/// A BARRA DE ACOES: responder e repostar, so icones (como num feed de
/// fotos), com alvo de 44 px.
class _BarraDoCartao extends StatelessWidget {
  const _BarraDoCartao({
    required this.post,
    this.onResponder,
    this.onRepostar,
    this.onAbrir,
  });

  final PostDaComunidade post;
  final VoidCallback? onResponder;
  final VoidCallback? onRepostar;
  final VoidCallback? onAbrir;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(6, 2, 6, 0),
    child: Row(
      children: [
        if (onResponder != null)
          _BotaoDaBarra(
            chave: 'post-responder-${post.id}',
            icone: CupertinoIcons.chat_bubble,
            rotulo: 'Responder',
            onTap: onResponder!,
          ),
        if (onRepostar != null)
          _BotaoDaBarra(
            chave: 'post-repostar-${post.id}',
            icone: CupertinoIcons.arrow_2_squarepath,
            rotulo: 'Repostar',
            onTap: onRepostar!,
          ),
      ],
    ),
  );
}

class _BotaoDaBarra extends StatelessWidget {
  const _BotaoDaBarra({
    required this.chave,
    required this.icone,
    required this.rotulo,
    required this.onTap,
  });

  final String chave;
  final IconData icone;
  final String rotulo;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Semantics(
    button: true,
    label: translate(context, rotulo),
    child: GestureDetector(
      key: ValueKey(chave),
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: SizedBox(
        width: 44,
        height: 44,
        child: Icon(icone, size: 23, color: AppColors.onDark),
      ),
    ),
  );
}

/// O POST CITADO dentro de um repost.
///
/// Ele vem GUARDADO no proprio repost, e nao buscado de novo: o original
/// pode ser antigo demais para estar no feed, ou ter sido apagado. Uma
/// citacao que some quando o original some nao e uma citacao.
class _Citacao extends StatelessWidget {
  const _Citacao({required this.original});
  final PostDaComunidade original;

  @override
  Widget build(BuildContext context) => Container(
    key: ValueKey('citacao-${original.id}'),
    decoration: BoxDecoration(
      color: AppColors.surfaceHigh,
      borderRadius: BorderRadius.circular(12),
    ),
    clipBehavior: Clip.antiAlias,
    child: Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(12, 10, 12, 0),
          child: Row(
            children: [
              _Avatar(nome: original.autor, raio: 11),
              const SizedBox(width: 8),
              Flexible(
                child: Text(
                  original.autor,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    fontSize: 12.5,
                    fontWeight: FontWeight.w700,
                    color: AppColors.onDark,
                  ),
                ),
              ),
              const SizedBox(width: 8),
              AppText(
                original.quandoEmPalavras(),
                style: TextStyle(fontSize: 11, color: AppColors.muted),
              ),
            ],
          ),
        ),
        if (original.texto.trim().isNotEmpty)
          Padding(
            padding: const EdgeInsets.fromLTRB(12, 6, 12, 10),
            child: Text(original.texto,
              maxLines: 6,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 13,
                height: 1.35,
                color: AppColors.onDark,
              ),
            ),
          )
        else
          const SizedBox(height: 10),
        if (original.imagem != null && !original.temProjeto)
          _Midia(post: original),
      ],
    ),
  );
}

/// UM PROJETO PUBLICADO. O cartao diz o nome e abre como projeto novo.
class _CartaoDeProjeto extends StatelessWidget {
  const _CartaoDeProjeto({required this.post, this.onAbrir});

  final PostDaComunidade post;
  final VoidCallback? onAbrir;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(14, 0, 14, 12),
    child: GestureDetector(
      key: ValueKey('post-projeto-${post.id}'),
      behavior: HitTestBehavior.opaque,
      onTap: onAbrir,
      child: Container(
        padding: const EdgeInsets.all(12),
        decoration: BoxDecoration(
          color: AppColors.accentDim,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Row(
          children: [
            Icon(CupertinoIcons.cube_box, size: 22, color: AppColors.lime),
            const SizedBox(width: 12),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  AppText(
                    post.nomeDoProjeto ?? 'Projeto do Aurea',
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      fontSize: 13.5,
                      fontWeight: FontWeight.w700,
                      color: AppColors.onDark,
                    ),
                  ),
                  const SizedBox(height: 2),
                  AppText(
                    onAbrir == null
                        ? 'Anexado — vai subir junto com o post.'
                        : 'Toque para abrir como um projeto seu.',
                    style: TextStyle(fontSize: 11.5, color: AppColors.muted),
                  ),
                ],
              ),
            ),
            if (onAbrir != null)
              Icon(
                CupertinoIcons.arrow_down_circle,
                size: 18,
                color: AppColors.lime,
              ),
          ],
        ),
      ),
    ),
  );
}

/// A CONVERSA: o post em cima, as respostas embaixo.
///
/// As respostas SO SAO BUSCADAS AQUI. Traze-las junto com o feed seria
/// baixar as respostas de duzentos posts para ler as de um.
class _Conversa extends ConsumerStatefulWidget {
  const _Conversa({required this.post, required this.aoResponder});

  final PostDaComunidade post;
  final Future<void> Function() aoResponder;

  @override
  ConsumerState<_Conversa> createState() => _ConversaState();
}

class _ConversaState extends ConsumerState<_Conversa> {
  List<PostDaComunidade> _respostas = const [];
  bool _carregando = true;

  @override
  void initState() {
    super.initState();
    _buscar();
  }

  Future<void> _buscar() async {
    if (mounted) setState(() => _carregando = true);
    final r = await ref
        .read(comunidadeServiceProvider)
        .respostas(widget.post.id);
    if (!mounted) return;
    setState(() {
      _respostas = r;
      _carregando = false;
    });
  }

  @override
  Widget build(BuildContext context) => Scaffold(
    backgroundColor: AppColors.background,
    appBar: AppBar(
      backgroundColor: AppColors.background,
      elevation: 0,
      title: const AppText('Conversa'),
    ),
    body: RefreshIndicator(
      onRefresh: _buscar,
      color: AppColors.lime,
      backgroundColor: AppColors.surface,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(20, 8, 20, 40),
        physics: const AlwaysScrollableScrollPhysics(),
        children: [
          _Cartao(
            post: widget.post,
            dentroDaConversa: true,
            onResponder: () async {
              await widget.aoResponder();
              await _buscar();
            },
          ),
          const SizedBox(height: 18),
          AppText(
            _carregando
                ? 'Carregando as respostas...'
                : _respostas.isEmpty
                ? 'Ninguém respondeu ainda. Seja o primeiro.'
                : '${_respostas.length} '
                      '${_respostas.length == 1 ? "resposta" : "respostas"}',
            style: TextStyle(fontSize: 12.5, color: AppColors.muted),
          ),
          const SizedBox(height: 12),
          for (final r in _respostas)
            Padding(
              padding: const EdgeInsets.only(left: 14, bottom: 12),
              child: _Cartao(post: r, dentroDaConversa: true),
            ),
        ],
      ),
    ),
  );
}

/// O TIPO DO ARQUIVO PELO NOME. O servidor so aceita o que reconhece, e
/// e o nome do arquivo escolhido na galeria que diz o que ele e.
String tipoDoArquivo(String caminho, TipoDeMidia tipo) {
  final ext = caminho.toLowerCase().split('.').last;
  return switch (ext) {
    'png' => 'image/png',
    'webp' => 'image/webp',
    'mp4' => 'video/mp4',
    'mov' || 'qt' => 'video/quicktime',
    'json' || 'aurea' => 'application/json',
    _ => switch (tipo) {
      TipoDeMidia.video => 'video/mp4',
      TipoDeMidia.projeto => 'application/json',
      TipoDeMidia.imagem => 'image/jpeg',
    },
  };
}

/// A MIDIA DO POST: imagem direto, video com play.
///
/// O video so carrega quando alguem toca. Um mural com dez videos que se
/// inicializam sozinhos ocupa dez decodificadores e trava o aparelho —
/// e ninguem assiste dez videos de uma vez.
class _Midia extends StatefulWidget {
  const _Midia({required this.post});
  final PostDaComunidade post;

  @override
  State<_Midia> createState() => _MidiaState();
}

class _MidiaState extends State<_Midia> {
  VideoPlayerController? _player;
  bool _preparando = false;

  @override
  void dispose() {
    _player?.dispose();
    super.dispose();
  }

  Future<void> _tocar() async {
    final endereco = widget.post.imagem;
    if (endereco == null || _preparando) return;
    if (_player != null) {
      setState(() {
        _player!.value.isPlaying ? _player!.pause() : _player!.play();
      });
      return;
    }
    setState(() => _preparando = true);
    try {
      final c = widget.post.imagemLocal
          ? VideoPlayerController.file(File(endereco))
          : VideoPlayerController.networkUrl(Uri.parse(endereco));
      await c.initialize();
      await c.setLooping(true);
      await c.play();
      if (!mounted) {
        await c.dispose();
        return;
      }
      setState(() {
        _player = c;
        _preparando = false;
      });
    } catch (_) {
      if (mounted) setState(() => _preparando = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    final post = widget.post;
    final erro = Container(
      height: 120,
      color: AppColors.surfaceHigh,
      alignment: Alignment.center,
      child: Icon(CupertinoIcons.photo, color: AppColors.muted, size: 28),
    );

    if (!post.temVideo) {
      return ConstrainedBox(
        constraints: const BoxConstraints(maxHeight: 540),
        child: SizedBox(
          width: double.infinity,
          child: post.imagemLocal
              ? Image.file(
                  File(post.imagem!),
                  fit: BoxFit.cover,
                  errorBuilder: (_, _, _) => erro,
                )
              : Image.network(
                  post.imagem!,
                  fit: BoxFit.cover,
                  errorBuilder: (_, _, _) => erro,
                  loadingBuilder: (_, filho, progresso) => progresso == null
                      ? filho
                      : SizedBox(
                          height: 120,
                          child: Center(
                            child: CupertinoActivityIndicator(
                              color: AppColors.muted,
                            ),
                          ),
                        ),
                ),
        ),
      );
    }

    final p = _player;
    return GestureDetector(
      key: ValueKey('post-video-${post.id}'),
      onTap: _tocar,
      child: AspectRatio(
        aspectRatio: p != null && p.value.isInitialized
            ? p.value.aspectRatio
            : 16 / 9,
        child: Stack(
          fit: StackFit.expand,
          children: [
            if (p != null && p.value.isInitialized)
              VideoPlayer(p)
            else
              ColoredBox(color: AppColors.surfaceHigh),
            if (p == null || !p.value.isPlaying)
              Center(
                child: Container(
                  width: 54,
                  height: 54,
                  decoration: BoxDecoration(
                    color: Colors.black.withValues(alpha: .55),
                    shape: BoxShape.circle,
                  ),
                  child: _preparando
                      ? const Center(
                          child: CupertinoActivityIndicator(
                            color: Colors.white,
                          ),
                        )
                      : const Icon(
                          CupertinoIcons.play_fill,
                          color: Colors.white,
                          size: 26,
                        ),
                ),
              ),
            if (post.duracaoDaMidia != null)
              Positioned(
                right: 8,
                bottom: 8,
                child: Container(
                  padding: const EdgeInsets.symmetric(
                    horizontal: 6,
                    vertical: 2,
                  ),
                  decoration: BoxDecoration(
                    color: Colors.black.withValues(alpha: .6),
                    borderRadius: BorderRadius.circular(6),
                  ),
                  child: AppText(
                    _emMinutos(post.duracaoDaMidia!),
                    style: const TextStyle(
                      fontSize: 11,
                      fontWeight: FontWeight.w600,
                      color: Colors.white,
                    ),
                  ),
                ),
              ),
          ],
        ),
      ),
    );
  }
}

String _emMinutos(Duration d) {
  final m = d.inMinutes;
  final s = (d.inSeconds % 60).toString().padLeft(2, '0');
  return '$m:$s';
}

class _Selo extends StatelessWidget {
  const _Selo({required this.estado});
  final EstadoDoPost estado;

  @override
  Widget build(BuildContext context) {
    final enviado = estado == EstadoDoPost.enviado;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: AppColors.surfaceHigh,
        borderRadius: BorderRadius.circular(8),
      ),
      child: AppText(
        // "so voce ve" e literal: enquanto nao entrou no servidor, o post
        // existe so neste aparelho.
        enviado ? 'no mural' : 'só você vê',
        style: TextStyle(
          fontSize: 10.5,
          fontWeight: FontWeight.w700,
          color: AppColors.muted,
        ),
      ),
    );
  }
}

class _AcaoDoCartao extends StatelessWidget {
  const _AcaoDoCartao({
    required this.chave,
    required this.icone,
    required this.rotulo,
    required this.onTap,
    this.destaque = false,
  });

  final String chave;
  final IconData icone;
  final String rotulo;
  final VoidCallback onTap;
  final bool destaque;

  @override
  Widget build(BuildContext context) => GestureDetector(
    key: ValueKey(chave),
    behavior: HitTestBehavior.opaque,
    onTap: onTap,
    child: Container(
      height: 44,
      padding: const EdgeInsets.symmetric(horizontal: 14),
      decoration: BoxDecoration(
        color: destaque ? AppColors.accentDim : AppColors.surfaceHigh,
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(
            icone,
            size: 15,
            color: destaque ? AppColors.lime : AppColors.muted,
          ),
          const SizedBox(width: 6),
          Flexible(
            child: AppText(
              rotulo,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 13,
                fontWeight: FontWeight.w600,
                color: destaque ? AppColors.lime : AppColors.muted,
              ),
            ),
          ),
        ],
      ),
    ),
  );
}

class _Aviso extends StatelessWidget {
  const _Aviso({required this.icone, required this.texto, this.alerta = false});
  final IconData icone;
  final String texto;
  final bool alerta;

  @override
  Widget build(BuildContext context) => Container(
    key: const ValueKey('comunidade-aviso'),
    padding: const EdgeInsets.all(12),
    decoration: BoxDecoration(
      color: alerta
          ? const Color(0xFFFF6B6B).withValues(alpha: .12)
          : AppColors.surface,
      borderRadius: BorderRadius.circular(12),
    ),
    child: Row(
      children: [
        Icon(
          icone,
          size: 17,
          color: alerta ? const Color(0xFFFF6B6B) : AppColors.muted,
        ),
        const SizedBox(width: 10),
        Expanded(
          child: AppText(texto,
            style: TextStyle(
              fontSize: 12.5,
              height: 1.35,
              color: alerta ? const Color(0xFFFF8A8A) : AppColors.muted,
            ),
          ),
        ),
      ],
    ),
  );
}

class _Vazio extends StatelessWidget {
  const _Vazio();

  @override
  Widget build(BuildContext context) => Padding(
    key: const ValueKey('comunidade-vazio'),
    padding: const EdgeInsets.symmetric(vertical: 40),
    child: Column(
      children: [
        Icon(
          CupertinoIcons.person_2,
          size: 42,
          color: AppColors.muted.withValues(alpha: .6),
        ),
        const SizedBox(height: 14),
        AppText('O mural ainda está vazio',
          style: TextStyle(
            fontSize: 15,
            fontWeight: FontWeight.w700,
            color: AppColors.onDark,
          ),
        ),
        const SizedBox(height: 6),
        AppText('Seja quem começa. Toque em Publicar e mostre o que você fez '
          'no Aurea.',
          textAlign: TextAlign.center,
          style: TextStyle(fontSize: 13, height: 1.4, color: AppColors.muted),
        ),
      ],
    ),
  );
}

// ------------------------------------------------------------- a conta

class _FolhaDaConta extends ConsumerStatefulWidget {
  const _FolhaDaConta();

  @override
  ConsumerState<_FolhaDaConta> createState() => _FolhaDaContaState();
}

class _FolhaDaContaState extends ConsumerState<_FolhaDaConta> {
  late final TextEditingController _apelido;
  String? _avatar;
  String? _erro;
  bool _salvando = false;
  bool _entrando = false;
  final _codigo = TextEditingController();

  @override
  void initState() {
    super.initState();
    final c = ref.read(contaDaComunidadeProvider);
    _apelido = TextEditingController(text: c?.apelido ?? '');
    _avatar = c?.avatar;
  }

  @override
  void dispose() {
    _apelido.dispose();
    _codigo.dispose();
    super.dispose();
  }

  Future<void> _escolherFoto() async {
    try {
      final x = await ImagePicker().pickImage(
        source: ImageSource.gallery,
        maxWidth: 512,
      );
      if (x == null) return;
      if (!mounted) return;
      setState(() => _avatar = x.path);
    } catch (_) {
      if (!mounted) return;
      AureaSnack.show(context, 'Não consegui abrir a galeria');
    }
  }

  Future<void> _salvar() async {
    if (_salvando) return;
    setState(() {
      _salvando = true;
      _erro = null;
    });
    final n = ref.read(contaDaComunidadeProvider.notifier);
    final existe = ref.read(contaDaComunidadeProvider) != null;
    final erro = await (_entrando && !existe
        ? n.entrar(_codigo.text)
        : existe
        ? n.atualizar(apelido: _apelido.text, avatar: _avatar)
        : n.criar(_apelido.text, avatar: _avatar));
    if (!mounted) return;
    setState(() => _salvando = false);
    if (erro != null) {
      setState(() => _erro = erro);
      return;
    }
    Navigator.of(context).pop(true);
  }

  @override
  Widget build(BuildContext context) {
    final conta = ref.watch(contaDaComunidadeProvider);
    return _Folha(
      titulo: conta == null
          ? (_entrando ? 'Entrar na minha conta' : 'Criar minha conta')
          : 'Minha conta',
      subtitulo: conta == null
          ? 'Sua conta fica no mural. Guarde o código de acesso para '
                'entrar em outro aparelho.'
          : 'Trocar o apelido não muda os posts que você já publicou.',
      filhos: [
        if (_entrando && conta == null)
          TextField(
            key: const ValueKey('conta-codigo'),
            controller: _codigo,
            autocorrect: false,
            enableSuggestions: false,
            obscureText: true,
            textInputAction: TextInputAction.done,
            onSubmitted: (_) => _salvar(),
            decoration: InputDecoration(labelText: translate(context, 'Código de acesso')),
          )
        else
          Row(
            children: [
              GestureDetector(
                key: const ValueKey('conta-foto'),
                onTap: _escolherFoto,
                child: Stack(
                  alignment: Alignment.bottomRight,
                  children: [
                    _Avatar(nome: _apelido.text, arquivo: _avatar, raio: 30),
                    Container(
                      padding: const EdgeInsets.all(3),
                      decoration: BoxDecoration(
                        color: AppColors.lime,
                        shape: BoxShape.circle,
                      ),
                      child: const Icon(
                        CupertinoIcons.camera_fill,
                        size: 12,
                        color: AureaColors.onAccent,
                      ),
                    ),
                  ],
                ),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: TextField(
                  key: const ValueKey('conta-apelido'),
                  controller: _apelido,
                  maxLength: 20,
                  autofocus: conta == null,
                  onChanged: (_) => setState(() => _erro = null),
                  style: TextStyle(fontSize: 15, color: AppColors.onDark),
                  decoration: InputDecoration(
                    hintText: translate(context, 'Seu apelido no mural'),
                    hintStyle: TextStyle(color: AppColors.muted),
                    counterText: '',
                    filled: true,
                    fillColor: AppColors.surface,
                    border: OutlineInputBorder(
                      borderRadius: BorderRadius.circular(12),
                      borderSide: BorderSide.none,
                    ),
                  ),
                ),
              ),
            ],
          ),
        if (_erro != null) ...[
          const SizedBox(height: 10),
          _Aviso(
            icone: CupertinoIcons.exclamationmark_triangle,
            texto: _erro!,
            alerta: true,
          ),
        ],
        const SizedBox(height: 12),
        if (conta == null)
          TextButton(
            key: const ValueKey('conta-alternar-entrada'),
            onPressed: _salvando
                ? null
                : () => setState(() {
                    _entrando = !_entrando;
                    _erro = null;
                  }),
            child: AppText(
              _entrando
                  ? 'Criar uma nova conta'
                  : 'Já tenho um código de acesso',
            ),
          ),
        if (conta != null)
          TextButton.icon(
            key: const ValueKey('conta-copiar-codigo'),
            icon: const Icon(CupertinoIcons.doc_on_doc),
            label: const AppText('Copiar meu código de acesso'),
            onPressed: () async {
              await Clipboard.setData(ClipboardData(text: conta.codigo));
              if (!context.mounted) return;
              AureaSnack.show(
                context,
                'Código copiado. Guarde para entrar novamente.',
              );
            },
          ),
        Row(
          children: [
            if (conta != null)
              Expanded(
                child: _AcaoDoCartao(
                  chave: 'conta-sair',
                  icone: CupertinoIcons.person_badge_minus,
                  rotulo: 'Sair da conta',
                  onTap: () {
                    ref.read(contaDaComunidadeProvider.notifier).sair();
                    Navigator.of(context).pop(false);
                  },
                ),
              ),
            if (conta != null) const SizedBox(width: 8),
            Expanded(
              child: _AcaoDoCartao(
                chave: 'conta-salvar',
                icone: CupertinoIcons.check_mark,
                rotulo: _salvando
                    ? 'Salvando…'
                    : conta == null
                    ? (_entrando ? 'Entrar' : 'Criar conta')
                    : 'Salvar',
                destaque: true,
                onTap: _salvar,
              ),
            ),
          ],
        ),
      ],
    );
  }
}

// -------------------------------------------------------- o compositor

class _Compositor extends ConsumerStatefulWidget {
  const _Compositor({required this.conta, this.responderA, this.repostar});

  final ContaDaComunidade conta;

  /// A quem isto responde, ou o que isto reposta. No maximo um dos dois:
  /// responder a um post repostando outro nao quer dizer nada.
  final PostDaComunidade? responderA;
  final PostDaComunidade? repostar;

  @override
  ConsumerState<_Compositor> createState() => _CompositorState();
}

class _CompositorState extends ConsumerState<_Compositor> {
  final _texto = TextEditingController();
  final _etiquetas = TextEditingController();
  String? _midia;
  TipoDeMidia _tipo = TipoDeMidia.imagem;
  Duration? _duracao;
  String? _nomeDoProjeto;
  String? _erro;
  bool _apenasAviso = false;

  /// No repost o texto e opcional — repostar sem comentar e o uso normal.
  bool get _repostando => widget.repostar != null;

  @override
  void dispose() {
    _texto.dispose();
    _etiquetas.dispose();
    super.dispose();
  }

  /// ANEXAR UM PROJETO: escolhe da lista e empacota como template.
  ///
  /// O que sobe e o PROJETO, e nao os videos e as fotos que ele usa —
  /// esses ficam no aparelho de quem fez. Quem abrir recebe as camadas,
  /// as animacoes e a cena 3D, e os lugares onde havia midia aparecem
  /// vazios. Dizer isso antes evita a pergunta depois.
  Future<void> _anexarProjeto() async {
    final projetos = ref.read(projectsControllerProvider);
    if (projetos.isEmpty) {
      setState(
        () => _erro = 'Você ainda não tem nenhum projeto para publicar.',
      );
      return;
    }
    final escolhido = await showModalBottomSheet<VideoProject>(
      context: context,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      builder: (_) => _EscolherProjeto(projetos: projetos),
    );
    if (escolhido == null || !mounted) return;
    try {
      final pacote = TemplatePack(
        name: escolhido.name,
        author: widget.conta.apelido,
        project: escolhido,
      );
      final pasta = await getTemporaryDirectory();
      final arquivo = File(
        '${pasta.path}/aurea-${escolhido.id}-para-o-mural.json',
      );
      await arquivo.writeAsString(pacote.encode(), flush: true);
      if (!mounted) return;
      setState(() {
        _midia = arquivo.path;
        _tipo = TipoDeMidia.projeto;
        _nomeDoProjeto = escolhido.name;
        _duracao = null;
        _erro = null;
      });
    } catch (_) {
      if (!mounted) return;
      setState(() => _erro = 'Não consegui empacotar esse projeto.');
    }
  }

  Future<void> _escolherImagem() async {
    try {
      // COMPRIMIDA AQUI, e nao no servidor. Uma foto de celular sai com
      // oito megabytes; o mural aceita dois. Reduzir antes de sair do
      // aparelho poupa a internet de quem publica e faz caber.
      final x = await ImagePicker().pickImage(
        source: ImageSource.gallery,
        maxWidth: 1600,
        imageQuality: 82,
      );
      if (x == null) return;
      if (!mounted) return;
      setState(() {
        _midia = x.path;
        _tipo = TipoDeMidia.imagem;
        _duracao = null;
        _nomeDoProjeto = null;
      });
    } catch (_) {
      if (!mounted) return;
      AureaSnack.show(context, 'Não consegui abrir a galeria');
    }
  }

  Future<void> _escolherVideo() async {
    try {
      final x = await ImagePicker().pickVideo(source: ImageSource.gallery);
      if (x == null) return;
      // A DURACAO SE LE ANTES DE ANEXAR, e nao na hora de mostrar: e ela
      // que decide se o video cabe no mural, e recusar depois de a pessoa
      // ja ter publicado seria pior.
      Duration? duracao;
      try {
        final c = VideoPlayerController.file(File(x.path));
        await c.initialize();
        duracao = c.value.duration;
        await c.dispose();
      } catch (_) {}
      if (duracao != null && duracao.inSeconds > 120) {
        if (!mounted) return;
        setState(
          () => _erro =
              'Vídeo de até 2 minutos no mural. '
              'Corte o trecho que interessa e anexe de novo.',
        );
        return;
      }
      if (!mounted) return;
      setState(() {
        _midia = x.path;
        _tipo = TipoDeMidia.video;
        _duracao = duracao;
        _nomeDoProjeto = null;
        _erro = null;
      });
    } catch (_) {
      if (!mounted) return;
      AureaSnack.show(context, 'Não consegui abrir a galeria');
    }
  }

  void _pronto() {
    // REPOST SEM COMENTARIO passa direto pelo filtro de texto: nao ha
    // texto para filtrar, e o original ja passou pelo dele.
    if (_repostando && _texto.text.trim().isEmpty) {
      _entregar();
      return;
    }
    final veredito = moderarTexto(_texto.text);
    // BLOQUEIO E AVISO SAO COISAS DIFERENTES na tela: um impede, o outro
    // so recomenda. Tratar os dois igual faria a pessoa achar que o app
    // travou por causa de uma letra repetida — e insistir num aviso e um
    // direito dela: quem quer escrever em caixa alta, escreve.
    if (veredito.bloqueia) {
      setState(() {
        _erro = veredito.motivo;
        _apenasAviso = false;
      });
      return;
    }
    if (veredito.veredito == Veredito.ajustar && !_apenasAviso) {
      setState(() {
        _erro = veredito.motivo;
        _apenasAviso = true;
      });
      return;
    }
    _entregar();
  }

  void _entregar() {
    Navigator.of(context).pop(
      PostDaComunidade(
        id: const Uuid().v4(),
        autor: widget.conta.apelido,
        autorId: widget.conta.id,
        texto: _texto.text.trim(),
        quando: DateTime.now(),
        imagem: _midia,
        imagemLocal: _midia != null,
        tipoDeMidia: _tipo,
        duracaoDaMidia: _duracao,
        nomeDoProjeto: _nomeDoProjeto,
        etiquetas: [
          for (final e in _etiquetas.text.split(RegExp(r'[,\s]+')))
            if (e.trim().isNotEmpty) e.trim().replaceAll('#', ''),
        ],
        estado: EstadoDoPost.rascunho,
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    final citado = widget.repostar ?? widget.responderA;
    return _Folha(
      titulo: _repostando
          ? 'Repostar'
          : widget.responderA != null
          ? 'Responder'
          : 'Publicar no mural',
      subtitulo: _repostando
          ? 'Comente se quiser — dá para repostar sem escrever nada.'
          : widget.responderA != null
          ? 'Respondendo ${widget.responderA!.autor}.'
          : 'Assinado como ${widget.conta.apelido}.',
      filhos: [
        if (citado != null) ...[
          _Citacao(original: citado),
          const SizedBox(height: 12),
        ],
        TextField(
          key: const ValueKey('comunidade-texto'),
          controller: _texto,
          maxLines: 5,
          minLines: 3,
          autofocus: true,
          onChanged: (_) => setState(() => _erro = null),
          style: TextStyle(fontSize: 14, color: AppColors.onDark),
          decoration: InputDecoration(
            hintText: _repostando
                ? 'Comentar (opcional)'
                : widget.responderA != null
                ? 'Sua resposta'
                : 'O que você fez no Aurea?',
            hintStyle: TextStyle(color: AppColors.muted),
            filled: true,
            fillColor: AppColors.surface,
            border: OutlineInputBorder(
              borderRadius: BorderRadius.circular(12),
              borderSide: BorderSide.none,
            ),
          ),
        ),
        const SizedBox(height: 10),
        TextField(
          key: const ValueKey('comunidade-etiquetas'),
          controller: _etiquetas,
          style: TextStyle(fontSize: 13, color: AppColors.onDark),
          decoration: InputDecoration(
            hintText: translate(context, 'etiquetas: motion, 3d, tutorial'),
            hintStyle: TextStyle(color: AppColors.muted),
            filled: true,
            fillColor: AppColors.surface,
            border: OutlineInputBorder(
              borderRadius: BorderRadius.circular(12),
              borderSide: BorderSide.none,
            ),
          ),
        ),
        if (_erro != null) ...[
          const SizedBox(height: 10),
          _Aviso(
            icone: _apenasAviso
                ? CupertinoIcons.info_circle
                : CupertinoIcons.exclamationmark_triangle,
            texto: _apenasAviso
                ? '$_erro Toque de novo para publicar assim.'
                : _erro!,
            alerta: !_apenasAviso,
          ),
        ],
        if (_midia != null && _tipo == TipoDeMidia.projeto) ...[
          const SizedBox(height: 10),
          _CartaoDeProjeto(
            post: PostDaComunidade(
              id: 'previa',
              autor: widget.conta.apelido,
              texto: '',
              quando: DateTime.now(),
              imagem: _midia,
              imagemLocal: true,
              tipoDeMidia: TipoDeMidia.projeto,
              nomeDoProjeto: _nomeDoProjeto,
            ),
          ),
        ] else if (_midia != null) ...[
          const SizedBox(height: 10),
          ClipRRect(
            borderRadius: BorderRadius.circular(12),
            child: Stack(
              alignment: Alignment.center,
              children: [
                if (_tipo == TipoDeMidia.imagem)
                  Image.file(
                    File(_midia!),
                    height: 140,
                    width: double.infinity,
                    fit: BoxFit.cover,
                  )
                else
                  Container(
                    height: 140,
                    width: double.infinity,
                    color: AppColors.surfaceHigh,
                    alignment: Alignment.center,
                    child: Column(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(
                          CupertinoIcons.play_rectangle_fill,
                          size: 30,
                          color: AppColors.muted,
                        ),
                        const SizedBox(height: 4),
                        AppText(
                          _duracao == null
                              ? 'Vídeo anexado'
                              : 'Vídeo · ${_emMinutos(_duracao!)}',
                          style: TextStyle(
                            fontSize: 12,
                            color: AppColors.muted,
                          ),
                        ),
                      ],
                    ),
                  ),
              ],
            ),
          ),
        ],
        const SizedBox(height: 10),
        Row(
          children: [
            Expanded(
              child: _AcaoDoCartao(
                chave: 'comunidade-imagem',
                icone: CupertinoIcons.photo,
                rotulo: 'Imagem',
                onTap: _escolherImagem,
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: _AcaoDoCartao(
                chave: 'comunidade-video',
                icone: CupertinoIcons.videocam,
                rotulo: translate(context, 'Vídeo'),
                onTap: _escolherVideo,
              ),
            ),
            const SizedBox(width: 8),
            Expanded(
              child: _AcaoDoCartao(
                chave: 'comunidade-projeto',
                icone: CupertinoIcons.cube_box,
                rotulo: 'Projeto',
                onTap: _anexarProjeto,
              ),
            ),
          ],
        ),
        const SizedBox(height: 8),
        // PUBLICAR SOZINHO NA LINHA. Ele estava espremido entre os
        // anexos, do mesmo tamanho deles — a acao principal da folha
        // parecia mais um anexo.
        SizedBox(
          width: double.infinity,
          child: _AcaoDoCartao(
            chave: 'comunidade-guardar',
            icone: CupertinoIcons.check_mark,
            rotulo: _repostando
                ? 'Repostar'
                : widget.responderA != null
                ? 'Responder'
                : 'Publicar',
            destaque: true,
            onTap: _pronto,
          ),
        ),
      ],
    );
  }
}

/// A LISTA DE PROJETOS na hora de anexar um ao post.
class _EscolherProjeto extends StatelessWidget {
  const _EscolherProjeto({required this.projetos});
  final List<VideoProject> projetos;

  @override
  Widget build(BuildContext context) => _Folha(
    titulo: 'Publicar um projeto',
    subtitulo:
        'Vai o projeto: camadas, animações e cena 3D. Os vídeos e as '
        'fotos que você importou ficam no seu aparelho.',
    filhos: [
      for (final p in projetos.take(60))
        GestureDetector(
          key: ValueKey('escolher-projeto-${p.id}'),
          behavior: HitTestBehavior.opaque,
          onTap: () => Navigator.of(context).pop(p),
          child: Container(
            margin: const EdgeInsets.only(bottom: 8),
            padding: const EdgeInsets.all(12),
            decoration: BoxDecoration(
              color: AppColors.surface,
              borderRadius: BorderRadius.circular(12),
            ),
            child: Row(
              children: [
                Icon(CupertinoIcons.cube_box, size: 18, color: AppColors.muted),
                const SizedBox(width: 10),
                Expanded(
                  child: AppText(
                    p.name,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w600,
                      color: AppColors.onDark,
                    ),
                  ),
                ),
                AppText(
                  '${p.layers.length} '
                  '${p.layers.length == 1 ? "camada" : "camadas"}',
                  style: TextStyle(fontSize: 11.5, color: AppColors.muted),
                ),
              ],
            ),
          ),
        ),
    ],
  );
}

/// A casca das folhas desta aba: alca, titulo, subtitulo e o conteudo.
class _Folha extends StatelessWidget {
  const _Folha({
    required this.titulo,
    required this.subtitulo,
    required this.filhos,
  });

  final String titulo;
  final String subtitulo;
  final List<Widget> filhos;

  @override
  Widget build(BuildContext context) {
    final fundo = MediaQuery.of(context).viewInsets.bottom;
    return Padding(
      padding: EdgeInsets.only(bottom: fundo),
      child: Container(
        decoration: BoxDecoration(
          color: AppColors.background,
          borderRadius: const BorderRadius.vertical(top: Radius.circular(20)),
        ),
        padding: const EdgeInsets.fromLTRB(18, 12, 18, 18),
        child: SafeArea(
          top: false,
          child: SingleChildScrollView(
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Center(
                  child: Container(
                    width: 36,
                    height: 4,
                    decoration: BoxDecoration(
                      color: AppColors.muted.withValues(alpha: .45),
                      borderRadius: BorderRadius.circular(2),
                    ),
                  ),
                ),
                const SizedBox(height: 14),
                AppText(titulo,
                  style: TextStyle(
                    fontSize: 17,
                    fontWeight: FontWeight.w700,
                    color: AppColors.onDark,
                  ),
                ),
                const SizedBox(height: 3),
                AppText(subtitulo,
                  style: TextStyle(fontSize: 12, color: AppColors.muted),
                ),
                const SizedBox(height: 14),
                ...filhos,
              ],
            ),
          ),
        ),
      ),
    );
  }
}
