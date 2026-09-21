import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/pedir_nome.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../export/presentation/export_video_screen.dart';
import '../../../../help/presentation/quick_guide_screen.dart';
import '../../../../projects/application/thumbnail_service.dart';
import '../../../application/editor_controller.dart';
import '../../../application/freehand_session.dart';
import '../../../application/perfil3d.dart';
import '../../../application/playback_controller.dart';
import '../../../application/preview_stats.dart';
import '../../../application/ui/editor_session.dart';
import '../../../application/video_layer_manager.dart';
import '../../../domain/layer.dart';
import '../toolbar/menu_do_projeto.dart' show mostrarMenuDoProjeto;
import 'ajustes_do_projeto.dart' show showProjectSettingsSheet;
import '../../widgets/preview_stage.dart';
import '../paineis/pontos.dart' show fecharEditarPontos;
import '../timeline/timeline.dart';
import '../toolbar/adicionar.dart';
import '../toolbar/barra_contextual.dart';
import '../toolbar/barra_do_lote.dart';
import '../toolbar/barra_do_projeto.dart';
import '../toolbar/menu_da_camada.dart';
import 'barra_de_transporte.dart';
import 'barra_do_topo.dart';
import 'contrato.dart';
import 'sobreposicoes_da_previa.dart';

/// A CASCA DO EDITOR NOVO — as zonas, de cima para baixo:
///
///   barra do topo   42   voltar · nome · ⋯ · projeto · exportar
///   PREVIA          resto   o `PreviewStage` de sempre (a UNICA previa)
///   transporte      46   desfazer/refazer · quadros e play · tempo · modo
///   timeline       280   regua + linhas; na base, a barra da camada (57)
///                        e o "+" (73, a 6 da borda); o painel aberto (200)
///                        SOBE por cima da parte de baixo, em 100 ms
///
/// A casca e VISUAL. O que e ciclo de vida (relogio, gerente de video,
/// salvar, pausa em segundo plano) mora em `EditorScreen`, que a monta com
/// os dois objetos que so ele cria.
///
/// REGRA DE RECONSTRUCAO: a raiz observa a SELECAO (id), o painel aberto e
/// a tela cheia — nunca a camada viva nem o projeto. Um passo de slider
/// nao passa por aqui (so a barra da camada e o painel acompanham).
class EditorShell extends ConsumerStatefulWidget {
  const EditorShell({super.key, required this.playback, required this.videos});

  final PlaybackController playback;
  final VideoLayerManager videos;

  @override
  ConsumerState<EditorShell> createState() => _EditorShellState();
}

class _EditorShellState extends ConsumerState<EditorShell> {
  PlaybackController get _pb => widget.playback;
  EditorController get _c => ref.read(editorControllerProvider.notifier);

  // ------------------------------------------------------------ paineis

  void _abrirPainel(PainelId id) {
    _pb.pause();
    ref.read(painelAbertoProvider.notifier).state = id;
  }

  void _fecharPainel() {
    ref.read(painelAbertoProvider.notifier).state = null;
  }

  void _tocarFerramenta(Ferramenta f) {
    final alvo = f.abre;
    if (alvo != null) {
      HapticFeedback.selectionClick();
      // A MESMA ferramenta de novo fecha: e o jeito rapido de ver a
      // timeline inteira sem procurar o ✓.
      if (ref.read(painelAbertoProvider) == alvo) {
        _fecharPainel();
      } else {
        _abrirPainel(alvo);
      }
      return;
    }
    f.acao?.call();
  }

  /// As acoes diretas da barra (as que nao abrem painel).
  void _acionar(String acao, String layerId) {
    switch (acao) {
      case AcaoDaFerramenta.mais:
        mostrarMenuDaCamada(context, ref, layerId, playback: _pb);
      case AcaoDaFerramenta.dividir:
        _pb.pause();
        HapticFeedback.lightImpact();
        _c.splitLayer(layerId, _pb.time.value);
      case AcaoDaFerramenta.ativar3d:
        _ativarTexto3D(layerId);
    }
  }

  /// As acoes da selecao multipla (a barra do lote).
  void _acionarLote(String acao) =>
      acionarLote(context, ref, acao, playback: _pb);

