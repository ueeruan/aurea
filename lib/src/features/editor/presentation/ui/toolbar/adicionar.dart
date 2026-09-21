import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart' show MaterialPageRoute;
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../help/presentation/quick_guide_screen.dart';
import '../../../application/editor_controller.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/layer.dart';
import '../../am/beats_sheet.dart' show showBeatsSheet;
import '../../am/layer_menu.dart' show showReasonToast;
import '../../context/add_toolbar.dart' show AddTarget;
import '../../shell/layer_actions.dart' show agruparSelecao, menuDasMarcas;
import '../../sketchfab/sketchfab_screen.dart' show abrirTelaDoSketchfab;
import '../../widgets/add_layer_sheet.dart'
    show AddLayerPanel, AddTab, showCaptionCreationSheet;
import '../shell/contrato.dart';

// ===========================================================================
// ADICIONAR — ESQUELETO DA FUNDACAO
// ===========================================================================
//
// A folha do "+": uma grade de blocos que chama as acoes de adicionar que
// JA EXISTEM no controlador (texto, camera, nulo, grupo, ajuste,
// particulas) e, para o que tem seletor proprio (midia da galeria, formas,
// audio, 3D do aparelho, Texto 3D, SVG, desenho), o menu de adicionar
// completo que ja existe, aberto na aba certa. A frente da toolbar
// reescreve esta folha; nenhuma porta de adicionar pode sumir.

/// ABRE A FOLHA DE ADICIONAR no cabecote atual.
Future<void> abrirAdicionar(
  BuildContext context,
  WidgetRef ref, {
  required PlaybackController playback,
  required void Function(PainelId id) abrirPainel,
}) {
  playback.pause();
  return mostrarAureaFolha<void>(
    context,
    titulo: 'Adicionar',
    grande: true,
    construtor: (folha) => _FolhaDeAdicionar(
      playback: playback,
      abrirPainel: abrirPainel,
      contextoDoEditor: context,
      ref: ref,
    ),
  );
}

class _FolhaDeAdicionar extends StatelessWidget {
  const _FolhaDeAdicionar({
    required this.playback,
    required this.abrirPainel,
    required this.contextoDoEditor,
    required this.ref,
  });

  /// O `ref` do EDITOR, e nao um da folha: o que a folha dispara (agrupar,
  /// legendas) continua rodando DEPOIS de ela fechar, e um `ref` de widget
  /// desmontado estoura na primeira leitura.
  final WidgetRef ref;

  final PlaybackController playback;
  final void Function(PainelId id) abrirPainel;

  /// O contexto do EDITOR (e nao o da folha): o que abre depois de a folha
  /// fechar precisa de um contexto que continua vivo.
  final BuildContext contextoDoEditor;

  @override
  Widget build(BuildContext context) {
    final c = ref.read(editorControllerProvider.notifier);
    Duration agora() => playback.time.value;
    void fechar() => Navigator.of(context).maybePop();

    /// Fecha a folha e abre o menu completo na aba [aba].
    void completo(AddTab aba) {
      fechar();
      abrirMenuCompletoDeAdicionar(
        contextoDoEditor,
        ref,
        playback: playback,
        abrirPainel: abrirPainel,
        aba: aba,
      );
    }

    final blocos = <(String, IconData, String, VoidCallback)>[
      ('midia', CupertinoIcons.photo_on_rectangle, 'Mídia', () {
        completo(AddTab.midia);
      }),
      ('texto', CupertinoIcons.textformat, 'Texto', () {
        c.addTextLayer(agora());
        fechar();
      }),
      ('forma', CupertinoIcons.square_on_circle, 'Forma', () {
        completo(AddTab.forma);
      }),
      ('audio', CupertinoIcons.music_note_2, 'Áudio', () {
        completo(AddTab.audio);
      }),
      ('3d', CupertinoIcons.cube, '3D e objetos', () {
        completo(AddTab.objeto);
      }),
      ('sketchfab', CupertinoIcons.cloud_download, 'Sketchfab', () async {
        fechar();
        await abrirTelaDoSketchfab(contextoDoEditor, playhead: agora());
      }),
      ('camera', CupertinoIcons.videocam, 'Câmera', () {
        c.addCameraLayer(agora());
        fechar();
      }),
      ('nulo', CupertinoIcons.smallcircle_circle, 'Nulo', () {
        c.addNullLayer(agora());
        fechar();
      }),
      ('grupo', CupertinoIcons.folder, 'Grupo', () {
        fechar();
        agruparPorEscolha(contextoDoEditor, ref);
      }),
      ('ajuste', CupertinoIcons.slider_horizontal_3, 'Ajuste', () {
        // Um efeito sobre tudo: camada de ajuste, ja com Efeitos aberto.
        c.addAdjustmentLayer(agora());
        fechar();
        abrirPainel(PainelId.efeitos);
      }),
      ('particulas', CupertinoIcons.sparkles, 'Partículas', () {
        c.addParticulasLayer(agora());
        fechar();
      }),
      ('legendas', CupertinoIcons.captions_bubble, 'Legendas', () async {
        fechar();
        await showCaptionCreationSheet(contextoDoEditor, ref);
      }),
      ('mais', CupertinoIcons.ellipsis, 'Todos', () {
        completo(AddTab.forma);
      }),
    ];
    return Padding(
      padding: const EdgeInsets.fromLTRB(
        AureaDims.margemDoPainel,
        0,
        AureaDims.margemDoPainel,
        AureaDims.e15,
      ),
      child: LayoutBuilder(
        builder: (context, c) {
          const colunas = 4;
          final largura =
              (c.maxWidth - AureaDims.vaoDoPainel * (colunas - 1)) / colunas;
          return Wrap(
            spacing: AureaDims.vaoDoPainel,
            runSpacing: AureaDims.vaoDoPainel,
            children: [
              for (final (id, icone, rotulo, acao) in blocos)
                AureaToolbarButton(
                  key: ValueKey('adicionar-$id'),
                  icone: icone,
                  rotulo: rotulo,
                  bloco: true,
                  largura: largura,
                  aoTocar: acao,
                ),
            ],
          );
        },
      ),
    );
  }
}

