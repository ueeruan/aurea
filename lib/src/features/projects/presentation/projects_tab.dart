import 'dart:io';
import 'dart:ui' show ImageFilter;

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';

import '../../../core/l10n/app_language.dart';
import '../../../core/theme/app_theme.dart';
import '../../../core/ui/tocavel.dart';
import '../../../core/widgets/aurea_logo.dart';
import '../../about/presentation/report_sheet.dart';
import '../../community/application/conta_da_comunidade.dart';
import '../../editor/application/editor_controller.dart';
import '../../editor/domain/ajuste_da_midia.dart';
import '../../editor/domain/template_pack.dart';
import '../../editor/domain/video_project.dart';
import '../../editor/presentation/editor_screen.dart';
import '../../media/application/media_import_service.dart' show proporcaoDaFoto;
import '../../tutoriais/presentation/tutorial_screen.dart';
import '../application/dnyx_remix_assets.dart';
import '../application/projects_controller.dart';
import '../application/projects_view.dart';
import '../application/reference_rebuild_assets.dart';
import '../application/thumbnail_service.dart';
import '../application/vhf_motion_assets.dart';
import '../domain/campo_arvore_template.dart';
import '../domain/abyss_cinematic_template.dart';
import '../domain/cena_xml_import.dart';
import '../domain/colina_tv_template.dart';
import '../domain/deriva_template.dart';
import '../domain/flor_template.dart';
import '../domain/mao_enterrada_template.dart';
import '../domain/monolito_template.dart';
import '../domain/notes_motion_template.dart';
import '../domain/pacote_aurea.dart';
import '../domain/pacote_zip_import.dart';
import '../domain/pindown_motion_template.dart';
import '../domain/prisma_template.dart';
import '../domain/project_presets.dart';
import 'home_shell.dart';
import 'new_project_sheet.dart';
import 'whats_new.dart';

/// A INICIO, do jeito de um app de video: o titulo, UM botao de criar,
/// os projetos recentes numa lista com a miniatura DE VERDADE e um menu
/// que se ve, os modelos com um quadro renderizado.
///
/// Reescrita otimizada (mesmo visual, mesmo contrato de chaves):
/// - [ProjectsTab] virou [ConsumerStatefulWidget]: o
///   `ThumbnailService.init()` roda UMA vez no [initState], nao a cada
///   build; antes ele era chamado dentro do `build`.
/// - cada cartao escuta o [ThumbnailService.revision] sozinho
///   ([_ThumbProjeto]): uma miniatura nova repinta UM cartao, nao a
///   grade inteira (antes um ValueListenableBuilder envolvia o SliverGrid).
/// - o avatar do cabecalho ([_AvatarDaConta]) checa o arquivo uma vez
///   por caminho, nao com `File.existsSync()` a cada build.
/// - a lista de modelos e `const` + `ListView.builder` com `cacheExtent`:
///   nada de 13 `Image.asset` montados de uma vez fora da tela.
/// - um unico caminho de abertura ([_abrirNoEditor]) em vez de
///   add+open+navigate repetidos em 8 lugares.
/// - [RepaintBoundary] nos cartoes e barras com blur; estilos `static const`.
class ProjectsTab extends ConsumerStatefulWidget {
  const ProjectsTab({super.key});

  @override
  ConsumerState<ProjectsTab> createState() => _ProjectsTabState();
}

class _ProjectsTabState extends ConsumerState<ProjectsTab> {
  @override
  void initState() {
    super.initState();
    // Uma vez por montagem — nunca no build.
    ThumbnailService.instance.init();
  }

  // -- Abertura ---------------------------------------------------------

  void _abrirNoEditor(BuildContext context, VideoProject project, {bool add = true}) {
    if (add) ref.read(projectsControllerProvider.notifier).add(project);
    ref.read(editorControllerProvider.notifier).openProject(project);
    if (!context.mounted) return;
    Navigator.of(context).push(MaterialPageRoute(builder: (_) => const EditorScreen()));
  }

  Future<void> _createProject(BuildContext context, int total, {String? nomeSugerido}) async {
    final project = await showNewProjectSheet(
      context,
      nomeSugerido: nomeSugerido ?? 'Projeto ${total + 1}',
    );
    if (project == null || !context.mounted) return;
    _abrirNoEditor(context, project);
  }

  /// MODELO PRONTO: um projeto montado por codigo, com id novo a cada
  /// abertura — o mesmo cuidado do template em arquivo.
  void _abrirModelo(BuildContext context, VideoProject modelo) {
    _abrirNoEditor(context, modelo.comIdNovo());
  }