  /// As acoes do projeto (a barra sem camada escolhida).
  void _acionarProjeto(String acao) => acionarProjeto(
    context,
    ref,
    acao,
    playback: _pb,
    abrirPainel: _abrirPainel,
  );

  /// TEXTO -> TEXTO 3D: o texto vira uma cena com a palavra em volume, a
  /// cena passa a ser a escolhida e o PAINEL Texto 3D abre nela (a mesma
  /// porta da barra da camada — nao ha folha propria do Texto 3D).
  Future<void> _ativarTexto3D(String layerId) async {
    _pb.pause();
    final noId = await _c.ativarTexto3D(layerId);
    if (!mounted) return;
    if (noId == null) {
      showReasonToast(
        context,
        _c.ultimoMotivoDoTexto3D ??
            'Nao foi possivel transformar este texto em 3D.',
      );
      return;
    }
    final cena = ref
        .read(editorControllerProvider)
        .layers
        .whereType<Scene3DLayer>()
        .where((c) => c.scene.nodeById(noId) != null)
        .firstOrNull;
    if (cena == null) return;
    ref.read(selectedLayerProvider.notifier).state = cena.id;
    _abrirPainel(PainelId.texto3d);
  }

  /// O PAINEL NA SESSAO: o palco desenha as alcas da forma viva e o
  /// editor de nos pela SESSAO antiga (`editorSessionProvider`). Enquanto o
  /// palco nao for reescrito, a casca espelha o painel aberto la.
  void _espelharNaSessao(PainelId? antes, PainelId? agora) {
    final sessao = ref.read(editorSessionProvider.notifier);
    if (antes == PainelId.pontos && agora != PainelId.pontos) {
      fecharEditarPontos(ref);
    }
    switch (agora) {
      case PainelId.forma:
        sessao.openShape(ShapeTool.size);
      case PainelId.pontos:
        // Quem abre o de pontos ja escreve a sessao (com o item certo).
        break;
      default:
        final p = ref.read(editorSessionProvider).panel;
        if (p == EditorPanel.editShape || p == EditorPanel.editPoints) {
          sessao.closePanel();
        }
    }
  }

  // ----------------------------------------------------------- barra do topo

  Future<void> _renomear() async {
    final atual = ref.read(editorControllerProvider).name;
    final novo = await pedirNome(
      context,
      titulo: 'Nome do projeto',
      atual: atual,
    );
    if (novo == null || novo.trim().isEmpty) return;
    _c.renameProject(novo.trim());
  }

  void _exportar() {
    _pb.pause();
    // EXPORTAR DE DENTRO DE UM GRUPO exportava so o grupo: o palco da
    // exportacao le o estado do editor. Sai de todos antes.
    _c.exitAllGroups();
    Navigator.of(context).push(
      MaterialPageRoute<void>(
        fullscreenDialog: true,
        builder: (_) => const ExportVideoScreen(),
      ),
    );
  }

  void _menuDoProjeto() {
    mostrarMenuDoProjeto(
      context,
      ref,
      _pb,
      onDefinirMiniatura: _usarQuadroComoMiniatura,
      onAgrupar: () => agruparPorEscolha(context, ref),
      onGuia: () {
        _pb.pause();
        Navigator.of(context).push(
          MaterialPageRoute<void>(
            builder: (_) => const QuickGuideScreen(initialQuery: ''),
          ),
        );
      },
    );
  }

  /// USAR ESTE QUADRO COMO MINIATURA: sem selecao (a borda da camada
  /// escolhida entraria na foto), o palco e fotografado no quadro seguinte
  /// e o instante fica gravado no projeto.
  Future<void> _usarQuadroComoMiniatura() async {
    final id = ref.read(editorControllerProvider).id;
    _c.definirQuadroDaMiniatura(_pb.time.value);
    ref.read(multiSelectProvider.notifier).state = const {};
    ref.read(selectedLayerProvider.notifier).state = null;
    await WidgetsBinding.instance.endOfFrame;
    if (!mounted) return;
    await ThumbnailService.instance.capture(previewStageKey, id);
    if (!mounted) return;
    AureaSnack.show(context, 'Este quadro virou a miniatura do projeto');
  }

  void _alternarTelaCheia() {
    final s = ref.read(editorSessionProvider.notifier);
    s.setPreviewExpanded(!ref.read(editorSessionProvider).previewExpanded);
  }