/// O MENU DE ADICIONAR COMPLETO que ja existe (galeria, formas, sons
/// recentes, 3D, Texto 3D, SVG, desenho), numa folha da UI nova.
Future<void> abrirMenuCompletoDeAdicionar(
  BuildContext context,
  WidgetRef ref, {
  required PlaybackController playback,
  required void Function(PainelId id) abrirPainel,
  AddTab aba = AddTab.forma,
}) => mostrarAureaFolha<void>(
  context,
  titulo: 'Adicionar',
  altura: 340,
  grande: true,
  construtor: (folha) => AddLayerPanel(
    playhead: playback.time.value,
    initialTab: aba,
    onClose: () => Navigator.of(folha).maybePop(),
    onProjectAction: (alvo) {
      Navigator.of(folha).maybePop();
      executarAlvoDeAdicionar(
        context,
        ref,
        alvo,
        playback: playback,
        abrirPainel: abrirPainel,
      );
    },
  ),
);

/// O QUE O MENU DE ADICIONAR PEDE PARA O PROJETO (legendas, marcas,
/// batidas, grupo, ajuste...). Veio intacto do `EditorScreen` antigo
/// (`_onAddTarget`).
Future<void> executarAlvoDeAdicionar(
  BuildContext context,
  WidgetRef ref,
  AddTarget alvo, {
  required PlaybackController playback,
  required void Function(PainelId id) abrirPainel,
}) async {
  final controller = ref.read(editorControllerProvider.notifier);
  final t = playback.time.value;
  Future<void> aba(AddTab a) => abrirMenuCompletoDeAdicionar(
    context,
    ref,
    playback: playback,
    abrirPainel: abrirPainel,
    aba: a,
  );
  switch (alvo) {
    case AddTarget.midia:
      await aba(AddTab.midia);
    case AddTarget.audio:
      await aba(AddTab.audio);
    case AddTarget.forma:
      await aba(AddTab.forma);
    case AddTarget.objeto:
    case AddTarget.icone:
      await aba(AddTab.objeto);
    case AddTarget.ajuda:
      playback.pause();
      await Navigator.of(context).push(
        MaterialPageRoute<void>(
          builder: (_) => const QuickGuideScreen(initialQuery: ''),
        ),
      );
    case AddTarget.texto:
      playback.pause();
      controller.addTextLayer(t);
    case AddTarget.efeito:
      playback.pause();
      controller.addAdjustmentLayer(t);
      abrirPainel(PainelId.efeitos);
    case AddTarget.grupo:
      await agruparPorEscolha(context, ref);
    case AddTarget.legendas:
      playback.pause();
      await showCaptionCreationSheet(context, ref);
    case AddTarget.marcas:
      await menuDasMarcas(context, ref, playback);
    case AddTarget.batidas:
      final som = ref
          .read(editorControllerProvider)
          .layers
          .where((l) => l is AudioLayer || l is VideoLayer)
          .firstOrNull;
      if (som == null) {
        showReasonToast(context, 'Adicione um audio ou um video primeiro');
        return;
      }
      playback.pause();
      await showBeatsSheet(context, ref, som.id);
    case AddTarget.autoEdit:
      // Fora do app por enquanto: sem entrada na interface.
      break;
  }
}

/// GRUPO SEM SELECAO: escolher as camadas numa lista. Veio do `EditorScreen`
/// antigo (`_agruparPorEscolha`), agora com as pecas do design system.
Future<void> agruparPorEscolha(BuildContext context, WidgetRef ref) async {
  final layers = ref.read(editorControllerProvider).layers;
  if (layers.length < 2) {
    showReasonToast(context, 'Um grupo precisa de duas ou mais camadas');
    return;
  }
  final escolhidas = <String>{};
  final ok = await mostrarAureaFolha<bool>(
    context,
    titulo: 'Agrupar quais camadas?',
    construtor: (folha) => StatefulBuilder(
      builder: (folha, setFolha) => Column(
        mainAxisSize: MainAxisSize.min,
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Flexible(
            child: ListView(
              shrinkWrap: true,
              padding: const EdgeInsets.symmetric(
                horizontal: AureaDims.margemDoPainel,
              ),
              children: [
                for (final l in layers)
                  AureaLayerRow(
                    key: ValueKey('agrupar-${l.id}'),
                    nome: l.name,
                    icone: escolhidas.contains(l.id)
                        ? CupertinoIcons.checkmark_circle_fill
                        : CupertinoIcons.circle,
                    selecionada: escolhidas.contains(l.id),
                    aoTocar: () => setFolha(() {
                      if (!escolhidas.remove(l.id)) escolhidas.add(l.id);
                    }),
                  ),
              ],
            ),
          ),
          Padding(
            padding: const EdgeInsets.all(AureaDims.e15),
            child: CupertinoButton.filled(
              key: const ValueKey('agrupar-confirmar'),
              onPressed: escolhidas.length >= 2
                  ? () => Navigator.of(folha).pop(true)
                  : null,
              child: AppText(
                'Agrupar',
                style: TextStyle(color: AureaCores.sobreAcao),
              ),
            ),
          ),
        ],
      ),
    ),
  );
  if (ok == true && escolhidas.length >= 2) {
    agruparSelecao(ref, escolhidas);
  }
}