  void _falha(BuildContext context, String texto) {
    if (!context.mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: AppText(texto)));
  }

  /// O CAMPO ABRE DIRETO — sem a espera de "Preparando o campo 3D".
  ///
  /// A ESPERA EXISTIA POR CAUSA DE UM ARQUIVO: o campo copiava um scan de
  /// arvore do bundle para o disco e o importava em isolate antes de
  /// montar a cena. A arvore do campo e gerada em codigo desde antes — o
  /// scan era o primeiro plano. Sem ele, nao ha o que preparar.
  void _openCampo(BuildContext context) {
    _abrirModelo(context, buildCampoArvoreTemplate());
  }

  Future<void> _openVhfMotion(BuildContext context) async {
    try {
      final model = await prepareVhfMotion();
      if (!context.mounted) return;
      _abrirModelo(context, model);
    } catch (_) {
      _falha(context, 'Nao consegui preparar o motion VHF. Tente novamente.');
    }
  }

  /// A Deriva abre DIRETO: o explorador e malha propria, gerada em
  /// codigo. Antes daqui saia uma espera para converter um arquivo
  /// importado em disco — e, quando ele faltava, a cena abria com uma
  /// capsula no lugar do astronauta.
  void _openDeriva(BuildContext context) {
    _abrirModelo(context, buildDerivaTemplate());
  }

  /// O Monolito abre DIRETO, pelo mesmo motivo: a floresta, o explorador
  /// e o bloco sao gerados em codigo.
  void _openMonolito(BuildContext context) {
    _abrirModelo(context, buildMonolitoTemplate());
  }

  /// Prepara a trilha empacotada antes de abrir a nova recriacao.
  Future<void> _openDnyxRemix(BuildContext context) async {
    try {
      final model = await prepareDnyxRemix();
      if (!context.mounted) return;
      _abrirModelo(context, model);
    } catch (_) {
      _falha(context, 'Nao consegui preparar o motion. Tente abrir novamente.');
    }
  }

  /// Prepara a trilha empacotada antes de abrir a nova recriacao.
  Future<void> _openReferenceRebuild(BuildContext context) async {
    try {
      final model = await prepareReferenceRebuild();
      if (!context.mounted) return;
      _abrirModelo(context, model);
    } catch (_) {
      _falha(context, 'Nao consegui preparar a trilha. Tente abrir o modelo novamente.');
    }
  }

  void _openModeloId(BuildContext context, String id) {
    switch (id) {
      case 'campo':
        _openCampo(context);
      case 'flor':
        _abrirModelo(context, buildFlorTemplate());
      case 'prisma':
        _abrirModelo(context, buildPrismaTemplate());
      case 'deriva':
        _openDeriva(context);
      case 'monolito':
        _openMonolito(context);
      case 'colina':
        _abrirModelo(context, buildColinaTvTemplate());
      case 'mao':
        _abrirModelo(context, buildMaoEnterradaTemplate());
      case 'abismo':
        _abrirModelo(context, buildAbyssCinematicTemplate());
      case 'vhf':
        _openVhfMotion(context);
      case 'dnyx':
        _openDnyxRemix(context);
      case 'reference':
        _openReferenceRebuild(context);
      case 'notes':
        _abrirModelo(context, buildNotesMotionTemplate());
      case 'pindown':
        _abrirModelo(context, buildPindownMotionTemplate());
    }
  }

  /// ABRIR TEMPLATE: o arquivo vira um projeto NOVO, com id novo.
  ///
  /// Sem id novo, abrir o mesmo template duas vezes sobrescreveria o
  /// trabalho da primeira vez — e a pessoa perderia o que fez sem
  /// entender por que.
  Future<void> _openTemplate(BuildContext context) async {
    final r = await FilePicker.platform.pickFiles(
      type: Platform.isIOS ? FileType.any : FileType.custom,
      allowedExtensions: Platform.isIOS ? null : const ['json', 'aurea'],
    );
    final caminho = r?.files.single.path;
    if (caminho == null || !context.mounted) return;
    final ext = caminho.split('.').last.toLowerCase();
    if (ext != 'aurea' && ext != 'json') {
      _falha(context, 'Escolha um arquivo .aurea');
      return;
    }

    TemplatePack? pack;
    try {
      pack = TemplatePack.decode(await File(caminho).readAsString());
    } catch (_) {
      pack = null;
    }
    if (!context.mounted) return;
    if (pack == null) {
      _falha(context, 'Nao consegui ler esse template');
      return;
    }
    final packNaoNulo = pack;
    _abrirNoEditor(context, packNaoNulo.project.copyWith(name: packNaoNulo.name).comIdNovo());
  }

  /// IMPORTAR MÍDIA: escolhe foto ou vídeo da galeria e já inicia um
  /// projeto novo com a mídia inserida no palco.
  Future<void> _importarMidia(BuildContext context) async {
    try {
      final r = await FilePicker.platform.pickFiles(
        type: FileType.media,
        allowMultiple: false,
      );
      final caminho = r?.files.single.path;
      if (caminho == null || !context.mounted) return;
      final nome = r?.files.single.name ?? 'Mídia Importada';
      final controller = ref.read(editorControllerProvider.notifier);
      final ext = caminho.split('.').last.toLowerCase();
      final video = _extensoesDeVideo.contains(ext);
      // O PROJETO TEM A PROPORCAO DA MIDIA: a previa abre cheia, sem faixa.
      // Era 9:16 sempre, e o video deitado ficava pequeno no meio da tela.
      final sonda = video ? await controller.sondarVideo(caminho) : null;
      final proporcao = video ? sonda!.proporcao : await proporcaoDaFoto(caminho);
      if (!context.mounted) return;
      final projeto = VideoProject(
        id: 'projeto-${DateTime.now().millisecondsSinceEpoch}',
        name: nome.replaceAll(RegExp(r'\.[^.]+$'), ''),
        createdAt: DateTime.now(),
        fps: 30,
        aspectRatio: proporcaoDoProjeto(proporcao),
      );
      ref.read(projectsControllerProvider.notifier).add(projeto);
      controller.openProject(projeto);
      if (video) {
        final duracao = sonda!.duracao > Duration.zero ? sonda.duracao : const Duration(seconds: 5);
        controller.addVideoLayer(
          Duration.zero,
          caminho,
          nome,
          duracao,
          fonte: sonda.duracao > Duration.zero ? sonda.duracao : null,
          proporcao: proporcao,
        );
      } else {
        controller.addImageLayer(Duration.zero, caminho, nome, proporcao: proporcao);
      }
      Navigator.of(context).push(MaterialPageRoute(builder: (_) => const EditorScreen()));
    } catch (_) {
      _falha(context, 'Não consegui importar essa mídia.');
    }
  }

  static const _extensoesDeVideo = {
    'mp4', 'mov', 'm4v', 'avi', 'mkv', 'webm', '3gp',
  };

  /// CENA EM XML: le o que reasonhece, mostra o balanco (camadas,
  /// keyframes, o que ficou de fora) e abre como projeto novo.
  Future<void> _importarCena(BuildContext context) async {
    final r = await FilePicker.platform.pickFiles(type: FileType.any);
    final caminho = r?.files.single.path;
    if (caminho == null || !context.mounted) return;
    final nomeArquivo = caminho.split(RegExp(r'[\\/]')).last;
    final nomeLimpo = nomeArquivo.replaceAll(
      RegExp(r'\.(xml|zip|amproj|aurea)$', caseSensitive: false),
      '',
    );
    CenaXmlResult resultado;
    try {
      // PACOTE (.amproj, zip com XML) ou PACOTE .aurea: as midias vem
      // junto e entram de verdade. XML solto continua como sempre.
      final minusculo = nomeArquivo.toLowerCase();
      if (minusculo.endsWith('.aurea')) {
        final projeto = PacoteAurea.abrir(
          await File(caminho).readAsBytes(),
          await _pastaDasMidiasImportadas(),
        );
        if (!context.mounted) return;
        _abrirNoEditor(context, projeto);
        return;
      }
      if (minusculo.endsWith('.zip') || minusculo.endsWith('.amproj')) {
        resultado = CenaEmZip.abrir(
          await File(caminho).readAsBytes(),
          await _pastaDasMidiasImportadas(),
          nome: nomeLimpo,
        );
      } else {
        final texto = await File(caminho).readAsString();
        resultado = importarCenaXml(texto, nome: nomeLimpo);
      }
    } on FormatException catch (e) {
      if (!context.mounted) return;
      await _aviso(context, 'Nao deu para importar', e.message);
      return;
    } on CenaXmlException catch (e) {
      if (!context.mounted) return;
      await _aviso(context, 'Nao deu para importar', e.message);
      return;
    } catch (e) {
      if (!context.mounted) return;
      await _aviso(context, 'Nao deu para importar', 'Arquivo ilegivel: $e');
      return;
    }
    if (!context.mounted) return;
    final abrir = await showCupertinoDialog<bool>(
      context: context,
      builder: (c) => CupertinoAlertDialog(
        title: const AppText('Cena importada'),
        content: Padding(
          padding: const EdgeInsets.only(top: 8),
          child: AppText(_resumoDaImportacao(resultado), textAlign: TextAlign.left),
        ),
        actions: [
          CupertinoDialogAction(
            onPressed: () => Navigator.of(c).pop(false),
            child: const AppText('Cancelar'),
          ),
          CupertinoDialogAction(
            isDefaultAction: true,
            onPressed: () => Navigator.of(c).pop(true),
            child: const AppText('Abrir'),
          ),
        ],
      ),
    );
    if (abrir != true || !context.mounted) return;
    _abrirNoEditor(context, resultado.project);
  }

  /// Onde as midias que chegam em pacote moram no aparelho.
  static Future<Directory> _pastaDasMidiasImportadas() async {
    final docs = await getApplicationDocumentsDirectory();
    return Directory('${docs.path}/midias_importadas/${DateTime.now().millisecondsSinceEpoch}');
  }

  static String _resumoDaImportacao(CenaXmlResult r) {
    final b = StringBuffer()
      ..write('${r.layersImported} camadas e ')
      ..write('${r.keyframesImported} keyframes reconhecidos.');
    if (r.ignored.isNotEmpty) {
      b.write('\n\nFicou de fora:');
      for (final item in r.ignored.take(6)) {
        b.write('\n- $item');
      }
      if (r.ignored.length > 6) {
        b.write('\n- e mais ${r.ignored.length - 6}');
      }
    }
    return b.toString();
  }

  Future<void> _aviso(BuildContext context, String titulo, String texto) =>
      showCupertinoDialog<void>(
        context: context,
        builder: (c) => CupertinoAlertDialog(
          title: AppText(titulo),
          content: Padding(padding: const EdgeInsets.only(top: 8), child: AppText(texto)),
          actions: [
            CupertinoDialogAction(
              isDefaultAction: true,
              onPressed: () => Navigator.of(c).pop(),
              child: const AppText('OK'),
            ),
          ],
        ),
      );

  void _openProject(BuildContext context, VideoProject project) {
    ref.read(editorControllerProvider.notifier).openProject(project);
    Navigator.of(context).push(MaterialPageRoute(builder: (_) => const EditorScreen()));
  }

  // -- Mutacoes ----------------------------------------------------------

  Future<void> _confirmarExclusao(BuildContext context, VideoProject project) async {
    final apaga = await showCupertinoModalPopup<bool>(
      context: context,
      builder: (c) => CupertinoActionSheet(
        title: Text(project.name),
        actions: [
          CupertinoActionSheetAction(
            isDestructiveAction: true,
            onPressed: () => Navigator.of(c).pop(true),
            child: const AppText('Excluir projeto'),
          ),
          CupertinoActionSheetAction(
            isDestructiveAction: true,
            onPressed: () {
              Navigator.of(c).pop(false);
              apagarTodosOsProjetos(context, ref);
            },
            child: const AppText('Apagar todos os projetos'),
          ),
        ],
        cancelButton: CupertinoActionSheetAction(
          onPressed: () => Navigator.of(c).pop(false),
          child: const AppText('Cancelar'),
        ),
      ),
    );
    if (apaga != true) return;
    ref.read(projectsControllerProvider.notifier).remove(project.id);
    ThumbnailService.instance.delete(project.id);
  }

  /// DUPLICAR: o mesmo projeto com id novo, na frente da lista.
  /// MARCAR/DESMARCAR um projeto (e sair do modo ao desmarcar o ultimo).
  void _marcar(String id) {
    final atual = {...ref.read(selecaoDeProjetosProvider)};
    if (!atual.add(id)) atual.remove(id);
    ref.read(selecaoDeProjetosProvider.notifier).state = atual;
  }

  /// EXCLUIR OS ESCOLHIDOS, com uma pergunta so para o lote inteiro.
  Future<void> _excluirEscolhidos(BuildContext context, Set<String> ids) async {
    final apaga = await showCupertinoModalPopup<bool>(
      context: context,
      builder: (c) => CupertinoActionSheet(
        title: AppText('Excluir ${ids.length} projetos?'),
        message: const AppText('Nao da para desfazer.'),
        actions: [
          CupertinoActionSheetAction(
            key: const ValueKey('projetos-excluir-lote'),
            isDestructiveAction: true,
            onPressed: () => Navigator.of(c).pop(true),
            child: const AppText('Excluir'),
          ),
        ],
        cancelButton: CupertinoActionSheetAction(
          onPressed: () => Navigator.of(c).pop(false),
          child: const AppText('Cancelar'),
        ),
      ),
    );
    if (apaga != true) return;
    final controlador = ref.read(projectsControllerProvider.notifier);
    for (final id in ids) {
      controlador.remove(id);
    }
    ref.read(selecaoDeProjetosProvider.notifier).state = const {};
  }

  void _duplicar(VideoProject project) {
    final copia = project.copyWith(name: '${project.name} (cópia)').comIdNovo();
    ref.read(projectsControllerProvider.notifier).add(copia);
  }

  Future<void> _renomear(BuildContext context, VideoProject project) async {
    final nome = await showCupertinoDialog<String>(
      context: context,
      builder: (_) => _DialogoDeNome(inicial: project.name),
    );
    final limpo = nome?.trim();
    if (limpo == null || limpo.isEmpty || limpo == project.name) return;
    ref.read(projectsControllerProvider.notifier).upsert(project.copyWith(name: limpo));
  }

  /// O MENU DO PROJETO, no botao de reticencias e no toque longo. Antes
  /// so existia "segurar para excluir", que ninguem descobre sozinho.
  Future<void> _menuDoProjeto(BuildContext context, VideoProject project) async {
    final acao = await showCupertinoModalPopup<String>(
      context: context,
      builder: (c) => CupertinoActionSheet(
        title: Text(project.name),
        message: AppText(fichaDoProjeto(project)),
        actions: [
          for (final (rotulo, chave) in const [
            ('Abrir', 'abrir'),
            ('Duplicar', 'duplicar'),
            ('Renomear', 'renomear'),
          ])
            CupertinoActionSheetAction(
              key: ValueKey('projeto-$chave'),
              onPressed: () => Navigator.of(c).pop(chave),
              child: AppText(rotulo),
            ),
          CupertinoActionSheetAction(
            key: const ValueKey('projeto-excluir'),
            isDestructiveAction: true,
            onPressed: () => Navigator.of(c).pop('excluir'),
            child: const AppText('Excluir projeto'),
          ),
          CupertinoActionSheetAction(
            isDestructiveAction: true,
            onPressed: () {
              Navigator.of(c).pop(false);
              apagarTodosOsProjetos(context, ref);
            },
            child: const AppText('Apagar todos os projetos'),
          ),
        ],
        cancelButton: CupertinoActionSheetAction(
          onPressed: () => Navigator.of(c).pop(),
          child: const AppText('Cancelar'),
        ),
      ),
    );
    if (acao == null || !context.mounted) return;
    switch (acao) {
      case 'abrir':
        _openProject(context, project);
      case 'duplicar':
        _duplicar(project);
      case 'renomear':
        await _renomear(context, project);
      case 'excluir':
        await _confirmarExclusao(context, project);
    }
  }

  // -- Build -------------------------------------------------------------

  @override
  Widget build(BuildContext context) {
    final todos = ref.watch(projectsControllerProvider);
    final mostrarTodos = ref.watch(_mostrarTodosProvider);
    final ordem = ref.watch(ordemDosProjetosProvider);
    final busca = ref.watch(buscaDeProjetosProvider);
    final selecao = ref.watch(selecaoDeProjetosProvider);
    final selecionando = selecao.isNotEmpty;
    final projects = projetosArrumados(todos, ordem, busca);
    // O HEROI "Continuar editando": o projeto mais recente vira um cartao
    // largo em cima; a grade mostra o resto, sem repetir. Procurando,
    // ordenando por outra coisa ou escolhendo varios, nao ha heroi: a
    // lista inteira e o assunto.
    final emBuscaOuOrdem = busca.trim().isNotEmpty || ordem != OrdemDosProjetos.recentes;
    final heroi = projects.isEmpty || selecionando || emBuscaOuOrdem ? null : projects.first;
    // Com busca, ordem ou selecao, a lista nao esconde nada atras do
    // "mostrar todos".
    final abertos = mostrarTodos || selecionando || emBuscaOuOrdem;
    final heroiId = heroi?.id;
    final visiveis = [
      for (final p in abertos ? projects : projects.take(_recentesNaInicio))
        if (p.id != heroiId) p,
    ];
    final largura = MediaQuery.sizeOf(context).width;
    final colunas = largura >= 700 ? 4 : (largura >= 520 ? 3 : 2);

    return SafeArea(
      bottom: false,
      child: Stack(
        children: [
          // A BARRA DAS ACOES EM LOTE fica por cima da lista, e so
          // existe enquanto ha projeto escolhido.
          _BarraAoRolar(
            onTemplate: () => _openTemplate(context),
            onPerfil: () => ref.read(homeTabProvider.notifier).state = 3,
            child: CustomScrollView(
              slivers: [
                SliverToBoxAdapter(
                  child: _Cabecalho(onTemplate: () => _openTemplate(context)),
                ),
                // UM BOTAO DE CRIAR, na largura inteira.
                SliverToBoxAdapter(
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(20, 6, 20, 0),
                    child: SizedBox(
                      height: 54,
                      width: double.infinity,
                      child: FilledButton.icon(
                        key: const ValueKey('novo-projeto'),
                        style: FilledButton.styleFrom(
                          shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(16),
                          ),
                        ),
                        icon: const Icon(CupertinoIcons.plus, size: 20),
                        label: const AppText('Novo projeto'),
                        onPressed: () => _createProject(context, projects.length),
                      ),
                    ),
                  ),
                ),
                // ATALHOS: icone num circulo e o nome embaixo. Sem caixa, sem borda.
                SliverToBoxAdapter(
                  child: Padding(
                    padding: const EdgeInsets.fromLTRB(12, 18, 12, 0),
                    child: Row(
                      children: [
                        Expanded(
                          child: _Atalho(
                            icon: CupertinoIcons.photo_on_rectangle,
                            rotulo: translate(context, 'Mídia'),
                            onTap: () => _importarMidia(context),
                          ),
                        ),
                        Expanded(
                          child: _Atalho(
                            icon: CupertinoIcons.doc_on_doc,
                            rotulo: 'Template',
                            onTap: () => _openTemplate(context),
                          ),
                        ),
                        Expanded(
                          child: _Atalho(
                            icon: CupertinoIcons.cube,
                            rotulo: 'Cena 3D',
                            onTap: () => _importarCena(context),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
                if (heroi != null)
                  SliverToBoxAdapter(
                    child: _CartaoContinuar(
                      key: ValueKey('projeto-${heroi.id}'),
                      project: heroi,
                      onOpen: () => _openProject(context, heroi),
                      onMenu: () => _menuDoProjeto(context, heroi),
                    ),
                  ),
                if (todos.length > 1)
                  SliverToBoxAdapter(
                    child: _BarraDaLista(
                      total: projects.length,
                      ordem: ordem,
                      busca: busca,
                      selecao: selecao,
                      projetos: projects,
                    ),
                  ),
                if (projects.isEmpty)
                  const SliverToBoxAdapter(child: _SemProjetos())
                else
                  SliverPadding(
                    padding: const EdgeInsets.symmetric(horizontal: 20),
                    sliver: SliverGrid(
                      gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                        crossAxisCount: colunas,
                        mainAxisSpacing: 16,
                        crossAxisSpacing: 12,
                        // miniatura 16:10 + duas linhas de texto
                        childAspectRatio: 1.12,
                      ),
                      delegate: SliverChildBuilderDelegate(
                        (context, i) {
                          final project = visiveis[i];
                          final marcado = selecao.contains(project.id);
                          return _CartaoProjeto(
                            key: ValueKey('projeto-${project.id}'),
                            project: project,
                            marcado: marcado,
                            escolhendo: selecionando,
                            onOpen: () => selecionando
                                ? _marcar(project.id)
                                : _openProject(context, project),
                            onMenu: () => selecionando
                                ? _marcar(project.id)
                                : _menuDoProjeto(context, project),
                            onMarcar: () => _marcar(project.id),
                          );
                        },
                        childCount: visiveis.length,
                        // Nao mantem estado fora da tela; a borda repinta sozinha.
                        addAutomaticKeepAlives: false,
                        addRepaintBoundaries: true,
                      ),
                    ),
                  ),
                if (!abertos && projects.length > _recentesNaInicio)
                  SliverToBoxAdapter(
                    child: _Linha(
                      key: const ValueKey('projetos-todos'),
                      icon: mostrarTodos
                          ? CupertinoIcons.chevron_up
                          : CupertinoIcons.square_grid_2x2,
                      texto: mostrarTodos
                          ? 'Mostrar menos'
                          : 'Mostrar todos os ${projects.length} projetos',
                      onTap: () =>
                          ref.read(_mostrarTodosProvider.notifier).state = !mostrarTodos,
                    ),
                  ),
                // MODELOS: motions inteiros montados camada por camada.
                const SliverToBoxAdapter(child: _TituloSecao('Modelos')),
                SliverToBoxAdapter(
                  child: SizedBox(
                    height: 196,
                    child: ListView.builder(
                      scrollDirection: Axis.horizontal,
                      padding: const EdgeInsets.symmetric(horizontal: 20),
                      addRepaintBoundaries: true,
                      itemCount: _modelos.length,
                      itemBuilder: (context, i) {
                        final m = _modelos[i];
                        return _CartaoModelo(
                          imagem: m.imagem,
                          titulo: m.titulo,
                          detalhe: m.detalhe,
                          onTap: () => _openModeloId(context, m.id),
                        );
                      },
                    ),
                  ),
                ),
                const SliverToBoxAdapter(child: _TituloSecao('Comunidade')),
                SliverToBoxAdapter(
                  child: _LinhaGrande(
                    icon: CupertinoIcons.person_2_fill,
                    titulo: 'Veja o que a galera está criando',
                    subtitulo: 'Poste o seu projeto, responda e reposte',
                    // A ABA DA COMUNIDADE E A 1. O atalho antigo mandava para a
                    // 2 — que e Ajustes.
                    onTap: () => ref.read(homeTabProvider.notifier).state = 1,
                  ),
                ),
                const SliverToBoxAdapter(child: _TituloSecao('Aprender')),
                SliverToBoxAdapter(
                  child: Column(
                    children: [
                      _Linha(
                        key: const ValueKey('inicio-tutorial-cena3d'),
                        icon: CupertinoIcons.play_rectangle,
                        texto: 'Tutorial em vídeo: sua primeira cena 3D',
                        onTap: () => Navigator.of(context).push(
                          MaterialPageRoute<void>(
                            builder: (_) => const TutorialScreen(id: 'cena3d'),
                          ),
                        ),
                      ),
                      _Linha(
                        key: const ValueKey('inicio-tutorial-cena-completa'),
                        icon: CupertinoIcons.cube_box,
                        texto: 'Tutorial em vídeo: cena 3D com modelos e câmeras',
                        onTap: () => Navigator.of(context).push(
                          MaterialPageRoute<void>(
                            builder: (_) => const TutorialScreen(id: 'cena-completa'),
                          ),
                        ),
                      ),
                      _Linha(
                        key: const ValueKey('inicio-tutorial-texto-bounce'),
                        icon: CupertinoIcons.textformat,
                        texto: 'Tutorial em vídeo: texto que quica, do seu jeito',
                        onTap: () => Navigator.of(context).push(
                          MaterialPageRoute<void>(
                            builder: (_) => const TutorialScreen(id: 'texto-bounce'),
                          ),
                        ),
                      ),
                      _Linha(
                        icon: CupertinoIcons.sparkles,
                        texto: 'O que ha de novo nesta versao',
                        onTap: () => showWhatsNewSheet(context),
                      ),
                      _Linha(
                        icon: CupertinoIcons.exclamationmark_bubble,
                        texto: 'Versao beta: achou um problema? Conte pra gente',
                        onTap: () => showReportSheet(context),
                      ),
                    ],
                  ),
                ),
                const SliverToBoxAdapter(child: SizedBox(height: 120)),
              ],
            ),
          ),
          if (selecionando)
            Positioned(
              left: 0,
              right: 0,
              bottom: 0,
              child: _AcoesEmLote(
                quantos: selecao.length,
                onDuplicar: () {
                  for (final p in todos) {
                    if (selecao.contains(p.id)) _duplicar(p);
                  }
                  ref.read(selecaoDeProjetosProvider.notifier).state = const {};
                },
                onExcluir: () => _excluirEscolhidos(context, selecao),
              ),
            ),
        ],
      ),
    );
  }
}

/// Os 13 modelos prontos: so dados const — a abertura resolve por id.
const _modelos = [
  (
    id: 'campo',
    imagem: 'assets/templates/campo.jpg',
    titulo: 'CAMPO · A árvore da manhã',
    detalhe: '25 s · cinco tomadas · cenário 3D editável',
  ),
  (
    id: 'flor',
    imagem: 'assets/templates/flor.png',
    titulo: 'FLOR · Dez segundos de manhã',
    detalhe: '10 s · três tomadas · câmera na mão',
  ),
  (
    id: 'prisma',
    imagem: 'assets/templates/prisma.jpg',
    titulo: 'PRISMA · Dezessete segundos em loop',
    detalhe: '17 s · seis cenas 3D · loop abstrato',
  ),
  (
    id: 'deriva',
    imagem: 'assets/templates/deriva.jpg',
    titulo: 'DERIVA · O astronauta perdido',
    detalhe: '18 s · tres tomadas · luz de vacuo',
  ),
  (
    id: 'monolito',
    imagem: 'assets/templates/monolito.jpg',
    titulo: 'MONOLITO · O astronauta e a porta',
    detalhe: '16 s · noite, neblina e luz magenta',
  ),
  (
    id: 'colina',
    imagem: 'assets/templates/colina.jpg',
    titulo: 'COLINA · A TV no morro',
    detalhe: '6 s · cena 3D realista · orbita rasteira',
  ),
  (
    id: 'mao',
    imagem: 'assets/templates/mao-enterrada.png',
    titulo: 'MÃO ENTERRADA · Deserto ao entardecer',
    detalhe: '15 s · 4 câmeras · malha orgânica e céu',
  ),
  (
    id: 'abismo',
    imagem: 'assets/templates/abyss.jpg',
    titulo: 'ABISMO · Cinema 3D',
    detalhe: '14 s · 4 cameras · personagem com rig',
  ),
  (
    id: 'vhf',
    imagem: 'assets/templates/vhf/thumbnail.jpg',
    titulo: 'VHF · Neon Orbit',
    detalhe: '12 cenas · vetores e gradientes animados',
  ),
  (
    id: 'dnyx',
    imagem: 'assets/templates/dnyx/thumbnail.jpg',
    titulo: 'Aurea App · RMK Dnyx',
    detalhe: 'Texto, fotos, cursores e audio editaveis',
  ),
  (
    id: 'reference',
    imagem: 'assets/templates/reference-rebuild.jpg',
    titulo: 'Nova recriacao · Codex',
    detalhe: '5 cenas · 280 quadros · camadas editaveis',
  ),
  (
    id: 'notes',
    imagem: 'assets/templates/notes.jpg',
    titulo: 'Notes',
    detalhe: 'Icone, botao, listas, whip e glow',
  ),
  (
    id: 'pindown',
    imagem: 'assets/templates/notes.jpg',
    titulo: 'Pindown',
    detalhe: 'Casa, faisca medida e coroas 3D',
  ),
];

/// A BARRA QUE APARECE AO ROLAR (o compacto do titulo grande do iOS):
/// o cabecalho rola embora com a pagina; passando dele, uma barra fina
/// com blur surge presa em cima, com o nome e os mesmos dois botoes.
class _BarraAoRolar extends StatefulWidget {
  const _BarraAoRolar({
    required this.child,
    required this.onTemplate,
    required this.onPerfil,
  });

  final Widget child;
  final VoidCallback onTemplate;
  final VoidCallback onPerfil;

  @override
  State<_BarraAoRolar> createState() => _BarraAoRolarState();
}

class _BarraAoRolarState extends State<_BarraAoRolar> {
  bool _visivel = false;

  bool _aoRolar(ScrollNotification n) {
    if (n.metrics.axis != Axis.vertical) return false;
    final mostrar = n.metrics.pixels > 64;
    if (mostrar != _visivel) setState(() => _visivel = mostrar);
    return false;
  }

  @override
  Widget build(BuildContext context) => Stack(
        children: [
          NotificationListener<ScrollNotification>(
            onNotification: _aoRolar,
            child: widget.child,
          ),
          Positioned(
            left: 0,
            right: 0,
            top: 0,
            // AnimatedSwitcher, e nao opacidade: a barra escondida NAO PODE
            // continuar na arvore — tooltip e leitor de tela a achariam.
            child: AnimatedSwitcher(
              duration: const Duration(milliseconds: 180),
              switchInCurve: Curves.easeOut,
              switchOutCurve: Curves.easeIn,
              child: !_visivel
                  ? const SizedBox.shrink()
                  : RepaintBoundary(
                      child: ClipRect(
                        child: BackdropFilter(
                          filter: ImageFilter.blur(sigmaX: 18, sigmaY: 18),
                          child: Container(
                            height: 52,
                            padding: const EdgeInsets.fromLTRB(20, 0, 12, 0),
                            color: AppColors.background.withValues(alpha: .62),
                            child: Row(
                              children: [
                                const AureaLogo(size: 22),
                                const SizedBox(width: 8),
                                Text(
                                  'Aurea',
                                  style: TextStyle(
                                    fontSize: 17,
                                    fontWeight: FontWeight.w700,
                                    letterSpacing: -0.4,
                                    color: AppColors.onDark,
                                  ),
                                ),
                                const Spacer(),
                                _BotaoRedondo(
                                  icon: CupertinoIcons.doc_on_doc,
                                  tooltip: 'Template',
                                  onTap: widget.onTemplate,
                                ),
                                const SizedBox(width: 2),
                                _BotaoRedondo(
                                  icon: CupertinoIcons.person_crop_circle,
                                  tooltip: 'Perfil',
                                  onTap: widget.onPerfil,
                                ),
                              ],
                            ),
                          ),
                        ),
                      ),
                    ),
            ),
          ),
        ],
      );
}

/// Miniatura de UM projeto: escuta o [ThumbnailService.revision] sozinha.
///
/// Antes, um unico ValueListenableBuilder envolvia o SliverGrid inteiro:
/// qualquer miniatura nova reconstruia TODOS os cartoes. Agora cada
/// cartao repinta so o seu [Image].
class _ThumbProjeto extends StatelessWidget {
  const _ThumbProjeto({
    required this.projectId,
    required this.hero,
    required this.placeholder,
  });

  final String projectId;
  final bool hero;
  final Widget placeholder;

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<int>(
      valueListenable: ThumbnailService.instance.revision,
      builder: (_, rev, _) {
        final t = ThumbnailService.instance.fileFor(projectId);
        if (t == null) return placeholder;
        return Image.file(
          t,
          // A revisao entra na chave: sem isso o ImageCache seguraria a
          // miniatura antiga (mesmo caminho = mesmo FileImage).
          key: ValueKey('$projectId/$rev'),
          fit: BoxFit.cover,
          width: double.infinity,
          height: double.infinity,
          gaplessPlayback: true,
          // Miniaturas sao PNGs grandes de captura: decodifica ja no
          // tamanho de exibicao em vez de textura cheia.
          cacheWidth: hero ? 1280 : 640,
          filterQuality: FilterQuality.low,
          errorBuilder: (_, _, _) => placeholder,
        );
      },
    );
  }
}

/// O CARTAO "CONTINUAR EDITANDO": o projeto mais recente em toda a
/// largura, com a miniatura de verdade, o nome por cima e o botao que
/// diz o verbo. Toque abre; toque longo (ou as reticencias) abre o menu
/// do projeto — as mesmas chaves do cartao da grade.
class _CartaoContinuar extends StatelessWidget {
  const _CartaoContinuar({
    super.key,
    required this.project,
    required this.onOpen,
    required this.onMenu,
  });

  final VideoProject project;
  final VoidCallback onOpen;
  final VoidCallback onMenu;

  static const _scrim = BoxDecoration(
    gradient: LinearGradient(
      begin: Alignment.topCenter,
      end: Alignment.bottomCenter,
      stops: [0.45, 1],
      colors: [Colors.transparent, Color(0xB3000000)],
    ),
  );

  @override
  Widget build(BuildContext context) {
    final placeholderDecor = BoxDecoration(
      gradient: LinearGradient(
        begin: Alignment.topLeft,
        end: Alignment.bottomRight,
        colors: [AppColors.surfaceHigh, AppColors.surface],
      ),
    );
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 18, 20, 0),
      child: RepaintBoundary(
        child: Tocavel(
          onTap: onOpen,
          onLongPress: onMenu,
          child: ClipRRect(
            borderRadius: BorderRadius.circular(20),
            child: AspectRatio(
              aspectRatio: 16 / 9,
              child: Stack(
                fit: StackFit.expand,
                children: [
                  _ThumbProjeto(
                    projectId: project.id,
                    hero: true,
                    placeholder: DecoratedBox(
                      decoration: placeholderDecor,
                      child: Center(
                        child: Icon(CupertinoIcons.film, size: 34, color: AppColors.muted),
                      ),
                    ),
                  ),
                  // O SCRIM: o nome le em cima de qualquer miniatura.
                  const DecoratedBox(decoration: _scrim),
                  Positioned(
                    left: 16,
                    right: 16,
                    bottom: 14,
                    child: Row(
                      crossAxisAlignment: CrossAxisAlignment.end,
                      children: [
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              AppText(
                                'Continuar editando',
                                style: TextStyle(
                                  fontSize: 11,
                                  fontWeight: FontWeight.w700,
                                  letterSpacing: 0.4,
                                  color: AppColors.lime,
                                ),
                              ),                              const SizedBox(height: 3),
                              Text(
                                project.name,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                style: const TextStyle(
                                  fontSize: 18,
                                  fontWeight: FontWeight.w700,
                                  letterSpacing: -0.2,
                                  color: Colors.white,
                                ),
                              ),
                              const SizedBox(height: 2),
                              AppText(
                                fichaDoProjeto(project),
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                style: const TextStyle(fontSize: 11, color: Colors.white70),
                              ),
                            ],
                          ),
                        ),
                        const SizedBox(width: 10),
                        Container(
                          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 9),
                          decoration: BoxDecoration(
                            color: AppColors.lime,
                            borderRadius: BorderRadius.circular(999),
                          ),
                          child: const Row(
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Icon(CupertinoIcons.play_fill, size: 13, color: Color(0xFF10130C)),
                              SizedBox(width: 6),
                              AppText(
                                'Continuar',
                                style: TextStyle(
                                  fontSize: 12.5,
                                  fontWeight: FontWeight.w700,
                                  color: Color(0xFF10130C),
                                ),
                              ),
                            ],
                          ),
                        ),
                      ],
                    ),
                  ),
                  // O MENU DO PROJETO, com a mesma chave da grade: apagar,
                  // duplicar e renomear nunca dependem de gesto escondido.
                  Positioned(
                    top: 2,
                    right: 2,
                    child: Tocavel(
                      key: ValueKey('projeto-menu-${project.id}'),
                      onTap: onMenu,
                      child: const SizedBox(
                        width: 40,
                        height: 40,
                        child: Icon(CupertinoIcons.ellipsis, size: 18, color: Colors.white70),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// Quantos projetos a Inicio mostra antes do "mostrar todos".
const _recentesNaInicio = 6;

final _mostrarTodosProvider = StateProvider<bool>((_) => false);

/// A ficha tecnica de um projeto: "16:9 · 1080p · 30 fps".
String fichaDoProjeto(VideoProject project) {
  final aspect = ProjectPresets.aspects
      .firstWhere(
        (a) => (a.ratio - project.aspectRatio).abs() < 0.01,
        orElse: () => ProjectPresets.aspects.first,
      )
      .label;
  return '$aspect · ${ProjectPresets.resolutionLabel(project.resolutionHeight)} · ${project.fps} fps';
}

/// O dialogo de renomear: dono do proprio controlador de texto, para
/// ele so ser descartado quando o dialogo sai da arvore.
class _DialogoDeNome extends StatefulWidget {
  const _DialogoDeNome({required this.inicial});

  final String inicial;

  @override
  State<_DialogoDeNome> createState() => _DialogoDeNomeState();
}

class _DialogoDeNomeState extends State<_DialogoDeNome> {
  late final TextEditingController _campo = TextEditingController(text: widget.inicial);

  @override
  void dispose() {
    _campo.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return CupertinoAlertDialog(
      title: const AppText('Renomear'),
      content: Padding(
        padding: const EdgeInsets.only(top: 12),
        child: CupertinoTextField(
          key: const ValueKey('renomear-campo'),
          controller: _campo,
          autofocus: true,
          textCapitalization: TextCapitalization.sentences,
          onSubmitted: (v) => Navigator.of(context).pop(v),
        ),
      ),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.of(context).pop(),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          key: const ValueKey('renomear-salvar'),
          isDefaultAction: true,
          onPressed: () => Navigator.of(context).pop(_campo.text),
          child: const AppText('Salvar'),
        ),
      ],
    );
  }
}

/// Cartao de modelo pronto: um quadro renderizado do proprio motion.
/// O CABECALHO: a marca em titulo grande, a saudacao com o apelido e dois
/// botoes redondos (abrir template, perfil). Sem degradê no texto — ele
/// cortava o "A" do nome e deixava o titulo apagado.
class _Cabecalho extends ConsumerWidget {
  const _Cabecalho({required this.onTemplate});

  final VoidCallback onTemplate;

  static String _saudacao() {
    final hora = DateTime.now().hour;
    if (hora >= 5 && hora < 12) return 'Bom dia';
    if (hora >= 12 && hora < 18) return 'Boa tarde';
    return 'Boa noite';
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final conta = ref.watch(contaDaComunidadeProvider);
    final apelido = conta?.apelido;
    final saudacao = translate(context, _saudacao());
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 14, 12, 12),
      child: Row(
        children: [
          const AureaLogo(size: 38),
          const SizedBox(width: 12),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  'Aurea',
                  style: TextStyle(
                    fontSize: 30,
                    height: 1.05,
                    fontWeight: FontWeight.w800,
                    letterSpacing: -0.8,
                    color: AppColors.onDark,
                  ),
                ),
                const SizedBox(height: 2),
                // O apelido e da pessoa: nunca passa pelo dicionario.
                Text(
                  apelido == null ? saudacao : '$saudacao, $apelido',
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(fontSize: 13.5, color: AppColors.muted),
                ),
              ],
            ),
          ),
          _BotaoRedondo(
            icon: CupertinoIcons.doc_on_doc,
            tooltip: translate(context, 'Abrir template'),
            onTap: onTemplate,
          ),
          const SizedBox(width: 6),
          _AvatarDaConta(caminho: conta?.avatar, inicial: conta?.inicial ?? '?'),
        ],
      ),
    );
  }
}

