import 'dart:io';

import 'package:file_picker/file_picker.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';
import 'package:image_picker/image_picker.dart' show XFile;

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/pedir_nome.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../media/application/media_import_service.dart';
import '../../../../media/application/midias_recentes.dart';
import '../../../../media/application/sons_recentes.dart';
import '../../../application/editor_controller.dart';
import '../../../application/font_service.dart';
import '../../../application/freehand_session.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/model_import3d.dart';
import '../../../domain/modelo_do_texto3d.dart';
import '../../../domain/shape.dart';
import '../../../domain/shape_library.dart';
import '../../../domain/svg_document.dart';
import '../../sketchfab/sketchfab_screen.dart' show abrirTelaDoSketchfab;
import '../../widgets/gallery_panel.dart' show MidiaDoLote;
import '../../widgets/importacao_3d.dart' show concluirImportacao3D;
import '../paineis/pontos.dart' show abrirEditarPontosDaForma;
import '../shell/contrato.dart';

// ===========================================================================
// AS ACOES DO "+"
// ===========================================================================
//
// A LOGICA veio da folha antiga (`widgets/add_layer_sheet.dart`): importar
// 3D com a ficha de modelo pesado, Texto 3D com material e fonte, SVG como
// forma editavel, audio de arquivo ou de video, midia em lote. A folha
// nova so troca a aparencia; nenhuma porta de adicionar sumiu.
//
// Tudo entra NO CABECOTE e a camada nova vira a selecao (o `_push` do
// controlador ja escolhe a que nasceu).

/// O QUE O "+" PRECISA DO EDITOR para agir — inclusive DEPOIS de a folha
/// fechar: o contexto e o `ref` sao os da casca, que continuam vivos
/// (um `ref` da folha desmontada estoura na primeira leitura).
class EditorParaAdicionar {
  const EditorParaAdicionar({
    required this.context,
    required this.ref,
    required this.playback,
    required this.abrirPainel,
  });

  final BuildContext context;
  final WidgetRef ref;
  final PlaybackController playback;
  final void Function(PainelId id) abrirPainel;

  Duration get agora => playback.time.value;
  EditorController get controlador =>
      ref.read(editorControllerProvider.notifier);

  void aviso(String texto) {
    if (context.mounted) AureaSnack.show(context, translate(context, texto));
  }
}

// ------------------------------------------------------------------ midia

/// UMA MIDIA do aparelho: video (com duracao conhecida ou a esperar) ou
/// imagem. Veio intacta do `onImport` da folha antiga.
Future<void> adicionarMidia(
  EditorParaAdicionar e,
  XFile arquivo, {
  required bool video,
  required Duration duracao,
}) async {
  final c = e.controlador;
  if (!video) {
    c.addImageLayer(e.agora, arquivo.path, arquivo.name);
  } else if (duracao > Duration.zero) {
    c.addVideoLayer(e.agora, arquivo.path, arquivo.name, duracao);
  } else {
    await c.importVideoAwaitingDuration(e.agora, arquivo.path, arquivo.name);
  }
}

/// VARIAS DE UMA VEZ: juntas no cabecote, ou uma comecando onde a outra
/// acaba — na ordem em que foram marcadas.
Future<void> adicionarLoteDeMidia(
  EditorParaAdicionar e,
  List<MidiaDoLote> midias, {
  required bool emSequencia,
}) async {
  final c = e.controlador;
  var t = e.agora;
  for (final m in midias) {
    if (m.video) {
      if (m.duration > Duration.zero) {
        c.addVideoLayer(t, m.file.path, m.file.name, m.duration);
      } else {
        await c.importVideoAwaitingDuration(t, m.file.path, m.file.name);
      }
    } else {
      c.addImageLayer(t, m.file.path, m.file.name, duracao: m.duration);
    }
    if (emSequencia && m.duration > Duration.zero) t += m.duration;
  }
}

