import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../application/keyframe_clipboard.dart';
import '../../../application/playback_controller.dart';
import '../../../application/ui/editor_session.dart';
import '../../../domain/cut_ops.dart' show hasTimeRemap;
import '../../../domain/layer.dart';
import '../../../domain/layer_meta.dart';
import '../../../domain/layout_ops.dart';
import '../../../domain/mask.dart';
import '../../../domain/video_project.dart';
import 'alinhar.dart' show showAlignSheet;
import '../paineis/batidas.dart' show showBeatPulseSheet, showBeatsSheet;
import '../paineis/cameras.dart' show showCamerasSheet;
import 'decupar.dart' show showDecuparSheet;
import 'congelar.dart' show showFreezeSheet;
import '../paineis/extrude.dart' show showExtrudeSheet;
import 'oficio.dart' show showLoopSheet, showOrganizeSheet;
import 'texto_no_caminho.dart' show showTextPathSheet;
import 'acoes_da_camada.dart' show excluirCamadas, renomearCamada;
import 'acoes_de_midia.dart'
    show extrairAudioComAviso, mostrarColarEstilo, mostrarInfoDaMidia;
import 'legendar.dart' show showCaptionCreationSheet;
import '../shell/contrato.dart';
import 'barra_do_lote.dart';
import 'escolher_pai.dart';

// ===========================================================================
// O MENU DA CAMADA
// ===========================================================================
//
// Toque longo no clipe da timeline e o "Mais" da barra abrem o MESMO menu:
// uma lista de 250 com itens de 40 (AureaMenu), sem folha subindo por cima
// da timeline. O que e raro mora em "Mais ações…" — mas mora: cada acao do
// menu antigo (`shell/menu_da_camada.dart`, a doca `am/layer_menu.dart` e
// as acoes rapidas) tem porta aqui.
//
// UMA ACAO, UM PASSO DE DESFAZER: o que e sincrono passa por
// `runAsOneUndo` — sem ele, "ocultar" meio segundo depois de mexer num
// numero entrava no mesmo passo do numero, e a cena 3D vinculada gravava
// dois passos (o vinculo e a camera).

/// Os ids das acoes do menu (a chave de teste e `menu-camada-<id>`).
abstract final class AcaoDaCamada {
  static const duplicar = 'duplicar';
  static const dividir = 'dividir';
  static const apagar = 'apagar';
  static const renomear = 'renomear';
  static const ocultar = 'ocultar';
  static const travar = 'travar';
  static const solo = 'solo';
  static const selecionar = 'selecionar';
  static const subir = 'subir';
  static const descer = 'descer';
  static const copiar = 'copiar';
  static const colar = 'colar';
  static const copiarKeyframes = 'copiar-keyframes';
  static const colarKeyframes = 'colar-keyframes';
  static const copiarEstilo = 'copiar-estilo';
  static const colarEstilo = 'colar-estilo';
  static const agrupar = 'agrupar';
  static const entrarNoGrupo = 'entrar-no-grupo';
  static const desagrupar = 'desagrupar';
  static const tempoDoGrupo = 'tempo-do-grupo';
  static const vincular = 'vincular';
  static const alinhar = 'alinhar';
  static const etiqueta = 'etiqueta';
  static const maisAcoes = 'mais-acoes';

  // ---- "Mais ações…"
  static const decupar = 'decupar';
  static const copiarEfeitos = 'copiar-efeitos';
  static const colarEfeitos = 'colar-efeitos';
  static const mudo = 'mudo';
  static const congelar = 'congelar';
  static const apararInicio = 'aparar-inicio';
  static const apararFim = 'aparar-fim';
  static const moverInicio = 'mover-inicio';
  static const moverFim = 'mover-fim';
  static const inserir = 'inserir';
  static const sobrescrever = 'sobrescrever';
  static const levantar = 'levantar';
  static const extrair = 'extrair';
  static const batidas = 'batidas';
  static const legendar = 'legendar';
  static const reenquadrar = 'reenquadrar';
  static const estabilizar = 'estabilizar';
  static const cameras = 'cameras';
  static const camada3d = 'camada-3d';
  static const motionBlur = 'motion-blur';
  static const extrude = 'extrude';
  static const pulsar = 'pulsar';
  static const organizar = 'organizar';
  static const loop = 'loop';
  static const caminho = 'caminho';
  static const infoDaMidia = 'info-da-midia';
  static const extrairAudio = 'extrair-audio';
  static const caber = 'caber';
  static const preencher = 'preencher';
  static const esticar = 'esticar';
  static const espelharH = 'espelhar-h';
  static const espelharV = 'espelhar-v';
  static const recortar = 'recortar';
  static const soltarRecorte = 'soltar-recorte';
  static const grupoMascara = 'grupo-mascara';
  static const grupoRecorte = 'grupo-recorte';
  static const excluirEFechar = 'excluir-e-fechar';
  static const fecharBuracos = 'fechar-buracos';
}