/// Avatar do cabecalho: resolve `File.existsSync` UMA vez por caminho
/// (no init/didUpdate), nao a cada rebuild do header.
class _AvatarDaConta extends ConsumerWidget {
  const _AvatarDaConta({required this.caminho, required this.inicial});

  final String? caminho;
  final String inicial;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final existe = caminho != null && _existe(caminho!);
    return Tocavel(
      key: const ValueKey('inicio-perfil'),
      onTap: () => ref.read(homeTabProvider.notifier).state = 3,
      child: SizedBox(
        width: 44,
        height: 44,
        child: Center(
          child: existe
              ? CircleAvatar(radius: 17, backgroundImage: FileImage(File(caminho!)))
              : CircleAvatar(
                  radius: 17,
                  backgroundColor: AppColors.violet,
                  child: Text(
                    inicial,
                    style: const TextStyle(
                      fontSize: 14,
                      fontWeight: FontWeight.w700,
                      color: Colors.white,
                    ),
                  ),
                ),
        ),
      ),
    );
  }

  // Cache simples por caminho: evita stat repetido no mesmo frame.
  static final _cache = <String, bool>{};
  static bool _existe(String caminho) {
    final hit = _cache[caminho];
    if (hit != null) return hit;
    final v = File(caminho).existsSync();
    if (_cache.length > 64) _cache.clear();
    _cache[caminho] = v;
    return v;
  }
}