/// UM RECENTE DA AUREA: o arquivo ja esta copiado dentro do app, entao nao
/// ha seletor nem copia nova. Sumiu do disco: sai da lista e avisa.
/// Devolve se entrou.
Future<bool> usarMidiaRecente(EditorParaAdicionar e, MidiaRecente m) async {
  final recentes = e.ref.read(midiasRecentesProvider.notifier);
  if (!File(m.caminho).existsSync()) {
    recentes.tirar(m.caminho);
    e.aviso('Essa mídia não está mais no aparelho.');
    return false;
  }
  await adicionarMidia(
    e,
    arquivoJaImportado(m.caminho, m.nome),
    video: m.video,
    duracao: m.duracao,
  );
  // Usou de novo: volta para a frente da fila.
  try {
    recentes.registrar(m);
  } catch (_) {}
  return true;
}

// ------------------------------------------------------------------ audio

/// AUDIO de um arquivo, ou o som de um video. Devolve se entrou.
Future<bool> importarAudio(
  EditorParaAdicionar e, {
  bool doVideo = false,
}) async {
  final antes = e.ref.read(editorControllerProvider).layers.length;
  try {
    await e.controlador.importAudioFile(e.agora, fromVideo: doVideo);
  } catch (erro) {
    e.aviso(
      erro is FormatException
          ? erro.message
          : 'Não consegui importar esse áudio. Tente outro arquivo.',
    );
    return false;
  }
  return e.ref.read(editorControllerProvider).layers.length > antes;
}

/// UM SOM DOS RECENTES entra direto (a duracao e lida na hora).
Future<bool> usarSomRecente(EditorParaAdicionar e, SomRecente som) async {
  try {
    await e.controlador.addAudioRecente(e.agora, som.caminho, som.nome);
    return true;
  } catch (_) {
    e.aviso('Não consegui usar esse som. Importe o arquivo de novo.');
    e.ref.read(sonsRecentesProvider.notifier).tirar(som.caminho);
    return false;
  }
}

// ----------------------------------------------------------------- formas

/// UMA FORMA DA BIBLIOTECA, no cabecote.
void adicionarForma(EditorParaAdicionar e, ShapeLibraryEntry forma) =>
    e.controlador.addShapeLayer(e.agora, contents: forma.build(), name: forma.nome);

/// DESENHO A MAO LIVRE: o palco passa a desenhar (o traco vira camada).
void comecarDesenhoLivre(EditorParaAdicionar e) {
  e.playback.pause();
  e.ref.read(freehandRequestProvider.notifier).state = true;
}

/// DESENHO VETORIAL: uma forma so de traco e o editor de pontos aberto
/// nela — desenhar e colocar nos.
void comecarDesenhoVetorial(EditorParaAdicionar e) {
  e.controlador.addShapeLayer(
    e.agora,
    contents: [
      ShapeStroke(color: const Color(0xFFFFFFFF), width: AnimatedDouble(10)),
    ],
    name: 'Desenho',
  );
  final id = e.ref.read(selectedLayerProvider);
  if (id == null || !e.context.mounted) return;
  abrirEditarPontosDaForma(e.context, e.ref, e.playback, id);
}

/// ARQUIVO SVG: entra como forma editavel, com a cor de cada desenho.
Future<void> importarSvg(EditorParaAdicionar e) async {
  String? caminho;
  try {
    final r = await FilePicker.platform.pickFiles(
      type: FileType.custom,
      allowedExtensions: const ['svg'],
    );
    caminho = r?.files.single.path;
  } catch (_) {
    caminho = null;
  }
  if (caminho == null || !e.context.mounted) return;
  SvgImportado svg;
  try {
    svg = lerSvg(await File(caminho).readAsString());
  } on SvgException catch (erro) {
    e.aviso(erro.message);
    return;
  } catch (_) {
    e.aviso('Não consegui ler esse SVG.');
    return;
  }
  final nome = caminho
      .split(RegExp(r'[\\/]'))
      .last
      .replaceAll(RegExp(r'\.svg$', caseSensitive: false), '');
  e.controlador.addSvgLayers(svg, e.agora, nome: nome);
  if (svg.ignorados.isNotEmpty && e.context.mounted) {
    AureaSnack.show(
      e.context,
      moldar(e.context, '{0} desenho(s); ficou de fora: {1}', [
        svg.formas.length,
        svg.ignorados.join(', '),
      ]),
    );
  }
}