AureaMenuItem<String> _item(
  String id,
  String rotulo,
  IconData icone, {
  bool habilitado = true,
  bool marcado = false,
  bool destrutivo = false,
}) => AureaMenuItem<String>(
  valor: id,
  rotulo: rotulo,
  icone: icone,
  habilitado: habilitado,
  marcado: marcado,
  destrutivo: destrutivo,
  chave: 'camada-$id',
);

/// OS ITENS DO MENU PRINCIPAL da [camada], no instante [t] (global). Pura:
/// o que ela precisa saber de fora vem nos parametros.
List<AureaMenuItem<String>> itensDoMenuDaCamada(
  VideoProject projeto,
  Layer camada,
  Duration t, {
  required bool temCamadaCopiada,
  required bool temKeyframesCopiados,
  required bool podeColarEstilo,
}) {
  final id = camada.id;
  final meta = projeto.metaOf(id);
  final idx = projeto.layers.indexWhere((l) => l.id == id);
  final temPai =
      projeto.linkFor(id, LayerProp.parent) != null ||
      (camada is Scene3DLayer && camada.cameraParentLayerId != null);
  final grupo = camada is GroupLayer;
  return [
    _item(
      AcaoDaCamada.duplicar,
      'Duplicar',
      CupertinoIcons.plus_square_on_square,
    ),
    _item(
      AcaoDaCamada.dividir,
      'Dividir no cabeçote',
      CupertinoIcons.scissors,
      habilitado: camada.activeAt(t),
    ),
    _item(
      AcaoDaCamada.apagar,
      'Apagar',
      CupertinoIcons.trash,
      destrutivo: true,
    ),
    // Bloqueada nao se renomeia: o cadeado fecha a edicao inteira.
    _item(
      AcaoDaCamada.renomear,
      'Renomear',
      CupertinoIcons.pencil,
      habilitado: !meta.locked,
    ),
    _item(
      AcaoDaCamada.ocultar,
      meta.hidden ? 'Mostrar' : 'Ocultar',
      meta.hidden ? CupertinoIcons.eye : CupertinoIcons.eye_slash,
    ),
    _item(
      AcaoDaCamada.travar,
      meta.locked ? 'Destravar' : 'Travar',
      meta.locked ? CupertinoIcons.lock_open : CupertinoIcons.lock,
    ),
    _item(AcaoDaCamada.solo, 'Solo', CupertinoIcons.scope, marcado: meta.solo),
    _item(AcaoDaCamada.selecionar, 'Selecionar várias', iconeDeSelecionar),
    _item(
      AcaoDaCamada.subir,
      'Mover para cima',
      CupertinoIcons.arrow_up_to_line,
      habilitado: idx > 0,
    ),
    _item(
      AcaoDaCamada.descer,
      'Mover para baixo',
      CupertinoIcons.arrow_down_to_line,
      habilitado: idx >= 0 && idx < projeto.layers.length - 1,
    ),
    _item(AcaoDaCamada.copiar, 'Copiar camada', CupertinoIcons.doc_on_doc),
    _item(
      AcaoDaCamada.colar,
      'Colar camada',
      CupertinoIcons.doc_on_clipboard,
      habilitado: temCamadaCopiada,
    ),
    _item(
      AcaoDaCamada.copiarKeyframes,
      'Copiar keyframes',
      CupertinoIcons.suit_diamond,
      habilitado: camada.keyframeTimes.isNotEmpty,
    ),
    _item(
      AcaoDaCamada.colarKeyframes,
      'Colar keyframes',
      CupertinoIcons.suit_diamond_fill,
      habilitado: temKeyframesCopiados,
    ),
    _item(
      AcaoDaCamada.copiarEstilo,
      'Copiar estilo',
      CupertinoIcons.paintbrush,
    ),
    _item(
      AcaoDaCamada.colarEstilo,
      'Colar estilo…',
      CupertinoIcons.paintbrush_fill,
      habilitado: podeColarEstilo,
    ),
    if (!grupo)
      _item(
        AcaoDaCamada.agrupar,
        'Agrupar (pré-compor)',
        CupertinoIcons.rectangle_stack,
      )
    else ...[
      _item(
        AcaoDaCamada.entrarNoGrupo,
        'Editar o grupo',
        CupertinoIcons.arrow_down_right_square,
      ),
      _item(
        AcaoDaCamada.desagrupar,
        'Desagrupar',
        CupertinoIcons.square_split_2x2,
      ),
      _item(AcaoDaCamada.tempoDoGrupo, 'Tempo do grupo', CupertinoIcons.timer),
    ],
    _item(
      AcaoDaCamada.vincular,
      temPai ? 'Vínculo (pai)…' : 'Vincular a…',
      temPai ? CupertinoIcons.link_circle_fill : CupertinoIcons.link,
      marcado: temPai,
    ),
    _item(AcaoDaCamada.alinhar, 'Alinhar…', CupertinoIcons.square_grid_3x2),
    _item(AcaoDaCamada.etiqueta, 'Etiqueta…', CupertinoIcons.tag),
    _item(AcaoDaCamada.maisAcoes, 'Mais ações…', CupertinoIcons.ellipsis),
  ];
}

