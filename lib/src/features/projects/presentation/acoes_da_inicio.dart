import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' show MaterialPageRoute;
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:path_provider/path_provider.dart';

import '../../../core/l10n/app_language.dart';
import '../../../core/ui/pedir_nome.dart';
import '../../../core/ui/snack.dart';
import '../../editor/application/editor_controller.dart';
import '../../editor/domain/ajuste_da_midia.dart';
import '../../editor/domain/template_pack.dart';
import '../../editor/domain/video_project.dart';
import '../../editor/presentation/editor_screen.dart';
import '../../media/application/media_import_service.dart'
    show mediaImportServiceProvider, proporcaoDaFoto;
import '../../media/application/midias_recentes.dart'
    show midiasRecentesProvider, registrarMidiaImportada;
import '../application/dnyx_remix_assets.dart';
import '../application/projects_controller.dart';
import '../application/reference_rebuild_assets.dart';
import '../application/thumbnail_service.dart';
import '../application/vhf_motion_assets.dart';
import '../domain/cena_xml_import.dart';
import '../domain/notes_motion_template.dart';
import '../domain/pacote_aurea.dart';
import '../domain/pacote_zip_import.dart';
import '../domain/pindown_motion_template.dart';

/// AS ACOES DA INICIO, separadas da tela.
///
/// A tela so decide O QUE mostrar; o que acontece ao tocar mora aqui, e
/// tudo que ja existia (controlador de projetos, controlador do editor,
/// miniaturas, pacote .aurea) e REUSADO — nada de persistencia nova.
///
/// Tres portas viram provider para o teste poder trocar sem montar o
/// editor nem abrir o seletor de arquivos do sistema: carregar o projeto
/// no editor, a tela do editor e salvar o pacote.

/// Poe o projeto no controlador do editor (sem navegar).
typedef CarregarNoEditor = void Function(VideoProject projeto);

final carregarNoEditorProvider = Provider<CarregarNoEditor>(
  (ref) =>
      (projeto) => ref.read(editorControllerProvider.notifier).openProject(projeto),
);

/// A tela do editor. O editor e UMA tela e nao recebe argumento: o
/// projeto ja esta no controlador quando ela nasce.
final telaDoEditorProvider = Provider<WidgetBuilder>(
  (ref) =>
      (_) => const EditorScreen(),
);

/// Grava o pacote .aurea do projeto onde a pessoa escolher. Verdadeiro =
/// salvou; falso = cancelou.
typedef CompartilharProjeto = Future<bool> Function(VideoProject projeto);

final compartilharProjetoProvider = Provider<CompartilharProjeto>(
  (ref) => salvarPacoteDoProjeto,
);

/// O PACOTE, e nao o JSON: o projeto leva as midias junto e abre em
/// qualquer aparelho (ver [PacoteAurea]). Mesmo caminho do "Exportar
/// pacote .aurea" do editor: o seletor do sistema recebe os bytes — no
/// Android e no iOS e ele quem grava (e oferece Drive, Arquivos...).
Future<bool> salvarPacoteDoProjeto(VideoProject projeto) async {
  final limpo = projeto.name
      .replaceAll(RegExp(r'[<>:"/\\|?*\x00-\x1f]'), '_')
      .trim();
  final arquivo =
      '${limpo.isEmpty ? 'projeto' : limpo}.${PacoteAurea.extensao}';
  final bytes = PacoteAurea.montar(projeto);
  final caminho = await FilePicker.platform.saveFile(
    dialogTitle: arquivo,
    fileName: arquivo,
    type: FileType.custom,
    allowedExtensions: const [PacoteAurea.extensao],
    bytes: bytes,
  );
  if (caminho == null) return false;
  if (!Platform.isAndroid && !Platform.isIOS) {
    await File(caminho).writeAsBytes(bytes, flush: true);
  }
  return true;
}

// ------------------------------------------------------------- abrir

