import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart'
    show MaterialLocalizations, MaterialPageRoute, TimeOfDay;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/ds/ds.dart';
import '../../../core/l10n/app_language.dart';
import '../../../core/ui/tocavel.dart';
import '../../../core/widgets/aurea_logo.dart';
import '../../about/presentation/report_sheet.dart';
import '../../editor/domain/video_project.dart';
import '../../tutoriais/presentation/tutorial_screen.dart';
import '../application/project_repository.dart';
import '../application/projects_controller.dart';
import '../application/projects_view.dart';
import '../application/thumbnail_service.dart';
import 'acoes_da_inicio.dart';
import 'home_shell.dart';
import 'new_project_sheet.dart';
import 'whats_new.dart';

export 'acoes_da_inicio.dart' show apagarTodosOsProjetos;

/// A INICIO: a marca, UM botao de criar e os projetos recentes.
///
/// O que o dono pediu, na ordem: AUREA · + Novo projeto · Projetos
/// recentes · cartoes. Cada cartao diz o que importa para achar um
/// trabalho — miniatura, nome, quadro, duracao e quando foi mexido — e
/// tem o seu ⋯ com abrir, renomear, duplicar, compartilhar e excluir.
///
/// O resto que a Inicio ja fazia (importar midia, template, projeto,
/// modelos prontos, tutoriais, apagar todos) continua a um toque, no menu
/// do topo: existe, mas nao disputa a tela com os projetos.
///
/// RAPIDA DE PROPOSITO:
/// - a lista e preguicosa (SliverGrid com builder): so os cartoes na tela
///   existem, e so as miniaturas deles sao lidas do disco;
/// - a miniatura e decodificada no tamanho do cartao, e o provedor dela
///   fica guardado por projeto ([_Miniaturas]): voltar a Inicio nao
///   decodifica de novo, e uma miniatura nova troca so a sua;
/// - com o editor por cima, a Inicio para de ouvir a lista: cada edicao
///   publica o projeto, e ela refazia os cartoes escondida a cada toque.
///   Ao voltar ela le a lista uma vez, ja em dia.
class ProjectsTab extends ConsumerStatefulWidget {
  const ProjectsTab({super.key});

  @override
  ConsumerState<ProjectsTab> createState() => _ProjectsTabState();
}

class _ProjectsTabState extends ConsumerState<ProjectsTab> {
  /// A ultima lista vista com a Inicio na frente (ver o cabecalho).
  List<VideoProject> _vistos = const [];

  bool _procurando = false;
  final _busca = TextEditingController();

  @override
  void initState() {
    super.initState();
    // Uma vez por montagem — nunca no build.
    ThumbnailService.instance.init();
  }

  @override
  void dispose() {
    _busca.dispose();
    super.dispose();
  }

  // -- acoes ---------------------------------------------------------------

  Future<void> _criar() async {
    final total = ref.read(projectsControllerProvider).length;
    final projeto = await showNewProjectSheet(
      context,
      nomeSugerido: 'Projeto ${total + 1}',
    );
    if (projeto == null || !mounted) return;
    entrarNoEditor(context, ref, projeto, novo: true);
  }

  Future<void> _menuDoProjeto(BuildContext ancora, VideoProject p) async {
    final acao = await mostrarAureaMenu<String>(
      ancora,
      itens: const [
        AureaMenuItem(
          valor: 'abrir',
          rotulo: 'Abrir',
          icone: CupertinoIcons.play,
          chave: 'abrir',
        ),
        AureaMenuItem(
          valor: 'renomear',
          rotulo: 'Renomear',
          icone: CupertinoIcons.pencil,
          chave: 'renomear',
        ),
        AureaMenuItem(
          valor: 'duplicar',
          rotulo: 'Duplicar',
          icone: CupertinoIcons.plus_square_on_square,
          chave: 'duplicar',
        ),
        AureaMenuItem(
          valor: 'compartilhar',
          rotulo: 'Compartilhar',
          icone: CupertinoIcons.square_arrow_up,
          chave: 'compartilhar',
        ),
        AureaMenuItem(
          valor: 'excluir',
          rotulo: 'Excluir',
          icone: CupertinoIcons.trash,
          destrutivo: true,
          chave: 'excluir',
        ),
      ],
    );
    if (acao == null || !mounted) return;
    switch (acao) {
      case 'abrir':
        entrarNoEditor(context, ref, p);
      case 'renomear':
        await renomearProjeto(context, ref, p);
      case 'duplicar':
        duplicarProjeto(ref, p);
      case 'compartilhar':
        await compartilharProjeto(context, ref, p);
      case 'excluir':
        await excluirProjeto(context, ref, p);
    }
  }