/// "MAIS AÇÕES…": o que e raro, mas nao pode sumir — as acoes rapidas e a
/// doca do editor antigo, e as secoes Midia, Na composicao e Recorte do
/// menu antigo.
List<AureaMenuItem<String>> itensDeMaisAcoes(
  VideoProject projeto,
  Layer camada,
  Duration t, {
  required bool temEfeitosCopiados,
  required bool mudo,
  required bool temBaseDeRecorte,
}) {
  final id = camada.id;
  final video = camada is VideoLayer;
  final temSom = camada is AudioLayer || video;
  final visual = camada is! AudioLayer;
  final dentro = camada.activeAt(t);
  final recortada = camada.matteMode == MatteMode.recorte;
  final grupo = camada is GroupLayer ? camada : null;
  final midia = camada is ImageLayer || video || camada is AudioLayer;
  final extrudavel =
      camada is! NullLayer &&
      camada is! VideoLayer &&
      camada is! ParticulasLayer &&
      camada is! Element3DLayer &&
      camada is! AudioLayer;
  return [
    if (camada is VideoLayer)
      _item(
        AcaoDaCamada.decupar,
        'Decupar',
        CupertinoIcons.rectangle_split_3x1,
        habilitado: !camada.reverse && !hasTimeRemap(camada),
      ),
    _item(
      AcaoDaCamada.apararInicio,
      'Aparar o início no cabeçote',
      CupertinoIcons.arrow_right_to_line,
      habilitado: dentro,
    ),
    _item(
      AcaoDaCamada.apararFim,
      'Aparar o fim no cabeçote',
      CupertinoIcons.arrow_left_to_line,
      habilitado: dentro,
    ),
    _item(
      AcaoDaCamada.moverInicio,
      'Mover o início para o cabeçote',
      CupertinoIcons.arrow_right_to_line_alt,
    ),
    _item(
      AcaoDaCamada.moverFim,
      'Mover o fim para o cabeçote',
      CupertinoIcons.arrow_left_to_line_alt,
    ),
    if (video)
      _item(
        AcaoDaCamada.congelar,
        'Congelar quadro',
        CupertinoIcons.snow,
        habilitado: dentro,
      ),
    _item(
      AcaoDaCamada.copiarEfeitos,
      'Copiar efeitos',
      CupertinoIcons.sparkles,
      habilitado: camada.effects.isNotEmpty,
    ),
    _item(
      AcaoDaCamada.colarEfeitos,
      'Colar efeitos',
      CupertinoIcons.wand_stars,
      habilitado: temEfeitosCopiados,
    ),
    if (temSom) ...[
      _item(
        AcaoDaCamada.mudo,
        'Mudo',
        CupertinoIcons.speaker_slash,
        marcado: mudo,
      ),
      _item(AcaoDaCamada.batidas, 'Detectar batidas', CupertinoIcons.metronome),
      _item(
        AcaoDaCamada.inserir,
        'Inserir cópia no cabeçote',
        CupertinoIcons.arrow_right_to_line,
      ),
      _item(
        AcaoDaCamada.sobrescrever,
        'Sobrescrever no cabeçote',
        CupertinoIcons.rectangle_on_rectangle,
      ),
    ],
    _item(
      AcaoDaCamada.levantar,
      'Levantar trecho (Entrada–Saída)',
      CupertinoIcons.arrow_up_to_line,
    ),
    _item(
      AcaoDaCamada.extrair,
      'Extrair trecho (Entrada–Saída)',
      CupertinoIcons.scissors_alt,
    ),
    if (video) ...[
      _item(AcaoDaCamada.legendar, 'Legendar', CupertinoIcons.captions_bubble),
      _item(AcaoDaCamada.reenquadrar, 'Reenquadrar', CupertinoIcons.crop),
      _item(
        AcaoDaCamada.estabilizar,
        'Estabilizar',
        CupertinoIcons.camera_viewfinder,
      ),
    ],
    if (camada is Scene3DLayer)
      _item(AcaoDaCamada.cameras, 'Câmeras', CupertinoIcons.videocam),
    if (camada is TextLayer)
      _item(
        AcaoDaCamada.caminho,
        'Texto no caminho',
        CupertinoIcons.arrow_turn_up_right,
      ),
    _item(
      AcaoDaCamada.camada3d,
      'Camada 3D',
      CupertinoIcons.cube,
      marcado: camada.is3D,
    ),
    _item(
      AcaoDaCamada.motionBlur,
      'Motion blur',
      CupertinoIcons.wind,
      marcado: projeto.metaOf(id).motionBlur,
    ),
    if (extrudavel)
      _item(AcaoDaCamada.extrude, 'Extrude 3D', CupertinoIcons.cube_box),
    if (visual)
      _item(AcaoDaCamada.pulsar, 'Pulsar na batida', CupertinoIcons.waveform),
    _item(AcaoDaCamada.loop, 'Loop de keyframes', CupertinoIcons.repeat),
    _item(AcaoDaCamada.organizar, 'Organizar', CupertinoIcons.folder),
    if (midia) ...[
      _item(
        AcaoDaCamada.infoDaMidia,
        'Informações da mídia',
        CupertinoIcons.info_circle,
      ),
      if (video)
        _item(
          AcaoDaCamada.extrairAudio,
          'Extrair o áudio',
          CupertinoIcons.music_note_2,
        ),
    ],
    if (visual) ...[
      if (!recortada)
        _item(
          AcaoDaCamada.recortar,
          'Recortar pela camada de baixo',
          CupertinoIcons.arrow_turn_left_down,
          habilitado: temBaseDeRecorte,
        )
      else
        _item(
          AcaoDaCamada.soltarRecorte,
          'Soltar o recorte',
          CupertinoIcons.arrow_turn_up_right,
        ),
      _item(
        AcaoDaCamada.caber,
        'Caber na composição',
        CupertinoIcons.rectangle_arrow_up_right_arrow_down_left,
      ),
      _item(
        AcaoDaCamada.preencher,
        'Preencher a composição',
        CupertinoIcons.fullscreen,
      ),
      _item(
        AcaoDaCamada.esticar,
        'Esticar até as bordas',
        CupertinoIcons.arrow_up_left_arrow_down_right,
      ),
      _item(
        AcaoDaCamada.espelharH,
        'Espelhar na horizontal',
        CupertinoIcons.arrow_left_right_square,
      ),
      _item(
        AcaoDaCamada.espelharV,
        'Espelhar na vertical',
        CupertinoIcons.arrow_up_down_square,
      ),
    ],
    if (grupo != null) ...[
      _item(
        AcaoDaCamada.grupoMascara,
        'Grupo de máscara',
        CupertinoIcons.square_stack_3d_down_right_fill,
        habilitado: grupo.children.length >= 2,
        marcado:
            grupo.children.isNotEmpty &&
            grupo.children.first.blendMode == BlendMode.dstIn,
      ),
      _item(
        AcaoDaCamada.grupoRecorte,
        'Grupo de recorte',
        CupertinoIcons.square_stack_3d_down_right,
        habilitado: grupo.children.length >= 2,
        marcado:
            grupo.children.isNotEmpty &&
            grupo.children.first.blendMode == BlendMode.dstOut,
      ),
    ],
    _item(
      AcaoDaCamada.excluirEFechar,
      'Apagar e fechar o buraco',
      CupertinoIcons.delete_left,
      destrutivo: true,
    ),
    _item(
      AcaoDaCamada.fecharBuracos,
      'Fechar buracos da timeline',
      CupertinoIcons.arrow_left_right,
    ),
  ];
}

