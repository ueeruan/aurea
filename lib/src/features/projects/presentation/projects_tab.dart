import 'package:aurea/src/core/l10n/app_language.dart';
import 'dart:io';

import '../../enhance/presentation/enhance_screen.dart';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/app_theme.dart';
import '../../about/presentation/report_sheet.dart';
import '../../editor/application/editor_controller.dart';
import '../../editor/domain/template_pack.dart';
import '../../editor/domain/video_project.dart';
import '../../editor/presentation/editor_screen.dart';
import '../application/projects_controller.dart';
import '../application/campo_assets.dart';
import '../application/reference_rebuild_assets.dart';
import '../application/dnyx_remix_assets.dart';
import '../application/vhf_motion_assets.dart';
import '../application/thumbnail_service.dart';
import '../domain/cena_xml_import.dart';
import '../domain/abyss_cinematic_template.dart';
import '../domain/mao_enterrada_template.dart';
import '../domain/colina_tv_template.dart';
import '../domain/deriva_template.dart';
import '../domain/flor_template.dart';
import '../domain/prisma_template.dart';
import '../application/modelos_empacotados.dart';
import '../domain/monolito_template.dart';
import '../domain/notes_motion_template.dart';
import '../domain/pindown_motion_template.dart';
import '../domain/project_presets.dart';
import '../../tutoriais/presentation/tutorial_screen.dart';
import 'home_shell.dart';
import 'new_project_sheet.dart';
import 'whats_new.dart';
import 'widgets/aurea_welcome_header.dart';

/// A INICIO, do jeito de um app de video: o titulo, UM botao de criar,
/// os projetos recentes numa lista com a miniatura DE VERDADE e um menu
/// que se ve, os modelos com um quadro renderizado — e nada de
/// cartao-vitrine, pilula ou quadradinho.
///
/// O que saiu, e por que: a fila de pilulas de formato (quatro jeitos
/// de comecar um projeto, e o beta nao sabia qual era o certo — o
/// formato se escolhe dentro da folha, olhando para a moldura), os dois
/// quadrados ao lado do botao (viraram texto), o AutoEdit (fora por
/// enquanto) e o "segurar para excluir", que ninguem descobre sozinho
/// (virou um menu no botao de reticencias de cada projeto).
class ProjectsTab extends ConsumerWidget {
  const ProjectsTab({super.key});

  Future<void> _createProject(
    BuildContext context,
    WidgetRef ref, {
    String? nomeSugerido,
  }) async {
    final project = await showNewProjectSheet(
      context,
      nomeSugerido: nomeSugerido,
    );
    if (project == null || !context.mounted) return;

    ref.read(projectsControllerProvider.notifier).add(project);
    ref.read(editorControllerProvider.notifier).openProject(project);
    Navigator.of(context)
        .push(MaterialPageRoute(builder: (_) => const EditorScreen()));
  }

  /// MODELO PRONTO: um projeto montado por codigo, com id novo a cada
  /// abertura — o mesmo cuidado do template em arquivo.
  Future<void> _abrirModelo(
    BuildContext context,
    WidgetRef ref,
    VideoProject modelo,
  ) async {
    final project = modelo.comIdNovo();
    ref.read(projectsControllerProvider.notifier).add(project);
    ref.read(editorControllerProvider.notifier).openProject(project);
    Navigator.of(context)
        .push(MaterialPageRoute(builder: (_) => const EditorScreen()));
  }