  Future<void> _menuDaInicio(BuildContext ancora) async {
    final temProjeto = ref.read(projectsControllerProvider).isNotEmpty;
    final acao = await mostrarAureaMenu<String>(
      ancora,
      itens: [
        const AureaMenuItem(
          valor: 'midia',
          rotulo: 'Importar mídia',
          icone: CupertinoIcons.photo_on_rectangle,
          chave: 'importar-midia',
        ),
        const AureaMenuItem(
          valor: 'template',
          rotulo: 'Abrir template',
          icone: CupertinoIcons.doc_on_doc,
          chave: 'abrir-template',
        ),
        const AureaMenuItem(
          valor: 'projeto',
          rotulo: 'Importar projeto',
          icone: CupertinoIcons.tray_arrow_down,
          chave: 'importar-projeto',
        ),
        const AureaMenuItem(
          valor: 'modelos',
          rotulo: 'Modelos',
          icone: CupertinoIcons.square_stack_3d_up,
          chave: 'modelos',
        ),
        const AureaMenuItem(
          valor: 'aprender',
          rotulo: 'Aprender',
          icone: CupertinoIcons.play_rectangle,
          chave: 'aprender',
        ),
        const AureaMenuItem(
          valor: 'ajustes',
          rotulo: 'Ajustes',
          icone: CupertinoIcons.gear,
          chave: 'ajustes',
        ),
        // A aba Sobre nao tem lugar na barra: a porta dela e esta.
        const AureaMenuItem(
          valor: 'sobre',
          rotulo: 'Sobre',
          icone: CupertinoIcons.info_circle,
          chave: 'sobre',
        ),
        AureaMenuItem(
          valor: 'apagar-todos',
          rotulo: 'Apagar todos os projetos',
          icone: CupertinoIcons.trash,
          destrutivo: true,
          habilitado: temProjeto,
          chave: 'apagar-todos',
        ),
      ],
    );
    if (acao == null || !mounted) return;
    switch (acao) {
      case 'midia':
        await importarMidia(context, ref);
      case 'template':
        await abrirTemplate(context, ref);
      case 'projeto':
        await importarProjeto(context, ref);
      case 'modelos':
        await _folhaDeModelos();
      case 'aprender':
        await _folhaDeAprender();
      case 'ajustes':
        ref.read(homeTabProvider.notifier).state = 2;
      case 'sobre':
        ref.read(homeTabProvider.notifier).state = 4;
      case 'apagar-todos':
        await apagarTodosOsProjetos(context, ref);
    }
  }