/// ABRE O MENU DA CAMADA [layerId] — o toque longo no clipe da timeline e
/// o "Mais" da barra chamam isto.
///
/// [posicao] e o ponto GLOBAL do dedo (o menu abre junto dele); sem ele, o
/// menu abre acima da barra, no canto direito. O relogio vem da casca
/// ([EscopoDoEditor]) quando [context] esta dentro dela; quem chama de
/// fora (a propria casca) passa o [playback].
///
/// O toque longo num clipe que nao era o escolhido passa a escolher ele:
/// o menu e daquela camada, e a barra embaixo tem de concordar.
Future<void> mostrarMenuDaCamada(
  BuildContext context,
  WidgetRef ref,
  String layerId, {
  Offset? posicao,
  PlaybackController? playback,
}) async {
  final pb =
      playback ??
      context.getInheritedWidgetOfExactType<EscopoDoEditor>()?.playback;
  pb?.pause();
  final projeto = ref.read(editorControllerProvider);
  final camada = projeto.layerById(layerId);
  if (camada == null) return;
  if (!ref.read(modoSelecionarProvider) &&
      ref.read(selectedLayerProvider) != layerId) {
    ref.read(multiSelectProvider.notifier).state = const {};
    ref.read(selectedLayerProvider.notifier).state = layerId;
  }
  final c = ref.read(editorControllerProvider.notifier);
  final t = pb?.time.value ?? Duration.zero;
  final ancora = posicao == null
      ? ancoraPadraoDoMenu(context)
      : Rect.fromLTWH(posicao.dx, posicao.dy, 0, 0);
  final acao = await mostrarAureaMenu<String>(
    context,
    ancora: ancora,
    itens: itensDoMenuDaCamada(
      projeto,
      camada,
      t,
      temCamadaCopiada: c.temCamadaCopiada,
      temKeyframesCopiados: KeyframeClipboard.temAlgo,
      podeColarEstilo: c.categoriasColaveis(layerId).isNotEmpty,
    ),
  );
  if (acao == null || !context.mounted) return;
  await executarAcaoDaCamada(
    context,
    ref,
    layerId,
    acao,
    playback: pb,
    ancora: ancora,
  );
}