  // ------------------------------------------------------------- voltar

  /// FECHA A COISA MAIS INTERNA que estiver aberta; sem nada aberto, sai do
  /// editor (com a miniatura do projeto capturada antes).
  void _voltar() {
    if (MediaQuery.viewInsetsOf(context).bottom > 0) {
      FocusManager.instance.primaryFocus?.unfocus();
      return;
    }
    if (ref.read(freehandRequestProvider)) {
      ref.read(freehandRequestProvider.notifier).state = false;
      return;
    }
    // Folhas persistentes tem uma entrada local de historico: consumi-la
    // primeiro nao remove o editor nem muda a selecao.
    if (ModalRoute.of(context)?.willHandlePopInternally ?? false) {
      Navigator.of(context).pop();
      return;
    }
    if (ref.read(editorSessionProvider).previewExpanded) {
      ref.read(editorSessionProvider.notifier).setPreviewExpanded(false);
      return;
    }
    if (ref.read(painelAbertoProvider) != null) {
      _fecharPainel();
      return;
    }
    if (ref.read(multiSelectProvider).isNotEmpty ||
        ref.read(modoSelecionarProvider)) {
      sairDoModoSelecionar(ref);
      return;
    }
    if (ref.read(selectedLayerProvider) != null) {
      ref.read(selectedLayerProvider.notifier).state = null;
      return;
    }
    // Dentro de um grupo, Voltar sai do grupo (um nivel).
    if (_c.dentroDeGrupo) {
      _c.exitGroup();
      return;
    }
    // A miniatura do projeto para a tela inicial: capturada AGORA, com o
    // palco ainda vivo; a escrita segue em segundo plano. Com um quadro
    // escolhido no menu do projeto, fica o escolhido.
    if (ref.read(editorControllerProvider).thumbTime == null) {
      ThumbnailService.instance.capture(
        previewStageKey,
        ref.read(editorControllerProvider).id,
      );
    }
    Navigator.of(context).maybePop();
  }

  void _soltarSelecao() {
    if (ref.read(painelAbertoProvider) != null) {
      _fecharPainel();
      return;
    }
    ref.read(multiSelectProvider.notifier).state = const {};
    ref.read(selectedLayerProvider.notifier).state = null;
  }

  // -------------------------------------------------------------- build

  @override
  Widget build(BuildContext context) {
    // A REGUA DAS RECONSTRUCOES (ver Perfil3D e o teste da casca): um passo
    // de slider nao pode aparecer aqui.
    Perfil3D.contar('build.casca');
    final selecionada = ref.watch(selectedLayerProvider);
    // O LOTE: varias marcadas, ou o modo Selecionar ligado (mesmo com uma
    // so — a barra do lote diz quantas e espera a proxima).
    final lote =
        ref.watch(multiSelectProvider).isNotEmpty ||
        ref.watch(modoSelecionarProvider);
    final painel = ref.watch(painelAbertoProvider);
    final telaCheia = ref.watch(
      editorSessionProvider.select((s) => s.previewExpanded),
    );
    final temContexto =
        painel != null ||
        telaCheia ||
        selecionada != null ||
        lote ||
        ref.watch(freehandRequestProvider);

    ref.listen<PainelId?>(painelAbertoProvider, (antes, agora) {
      if (antes != agora) _espelharNaSessao(antes, agora);
    });

    return EscopoDoEditor(
      playback: _pb,
      videos: widget.videos,
      abrirPainel: _abrirPainel,
      fecharPainel: _fecharPainel,
      child: PopScope(
        canPop: !temContexto && !_c.dentroDeGrupo,
        onPopInvokedWithResult: (didPop, _) {
          if (!didPop) _voltar();
        },
        child: Scaffold(
          resizeToAvoidBottomInset: ModalRoute.of(context)?.isCurrent ?? true,
          backgroundColor: AureaCores.cromo,
          body: SafeArea(
            child: LayoutBuilder(
              builder: (context, c) {
                final h = c.maxHeight;
                // A TIMELINE TEM 280, mas nunca deixa a previa com menos de
                // 160: numa tela baixa quem cede e a timeline.
                final alturaDaTimeline = telaCheia
                    ? 0.0
                    : math.max(
                        0.0,
                        math.min(
                          AureaDims.timeline,
                          h -
                              AureaDims.barraDoTopo -
                              AureaDims.transporte -
                              160,
                        ),
                      );
                return Column(
                  children: [
                    if (!telaCheia)
                      BarraDoTopo(
                        aoVoltar: _voltar,
                        aoRenomear: _renomear,
                        aoMenu: _menuDoProjeto,
                        aoConfigurar: () {
                          _pb.pause();
                          showProjectSettingsSheet(context, ref);
                        },
                        aoExportar: _exportar,
                      ),
                    Expanded(
                      child: _Previa(playback: _pb, videos: widget.videos),
                    ),
                    BarraDeTransporte(
                      playback: _pb,
                      telaCheia: telaCheia,
                      aoAlternarTelaCheia: _alternarTelaCheia,
                    ),
                    if (!telaCheia)
                      SizedBox(
                        key: const ValueKey('zona-timeline'),
                        height: alturaDaTimeline,
                        child: _AreaDaTimeline(
                          playback: _pb,
                          videos: widget.videos,
                          selecionada: selecionada,
                          lote: lote,
                          aoAcionarLote: _acionarLote,
                          aoAcionarProjeto: _acionarProjeto,
                          painel: painel,
                          altura: alturaDaTimeline,
                          aoTocarFerramenta: _tocarFerramenta,
                          aoAcionar: _acionar,
                          aoSoltar: _soltarSelecao,
                          aoAdicionar: () => abrirAdicionar(
                            context,
                            ref,
                            playback: _pb,
                            abrirPainel: _abrirPainel,
                          ),
                        ),
                      ),
                  ],
                );
              },
            ),
          ),
        ),
      ),
    );
  }
}