  Future<void> _folhaDeModelos() async {
    final id = await mostrarAureaFolha<String>(
      context,
      titulo: 'Modelos',
      construtor: (folha) => Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          for (final m in modelosProntos)
            _LinhaDeModelo(
              key: ValueKey('modelo-${m.id}'),
              imagem: m.imagem,
              titulo: m.titulo,
              detalhe: m.detalhe,
              aoTocar: () => Navigator.of(folha).pop(m.id),
            ),
          const SizedBox(height: AureaDims.e10),
        ],
      ),
    );
    if (id == null || !mounted) return;
    await abrirModelo(context, ref, id);
  }

  Future<void> _folhaDeAprender() async {
    const itens = [
      (
        id: 'cena3d',
        chave: 'inicio-tutorial-cena3d',
        icone: CupertinoIcons.play_rectangle,
        texto: 'Tutorial em vídeo: sua primeira cena 3D',
      ),
      (
        id: 'cena-completa',
        chave: 'inicio-tutorial-cena-completa',
        icone: CupertinoIcons.cube_box,
        texto: 'Tutorial em vídeo: cena 3D com modelos e câmeras',
      ),
      (
        id: 'texto-bounce',
        chave: 'inicio-tutorial-texto-bounce',
        icone: CupertinoIcons.textformat,
        texto: 'Tutorial em vídeo: texto que quica, do seu jeito',
      ),
      (
        id: 'novidades',
        chave: 'inicio-novidades',
        icone: CupertinoIcons.sparkles,
        texto: 'O que ha de novo nesta versao',
      ),
      (
        id: 'relatar',
        chave: 'inicio-relatar',
        icone: CupertinoIcons.exclamationmark_bubble,
        texto: 'Versao beta: achou um problema? Conte pra gente',
      ),
    ];
    final id = await mostrarAureaFolha<String>(
      context,
      titulo: 'Aprender',
      construtor: (folha) => Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          for (final i in itens)
            _LinhaDaFolha(
              key: ValueKey(i.chave),
              icone: i.icone,
              texto: i.texto,
              aoTocar: () => Navigator.of(folha).pop(i.id),
            ),
          const SizedBox(height: AureaDims.e10),
        ],
      ),
    );
    if (id == null || !mounted) return;
    switch (id) {
      case 'novidades':
        await showWhatsNewSheet(context);
      case 'relatar':
        await showReportSheet(context);
      default:
        await Navigator.of(context).push(
          MaterialPageRoute<void>(builder: (_) => TutorialScreen(id: id)),
        );
    }
  }

  Future<void> _escolherOrdem(BuildContext ancora) async {
    final atual = ref.read(ordemDosProjetosProvider);
    final escolha = await mostrarAureaMenu<OrdemDosProjetos>(
      ancora,
      itens: [
        for (final o in OrdemDosProjetos.values)
          AureaMenuItem(
            valor: o,
            rotulo: rotuloDaOrdem(o),
            marcado: o == atual,
            chave: 'projetos-ordem-${o.name}',
          ),
      ],
    );
    if (escolha != null) {
      ref.read(ordemDosProjetosProvider.notifier).escolher(escolha);
    }
  }

  void _marcar(String id) {
    final atual = {...ref.read(selecaoDeProjetosProvider)};
    if (!atual.add(id)) atual.remove(id);
    ref.read(selecaoDeProjetosProvider.notifier).state = atual;
  }

  void _fecharBusca() {
    _busca.clear();
    ref.read(buscaDeProjetosProvider.notifier).state = '';
    setState(() => _procurando = false);
  }

  // -- build -----------------------------------------------------------------

  @override
  Widget build(BuildContext context) {
    // O "+" da barra de abas pede um projeto novo daqui.
    ref.listen(novoProjetoSolicitadoProvider, (_, _) => _criar());

    // COM O EDITOR POR CIMA, NAO OUVE A LISTA (ver o cabecalho da classe).
    // Voltar a ser a rota da frente reconstroi esta tela (dependencia do
    // ModalRoute), e ai ela le a lista de novo.
    if (ModalRoute.isCurrentOf(context) ?? true) {
      _vistos = ref.watch(projectsControllerProvider);
    }
    final todos = _vistos;
    final ordem = ref.watch(ordemDosProjetosProvider);
    final busca = ref.watch(buscaDeProjetosProvider);
    final selecao = ref.watch(selecaoDeProjetosProvider);
    final escolhendo = selecao.isNotEmpty;
    final projetos = projetosArrumados(todos, ordem, busca);
    final repositorio = ref.read(projectRepositoryProvider);

    return ColoredBox(
      color: AureaCores.cromo,
      child: SafeArea(
        bottom: false,
        child: Column(
          children: [
            _Topo(aoMenu: _menuDaInicio),
            Expanded(
              child: Stack(
                children: [
                  CustomScrollView(
                    slivers: [
                      SliverToBoxAdapter(
                        child: _BotaoNovoProjeto(aoTocar: _criar),
                      ),
                      if (todos.isNotEmpty)
                        SliverToBoxAdapter(
                          child: escolhendo
                              ? _CabecalhoDaSelecao(
                                  quantos: selecao.length,
                                  aoMarcarTodos: () =>
                                      ref
                                          .read(
                                            selecaoDeProjetosProvider.notifier,
                                          )
                                          .state = {
                                        for (final p in projetos) p.id,
                                      },
                                  aoSair: () =>
                                      ref
                                              .read(
                                                selecaoDeProjetosProvider
                                                    .notifier,
                                              )
                                              .state =
                                          const {},
                                )
                              : _CabecalhoDaLista(
                                  procurando: _procurando,
                                  busca: _busca,
                                  aoProcurar: () =>
                                      setState(() => _procurando = true),
                                  aoDigitar: (v) =>
                                      ref
                                              .read(
                                                buscaDeProjetosProvider
                                                    .notifier,
                                              )
                                              .state =
                                          v,
                                  aoFecharBusca: _fecharBusca,
                                  aoOrdenar: _escolherOrdem,
                                ),
                        ),
                      if (projetos.isEmpty)
                        SliverToBoxAdapter(
                          child: _Vazio(procurando: busca.trim().isNotEmpty),
                        )
                      else
                        SliverPadding(
                          padding: const EdgeInsets.symmetric(
                            horizontal: _margem,
                          ),
                          sliver: SliverGrid(
                            // UMA COLUNA NO CELULAR; duas ou mais so em tela
                            // larga (tablet, celular deitado).
                            gridDelegate:
                                const SliverGridDelegateWithMaxCrossAxisExtent(
                                  maxCrossAxisExtent: 560,
                                  mainAxisExtent: _alturaDoCartao,
                                  mainAxisSpacing: AureaDims.e8,
                                  crossAxisSpacing: AureaDims.e10,
                                ),
                            delegate: SliverChildBuilderDelegate(
                              (context, i) {
                                final p = projetos[i];
                                return _CartaoDoProjeto(
                                  key: ValueKey('projeto-${p.id}'),
                                  projeto: p,
                                  editadoEm: _Edicoes.de(p, repositorio),
                                  escolhendo: escolhendo,
                                  marcado: selecao.contains(p.id),
                                  aoAbrir: () => escolhendo
                                      ? _marcar(p.id)
                                      : entrarNoEditor(context, ref, p),
                                  aoSegurar: () => _marcar(p.id),
                                  aoMenu: (ancora) => _menuDoProjeto(ancora, p),
                                );
                              },
                              childCount: projetos.length,
                              // Fora da tela nao guarda estado nenhum.
                              addAutomaticKeepAlives: false,
                            ),
                          ),
                        ),
                      SliverToBoxAdapter(
                        child: SizedBox(
                          height: escolhendo
                              ? _alturaDoLote + AureaDims.e20
                              : AureaDims.e20,
                        ),
                      ),
                    ],
                  ),
                  if (escolhendo)
                    Positioned(
                      left: 0,
                      right: 0,
                      bottom: 0,
                      child: _AcoesEmLote(
                        quantos: selecao.length,
                        aoDuplicar: () {
                          for (final p in todos) {
                            if (selecao.contains(p.id)) duplicarProjeto(ref, p);
                          }
                          ref.read(selecaoDeProjetosProvider.notifier).state =
                              const {};
                        },
                        aoExcluir: () async {
                          final foi = await excluirEscolhidos(
                            context,
                            ref,
                            selecao,
                          );
                          if (foi) {
                            ref
                                    .read(selecaoDeProjetosProvider.notifier)
                                    .state =
                                const {};
                          }
                        },
                      ),
                    ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

const double _margem = 16;

/// A altura do cartao do projeto: a linha da lista da referencia (80),
/// com a miniatura de 64.
const double _alturaDoCartao = 80;
const double _ladoDaMiniatura = 64;
const double _alturaDoLote = 52;

/// A duracao como o relogio do editor mostra: 0:05, 12:40, 1:02:03.
String duracaoDoProjeto(Duration d) {
  final s = d.inSeconds;
  final h = s ~/ 3600;
  final m = (s % 3600) ~/ 60;
  final ss = (s % 60).toString().padLeft(2, '0');
  return h > 0 ? '$h:${m.toString().padLeft(2, '0')}:$ss' : '$m:$ss';
}

/// A ficha do cartao: o quadro de saida e a duracao — "1080 × 1920 · 0:05".
String fichaDoProjeto(VideoProject p) =>
    '${p.outputWidth} × ${p.outputHeight} · ${duracaoDoProjeto(p.duration)}';

/// QUANDO FOI MEXIDO, no formato do idioma da tela: hoje e a hora, este
/// ano e o dia, antes disso a data curta. Sem palavra nenhuma — o relogio
/// ao lado diz o que o numero e.
String quandoFoiEditado(BuildContext context, DateTime t, DateTime agora) {
  final loc = MaterialLocalizations.of(context);
  if (t.year == agora.year && t.month == agora.month && t.day == agora.day) {
    return loc.formatTimeOfDay(TimeOfDay.fromDateTime(t));
  }
  if (t.year == agora.year) return loc.formatShortMonthDay(t);
  return loc.formatShortDate(t);
}

/// A ULTIMA EDICAO de cada projeto, sem `stat` a cada quadro.
///
/// O modelo nao guarda a data da ultima edicao; o arquivo do projeto sim
/// (ele e regravado a cada edicao — [ProjectRepository.editadoEm]). A
/// data fica presa a INSTANCIA do projeto (o projeto e imutavel: editar
/// cria outro), entao cada cartao consulta o disco uma vez. Uma instancia
/// nova de um id ja visto e uma edicao feita agora, nesta sessao — o
/// arquivo pode nem ter sido gravado ainda (a gravacao espera 900 ms).
abstract final class _Edicoes {
  static final _instancia = <String, VideoProject>{};
  static final _quando = Expando<DateTime>('editado-em');

  static DateTime de(VideoProject p, ProjectRepository repositorio) {
    final ja = _quando[p];
    if (ja != null) return ja;
    final anterior = _instancia[p.id];
    final q = anterior != null && !identical(anterior, p)
        ? DateTime.now()
        : (repositorio.editadoEm(p.id) ?? DateTime.now());
    _instancia[p.id] = p;
    _quando[p] = q;
    return q;
  }
}

/// O PROVEDOR DE CADA MINIATURA, guardado por projeto.
///
/// A miniatura so muda quando o editor fecha (captura) — e o
/// [ThumbnailService.revision] sobe. Sem guardar, cada revisao (de
/// QUALQUER projeto) faria cada cartao visivel olhar o disco de novo.
/// Guardando, o cartao olha a data do arquivo uma vez por revisao e so
/// descarta a imagem decodificada quando o arquivo mudou de verdade — o
/// `evict` e necessario porque a imagem decodificada no tamanho do cartao
/// ([ResizeImage]) tem chave propria no cache, e a captura so descarta a
/// do arquivo cheio.
abstract final class _Miniaturas {
  static final _porId =
      <
        String,
        ({int revisao, int largura, DateTime? quando, ImageProvider? imagem})
      >{};

  static ImageProvider? de(String id, int revisao, int largura) {
    final c = _porId[id];
    if (c != null && c.revisao == revisao && c.largura == largura) {
      return c.imagem;
    }
    final arquivo = ThumbnailService.instance.fileFor(id);
    DateTime? quando;
    if (arquivo != null) {
      try {
        quando = arquivo.lastModifiedSync();
      } catch (_) {}
    }
    if (c?.imagem != null && (arquivo == null || c!.quando != quando)) {
      c!.imagem!.evict();
    }
    final imagem = arquivo == null
        ? null
        : ResizeImage(FileImage(arquivo), width: largura);
    _porId[id] = (
      revisao: revisao,
      largura: largura,
      quando: quando,
      imagem: imagem,
    );
    return imagem;
  }
}

// ---------------------------------------------------------------- pecas

/// O TOPO: a marca e o menu. Altura da barra de cima da referencia (57).
class _Topo extends StatelessWidget {
  const _Topo({required this.aoMenu});

  final void Function(BuildContext ancora) aoMenu;

  @override
  Widget build(BuildContext context) => SizedBox(
    height: 57,
    child: Row(
      children: [
        const SizedBox(width: _margem),
        const AureaLogo(size: 30),
        const SizedBox(width: AureaDims.e10),
        // A marca: nome proprio, nao passa pelo dicionario.
        Text(
          'AUREA',
          style: TextStyle(
            fontSize: 19,
            fontWeight: FontWeight.w800,
            letterSpacing: 3,
            color: AureaCores.texto,
          ),
        ),
        const Spacer(),
        Builder(
          builder: (ancora) => Tocavel(
            key: const ValueKey('home-menu'),
            onTap: () => aoMenu(ancora),
            child: SizedBox(
              width: AureaDims.toqueConfortavel,
              height: AureaDims.toqueConfortavel,
              child: Icon(
                CupertinoIcons.line_horizontal_3,
                size: AureaDims.iconeLg,
                color: AureaCores.texto,
              ),
            ),
          ),
        ),
        const SizedBox(width: AureaDims.e6),
      ],
    ),
  );
}

/// O BOTAO DE CRIAR, na largura inteira: a primeira coisa depois da marca.
class _BotaoNovoProjeto extends StatelessWidget {
  const _BotaoNovoProjeto({required this.aoTocar});

  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(_margem, AureaDims.e4, _margem, 0),
    child: Tocavel(
      key: const ValueKey('novo-projeto'),
      haptico: true,
      onTap: aoTocar,
      child: Container(
        height: 52,
        decoration: BoxDecoration(
          color: AureaCores.acao,
          borderRadius: BorderRadius.circular(AureaDims.raioPilula),
        ),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(
              CupertinoIcons.plus,
              size: AureaDims.iconeMd,
              color: AureaCores.sobreAcao,
            ),
            const SizedBox(width: AureaDims.e8),
            Flexible(
              child: AppText(
                'Novo projeto',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  fontSize: 16,
                  fontWeight: FontWeight.w600,
                  color: AureaCores.sobreAcao,
                ),
              ),
            ),
          ],
        ),
      ),
    ),
  );
}

/// "Projetos recentes", a busca e a ordem.
class _CabecalhoDaLista extends StatelessWidget {
  const _CabecalhoDaLista({
    required this.procurando,
    required this.busca,
    required this.aoProcurar,
    required this.aoDigitar,
    required this.aoFecharBusca,
    required this.aoOrdenar,
  });

  final bool procurando;
  final TextEditingController busca;
  final VoidCallback aoProcurar;
  final ValueChanged<String> aoDigitar;
  final VoidCallback aoFecharBusca;
  final void Function(BuildContext ancora) aoOrdenar;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(
      _margem,
      AureaDims.e20,
      AureaDims.e6,
      AureaDims.e6,
    ),
    child: SizedBox(
      height: AureaDims.toqueConfortavel,
      child: Row(
        children: [
          if (procurando)
            Expanded(
              child: CupertinoTextField(
                key: const ValueKey('projetos-busca-campo'),
                controller: busca,
                autofocus: true,
                placeholder: translate(context, 'Procurar pelo nome'),
                style: AureaEstilos.corpo.copyWith(fontSize: 14),
                placeholderStyle: AureaEstilos.corpo.copyWith(
                  fontSize: 14,
                  color: AureaCores.textoSecundario,
                ),
                cursorColor: AureaCores.destaque,
                padding: const EdgeInsets.symmetric(
                  horizontal: AureaDims.e10,
                  vertical: AureaDims.e8,
                ),
                decoration: BoxDecoration(
                  color: AureaCores.campo,
                  borderRadius: BorderRadius.circular(AureaDims.raioPilula),
                ),
                onChanged: aoDigitar,
              ),
            )
          else
            Expanded(
              child: AppText(
                'Projetos recentes',
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: AureaEstilos.titulo.copyWith(fontSize: 16),
              ),
            ),
          _BotaoDeIcone(
            chave: procurando ? 'projetos-busca-limpar' : 'projetos-buscar',
            icone: procurando ? CupertinoIcons.xmark : CupertinoIcons.search,
            aoTocar: (_) => procurando ? aoFecharBusca() : aoProcurar(),
          ),
          _BotaoDeIcone(
            chave: 'projetos-ordenar',
            icone: CupertinoIcons.arrow_up_arrow_down,
            aoTocar: aoOrdenar,
          ),
        ],
      ),
    ),
  );
}

/// Escolhendo varios: quantos, marcar todos e sair.
class _CabecalhoDaSelecao extends StatelessWidget {
  const _CabecalhoDaSelecao({
    required this.quantos,
    required this.aoMarcarTodos,
    required this.aoSair,
  });

  final int quantos;
  final VoidCallback aoMarcarTodos;
  final VoidCallback aoSair;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(
      _margem,
      AureaDims.e20,
      AureaDims.e6,
      AureaDims.e6,
    ),
    child: SizedBox(
      height: AureaDims.toqueConfortavel,
      child: Row(
        children: [
          Expanded(
            child: AppTextMoldado(
              '{0} escolhidos',
              [quantos],
              maxLines: 1,
              style: AureaEstilos.titulo.copyWith(fontSize: 16),
            ),
          ),
          _BotaoDeIcone(
            chave: 'projetos-marcar-todos',
            icone: CupertinoIcons.checkmark_circle,
            aoTocar: (_) => aoMarcarTodos(),
          ),
          _BotaoDeIcone(
            chave: 'projetos-selecao-sair',
            icone: CupertinoIcons.xmark,
            aoTocar: (_) => aoSair(),
          ),
        ],
      ),
    ),
  );
}

class _BotaoDeIcone extends StatelessWidget {
  const _BotaoDeIcone({
    required this.chave,
    required this.icone,
    required this.aoTocar,
  });

  final String chave;
  final IconData icone;
  final void Function(BuildContext ancora) aoTocar;

  @override
  Widget build(BuildContext context) => Tocavel(
    key: ValueKey(chave),
    onTap: () => aoTocar(context),
    child: SizedBox(
      width: AureaDims.toqueConfortavel,
      height: AureaDims.toqueConfortavel,
      child: Icon(
        icone,
        size: AureaDims.iconeMd,
        color: AureaCores.textoSecundario,
      ),
    ),
  );
}

class _Vazio extends StatelessWidget {
  const _Vazio({required this.procurando});

  final bool procurando;

  @override
  Widget build(BuildContext context) => Padding(
    padding: const EdgeInsets.fromLTRB(
      _margem,
      AureaDims.e20,
      _margem,
      0,
    ),
    child: Row(
      children: [
        Icon(
          procurando ? CupertinoIcons.search : CupertinoIcons.film,
          size: AureaDims.iconeLg,
          color: AureaCores.textoSecundario,
        ),
        const SizedBox(width: AureaDims.e10),
        Expanded(
          child: procurando
              ? AppText(
                  'Nenhum projeto com esse nome.',
                  style: AureaEstilos.corpo.copyWith(
                    color: AureaCores.textoSecundario,
                  ),
                )
              : AppText(
                  'Seus projetos aparecem aqui, com a miniatura do que voce fez.',
                  style: AureaEstilos.corpo.copyWith(
                    color: AureaCores.textoSecundario,
                  ),
                ),
        ),
      ],
    ),
  );
}

/// UM PROJETO: miniatura, nome como foi digitado, quadro e duracao, a
/// ultima edicao e o ⋯. Toque abre; toque longo comeca a escolher varios
/// (duplicar/excluir em lote), como na referencia.
class _CartaoDoProjeto extends StatelessWidget {
  const _CartaoDoProjeto({
    super.key,
    required this.projeto,
    required this.editadoEm,
    required this.escolhendo,
    required this.marcado,
    required this.aoAbrir,
    required this.aoSegurar,
    required this.aoMenu,
  });

  final VideoProject projeto;
  final DateTime editadoEm;
  final bool escolhendo;
  final bool marcado;
  final VoidCallback aoAbrir;
  final VoidCallback aoSegurar;
  final void Function(BuildContext ancora) aoMenu;

  @override
  Widget build(BuildContext context) {
    final secundario = AureaEstilos.corpo.copyWith(
      fontSize: 12,
      color: AureaCores.textoSecundario,
      fontFeatures: const [FontFeature.tabularFigures()],
    );
    return RepaintBoundary(
      child: Tocavel(
        onTap: aoAbrir,
        onLongPress: aoSegurar,
        encolhe: .985,
        child: Container(
          padding: const EdgeInsets.all(AureaDims.e8),
          decoration: BoxDecoration(
            color: marcado ? AureaCores.destaqueApagado : AureaCores.painel,
            borderRadius: BorderRadius.circular(AureaDims.raioXl),
          ),
          child: Row(
            children: [
              ClipRRect(
                borderRadius: BorderRadius.circular(AureaDims.raioLg),
                child: SizedBox.square(
                  dimension: _ladoDaMiniatura,
                  child: _Miniatura(projeto: projeto),
                ),
              ),
              const SizedBox(width: AureaDims.e10 + 2),
              Expanded(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    // O nome e da pessoa: Text, nunca o dicionario.
                    Text(
                      projeto.name,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: AureaEstilos.corpo.copyWith(
                        fontSize: 15,
                        fontWeight: FontWeight.w600,
                      ),
                    ),
                    const SizedBox(height: AureaDims.e4),
                    Text(
                      fichaDoProjeto(projeto),
                      key: ValueKey('projeto-ficha-${projeto.id}'),
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: secundario,
                    ),
                    const SizedBox(height: AureaDims.e2),
                    Row(
                      children: [
                        Icon(
                          CupertinoIcons.clock,
                          size: 12,
                          color: AureaCores.textoSecundario,
                        ),
                        const SizedBox(width: AureaDims.e4),
                        Flexible(
                          child: Text(
                            quandoFoiEditado(
                              context,
                              editadoEm,
                              DateTime.now(),
                            ),
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: secundario,
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
              if (escolhendo)
                SizedBox(
                  width: AureaDims.toqueConfortavel,
                  child: Icon(
                    key: ValueKey('projeto-marca-${projeto.id}'),
                    marcado
                        ? CupertinoIcons.checkmark_circle_fill
                        : CupertinoIcons.circle,
                    size: AureaDims.iconeMd,
                    color: marcado
                        ? AureaCores.destaque
                        : AureaCores.textoSecundario,
                  ),
                )
              else
                // O MENU TEM ALVO DE 44 e fica a vista: abrir, renomear,
                // duplicar, compartilhar e excluir nunca dependem de um
                // gesto escondido.
                Builder(
                  builder: (ancora) => Tocavel(
                    key: ValueKey('projeto-menu-${projeto.id}'),
                    onTap: () => aoMenu(ancora),
                    child: SizedBox(
                      width: AureaDims.toqueConfortavel,
                      height: _ladoDaMiniatura,
                      child: Icon(
                        CupertinoIcons.ellipsis,
                        size: AureaDims.iconeMd,
                        color: AureaCores.textoSecundario,
                      ),
                    ),
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

/// A MINIATURA de um projeto: escuta a revisao das miniaturas sozinha
/// (uma captura nova repinta o seu cartao, nao a lista) e decodifica no
/// tamanho do cartao. Sem miniatura, a moldura do formato do projeto.
class _Miniatura extends StatelessWidget {
  const _Miniatura({required this.projeto});

  final VideoProject projeto;

  @override
  Widget build(BuildContext context) {
    final r = projeto.aspectRatio > 0 ? projeto.aspectRatio : 16 / 9;
    // BoxFit.cover num quadrado: o lado curto da imagem enche o quadrado,
    // entao a imagem deitada precisa de largura proporcional.
    final largura =
        (_ladoDaMiniatura *
                MediaQuery.devicePixelRatioOf(context) *
                math.max(1.0, r))
            .ceil()
            .clamp(32, 640);
    final vazio = _MolduraVazia(ratio: r);
    return ColoredBox(
      color: AureaCores.campo,
      child: ValueListenableBuilder<int>(
        valueListenable: ThumbnailService.instance.revision,
        builder: (_, revisao, _) {
          final imagem = _Miniaturas.de(projeto.id, revisao, largura);
          if (imagem == null) return vazio;
          return Image(
            image: imagem,
            fit: BoxFit.cover,
            width: _ladoDaMiniatura,
            height: _ladoDaMiniatura,
            gaplessPlayback: true,
            filterQuality: FilterQuality.low,
            excludeFromSemantics: true,
            errorBuilder: (_, _, _) => vazio,
          );
        },
      ),
    );
  }
}

/// Antes da primeira miniatura: o formato do projeto desenhado no tom.
class _MolduraVazia extends StatelessWidget {
  const _MolduraVazia({required this.ratio});

  final double ratio;

  @override
  Widget build(BuildContext context) {
    const lado = _ladoDaMiniatura * .5;
    final r = ratio.clamp(.2, 5.0);
    return Center(
      child: Container(
        width: r >= 1 ? lado : lado * r,
        height: r >= 1 ? lado / r : lado,
        decoration: BoxDecoration(
          color: AureaCores.campoAlto,
          borderRadius: BorderRadius.circular(AureaDims.raioSm),
        ),
      ),
    );
  }
}

/// AS ACOES DO LOTE, presas embaixo enquanto ha projeto escolhido. Tom de
/// painel sobre o cromo — sem blur, sem borda.
class _AcoesEmLote extends StatelessWidget {
  const _AcoesEmLote({
    required this.quantos,
    required this.aoDuplicar,
    required this.aoExcluir,
  });

  final int quantos;
  final VoidCallback aoDuplicar;
  final VoidCallback aoExcluir;

  @override
  Widget build(BuildContext context) => Container(
    height: _alturaDoLote,
    color: AureaCores.painel,
    padding: const EdgeInsets.symmetric(horizontal: AureaDims.e6),
    child: Row(
      children: [
        const SizedBox(width: AureaDims.e10),
        Expanded(
          child: AppTextMoldado(
            '{0} escolhidos',
            [quantos],
            maxLines: 1,
            style: AureaEstilos.corpo.copyWith(
              color: AureaCores.textoSecundario,
            ),
          ),
        ),
        _AcaoDoLote(
          chave: 'projetos-lote-duplicar',
          icone: CupertinoIcons.plus_square_on_square,
          rotulo: 'Duplicar',
          cor: AureaCores.texto,
          aoTocar: aoDuplicar,
        ),
        _AcaoDoLote(
          chave: 'projetos-lote-excluir',
          icone: CupertinoIcons.trash,
          rotulo: 'Excluir',
          cor: AureaCores.perigo,
          aoTocar: aoExcluir,
        ),
      ],
    ),
  );
}

class _AcaoDoLote extends StatelessWidget {
  const _AcaoDoLote({
    required this.chave,
    required this.icone,
    required this.rotulo,
    required this.cor,
    required this.aoTocar,
  });

  final String chave;
  final IconData icone;
  final String rotulo;
  final Color cor;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Tocavel(
    key: ValueKey(chave),
    onTap: aoTocar,
    child: Padding(
      padding: const EdgeInsets.symmetric(
        horizontal: AureaDims.e10,
        vertical: AureaDims.e10,
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(icone, size: AureaDims.iconeMd, color: cor),
          const SizedBox(width: AureaDims.e6),
          AppText(rotulo, style: AureaEstilos.corpo.copyWith(color: cor)),
        ],
      ),
    ),
  );
}

/// Uma linha de folha: icone, texto e a seta. Alvo de 44.
class _LinhaDaFolha extends StatelessWidget {
  const _LinhaDaFolha({
    super.key,
    required this.icone,
    required this.texto,
    required this.aoTocar,
  });

  final IconData icone;
  final String texto;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Tocavel(
    onTap: aoTocar,
    child: Padding(
      padding: const EdgeInsets.symmetric(
        horizontal: AureaDims.margemDoPainel,
        vertical: AureaDims.e10 + 2,
      ),
      child: Row(
        children: [
          Icon(icone, size: AureaDims.iconeMd, color: AureaCores.destaque),
          const SizedBox(width: AureaDims.e15),
          Expanded(
            child: AppText(
              texto,
              maxLines: 2,
              overflow: TextOverflow.ellipsis,
              style: AureaEstilos.corpo.copyWith(fontSize: 14),
            ),
          ),
          Icon(
            CupertinoIcons.chevron_right,
            size: AureaDims.iconeSm,
            color: AureaCores.textoSecundario,
          ),
        ],
      ),
    ),
  );
}

/// Um modelo pronto: o quadro renderizado dele, o nome e o que tem.
class _LinhaDeModelo extends StatelessWidget {
  const _LinhaDeModelo({
    super.key,
    required this.imagem,
    required this.titulo,
    required this.detalhe,
    required this.aoTocar,
  });

  final String imagem;
  final String titulo;
  final String detalhe;
  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) {
    final dpr = MediaQuery.devicePixelRatioOf(context);
    return Tocavel(
      onTap: aoTocar,
      child: Padding(
        padding: const EdgeInsets.symmetric(
          horizontal: AureaDims.margemDoPainel,
          vertical: AureaDims.e6,
        ),
        child: Row(
          children: [
            ClipRRect(
              borderRadius: BorderRadius.circular(AureaDims.raioLg),
              child: Container(
                width: 80,
                height: 45,
                color: AureaCores.campo,
                child: Image.asset(
                  imagem,
                  fit: BoxFit.cover,
                  cacheWidth: (80 * dpr).ceil(),
                  filterQuality: FilterQuality.low,
                  excludeFromSemantics: true,
                  errorBuilder: (_, _, _) => const SizedBox.shrink(),
                ),
              ),
            ),
            const SizedBox(width: AureaDims.e10 + 2),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  AppText(
                    titulo,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: AureaEstilos.corpo.copyWith(
                      fontSize: 14,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  const SizedBox(height: AureaDims.e2),
                  AppText(
                    detalhe,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: AureaEstilos.corpo.copyWith(
                      fontSize: 12,
                      color: AureaCores.textoSecundario,
                    ),
                  ),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}