/// EXECUTA a acao [acao] ([AcaoDaCamada]) na camada [layerId].
Future<void> executarAcaoDaCamada(
  BuildContext context,
  WidgetRef ref,
  String layerId,
  String acao, {
  PlaybackController? playback,
  Rect? ancora,
}) async {
  final c = ref.read(editorControllerProvider.notifier);
  final projeto = ref.read(editorControllerProvider);
  final camada = projeto.layerById(layerId);
  if (camada == null) return;
  final id = layerId;
  final t = playback?.time.value ?? Duration.zero;
  final onde = ancora ?? ancoraPadraoDoMenu(context);
  void umPasso(void Function() fn) => c.runAsOneUndo(fn);
  void aviso(String texto) =>
      AureaSnack.show(context, translate(context, texto));

  switch (acao) {
    case AcaoDaCamada.duplicar:
      HapticFeedback.lightImpact();
      umPasso(() => c.duplicarCamada(id));
    case AcaoDaCamada.dividir:
      if (!camada.activeAt(t)) {
        showReasonToast(context, 'Leve o cabeçote para dentro da camada');
        return;
      }
      HapticFeedback.lightImpact();
      umPasso(() => c.splitLayer(id, t));
    case AcaoDaCamada.apagar:
      // A travada nao sai: excluirCamadas avisa por que, e sem mutacao nao
      // pode haver passo de desfazer vazio.
      if (c.isLocked(id)) {
        excluirCamadas(context, ref, {id});
      } else {
        umPasso(() => excluirCamadas(context, ref, {id}));
      }
    case AcaoDaCamada.renomear:
      await renomearCamada(context, ref, camada);
    case AcaoDaCamada.ocultar:
      umPasso(() => c.toggleHidden(id));
    case AcaoDaCamada.travar:
      umPasso(() => c.toggleLocked(id));
    case AcaoDaCamada.solo:
      umPasso(() => c.toggleSolo(id));
    case AcaoDaCamada.selecionar:
      ligarModoSelecionar(ref, comCamada: id);
    case AcaoDaCamada.subir:
      umPasso(() => c.reorderLayer(id, -1));
    case AcaoDaCamada.descer:
      umPasso(() => c.reorderLayer(id, 1));
    case AcaoDaCamada.copiar:
      c.copiarCamada(id);
      aviso('Camada copiada');
    case AcaoDaCamada.colar:
      umPasso(() => c.colarCamada(t, acimaDe: id));
    case AcaoDaCamada.copiarKeyframes:
      // AS ESCOLHIDAS DESTA CAMADA; sem escolha, todas as da camada.
      final escolhidas = ref
          .read(keyframesSelecionadosProvider)
          .where((m) => m.layerId == id)
          .toSet();
      final marcas = escolhidas.isNotEmpty
          ? escolhidas
          : <MarcaSelecionada>{
              for (final prop in LayerProp.values)
                for (final tempo in c.propKeyframeTimes(camada, prop))
                  (layerId: id, prop: prop, tempo: tempo),
            };
      final n = c.copiarKeyframes(marcas, layerId: id);
      aviso(n == 0 ? 'Nenhum keyframe para copiar' : 'Keyframes copiados');
    case AcaoDaCamada.colarKeyframes:
      final r = c.colarKeyframes(id, t);
      if (r.coladas == 0) {
        aviso('Nada colado: os keyframes cairiam fora da camada');
      } else if (r.foraDaCamada > 0) {
        aviso('Parte dos keyframes caiu fora da camada');
      }
    case AcaoDaCamada.copiarEstilo:
      c.copiarEstilo(id);
      aviso('Estilo copiado');
    case AcaoDaCamada.colarEstilo:
      await mostrarColarEstilo(context, ref, id);
    case AcaoDaCamada.agrupar:
      umPasso(() => c.groupLayer(id));
    case AcaoDaCamada.entrarNoGrupo:
      c.enterGroup(id);
    case AcaoDaCamada.desagrupar:
      var avisos = const <String>[];
      umPasso(() => avisos = c.ungroupLayer(id));
      if (avisos.isNotEmpty && context.mounted) {
        AureaSnack.show(context, avisos.join('\n'));
      }
    case AcaoDaCamada.tempoDoGrupo:
      // O TEMPO DO GRUPO (duracao interna, igualar a barra, remapear,
      // colapsar, recortar) mora no painel Grupo: o menu abre o painel.
      context.getInheritedWidgetOfExactType<EscopoDoEditor>()?.abrirPainel(
        PainelId.grupo,
      );
    case AcaoDaCamada.vincular:
      final pai = await escolherPai(context, ref, {id}, ancora: onde);
      if (pai == null) return;
      vincularAoPai(ref, {id}, pai.paiId, t);
    case AcaoDaCamada.alinhar:
      await showAlignSheet(context, ref, [id], t);
    case AcaoDaCamada.etiqueta:
      await _escolherEtiqueta(context, ref, id, onde);
    case AcaoDaCamada.maisAcoes:
      final mais = await mostrarAureaMenu<String>(
        context,
        ancora: onde,
        titulo: 'Mais ações',
        itens: itensDeMaisAcoes(
          projeto,
          camada,
          t,
          temEfeitosCopiados: c.temEfeitosCopiados,
          mudo: c.audioSpecOf(id)?.muted ?? false,
          temBaseDeRecorte: c.baseDeRecorteAbaixo(id) != null,
        ),
      );
      if (mais == null || !context.mounted) return;
      await executarAcaoDaCamada(
        context,
        ref,
        id,
        mais,
        playback: playback,
        ancora: onde,
      );

    // ------------------------------------------------ "Mais ações…"
    case AcaoDaCamada.decupar:
      await showDecuparSheet(context, ref, id);
    case AcaoDaCamada.apararInicio:
      if (camada.activeAt(t)) umPasso(() => c.trimLayerStart(id, t));
    case AcaoDaCamada.apararFim:
      if (camada.activeAt(t)) umPasso(() => c.trimLayerEnd(id, t));
    case AcaoDaCamada.moverInicio:
      umPasso(() => c.moveLayer(id, t));
    case AcaoDaCamada.moverFim:
      final inicio = t - camada.duration;
      umPasso(
        () => c.moveLayer(id, inicio < Duration.zero ? Duration.zero : inicio),
      );
    case AcaoDaCamada.congelar:
      await showFreezeSheet(context, ref, id, t);
    case AcaoDaCamada.copiarEfeitos:
      final n = c.copyEffects(id);
      aviso(n == 0 ? 'Esta camada não tem efeitos' : 'Efeitos copiados');
    case AcaoDaCamada.colarEfeitos:
      var n = 0;
      umPasso(() => n = c.pasteEffects(id));
      aviso(
        n == 0
            ? 'Copie os efeitos de outra camada primeiro'
            : 'Efeitos colados',
      );
    case AcaoDaCamada.mudo:
      final mudo = c.audioSpecOf(id)?.muted ?? false;
      umPasso(() => c.updateAudioSpec(id, (s) => s.copyWith(muted: !mudo)));
    case AcaoDaCamada.batidas:
      await showBeatsSheet(context, ref, id);
    case AcaoDaCamada.inserir:
      umPasso(() => c.insertLayerAt(camada.duplicated(), t));
    case AcaoDaCamada.sobrescrever:
      umPasso(() => c.overwriteLayerAt(camada.duplicated(), t));
    case AcaoDaCamada.levantar || AcaoDaCamada.extrair:
      final trecho = ref.read(editorSessionProvider).inOut;
      if (trecho == null) {
        showReasonToast(context, 'Marque Entrada (I) e Saída (O) na régua');
        return;
      }
      umPasso(
        () => acao == AcaoDaCamada.levantar
            ? c.liftTimeRange(trecho.$1, trecho.$2, only: {id})
            : c.extractTimeRange(trecho.$1, trecho.$2, only: {id}),
      );
    case AcaoDaCamada.legendar:
      await showCaptionCreationSheet(context, ref);
    case AcaoDaCamada.reenquadrar:
      aviso('Achando o assunto...');
      final n = await c.autoReframeLayer(id);
      if (!context.mounted) return;
      aviso((n ?? 0) == 0 ? 'Não achei um assunto claro' : 'Reenquadrado');
    case AcaoDaCamada.estabilizar:
      aviso('Lendo o vídeo para estabilizar...');
      final n = await c.stabilizeLayer(id);
      if (!context.mounted) return;
      aviso(n == null ? 'Não foi possível estabilizar' : 'Estabilizado');
    case AcaoDaCamada.cameras:
      if (playback != null) {
        await showCamerasSheet(context, ref, id, playback);
      }
    case AcaoDaCamada.caminho:
      await showTextPathSheet(context, ref, id);
    case AcaoDaCamada.camada3d:
      umPasso(() => c.toggle3D(id));
    case AcaoDaCamada.motionBlur:
      umPasso(() => c.toggleLayerMotionBlurReal(id));
    case AcaoDaCamada.extrude:
      await showExtrudeSheet(context, ref, id);
    case AcaoDaCamada.pulsar:
      await showBeatPulseSheet(context, ref, id);
    case AcaoDaCamada.loop:
      await showLoopSheet(context, ref, id);
    case AcaoDaCamada.organizar:
      await showOrganizeSheet(context, ref, id);
    case AcaoDaCamada.infoDaMidia:
      final caminho = switch (camada) {
        ImageLayer l => l.sourcePath,
        VideoLayer l => l.sourcePath,
        AudioLayer l => l.sourcePath,
        _ => null,
      };
      if (caminho != null) {
        await mostrarInfoDaMidia(context, caminho, camada.name);
      }
    case AcaoDaCamada.extrairAudio:
      if (camada is VideoLayer)
        await extrairAudioComAviso(context, ref, camada);
    case AcaoDaCamada.recortar:
      umPasso(() => c.recortarPelaDeBaixo(id));
    case AcaoDaCamada.soltarRecorte:
      umPasso(() => c.setMatte(id, MatteMode.none, null));
    case AcaoDaCamada.caber:
      umPasso(() => c.encaixarNaComposicao(id, EncaixeNaComposicao.caber, t));
    case AcaoDaCamada.preencher:
      umPasso(
        () => c.encaixarNaComposicao(id, EncaixeNaComposicao.preencher, t),
      );
    case AcaoDaCamada.esticar:
      umPasso(() => c.encaixarNaComposicao(id, EncaixeNaComposicao.esticar, t));
    case AcaoDaCamada.espelharH:
      umPasso(() => c.espelharCamada(id, horizontal: true));
    case AcaoDaCamada.espelharV:
      umPasso(() => c.espelharCamada(id, horizontal: false));
    case AcaoDaCamada.grupoMascara || AcaoDaCamada.grupoRecorte:
      if (camada is! GroupLayer || camada.children.isEmpty) return;
      final modo = acao == AcaoDaCamada.grupoMascara
          ? BlendMode.dstIn
          : BlendMode.dstOut;
      final ligado = camada.children.first.blendMode == modo;
      umPasso(() => c.definirFormaDoGrupo(id, ligado ? null : modo));
    case AcaoDaCamada.excluirEFechar:
      if (c.isLocked(id)) {
        excluirCamadas(context, ref, {id});
      } else {
        umPasso(() => c.rippleDeleteLayer(id));
      }
    case AcaoDaCamada.fecharBuracos:
      if (c.gapCount() == 0) {
        showReasonToast(context, 'Não há buraco para fechar');
        return;
      }
      umPasso(c.closeTimelineGaps);
  }
}