/// A PREVIA: o `PreviewStage` de sempre, montado como o editor antigo o
/// montava (dentro do `RepaintBoundary` com [previewStageKey] — e por ela
/// que a miniatura do projeto e o conta-gotas fotografam o palco).
class _Previa extends ConsumerWidget {
  const _Previa({required this.playback, required this.videos});

  final PlaybackController playback;
  final VideoLayerManager videos;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    return Stack(
      key: const ValueKey('zona-previa'),
      children: [
        Positioned.fill(
          child: RepaintBoundary(
            key: previewStageKey,
            child: PreviewStage(playback: playback, videos: videos),
          ),
        ),
        if (ref.watch(debugOverlayProvider))
          Positioned(
            top: 6,
            left: 8,
            child: IgnorePointer(
              child: DiagnosticoDaPrevia(playback: playback),
            ),
          ),
        // RASCUNHO ENQUANTO TOCA: a cena 3D e o brilho desenham
        // simplificados durante a reproducao.
        const Positioned(top: 6, right: 8, child: AvisoDeRascunho()),
      ],
    );
  }
}

/// A AREA DA TIMELINE: a timeline, a barra da camada na base, o "+" e o
/// painel que sobe por cima.
class _AreaDaTimeline extends StatelessWidget {
  const _AreaDaTimeline({
    required this.playback,
    required this.videos,
    required this.selecionada,
    required this.lote,
    required this.aoAcionarLote,
    required this.aoAcionarProjeto,
    required this.painel,
    required this.altura,
    required this.aoTocarFerramenta,
    required this.aoAcionar,
    required this.aoSoltar,
    required this.aoAdicionar,
  });

  final PlaybackController playback;
  final VideoLayerManager videos;
  final String? selecionada;

  /// Ha selecao multipla: a barra da base vira a do lote.
  final bool lote;
  final void Function(String idDaAcao) aoAcionarLote;

  /// Nada escolhido: a barra da base e a do projeto.
  final void Function(String idDaAcao) aoAcionarProjeto;
  final PainelId? painel;
  final double altura;
  final ValueChanged<Ferramenta> aoTocarFerramenta;
  final AoAcionarFerramenta aoAcionar;
  final VoidCallback aoSoltar;
  final VoidCallback aoAdicionar;