class _BotaoRedondo extends StatelessWidget {
  const _BotaoRedondo({required this.icon, required this.tooltip, required this.onTap});

  final IconData icon;
  final String tooltip;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Tooltip(
        message: tooltip,
        child: Tocavel(
          onTap: onTap,
          child: SizedBox(
            width: 44,
            height: 44,
            child: Center(
              child: Container(
                width: 36,
                height: 36,
                decoration: BoxDecoration(
                  color: AppColors.surfaceHigh,
                  shape: BoxShape.circle,
                ),
                child: Icon(icon, size: 17, color: AppColors.onDark),
              ),
            ),
          ),
        ),
      );
}

class _Atalho extends StatelessWidget {
  const _Atalho({required this.icon, required this.rotulo, required this.onTap});

  final IconData icon;
  final String rotulo;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Tocavel(
        onTap: onTap,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 56,
              height: 56,
              decoration: BoxDecoration(
                color: AppColors.surfaceHigh,
                shape: BoxShape.circle,
              ),
              child: Icon(icon, size: 23, color: AppColors.lime),
            ),
            const SizedBox(height: 7),
            AppText(
              rotulo,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: const TextStyle(fontSize: 12, fontWeight: FontWeight.w600),
            ),
          ],
        ),
      );
}

class _TituloSecao extends StatelessWidget {
  const _TituloSecao(this.titulo);