/// A ETIQUETA DE COR: "Sem etiqueta" e as cores da paleta; a atual marcada.
Future<void> _escolherEtiqueta(
  BuildContext context,
  WidgetRef ref,
  String id,
  Rect ancora,
) async {
  final atual = ref.read(editorControllerProvider).metaOf(id).label;
  const sem = -1;
  final escolha = await mostrarAureaMenu<int>(
    context,
    ancora: ancora,
    titulo: 'Etiqueta',
    itens: [
      AureaMenuItem(
        valor: sem,
        rotulo: 'Sem etiqueta',
        icone: CupertinoIcons.nosign,
        marcado: atual == null,
        chave: 'etiqueta-sem',
      ),
      for (var i = 0; i < LayerLabel.palette.length; i++)
        AureaMenuItem(
          valor: i,
          rotulo: LayerLabel.palette[i].name,
          icone: CupertinoIcons.circle_fill,
          marcado:
              atual?.color.toARGB32() == LayerLabel.palette[i].color.toARGB32(),
          chave: 'etiqueta-$i',
        ),
    ],
  );
  if (escolha == null) return;
  final c = ref.read(editorControllerProvider.notifier);
  c.runAsOneUndo(
    () => c.setLayerLabel(
      id,
      escolha == sem ? null : LayerLabel.palette[escolha],
    ),
  );
}