  @override
  Widget build(BuildContext context) {
    final id = selecionada;
    final aberto = painel;
    final alturaDoPainel = math.min(AureaDims.painel, altura);
    return Stack(
      children: [
        Positioned.fill(
          child: Column(
            children: [
              Expanded(
                child: TimelineDoEditor(
                  playback: playback,
                  aoScrub: videos.scrub,
                  aoTocarNoVazio: aoSoltar,
                ),
              ),
              if (lote)
                BarraDoLote(aoAcionar: aoAcionarLote)
              else if (id != null)
                _BarraDaCamada(
                  layerId: id,
                  painel: aberto,
                  aoTocar: aoTocarFerramenta,
                  aoAcionar: aoAcionar,
                )
              else
                BarraDoProjeto(aoAcionar: aoAcionarProjeto),
            ],
          ),
        ),
        if (aberto == null)
          Positioned(
            right: AureaDims.margemDoAdicionar,
            bottom:
                AureaDims.margemDoAdicionar +
                (id == null && !lote ? 0 : AureaDims.barraDeFerramentas),
            child: _BotaoAdicionar(aoTocar: aoAdicionar),
          ),
        Positioned(
          left: 0,
          right: 0,
          bottom: 0,
          height: alturaDoPainel,
          child: AnimatedSwitcher(
            duration: AureaMotion.rapido,
            switchInCurve: AureaMotion.entrada,
            switchOutCurve: AureaMotion.saida,
            transitionBuilder: (filho, anim) => SlideTransition(
              position: Tween<Offset>(
                begin: const Offset(0, 1),
                end: Offset.zero,
              ).animate(anim),
              child: filho,
            ),
            child: aberto == null || id == null
                ? const SizedBox.shrink(key: ValueKey('sem-painel'))
                : KeyedSubtree(
                    key: ValueKey('painel-aberto-${aberto.name}-$id'),
                    child: Builder(
                      builder: (context) =>
                          registroDePaineis(aberto)(context, id),
                    ),
                  ),
          ),
        ),
      ],
    );
  }
}

/// A BARRA DA CAMADA escolhida. Observa so o TIPO da camada (e se tem
/// texto 3D): a lista de ferramentas nao muda quando um numero muda.
class _BarraDaCamada extends ConsumerWidget {
  const _BarraDaCamada({
    required this.layerId,
    required this.painel,
    required this.aoTocar,
    required this.aoAcionar,
  });

  final String layerId;
  final PainelId? painel;
  final ValueChanged<Ferramenta> aoTocar;
  final AoAcionarFerramenta aoAcionar;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final tipo = ref.watch(
      editorControllerProvider.select((p) {
        final l = p.layerById(layerId);
        if (l == null) return null;
        final texto3d =
            l is Scene3DLayer && l.scene.nodes.any((n) => n.texto3d != null);
        return (l.runtimeType, texto3d);
      }),
    );
    if (tipo == null) return const SizedBox.shrink();
    final camada = ref.read(editorControllerProvider).layerById(layerId);
    return BarraContextual(
      // UMA BARRA POR CAMADA: sem a chave, trocar a selecao direto de uma
      // camada para outra reaproveitava a rolagem da barra anterior — a do
      // video rolada ate "Dividir" abria a do texto no fim, com Texto,
      // Fonte e Estilo fora da tela (visto no teste do fluxo completo).
      key: ValueKey('barra-da-camada-$layerId'),
      ferramentas: ferramentasDa(camada, aoAcionar: aoAcionar),
      ativo: painel,
      aoTocar: aoTocar,
    );
  }
}

/// O "+" (73, a 6 da borda): preenchimento de ACAO, sem anel nem sombra.
class _BotaoAdicionar extends StatelessWidget {
  const _BotaoAdicionar({required this.aoTocar});

  final VoidCallback aoTocar;

  @override
  Widget build(BuildContext context) => Semantics(
    label: translate(context, 'Adicionar'),
    button: true,
    child: GestureDetector(
      key: const ValueKey('editor-adicionar'),
      onTap: () {
        HapticFeedback.lightImpact();
        aoTocar();
      },
      child: Container(
        width: AureaDims.botaoAdicionar,
        height: AureaDims.botaoAdicionar,
        decoration: BoxDecoration(
          color: AureaCores.acao,
          shape: BoxShape.circle,
        ),
        child: Icon(
          Icons.add,
          size: AureaDims.iconeXl,
          color: AureaCores.sobreAcao,
        ),
      ),
    ),
  );
}