  final String titulo;

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.fromLTRB(20, 28, 20, 12),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.end,
          children: [
            Expanded(
              child: AppText(
                titulo,
                style: const TextStyle(
                  fontSize: 21,
                  fontWeight: FontWeight.w700,
                  letterSpacing: -0.4,
                ),
              ),
            ),
          ],
        ),
      );
}

class _SemProjetos extends StatelessWidget {
  const _SemProjetos();

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.fromLTRB(20, 4, 20, 0),
        child: Row(
          children: [
            Icon(CupertinoIcons.film, size: 22, color: AppColors.muted),
            const SizedBox(width: 12),
            Expanded(
              child: AppText(
                'Seus projetos aparecem aqui, com a miniatura do que voce fez.',
                style: TextStyle(color: AppColors.muted, fontSize: 13.5),
              ),
            ),
          ],
        ),
      );
}

/// UM PROJETO NA GRADE: a miniatura de verdade (ou a moldura do formato,
/// antes da primeira), o nome como foi digitado e o menu que se ve.
class _CartaoProjeto extends StatelessWidget {
  const _CartaoProjeto({
    super.key,
    required this.project,
    required this.onOpen,
    required this.onMenu,
    this.marcado = false,
    this.escolhendo = false,
    this.onMarcar,
  });