  /// Prepara o campo com suas texturas embutidas antes de abrir.
  Future<void> _openCampo(BuildContext context, WidgetRef ref) async {
    ScaffoldMessenger.of(context)
        .showSnackBar(const SnackBar(content: AppText('Preparando o campo 3D…')));
    try {
      final model = await prepareCampoArvore();
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).hideCurrentSnackBar();
      await _abrirModelo(context, ref, model);
    } catch (_) {
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: AppText('Não consegui preparar o campo. Tente novamente.'),
        ),
      );
    }
  }

  Future<void> _openVhfMotion(BuildContext context, WidgetRef ref) async {
    try {
      final model = await prepareVhfMotion();
      if (context.mounted) await _abrirModelo(context, ref, model);
    } catch (_) {
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: AppText('Nao consegui preparar o motion VHF. Tente novamente.'),
        ),
      );
    }
  }

  /// A Deriva usa so o astronauta: prepara o modelo e abre.
  Future<void> _openDeriva(BuildContext context, WidgetRef ref) async {
    try {
      final astronauta = await carregarAstronauta();
      if (context.mounted) {
        await _abrirModelo(
          context,
          ref,
          buildDerivaTemplate(astronauta: astronauta),
        );
      }
    } catch (e) {
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: AppText('Nao consegui preparar o astronauta: $e')),
      );
    }
  }

  /// Os modelos do Monolito (astronauta, portal, arvore) viram arquivos
  /// e passam pelo importador antes do projeto abrir.
  Future<void> _openMonolito(BuildContext context, WidgetRef ref) async {
    try {
      final modelos = await carregarMonolitoModelos();
      if (context.mounted) {
        await _abrirModelo(
          context,
          ref,
          buildMonolitoTemplate(modelos: modelos),
        );
      }
    } catch (e) {
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: AppText('Nao consegui preparar os modelos do Monolito: $e'),
        ),
      );
    }
  }

  /// Prepara a trilha empacotada antes de abrir a nova recriacao.
  Future<void> _openDnyxRemix(BuildContext context, WidgetRef ref) async {
    try {
      final model = await prepareDnyxRemix();
      if (context.mounted) await _abrirModelo(context, ref, model);
    } catch (_) {
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: AppText('Nao consegui preparar o motion. Tente abrir novamente.',
          ),
        ),
      );
    }
  }

  /// Prepara a trilha empacotada antes de abrir a nova recriacao.
  Future<void> _openReferenceRebuild(
    BuildContext context,
    WidgetRef ref,
  ) async {
    try {
      final model = await prepareReferenceRebuild();
      if (context.mounted) await _abrirModelo(context, ref, model);
    } catch (_) {
      if (!context.mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: AppText('Nao consegui preparar a trilha. Tente abrir o modelo novamente.',
          ),
        ),
      );
    }
  }

  /// ABRIR TEMPLATE: o arquivo vira um projeto NOVO, com id novo.
  ///
  /// Sem id novo, abrir o mesmo template duas vezes sobrescreveria o
  /// trabalho da primeira vez — e a pessoa perderia o que fez sem
  /// entender por que.
  Future<void> _openTemplate(BuildContext context, WidgetRef ref) async {
    final r = await FilePicker.platform.pickFiles(
      type: FileType.custom,
      allowedExtensions: const ['json', 'aurea'],
    );
    final caminho = r?.files.single.path;
    if (caminho == null || !context.mounted) return;

    TemplatePack? pack;
    try {
      pack = TemplatePack.decode(await File(caminho).readAsString());
    } catch (_) {
      pack = null;
    }
    if (!context.mounted) return;
    if (pack == null) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: AppText('Nao consegui ler esse template')),
      );
      return;
    }

    final novo = pack.project.copyWith(name: pack.name).comIdNovo();
    ref.read(projectsControllerProvider.notifier).add(novo);
    ref.read(editorControllerProvider.notifier).openProject(novo);
    Navigator.of(context)
        .push(MaterialPageRoute(builder: (_) => const EditorScreen()));
  }

  /// IMPORTAR MÍDIA: escolhe foto ou vídeo da galeria e já inicia um
  /// projeto novo com a mídia inserida no palco.
  Future<void> _importarMidia(BuildContext context, WidgetRef ref) async {
    try {
      final r = await FilePicker.platform.pickFiles(
        type: FileType.media,
        allowMultiple: false,
      );
      final caminho = r?.files.single.path;
      if (caminho == null || !context.mounted) return;
      final nome = r?.files.single.name ?? 'Mídia Importada';
      final projeto = VideoProject(
        id: 'projeto-${DateTime.now().millisecondsSinceEpoch}',
        name: nome.replaceAll(RegExp(r'\.[^.]+$'), ''),
        createdAt: DateTime.now(),
        fps: 30,
        aspectRatio: 9 / 16,
      );
      ref.read(projectsControllerProvider.notifier).add(projeto);
      final controller = ref.read(editorControllerProvider.notifier);
      controller.openProject(projeto);
      final ext = caminho.split('.').last.toLowerCase();
      if (['mp4', 'mov', 'm4v', 'avi', 'mkv'].contains(ext)) {
        controller.addVideoLayer(
          Duration.zero,
          caminho,
          nome,
          const Duration(seconds: 5),
        );
      } else {
        controller.addImageLayer(Duration.zero, caminho, nome);
      }
      Navigator.of(context)
          .push(MaterialPageRoute(builder: (_) => const EditorScreen()));
    } catch (_) {
      if (context.mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: AppText('Não consegui importar essa mídia.')),
        );
      }
    }
  }

  /// CENA EM XML: le o que reconhece, mostra o balanco (camadas,
  /// keyframes, o que ficou de fora) e abre como projeto novo.
  Future<void> _importarCena(BuildContext context, WidgetRef ref) async {
    final r = await FilePicker.platform.pickFiles(type: FileType.any);
    final caminho = r?.files.single.path;
    if (caminho == null || !context.mounted) return;
    final nomeArquivo = caminho.split(RegExp(r'[\\/]')).last;
    CenaXmlResult resultado;
    try {
      final texto = await File(caminho).readAsString();
      resultado = importarCenaXml(
        texto,
        nome: nomeArquivo.replaceAll(
          RegExp(r'\.xml$', caseSensitive: false),
          '',
        ),
      );
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
          child: AppText(
            _resumoDaImportacao(resultado),
            textAlign: TextAlign.left,
          ),
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
    final novo = resultado.project;
    ref.read(projectsControllerProvider.notifier).add(novo);
    ref.read(editorControllerProvider.notifier).openProject(novo);
    Navigator.of(context)
        .push(MaterialPageRoute(builder: (_) => const EditorScreen()));
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
          content: Padding(
            padding: const EdgeInsets.only(top: 8),
            child: AppText(texto),
          ),
          actions: [
            CupertinoDialogAction(
              isDefaultAction: true,
              onPressed: () => Navigator.of(c).pop(),
              child: const AppText('OK'),
            ),
          ],
        ),
      );

  void _openProject(BuildContext context, WidgetRef ref, VideoProject project) {
    ref.read(editorControllerProvider.notifier).openProject(project);
    Navigator.of(context)
        .push(MaterialPageRoute(builder: (_) => const EditorScreen()));
  }

  Future<void> _confirmarExclusao(
    BuildContext context,
    WidgetRef ref,
    VideoProject project,
  ) async {
    final apaga = await showCupertinoModalPopup<bool>(
      context: context,
      builder: (c) => CupertinoActionSheet(
        title: AppText(project.name),
        actions: [
          CupertinoActionSheetAction(
            isDestructiveAction: true,
            onPressed: () => Navigator.of(c).pop(true),
            child: const AppText('Excluir projeto'),
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
  void _duplicar(WidgetRef ref, VideoProject project) {
    final copia = project.copyWith(name: '${project.name} (cópia)').comIdNovo();
    ref.read(projectsControllerProvider.notifier).add(copia);
  }

  Future<void> _renomear(
    BuildContext context,
    WidgetRef ref,
    VideoProject project,
  ) async {
    final nome = await showCupertinoDialog<String>(
      context: context,
      builder: (_) => _DialogoDeNome(inicial: project.name),
    );
    final limpo = nome?.trim();
    if (limpo == null || limpo.isEmpty || limpo == project.name) return;
    ref
        .read(projectsControllerProvider.notifier)
        .upsert(project.copyWith(name: limpo));
  }

  /// O MENU DO PROJETO, no botao de reticencias e no toque longo. Antes
  /// so existia "segurar para excluir", que ninguem descobre sozinho.
  Future<void> _menuDoProjeto(
    BuildContext context,
    WidgetRef ref,
    VideoProject project,
  ) async {
    final acao = await showCupertinoModalPopup<String>(
      context: context,
      builder: (c) => CupertinoActionSheet(
        title: AppText(project.name),
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
        _openProject(context, ref, project);
      case 'duplicar':
        _duplicar(ref, project);
      case 'renomear':
        await _renomear(context, ref, project);
      case 'excluir':
        await _confirmarExclusao(context, ref, project);
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final projects = ref.watch(projectsControllerProvider);
    final mostrarTodos = ref.watch(_mostrarTodosProvider);
    const completo = true;
    ThumbnailService.instance.init();
    final visiveis = mostrarTodos
        ? projects
        : projects.take(_recentesNaInicio).toList();

    return SafeArea(
      bottom: false,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(0, 10, 0, 120),
        children: [
          // CABEÇALHO ANIMADO DE BOAS-VINDAS COM PERFIL INTEGRADO
          const AureaWelcomeHeader(),
          const SizedBox(height: 12),
          // UM BOTAO SO, na largura inteira.
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 20),
            child: SizedBox(
              height: 52,
              width: double.infinity,
              child: FilledButton.icon(
                key: const ValueKey('novo-projeto'),
                icon: const Icon(CupertinoIcons.plus, size: 19),
                label: const AppText('Novo projeto'),
                onPressed: () => _createProject(
                  context,
                  ref,
                  nomeSugerido: 'Projeto ${projects.length + 1}',
                ),
              ),
            ),
          ),
          Padding(
            padding: const EdgeInsets.fromLTRB(20, 10, 20, 0),
            child: OutlinedButton.icon(
              icon: const Icon(Icons.auto_awesome),
              label: const AppText('Melhorar qualidade • IA e cores'),
              onPressed: () => Navigator.of(context).push(
                MaterialPageRoute<void>(builder: (_) => const EnhanceScreen()),
              ),
            ),
          ),
          // ATALHOS RÁPIDOS MODERNOS (DOCK CRIATIVO)
          if (completo) ...[
            const SizedBox(height: 14),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 20),
              child: Row(
                children: [
                  Expanded(
                    child: _AtalhoModerno(
                      icon: CupertinoIcons.photo_on_rectangle,
                      rotulo: 'Mídia',
                      subrotulo: 'Galeria',
                      onTap: () => _importarMidia(context, ref),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: _AtalhoModerno(
                      icon: CupertinoIcons.arrow_down_doc,
                      rotulo: 'XML',
                      subrotulo: 'Cena 3D',
                      onTap: () => _importarCena(context, ref),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: _AtalhoModerno(
                      icon: CupertinoIcons.doc_on_doc,
                      rotulo: 'Template',
                      subrotulo: 'Modelos',
                      onTap: () => _openTemplate(context, ref),
                    ),
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: _AtalhoModerno(
                      icon: CupertinoIcons.sparkles,
                      rotulo: 'Mural',
                      subrotulo: 'Social',
                      onTap: () => ref.read(homeTabProvider.notifier).state = 2,
                    ),
                  ),
                ],
              ),
            ),
          ],
          const SizedBox(height: 26),
          _TituloSecao(
            'Recentes',
            detalhe: projects.isEmpty
                ? null
                : '${projects.length} ${projects.length == 1 ? 'projeto' : 'projetos'}',
          ),
          const SizedBox(height: 4),
          if (projects.isEmpty)
            Padding(
              padding: EdgeInsets.fromLTRB(20, 10, 20, 6),
              child: AppText('Seus projetos aparecem aqui, com a miniatura do que voce fez.',
                style: TextStyle(color: AppColors.muted, fontSize: 13.5),
              ),
            )
          else
            // UMA LISTA, e nao uma fila que rola de lado: o nome inteiro,
            // a ficha e o menu de cada projeto cabem numa linha.
            ValueListenableBuilder<int>(
              valueListenable: ThumbnailService.instance.revision,
              builder: (_, rev, _) => Column(
                children: [
                  for (final project in visiveis)
                    _LinhaProjeto(
                      key: ValueKey('projeto-${project.id}'),
                      project: project,
                      thumb: ThumbnailService.instance.fileFor(project.id),
                      revision: rev,
                      onOpen: () => _openProject(context, ref, project),
                      onMenu: () => _menuDoProjeto(context, ref, project),
                    ),
                ],
              ),
            ),
          if (projects.length > _recentesNaInicio)
            _Linha(
              key: const ValueKey('projetos-todos'),
              icon: mostrarTodos
                  ? CupertinoIcons.chevron_up
                  : CupertinoIcons.chevron_down,
              texto: mostrarTodos
                  ? 'Mostrar menos'
                  : 'Mostrar todos os ${projects.length} projetos',
              onTap: () => ref.read(_mostrarTodosProvider.notifier).state =
                  !mostrarTodos,
            ),
          // MODELOS: motions inteiros montados camada por camada — abrir
          // um e ver como cada coisa foi feita. Estudio: usam texto,
          // formas e efeitos que o nucleo nao mostra.
          if (completo) ...[
            const SizedBox(height: 24),
            const _TituloSecao('Modelos'),
            const SizedBox(height: 10),
            SizedBox(
              height: 176,
              child: ListView(
                scrollDirection: Axis.horizontal,
                padding: const EdgeInsets.symmetric(horizontal: 20),
                children: [
                  _CartaoModelo(
                    imagem: 'assets/templates/campo.jpg',
                    titulo: 'CAMPO · A árvore da manhã',
                    detalhe: '25 s · cinco tomadas · cenário 3D editável',
                    onTap: () => _openCampo(context, ref),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/flor.png',
                    titulo: 'FLOR · Dez segundos de manhã',
                    detalhe: '10 s · três tomadas · câmera na mão',
                    onTap: () =>
                        _abrirModelo(context, ref, buildFlorTemplate()),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/prisma.jpg',
                    titulo: 'PRISMA · Dezessete segundos em loop',
                    detalhe: '17 s · seis cenas 3D · loop abstrato',
                    onTap: () =>
                        _abrirModelo(context, ref, buildPrismaTemplate()),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/deriva.jpg',
                    titulo: 'DERIVA · O astronauta perdido',
                    detalhe: '18 s · tres tomadas · luz de vacuo',
                    onTap: () => _openDeriva(context, ref),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/monolito.jpg',
                    titulo: 'MONOLITO · O astronauta e a porta',
                    detalhe: '16 s · noite, neblina e luz magenta',
                    onTap: () => _openMonolito(context, ref),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/colina.jpg',
                    titulo: 'COLINA · A TV no morro',
                    detalhe: '6 s · cena 3D realista · orbita rasteira',
                    onTap: () =>
                        _abrirModelo(context, ref, buildColinaTvTemplate()),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/mao-enterrada.png',
                    titulo: 'MÃO ENTERRADA · Deserto ao entardecer',
                    detalhe: '15 s · 4 câmeras · malha orgânica e céu',
                    onTap: () =>
                        _abrirModelo(context, ref, buildMaoEnterradaTemplate()),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/abyss.jpg',
                    titulo: 'ABISMO · Cinema 3D',
                    detalhe: '14 s · 4 cameras · personagem com rig',
                    onTap: () => _abrirModelo(
                      context,
                      ref,
                      buildAbyssCinematicTemplate(),
                    ),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/vhf/thumbnail.jpg',
                    titulo: 'VHF · Neon Orbit',
                    detalhe: '12 cenas · vetores e gradientes animados',
                    onTap: () => _openVhfMotion(context, ref),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/dnyx/thumbnail.jpg',
                    titulo: 'Aurea App · RMK Dnyx',
                    detalhe: 'Texto, fotos, cursores e audio editaveis',
                    onTap: () => _openDnyxRemix(context, ref),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/reference-rebuild.jpg',
                    titulo: 'Nova recriacao · Codex',
                    detalhe: '5 cenas · 280 quadros · camadas editaveis',
                    onTap: () => _openReferenceRebuild(context, ref),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/notes.jpg',
                    titulo: 'Notes',
                    detalhe: 'Icone, botao, listas, whip e glow',
                    onTap: () =>
                        _abrirModelo(context, ref, buildNotesMotionTemplate()),
                  ),
                  _CartaoModelo(
                    imagem: 'assets/templates/notes.jpg',
                    titulo: 'Pindown',
                    detalhe: 'Casa, faisca medida e coroas 3D',
                    onTap: () => _abrirModelo(
                      context,
                      ref,
                      buildPindownMotionTemplate(),
                    ),
                  ),
                ],
              ),
            ),
          ],
          const SizedBox(height: 24),
          _SpotlightComunidade(
            onExplorar: () => ref.read(homeTabProvider.notifier).state = 2,
          ),
          const SizedBox(height: 12),
          _Linha(
            icon: CupertinoIcons.sparkles,
            texto: 'O que ha de novo nesta versao',
            onTap: () => showWhatsNewSheet(context),
          ),
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
            icon: CupertinoIcons.exclamationmark_bubble,
            texto: 'Versao beta: achou um problema? Conte pra gente',
            onTap: () => showReportSheet(context),
          ),
        ],
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

class _TituloSecao extends StatelessWidget {
  const _TituloSecao(this.titulo, {this.detalhe});

  final String titulo;
  final String? detalhe;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 20),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.end,
        children: [
          Expanded(
            child: AppText(titulo, style: Theme.of(context).textTheme.titleMedium),
          ),
          if (detalhe != null)
            AppText(
              detalhe!,
              style: TextStyle(fontSize: 12.5, color: AppColors.muted),
            ),
        ],
      ),
    );
  }
}

/// Uma linha de projeto: miniatura real (quando o projeto ja foi aberto
/// e fechado uma vez), nome, ficha e o menu. Toque abre; segurar ou as
/// reticencias abrem o menu.
class _LinhaProjeto extends StatelessWidget {
  const _LinhaProjeto({
    super.key,
    required this.project,
    required this.thumb,
    required this.revision,
    required this.onOpen,
    required this.onMenu,
  });

  final VideoProject project;
  final File? thumb;
  final int revision;
  final VoidCallback onOpen;
  final VoidCallback onMenu;

  @override
  Widget build(BuildContext context) {
    final t = thumb;
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onOpen,
      onLongPress: onMenu,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(20, 7, 6, 7),
        child: Row(
          children: [
            ClipRRect(
              borderRadius: BorderRadius.circular(10),
              child: SizedBox(
                width: 92,
                height: 58,
                child: t == null
                    ? ColoredBox(
                        color: AppColors.surfaceHigh,
                        child: Center(
                          child: Icon(
                            CupertinoIcons.film,
                            color: AppColors.muted,
                            size: 20,
                          ),
                        ),
                      )
                    : Image.file(
                        t,
                        key: ValueKey('${project.id}-$revision'),
                        fit: BoxFit.cover,
                        gaplessPlayback: true,
                      ),
              ),
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  AppText(
                    project.name,
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(
                      fontSize: 15,
                      fontWeight: FontWeight.w600,
                      letterSpacing: -0.1,
                      color: AppColors.onDark,
                    ),
                  ),
                  const SizedBox(height: 3),
                  AppText(
                    fichaDoProjeto(project),
                    maxLines: 1,
                    overflow: TextOverflow.ellipsis,
                    style: TextStyle(fontSize: 12, color: AppColors.muted),
                  ),
                ],
              ),
            ),
            // O MENU TEM 44 PX de alvo: e por onde se apaga, duplica e
            // renomeia — nada disso pode depender de um gesto escondido.
            GestureDetector(
              key: ValueKey('projeto-menu-${project.id}'),
              behavior: HitTestBehavior.opaque,
              onTap: onMenu,
              child: SizedBox(
                width: 44,
                height: 44,
                child: Icon(
                  CupertinoIcons.ellipsis,
                  size: 18,
                  color: AppColors.muted,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
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
  late final TextEditingController _campo = TextEditingController(
    text: widget.inicial,
  );

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
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: SizedBox(
        width: 156,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            ClipRRect(
              borderRadius: BorderRadius.circular(14),
              child: SizedBox(
                width: 156,
                height: 118,
                child: Image.asset(imagem, fit: BoxFit.cover),
              ),
            ),
            const SizedBox(height: 8),
            AppText(titulo,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w600,
                letterSpacing: -0.1,
                color: AppColors.onDark,
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
    );
  }
}

/// Linha simples com icone e seta: sem caixa, sem borda.
class _Linha extends StatelessWidget {
  const _Linha({
    super.key,
    required this.icon,
    required this.texto,
    required this.onTap,
  });

  final IconData icon;
  final String texto;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Padding(
        padding: const EdgeInsets.fromLTRB(20, 12, 20, 12),
        child: Row(
          children: [
            Icon(icon, size: 18, color: AppColors.lime),
            const SizedBox(width: 12),
            Expanded(
              child: AppText(texto,
                style: TextStyle(fontSize: 14, color: AppColors.onDark),
              ),
            ),
            Icon(
              CupertinoIcons.chevron_right,
              size: 15,
              color: AppColors.muted,
            ),
          ],
        ),
      ),
    );
  }
}

/// Atalho moderno do dock criativo da Home com ícone, título e subtítulo.
class _AtalhoModerno extends StatelessWidget {
  const _AtalhoModerno({
    required this.icon,
    required this.rotulo,
    required this.subrotulo,
    required this.onTap,
  });

  final IconData icon;
  final String rotulo;
  final String subrotulo;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Container(
        padding: const EdgeInsets.symmetric(vertical: 10, horizontal: 8),
        decoration: BoxDecoration(
          color: AppColors.surface,
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: AppColors.hairline, width: 1),
        ),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 20, color: AppColors.lime),
            const SizedBox(height: 5),
            AppText(
              rotulo,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                fontSize: 12.5,
                fontWeight: FontWeight.w600,
                color: AppColors.onDark,
              ),
            ),
            AppText(
              subrotulo,
              maxLines: 1,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(fontSize: 10, color: AppColors.muted),
            ),
          ],
        ),
      ),
    );
  }
}