/// O UNICO CAMINHO para o editor: [novo] poe o projeto na lista antes
/// (projeto criado, importado, modelo); um projeto da lista so abre.
void entrarNoEditor(
  BuildContext context,
  WidgetRef ref,
  VideoProject projeto, {
  bool novo = false,
}) {
  if (novo) ref.read(projectsControllerProvider.notifier).add(projeto);
  ref.read(carregarNoEditorProvider)(projeto);
  if (!context.mounted) return;
  Navigator.of(
    context,
  ).push(MaterialPageRoute<void>(builder: ref.read(telaDoEditorProvider)));
}

// ------------------------------------------------- menu do projeto

/// RENOMEAR: o nome novo volta pelo mesmo `upsert` do editor — e, como
/// toda edicao, leva o projeto para a frente dos recentes.
Future<void> renomearProjeto(
  BuildContext context,
  WidgetRef ref,
  VideoProject projeto,
) async {
  final nome = await pedirNome(context, titulo: 'Renomear', atual: projeto.name);
  if (nome == null || nome == projeto.name) return;
  ref
      .read(projectsControllerProvider.notifier)
      .upsert(projeto.copyWith(name: nome));
}

/// DUPLICAR: o mesmo projeto com id novo, na frente da lista.
VideoProject duplicarProjeto(WidgetRef ref, VideoProject projeto) {
  final copia = projeto
      .copyWith(name: '${projeto.name} (cópia)')
      .comIdNovo();
  ref.read(projectsControllerProvider.notifier).add(copia);
  return copia;
}

/// Tira o projeto da lista e do disco, com a miniatura junto.
void apagarProjeto(WidgetRef ref, String id) {
  ref.read(projectsControllerProvider.notifier).remove(id);
  ThumbnailService.instance.delete(id);
}

/// EXCLUIR, com uma pergunta: apagar nao tem volta.
Future<void> excluirProjeto(
  BuildContext context,
  WidgetRef ref,
  VideoProject projeto,
) async {
  final ok = await showCupertinoDialog<bool>(
    context: context,
    builder: (c) => CupertinoAlertDialog(
      // O nome e da pessoa: nao passa pelo dicionario.
      title: Text(projeto.name),
      content: const Padding(
        padding: EdgeInsets.only(top: 8),
        child: AppText('Nao da para desfazer.'),
      ),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.of(c).pop(false),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          key: const ValueKey('excluir-confirmar'),
          isDestructiveAction: true,
          onPressed: () => Navigator.of(c).pop(true),
          child: const AppText('Excluir'),
        ),
      ],
    ),
  );
  if (ok != true) return;
  apagarProjeto(ref, projeto.id);
}

/// EXCLUIR OS ESCOLHIDOS, com uma pergunta so para o lote inteiro.
Future<bool> excluirEscolhidos(
  BuildContext context,
  WidgetRef ref,
  Set<String> ids,
) async {
  final ok = await showCupertinoDialog<bool>(
    context: context,
    builder: (c) => CupertinoAlertDialog(
      title: AppTextMoldado('Excluir {0} projetos?', [ids.length]),
      content: const Padding(
        padding: EdgeInsets.only(top: 8),
        child: AppText('Nao da para desfazer.'),
      ),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.of(c).pop(false),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          key: const ValueKey('projetos-excluir-lote'),
          isDestructiveAction: true,
          onPressed: () => Navigator.of(c).pop(true),
          child: const AppText('Excluir'),
        ),
      ],
    ),
  );
  if (ok != true) return false;
  for (final id in ids) {
    apagarProjeto(ref, id);
  }
  return true;
}

/// COMPARTILHAR = o pacote .aurea (projeto + midias num arquivo so).
Future<void> compartilharProjeto(
  BuildContext context,
  WidgetRef ref,
  VideoProject projeto,
) async {
  try {
    final salvou = await ref.read(compartilharProjetoProvider)(projeto);
    if (!salvou || !context.mounted) return;
    AureaSnack.show(context, translate(context, 'Pacote .aurea salvo'));
  } catch (_) {
    if (!context.mounted) return;
    AureaSnack.show(
      context,
      translate(context, 'Não consegui salvar o pacote.'),
    );
  }
}