// --------------------------------------------------------------------- 3D

/// UM MODELO DO APARELHO: o seletor e depois a porta unica da importacao
/// 3D ([concluirImportacao3D]) — a ficha de modelo pesado com a opcao de
/// otimizar, os mapas preparados e o credito. Devolve o id do no.
Future<String?> importarModelo3DDoAparelho(EditorParaAdicionar e) async {
  try {
    final escolha = await FilePicker.platform.pickFiles(
      allowMultiple: true,
      type: FileType.any,
      withData: false,
    );
    final caminhos =
        escolha?.files.map((f) => f.path).whereType<String>().toList() ??
        const <String>[];
    if (caminhos.isEmpty || !e.context.mounted) return null;
    return await concluirImportacao3D(
      e.context,
      e.ref,
      caminhos,
      playhead: e.agora,
    );
  } on ModelImportException catch (erro) {
    e.aviso(erro.message);
  } catch (_) {
    e.aviso('Não consegui ler esse modelo. Tente GLB, glTF, OBJ ou FBX.');
  }
  return null;
}

/// O SKETCHFAB: buscar, ver a ficha, baixar — e o mesmo caminho do
/// aparelho depois do download.
Future<String?> abrirSketchfab(EditorParaAdicionar e) =>
    abrirTelaDoSketchfab(e.context, playhead: e.agora);

/// ESCOLHA NUMA LISTA CURTA, no meio da tela (material, fonte).
Future<T?> _escolher<T>(
  BuildContext context,
  String titulo,
  List<AureaMenuItem<T>> itens,
) {
  final tela = MediaQuery.sizeOf(context);
  return mostrarAureaMenu<T>(
    context,
    titulo: titulo,
    itens: itens,
    ancora: Rect.fromLTWH(
      (tela.width + AureaDims.larguraDoMenu) / 2,
      tela.height * .2,
      0,
      0,
    ),
  );
}

/// TEXTO 3D: o texto, o material e (havendo escolha) a fonte; nasce no
/// cabecote e o painel Texto 3D abre em cima dele, com a previa ao vivo.
Future<void> criarTexto3D(EditorParaAdicionar e) async {
  final ctx = e.context;
  final agora = e.agora;
  final c = e.controlador;
  final texto = await pedirNome(ctx, titulo: 'Texto 3D', atual: 'TEXTO 3D');
  if (texto == null || texto.trim().isEmpty || !ctx.mounted) return;
  final estilo = await _escolher<EstiloDoTexto3D>(ctx, 'Material', [
    for (final s in EstiloDoTexto3D.values)
      AureaMenuItem(
        valor: s,
        rotulo: nomeDoEstiloDoTexto3D(s),
        chave: 'texto3d-estilo-${s.name}',
      ),
  ]);
  if (estilo == null || !ctx.mounted) return;
  // As fontes importadas em Ajustes valem para o 3D tambem; o indice mora
  // no disco e pode ainda nao ter sido lido nesta sessao.
  await FontService.instance.loadAll();
  if (!ctx.mounted) return;
  String? familia;
  final familias = FontService.instance.families;
  if (familias.length > 1) {
    familia = await _escolher<String>(ctx, 'Fonte', [
      for (final f in familias)
        AureaMenuItem(
          valor: f,
          rotulo: f,
          traduzir: false,
          chave: 'texto3d-fonte-$f',
        ),
    ]);
    if (familia == null || !ctx.mounted) return;
  }
  final no = await c.addTexto3D(agora, texto, estilo, familia: familia);
  if (!ctx.mounted) return;
  if (no == null) {
    e.aviso('Não consegui criar o texto 3D com essa fonte.');
    return;
  }
  final cena = e.ref
      .read(editorControllerProvider)
      .layers
      .whereType<Scene3DLayer>()
      .where((l) => l.scene.nodeById(no) != null)
      .firstOrNull;
  if (cena == null) return;
  e.ref.read(selectedLayerProvider.notifier).state = cena.id;
  e.abrirPainel(PainelId.texto3d);
}