/// Destaque interativo da Comunidade na Home que conecta direto ao mural.
class _SpotlightComunidade extends ConsumerWidget {
  const _SpotlightComunidade({required this.onExplorar});

  final VoidCallback onExplorar;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 20),
      child: Container(
        padding: const EdgeInsets.all(16),
        decoration: BoxDecoration(
          gradient: LinearGradient(
            colors: [AppColors.surface, AppColors.surfaceHigh],
            begin: Alignment.topLeft,
            end: Alignment.bottomRight,
          ),
          borderRadius: BorderRadius.circular(16),
          border: Border.all(
            color: AppColors.lime.withValues(alpha: 0.25),
            width: 1,
          ),
        ),
        child: Row(
          children: [
            Container(
              width: 44,
              height: 44,
              decoration: BoxDecoration(
                color: AppColors.lime.withValues(alpha: 0.15),
                borderRadius: BorderRadius.circular(12),
              ),
              child: Icon(
                CupertinoIcons.person_2_fill,
                color: AppColors.lime,
                size: 22,
              ),
            ),
            const SizedBox(width: 14),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  AppText('Mural da Comunidade',
                    style: TextStyle(
                      fontSize: 14.5,
                      fontWeight: FontWeight.w700,
                      color: AppColors.onDark,
                    ),
                  ),
                  const SizedBox(height: 2),
                  AppText('Explore projetos reais, templates e criações no Cloudflare.',
                    style: TextStyle(
                      fontSize: 11.5,
                      color: AppColors.muted,
                      height: 1.3,
                    ),
                  ),
                ],
              ),
            ),
            const SizedBox(width: 8),
            FilledButton(
              style: FilledButton.styleFrom(
                backgroundColor: AppColors.lime,
                foregroundColor: Colors.black,
                padding: const EdgeInsets.symmetric(
                  horizontal: 12,
                  vertical: 8,
                ),
                visualDensity: VisualDensity.compact,
                textStyle: const TextStyle(
                  fontSize: 12,
                  fontWeight: FontWeight.w700,
                ),
              ),
              onPressed: onExplorar,
              child: const AppText('Ver feed'),
            ),
          ],
        ),
      ),
    );
  }
}