/// Confirma e apaga TODOS os projetos e miniaturas.
Future<void> apagarTodosOsProjetos(BuildContext context, WidgetRef ref) async {
  final total = ref.read(projectsControllerProvider).length;
  if (total == 0) return;
  final ok = await showCupertinoDialog<bool>(
    context: context,
    builder: (c) => CupertinoAlertDialog(
      title: const AppText('Apagar todos os projetos?'),
      content: Text(
        '$total projeto(s) serao apagados. Isso nao pode ser desfeito.',
      ),
      actions: [
        CupertinoDialogAction(
          onPressed: () => Navigator.of(c).pop(false),
          child: const AppText('Cancelar'),
        ),
        CupertinoDialogAction(
          key: const ValueKey('apagar-todos-confirmar'),
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

// ------------------------------------------------------- importar

void _falha(BuildContext context, String texto) {
  if (!context.mounted) return;
  AureaSnack.show(context, translate(context, texto));
}

Future<void> _aviso(BuildContext context, String titulo, String texto) =>
    showCupertinoDialog<void>(
      context: context,
      builder: (c) => CupertinoAlertDialog(
        title: AppText(titulo),
        content: Padding(
          padding: const EdgeInsets.only(top: 8),
          child: Text(texto),
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

const _extensoesDeVideo = {'mp4', 'mov', 'm4v', 'avi', 'mkv', 'webm', '3gp'};

/// IMPORTAR MIDIA: foto ou video da galeria vira um projeto novo, na
/// proporcao da midia, com ela ja no palco.
Future<void> importarMidia(BuildContext context, WidgetRef ref) async {
  try {
    // A MIDIA E COPIADA PARA O APP ANTES DE VIRAR CAMADA: o seletor
    // devolve um arquivo em cache, que o Android limpa quando quer.
    final recentes = ref.read(midiasRecentesProvider.notifier);
    final arquivo = await ref.read(mediaImportServiceProvider).pickMediaFile();
    if (arquivo == null || !context.mounted) return;
    final caminho = arquivo.path;
    final nome = arquivo.name;
    final controller = ref.read(editorControllerProvider.notifier);
    final video = _extensoesDeVideo.contains(
      caminho.split('.').last.toLowerCase(),
    );
    // O PROJETO TEM A PROPORCAO DA MIDIA: a previa abre cheia, sem faixa.
    final sonda = video ? await controller.sondarVideo(caminho) : null;
    final proporcao = video
        ? sonda!.proporcao
        : await proporcaoDaFoto(caminho);
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
      final duracao = sonda!.duracao > Duration.zero
          ? sonda.duracao
          : const Duration(seconds: 5);
      controller.addVideoLayer(
        Duration.zero,
        caminho,
        nome,
        duracao,
        fonte: sonda.duracao > Duration.zero ? sonda.duracao : null,
        proporcao: proporcao,
      );
    } else {
      controller.addImageLayer(
        Duration.zero,
        caminho,
        nome,
        proporcao: proporcao,
      );
    }
    Navigator.of(
      context,
    ).push(MaterialPageRoute<void>(builder: ref.read(telaDoEditorProvider)));
    await registrarMidiaImportada(
      recentes,
      caminho: caminho,
      nome: nome,
      video: video,
      duracao: sonda?.duracao ?? Duration.zero,
    );
  } catch (_) {
    // A tela pode ter saido no meio da copia (o seletor sai e volta).
    if (context.mounted) _falha(context, 'Não consegui importar essa mídia.');
  }
}

/// ABRIR TEMPLATE: o arquivo vira um projeto NOVO, com id novo — abrir o
/// mesmo template duas vezes nao pode sobrescrever o trabalho da primeira.
Future<void> abrirTemplate(BuildContext context, WidgetRef ref) async {
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
  entrarNoEditor(
    context,
    ref,
    pack.project.copyWith(name: pack.name).comIdNovo(),
    novo: true,
  );
}

/// Onde as midias que chegam em pacote moram no aparelho.
Future<Directory> _pastaDasMidiasImportadas() async {
  final docs = await getApplicationDocumentsDirectory();
  return Directory(
    '${docs.path}/midias_importadas/${DateTime.now().millisecondsSinceEpoch}',
  );
}

String _resumoDaImportacao(CenaXmlResult r) {
  final b = StringBuffer()
    ..write('${r.layersImported} camadas e ')
    ..write('${r.keyframesImported} keyframes reconhecidos.');
  if (r.ignored.isNotEmpty) {
    b.write('\n\nFicou de fora:');
    for (final item in r.ignored.take(6)) {
      b.write('\n- $item');
    }
    if (r.ignored.length > 6) b.write('\n- e mais ${r.ignored.length - 6}');
  }
  return b.toString();
}

/// IMPORTAR PROJETO: pacote .aurea (abre direto), pacote .zip/.amproj ou
/// cena em XML (mostra o balanco antes de abrir).
Future<void> importarProjeto(BuildContext context, WidgetRef ref) async {
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
    final minusculo = nomeArquivo.toLowerCase();
    if (minusculo.endsWith('.aurea')) {
      final projeto = PacoteAurea.abrir(
        await File(caminho).readAsBytes(),
        await _pastaDasMidiasImportadas(),
      );
      if (!context.mounted) return;
      entrarNoEditor(context, ref, projeto, novo: true);
      return;
    }
    if (minusculo.endsWith('.zip') || minusculo.endsWith('.amproj')) {
      resultado = CenaEmZip.abrir(
        await File(caminho).readAsBytes(),
        await _pastaDasMidiasImportadas(),
        nome: nomeLimpo,
      );
    } else {
      resultado = importarCenaXml(
        await File(caminho).readAsString(),
        nome: nomeLimpo,
      );
    }
  } on FormatException catch (e) {
    if (context.mounted) {
      await _aviso(context, 'Nao deu para importar', e.message);
    }
    return;
  } on CenaXmlException catch (e) {
    if (context.mounted) {
      await _aviso(context, 'Nao deu para importar', e.message);
    }
    return;
  } catch (e) {
    if (context.mounted) {
      await _aviso(context, 'Nao deu para importar', 'Arquivo ilegivel: $e');
    }
    return;
  }
  if (!context.mounted) return;
  final abrir = await showCupertinoDialog<bool>(
    context: context,
    builder: (c) => CupertinoAlertDialog(
      title: const AppText('Cena importada'),
      content: Padding(
        padding: const EdgeInsets.only(top: 8),
        child: Text(_resumoDaImportacao(resultado), textAlign: TextAlign.left),
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
  entrarNoEditor(context, ref, resultado.project, novo: true);
}

// ------------------------------------------------------- modelos

/// Os modelos prontos: so dados — a abertura resolve por id.
const modelosProntos = [
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

/// MODELO PRONTO: um projeto montado por codigo, com id novo a cada
/// abertura — o mesmo cuidado do template em arquivo.
Future<void> abrirModelo(BuildContext context, WidgetRef ref, String id) async {
  Future<VideoProject> Function()? preparar;
  VideoProject? pronto;
  switch (id) {
    case 'vhf':
      preparar = prepareVhfMotion;
    case 'dnyx':
      preparar = prepareDnyxRemix;
    case 'reference':
      preparar = prepareReferenceRebuild;
    case 'notes':
      pronto = buildNotesMotionTemplate();
    case 'pindown':
      pronto = buildPindownMotionTemplate();
    default:
      return;
  }
  try {
    final modelo = pronto ?? await preparar!();
    if (!context.mounted) return;
    entrarNoEditor(context, ref, modelo.comIdNovo(), novo: true);
  } catch (_) {
    _falha(context, 'Nao consegui preparar o motion. Tente abrir novamente.');
  }
}