  final VideoProject project;
  final VoidCallback onOpen;
  final VoidCallback onMenu;

  /// ESCOLHENDO VARIOS: o cartao mostra a marca e o toque passa a
  /// marcar, em vez de abrir.
  final bool marcado;
  final bool escolhendo;
  final VoidCallback? onMarcar;

  @override
  Widget build(BuildContext context) {
    final ratio = project.aspectRatio <= 0 ? 16 / 9 : project.aspectRatio;
    return RepaintBoundary(
      child: Tocavel(
        onTap: onOpen,
        onLongPress: escolhendo ? onMarcar : onMenu,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Expanded(
              child: ClipRRect(
                borderRadius: BorderRadius.circular(14),
                child: _ThumbProjeto(
                  projectId: project.id,
                  hero: false,
                  placeholder: DecoratedBox(
                    decoration: BoxDecoration(
                      gradient: LinearGradient(
                        begin: Alignment.topLeft,
                        end: Alignment.bottomRight,
                        colors: [AppColors.surfaceHigh, AppColors.surface],
                      ),
                    ),
                    child: Center(
                      // A moldura do formato do projeto (9:16, 16:9, 1:1).
                      child: FractionallySizedBox(
                        heightFactor: ratio >= 1 ? null : 0.62,
                        widthFactor: ratio >= 1 ? 0.52 : null,
                        child: AspectRatio(
                          aspectRatio: ratio,
                          child: DecoratedBox(
                            decoration: BoxDecoration(
                              color: AppColors.background.withValues(alpha: .55),
                              borderRadius: BorderRadius.circular(4),
                            ),
                            child: Icon(
                              CupertinoIcons.film,
                              size: 18,
                              color: AppColors.muted,
                            ),
                          ),
                        ),
                      ),
                    ),
                  ),
                ),
              ),
            ),
            const SizedBox(height: 6),
            Row(
              children: [
                if (escolhendo)
                  Padding(
                    padding: const EdgeInsets.only(right: 6),
                    child: Icon(
                      key: ValueKey('projeto-marca-${project.id}'),
                      marcado ? CupertinoIcons.checkmark_circle_fill : CupertinoIcons.circle,
                      size: 18,
                      color: marcado ? AppColors.lime : AppColors.muted,
                    ),
                  ),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        project.name,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                          fontSize: 14,
                          fontWeight: FontWeight.w600,
                          letterSpacing: -0.1,
                        ),
                      ),
                      const SizedBox(height: 1),
                      AppText(
                        fichaDoProjeto(project),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: TextStyle(fontSize: 11, color: AppColors.muted),
                      ),
                    ],
                  ),
                ),
                // O MENU TEM 40 PX de alvo: e por onde se apaga, duplica e
                // renomeia — nada disso pode depender de um gesto escondido.
                // Escolhendo varios, o mesmo alvo marca e desmarca.
                Tocavel(
                  key: ValueKey('projeto-menu-${project.id}'),
                  onTap: escolhendo ? onMarcar : onMenu,
                  child: SizedBox(
                    width: 40,
                    height: 40,
                    child: Icon(CupertinoIcons.ellipsis, size: 18, color: AppColors.muted),
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

/// UM MODELO: o quadro renderizado grande, com o nome por cima.
class _CartaoModelo extends StatelessWidget {
  const _CartaoModelo({
    required this.imagem,
    required this.titulo,
    required this.detalhe,
    required this.onTap,
  });

  final String imagem;
  final String titulo;
  final String detalhe;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(right: 12),
      child: RepaintBoundary(
        child: Tocavel(
          onTap: onTap,
          child: SizedBox(
            width: 232,
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                ClipRRect(
                  borderRadius: BorderRadius.circular(16),
                  child: SizedBox(
                    width: 232,
                    height: 146,
                    child: Image.asset(
                      imagem,
                      fit: BoxFit.cover,
                      cacheWidth: 464,
                      filterQuality: FilterQuality.low,
                      errorBuilder: (_, _, _) => ColoredBox(
                        color: AppColors.surfaceHigh,
                        child: Center(
                          child: Icon(CupertinoIcons.film, color: AppColors.muted),
                        ),
                      ),
                    ),
                  ),
                ),
                const SizedBox(height: 8),
                AppText(
                  titulo,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: const TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.w600,
                    letterSpacing: -0.1,
                  ),
                ),
                const SizedBox(height: 2),
                AppText(
                  detalhe,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(fontSize: 11.5, color: AppColors.muted),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }
}

/// Linha simples com icone e seta: sem caixa, sem borda.
class _Linha extends StatelessWidget {
  const _Linha({super.key, required this.icon, required this.texto, required this.onTap});

  final IconData icon;
  final String texto;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return Tocavel(
      onTap: onTap,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(20, 13, 20, 13),
        child: Row(
          children: [
            Icon(icon, size: 19, color: AppColors.lime),
            const SizedBox(width: 14),
            Expanded(child: AppText(texto, style: const TextStyle(fontSize: 14.5))),
            Icon(CupertinoIcons.chevron_right, size: 15, color: AppColors.muted),
          ],
        ),
      ),
    );
  }
}

/// Linha maior, com o icone num circulo e duas linhas de texto.
class _LinhaGrande extends StatelessWidget {
  const _LinhaGrande({
    required this.icon,
    required this.titulo,
    required this.subtitulo,
    required this.onTap,
  });

  final IconData icon;
  final String titulo;
  final String subtitulo;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Tocavel(
        key: const ValueKey('inicio-comunidade'),
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.fromLTRB(20, 2, 20, 2),
          child: Row(
            children: [
              Container(
                width: 48,
                height: 48,
                decoration: BoxDecoration(
                  color: AppColors.lime.withValues(alpha: .14),
                  shape: BoxShape.circle,
                ),
                child: Icon(icon, size: 22, color: AppColors.lime),
              ),
              const SizedBox(width: 14),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    AppText(
                      titulo,
                      style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600),
                    ),
                    const SizedBox(height: 2),
                    AppText(subtitulo, style: const TextStyle(fontSize: 12.5)),
                  ],
                ),
              ),
              Icon(CupertinoIcons.chevron_right, size: 15, color: AppColors.muted),
            ],
          ),
        ),
      );
}

/// Confirma e apaga TODOS os projetos e miniaturas.
Future<void> apagarTodosOsProjetos(BuildContext context, WidgetRef ref) async {
  final total = ref.read(projectsControllerProvider).length;
  if (total == 0) return;
  final ok = await showCupertinoDialog<bool>(
    context: context,
    builder: (c) => CupertinoAlertDialog(
      title: const AppText('Apagar todos os projetos?'),
      content: Text('$total projeto(s) serao apagados. Isso nao pode ser desfeito.'),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.of(c).pop(false),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          isDestructiveAction: true,
          onPressed: () => Navigator.of(c).pop(true),
          child: const AppText('Apagar todos'),
        ),
      ],
    ),
  );
  if (ok != true) return;
  final ids = [for (final p in ref.read(projectsControllerProvider)) p.id];
  ref.read(projectsControllerProvider.notifier).removeAll();
  for (final id in ids) {
    ThumbnailService.instance.delete(id);
  }
}

/// A BARRA DA LISTA: quantos sao, como estao ordenados, a busca e o
/// modo de escolher varios.
class _BarraDaLista extends ConsumerStatefulWidget {
  const _BarraDaLista({
    required this.total,
    required this.ordem,
    required this.busca,
    required this.selecao,
    required this.projetos,
  });

  final int total;
  final OrdemDosProjetos ordem;
  final String busca;
  final Set<String> selecao;
  final List<VideoProject> projetos;

  @override
  ConsumerState<_BarraDaLista> createState() => _BarraDaListaState();
}

class _BarraDaListaState extends ConsumerState<_BarraDaLista> {
  final _campo = TextEditingController();
  var _procurando = false;

  @override
  void initState() {
    super.initState();
    _campo.text = widget.busca;
    _procurando = widget.busca.isNotEmpty;
  }

  @override
  void didUpdateWidget(covariant _BarraDaLista old) {
    super.didUpdateWidget(old);
    // Limpeza externa (lote excluido): fecha a busca sem reconstruir nada.
    if (widget.busca.isEmpty && _campo.text.isNotEmpty && !_procurando) {
      _campo.clear();
    }
  }

  @override
  void dispose() {
    _campo.dispose();
    super.dispose();
  }

  Future<void> _escolherOrdem() async {
    final escolha = await showCupertinoModalPopup<OrdemDosProjetos>(
      context: context,
      builder: (c) => CupertinoActionSheet(
        title: const AppText('Ordenar os projetos'),
        actions: [
          for (final o in OrdemDosProjetos.values)
            CupertinoActionSheetAction(
              key: ValueKey('projetos-ordem-${o.name}'),
              onPressed: () => Navigator.of(c).pop(o),
              child: AppText(rotuloDaOrdem(o)),
            ),
        ],
        cancelButton: CupertinoActionSheetAction(
          onPressed: () => Navigator.of(c).pop(),
          child: const AppText('Cancelar'),
        ),
      ),
    );
    if (escolha != null) {
      ref.read(ordemDosProjetosProvider.notifier).escolher(escolha);
    }
  }

  @override
  Widget build(BuildContext context) {
    final selecionando = widget.selecao.isNotEmpty;
    if (selecionando) {
      return Padding(
        padding: const EdgeInsets.fromLTRB(20, 18, 12, 6),
        child: Row(
          children: [
            Expanded(
              child: AppTextMoldado(
                '{0} escolhidos', [widget.selecao.length],
                style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700),
              ),
            ),
            _BotaoDaBarra(
              chave: 'projetos-marcar-todos',
              icone: CupertinoIcons.checkmark_circle,
              onTap: () => ref.read(selecaoDeProjetosProvider.notifier).state = {
                for (final p in widget.projetos) p.id,
              },
            ),
            _BotaoDaBarra(
              chave: 'projetos-selecao-sair',
              icone: CupertinoIcons.xmark,
              onTap: () => ref.read(selecaoDeProjetosProvider.notifier).state = const {},
            ),
          ],
        ),
      );
    }
    return Padding(
      padding: const EdgeInsets.fromLTRB(20, 18, 12, 6),
      child: Row(
        children: [
          if (!_procurando) ...[
            Expanded(
              child: AppText(
                '${widget.total} ${translate(context, 'projetos')}',
                style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w700),
              ),
            ),
            _BotaoDaBarra(
              chave: 'projetos-buscar',
              icone: CupertinoIcons.search,
              onTap: () => setState(() => _procurando = true),
            ),
          ] else
            Expanded(
              child: SizedBox(
                height: 34,
                child: CupertinoTextField(
                  key: const ValueKey('projetos-busca-campo'),
                  controller: _campo,
                  autofocus: true,
                  placeholder: 'Procurar pelo nome',
                  suffix: Tocavel(
                    key: const ValueKey('projetos-busca-limpar'),
                    onTap: () {
                      _campo.clear();
                      ref.read(buscaDeProjetosProvider.notifier).state = '';
                      setState(() => _procurando = false);
                    },
                    child: const Padding(
                      padding: EdgeInsets.symmetric(horizontal: 8),
                      child: Icon(CupertinoIcons.xmark_circle_fill, size: 16),
                    ),
                  ),
                  onChanged: (v) => ref.read(buscaDeProjetosProvider.notifier).state = v,
                ),
              ),
            ),
          _BotaoDaBarra(
            chave: 'projetos-ordenar',
            icone: CupertinoIcons.arrow_up_arrow_down,
            onTap: _escolherOrdem,
          ),
          _BotaoDaBarra(
            chave: 'projetos-selecionar',
            icone: CupertinoIcons.checkmark_circle,
            onTap: () {
              final primeiro = widget.projetos.firstOrNull;
              if (primeiro == null) return;
              ref.read(selecaoDeProjetosProvider.notifier).state = {primeiro.id};
            },
          ),
        ],
      ),
    );
  }
}

class _BotaoDaBarra extends StatelessWidget {
  const _BotaoDaBarra({required this.chave, required this.icone, required this.onTap});

  final String chave;
  final IconData icone;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) => Tocavel(
        key: ValueKey(chave),
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.all(8),
          child: Icon(icone, size: 19, color: AppColors.muted),
        ),
      );
}

/// AS ACOES DO LOTE: duplicar e excluir o que estiver marcado.
class _AcoesEmLote extends StatelessWidget {
  const _AcoesEmLote({required this.quantos, required this.onDuplicar, required this.onExcluir});

  final int quantos;
  final VoidCallback onDuplicar;
  final VoidCallback onExcluir;

  @override
  Widget build(BuildContext context) => RepaintBoundary(
        child: ClipRect(
          child: BackdropFilter(
            filter: ImageFilter.blur(sigmaX: 20, sigmaY: 20),
            child: Container(
              decoration: BoxDecoration(
                color: AppColors.surface.withValues(alpha: .88),
                border: Border(top: BorderSide(color: AppColors.outline, width: .5)),
              ),
              child: SafeArea(
                top: false,
                child: Padding(
                  padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                  child: Row(
                    children: [
                      Expanded(
                        child: AppTextMoldado(
                          '{0} escolhidos', [quantos],
                          style: TextStyle(fontSize: 13, color: AppColors.muted),
                        ),
                      ),
                      Tocavel(
                        key: const ValueKey('projetos-lote-duplicar'),
                        onTap: onDuplicar,
                        child: Padding(
                          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
                          child: Row(
                            children: [
                              Icon(
                                CupertinoIcons.plus_square_on_square,
                                size: 18,
                                color: AppColors.onDark,
                              ),
                              const SizedBox(width: 6),
                              const AppText('Duplicar', style: TextStyle(fontSize: 13)),
                            ],
                          ),
                        ),
                      ),
                      Tocavel(
                        key: const ValueKey('projetos-lote-excluir'),
                        onTap: onExcluir,
                        child: const Padding(
                          padding: EdgeInsets.symmetric(horizontal: 14, vertical: 8),
                          child: Row(
                            children: [
                              Icon(CupertinoIcons.delete, size: 18, color: Color(0xFFFF6B6B)),
                              SizedBox(width: 6),
                              AppText(
                                'Excluir',
                                style: TextStyle(fontSize: 13, color: Color(0xFFFF6B6B)),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ),
      );
}
