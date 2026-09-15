import 'dart:typed_data';

import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:flutter/gestures.dart' show LongPressGestureRecognizer;
import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/utils/time_format.dart';
import '../../../../core/ui/snack.dart';
import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import '../../domain/mask.dart';
import '../../domain/cut.dart';
import '../../domain/cut_ops.dart';
import '../../domain/onda_no_clipe.dart';
import '../../domain/video_project.dart' as proj;
import '../../application/media_preview_service.dart';
import '../../application/proxy_service.dart';
import '../../application/ui/editor_session.dart';
import '../../application/ui/opcoes_de_visualizacao.dart';
import '../../../../core/storage/prefs.dart';
import '../shell/layer_actions.dart' show menuDasMarcas;
import 'am_colors.dart';
import 'layer_look.dart';
import 'clip_preview_painters.dart';
import 'transition_sheet.dart';
import '../../application/registro_de_travadas.dart';
import '../../application/perfil3d.dart';

// Alturas confortaveis para mobile (alvo de toque >= 44pt e visibilidade de keyframes).
const double kAmRowHeight = 46;
const double kAmBarHeight = 36;

/// LINHAS ALTAS QUANDO HA VIDEO OU AUDIO. A onda do som precisa de altura
/// para decupar ("deixar o espectro de audio bem visivel"): numa barra de
/// 36 px ela tinha 12 px. As alturas continuam UNIFORMES na timeline — as
/// duas listas sincronizadas, o revelar da selecao e o arrasto de pilha
/// dependem disso — e so crescem quando o projeto tem midia com som.
const double kAmRowHeightMidia = 78;
const double kAmBarHeightMidia = 68;

class _Alturas extends InheritedWidget {
  const _Alturas({
    required this.linha,
    required this.barra,
    required super.child,
  });

  final double linha;
  final double barra;

  static double linhaDe(BuildContext c) =>
      c.dependOnInheritedWidgetOfExactType<_Alturas>()?.linha ?? kAmRowHeight;
  static double barraDe(BuildContext c) =>
      c.dependOnInheritedWidgetOfExactType<_Alturas>()?.barra ?? kAmBarHeight;

  @override
  bool updateShouldNotify(_Alturas old) =>
      old.linha != linha || old.barra != barra;
}

/// A TIRA DE BAIXO DA BARRA, so dos keyframes.
const double kAmFaixaKeyframes = 15;

/// TIMELINE MAGNETICA — ligada por padrao.
///
/// Ligada: excluir fecha o buraco e o que vinha depois encosta. Sem
/// isso, apagar um pedaco deixa um vazio que a pessoa arruma na mao — e
/// era a reclamacao "corta, apaga e fica um buraco".
///
/// Desligada: cada clipe tem posicao livre, e excluir deixa o buraco.
/// E o que se quer quando outra trilha precisa continuar no mesmo lugar.
final magneticProvider = StateProvider<bool>((ref) {
  try {
    return ref.read(sharedPreferencesProvider).getBool(kTimelineImaPref) ??
        true;
  } catch (_) {
    return true;
  }
});

const kTimelineImaPref = 'timeline.ima';

/// Liga/desliga o ima pela regua, e lembra.
void alternarIma(WidgetRef ref) {
  final v = !ref.read(magneticProvider);
  ref.read(magneticProvider.notifier).state = v;
  try {
    ref.read(sharedPreferencesProvider).setBool(kTimelineImaPref, v);
  } catch (_) {}
}

/// EMPACOTAMENTO VISUAL (decisao Q8): pedacos do MESMO arquivo de video
/// ou audio, vizinhos na pilha e sem sobreposicao no tempo, desenham na
/// mesma linha. O modelo continua uma camada por pedaco.
List<List<Layer>> empacotarTrilhas(List<Layer> layers) {
  final out = <List<Layer>>[];
  for (final l in layers) {
    final fonte = _fonteDe(l);
    if (fonte != null && out.isNotEmpty) {
      final t = out.last;
      final cabe = t.every(
        (o) =>
            o.runtimeType == l.runtimeType &&
            _fonteDe(o) == fonte &&
            (o.endTime <= l.startTime || l.endTime <= o.startTime),
      );
      if (cabe) {
        t.add(l);
        continue;
      }
    }
    out.add([l]);
  }
  return out;
}

String? _fonteDe(Layer l) => switch (l) {
  VideoLayer v => v.sourcePath,
  AudioLayer a => a.sourcePath,
  _ => null,
};

/// Timeline do editor: regua com relogio central, playhead fixo no centro,
/// pilulas de camada (olho + miniatura) fixas a esquerda e barras teal.
///
/// [singleLayerId] != null -> modo pagina de ferramenta: mostra so a camada
/// selecionada, com setas < > para navegar entre camadas.
class AmTimeline extends ConsumerStatefulWidget {
  const AmTimeline({
    super.key,
    required this.playback,
    this.singleLayerId,
    this.playheadColor = Colors.white,
    this.height = 260,
    this.onTapLayer,
    this.onTapBackground,
    this.activeTimesUs,
    this.onScrub,
    this.onForeignKeyframe,
    this.onExpand,
    this.expanded = false,
    this.onKeyframeTap,
  });

  /// Tocaram num diamante ACESO: quem recebe abre o easing (E5).
  final void Function(Layer layer, Duration kfTime)? onKeyframeTap;

  final PlaybackController playback;
  final String? singleLayerId;

  /// Botao no canto direito da regua: a timeline toma a tela (Fase 2).
  final VoidCallback? onExpand;
  final bool expanded;
  final Color playheadColor;
  final double height;
  final void Function(Layer layer)? onTapLayer;

  /// TOQUE NO VAZIO DA TIMELINE. Com um painel aberto, e o caminho de
  /// volta: "queremos voltar para a timeline quando tocamos AQUI, e nao
  /// so no canto superior esquerdo" — o pedido dos testadores. Nulo
  /// quando nao ha para onde voltar.
  final VoidCallback? onTapBackground;

  /// Chamado enquanto a regua e arrastada — o editor toca o som em
  /// lasquinhas. Ouvir onde se esta e o que torna a decupagem rapida.
  final VoidCallback? onScrub;

  /// Tempos locais (em us) com keyframe da propriedade ATIVA: esses
  /// diamantes acendem; os demais aparecem apagados. null = todos acesos.
  final Set<int>? activeTimesUs;

  /// Tocaram num diamante APAGADO — de outra propriedade que nao a que
  /// esta em edicao. Quem recebe diz de quem e o keyframe e oferece o
  /// caminho ate la.
  final void Function(Duration kfTime)? onForeignKeyframe;

  @override
  ConsumerState<AmTimeline> createState() => _AmTimelineState();
}

class _AmTimelineState extends ConsumerState<AmTimeline> {
  final ScrollController _scroll = ScrollController();
  final ScrollController _rowsScroll = ScrollController();
  final ScrollController _pillsScroll = ScrollController();
  bool _syncingRows = false;
  String? _revealedSelection;
  String? _revealedSingleLayer;
  int _revealedKeyCount = -1;

  void _syncRows(ScrollController source, ScrollController target) {
    if (_syncingRows || !source.hasClients || !target.hasClients) return;
    _syncingRows = true;
    target.jumpTo(source.offset.clamp(0.0, target.position.maxScrollExtent));
    _syncingRows = false;
  }

  double _pps = 80;
  double _ppsAtGestureStart = 80;
  double _scrollAtGestureStart = 0;
  double _focalAtGestureStart = 0;
  bool _syncingScroll = false;
  bool _editingBar = false;

  /// O DEDO (ou a inercia dele) esta no comando da rolagem.
  ///
  /// Enquanto isso for verdade o relogio NAO puxa a rolagem de volta.
  /// O seek cai na grade de quadros, e a grade nao coincide com o pixel
  /// onde o dedo esta: a 80 px/s e 30 fps um quadro tem 2,7 px, entao
  /// quase todo evento de rolagem gerava um jumpTo de ate 1,3 px na
  /// direcao contraria. jumpTo mata a inercia e desalinha o arrasto — a
  /// timeline "travava" a cada deslize. Quando a rolagem para, uma
  /// unica acomodacao na grade, e pronto.
  bool _rolagemDoDedo = false;

  @override
  void initState() {
    super.initState();
    _rowsScroll.addListener(() => _syncRows(_rowsScroll, _pillsScroll));
    _pillsScroll.addListener(() => _syncRows(_pillsScroll, _rowsScroll));
    widget.playback.time.addListener(_onClock);
    // Sem isso, uma timeline recem-criada (ex.: ao abrir um painel) fica
    // com scroll 0 enquanto o tempo real esta em outro ponto — e o
    // keyframe parece nascer "fora" do playhead.
    WidgetsBinding.instance.addPostFrameCallback((_) => _onClock());
  }

  @override
  void dispose() {
    widget.playback.time.removeListener(_onClock);
    _scroll.dispose();
    _rowsScroll.dispose();
    _pillsScroll.dispose();
    super.dispose();
  }

  void _onClock() {
    if (!_scroll.hasClients || _editingBar) return;
    // A trava so vale enquanto a rolagem esta DE FATO em andamento. Se
    // o aviso de fim se perdeu (um arrasto de barra que comecou no meio
    // de uma inercia engolia o ScrollEnd), a regua parava de seguir o
    // relogio para sempre: o keyframe nascia no tempo certo e aparecia
    // longe do cabecote. isScrollingNotifier e a verdade do motor.
    if (_rolagemDoDedo && _scroll.position.isScrollingNotifier.value) {
      return;
    }
    _rolagemDoDedo = false;
    final target = _timeToPx(widget.playback.time.value);
    if ((target - _scroll.offset).abs() < 0.5) return;
    _syncingScroll = true;
    _scroll.jumpTo(target.clamp(0.0, _scroll.position.maxScrollExtent));
    _syncingScroll = false;
  }

  /// FIM DE UM ARRASTO NA BARRA OU NUM LOSANGO: a regua volta a seguir o
  /// relogio.
  ///
  /// Arrastar um losango leva o cabecote junto, para a previa mostrar o
  /// quadro, enquanto a regua fica parada de proposito (senao a marca
  /// fugiria do dedo). Sem esta acomodacao o traco do centro continuaria
  /// apontando para onde o arrasto comecou, e o relogio diria outra coisa
  /// — o "olha onde eu coloquei e olha onde aparece" de sempre. Depois do
  /// quadro, porque o fim tambem chega pelo dispose de uma barra, no meio
  /// da montagem da arvore.
  void _terminarEdicaoDaBarra() {
    _editingBar = false;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted && !_editingBar) _onClock();
    });
  }

  double _timeToPx(Duration t) => t.inMicroseconds / 1e6 * _pps;

  /// Onde o dedo tocou na regua, para o segundo toque saber o lugar.
  double _xDoDuploToque = 0;

  Duration _pxToTime(double px) =>
      Duration(microseconds: (px / _pps * 1e6).round());

  /// Com o magnetico ligado, o cabecote solto a menos de 10 px de uma
  /// marca vai para a marca.
  void _encaixarNasMarcas() {
    if (!ref.read(magneticProvider)) return;
    if (widget.playback.playing.value) return;
    final project = ref.read(editorControllerProvider);
    if (project.markers.isEmpty) return;
    final t = widget.playback.time.value;
    final tolUs = (_pxToTime(10) - _pxToTime(0)).inMicroseconds.abs();
    Duration? melhor;
    var melhorD = tolUs + 1;
    for (final m in project.markers) {
      final d = (m.time - t).inMicroseconds.abs();
      if (d < melhorD) {
        melhorD = d;
        melhor = m.time;
      }
    }
    if (melhor != null && melhor != t) {
      widget.playback.seek(melhor);
      HapticFeedback.selectionClick();
    }
  }

  bool _onScroll(ScrollNotification n) {
    if (_syncingScroll) return false;
    // SO A ROLAGEM HORIZONTAL e tempo. A lista de camadas rola na
    // vertical dentro desta, e os avisos dela sobem ate aqui: tratar o
    // deslocamento vertical como tempo mandava o relogio para o comeco
    // a cada rolada na lista.
    if (n.metrics.axis != Axis.horizontal) return false;
    if (n is ScrollStartNotification) {
      _rolagemDoDedo = true;
      if (n.dragDetails != null) {
        HapticFeedback.selectionClick();
      }
    } else if (n is ScrollEndNotification) {
      _rolagemDoDedo = false;
      // ENCAIXE DO CABECOTE: soltou perto de uma marca, cai nela — e o
      // keyframe cravado em seguida cai exatamente na batida.
      _encaixarNasMarcas();
      // Parou: acomoda o conteudo no quadro em que o relogio caiu.
      _onClock();
      return false;
    }
    if (_editingBar) return false;
    if (n is ScrollUpdateNotification) {
      if (n.dragDetails != null && widget.playback.playing.value) {
        widget.playback.pause();
      }
      if (!widget.playback.playing.value) {
        widget.playback.seek(_pxToTime(n.metrics.pixels));
        // Ouvir onde se esta enquanto arrasta.
        if (n.dragDetails != null) widget.onScrub?.call();
      }
    }
    return false;
  }

  @override
  Widget build(BuildContext context) => RegistroDeTravadas.marcando(
    'construindo a linha do tempo',
    () => Perfil3D.fase('build.timeline', () => _build(context)),
  );

  Widget _build(BuildContext context) {
    final project = ref.watch(editorControllerProvider);
    final selectedId = ref.watch(selectedLayerProvider);
    final multi = ref.watch(multiSelectProvider);
    final layers = widget.singleLayerId != null
        ? [
            if (project.layerById(widget.singleLayerId!) != null)
              project.layerById(widget.singleLayerId!)!,
          ]
        : project.layers;
    final matteSourceIds = <String>{
      for (final layer in project.layers)
        if (layer.matteMode != MatteMode.none && layer.matteSourceId != null)
          layer.matteSourceId!,
    };
    var maxEndUs = project.duration.inMicroseconds;
    for (final l in project.layers) {
      if (l.endTime.inMicroseconds > maxEndUs) {
        maxEndUs = l.endTime.inMicroseconds;
      }
    }
    final totalWidth = (maxEndUs / 1e6 * _pps) + 200.0;
    final trilhas = empacotarTrilhas(layers);
    final comMidia =
        widget.singleLayerId == null &&
        layers.any((l) => l is VideoLayer || l is AudioLayer);
    final alturaLinha = comMidia ? kAmRowHeightMidia : kAmRowHeight;
    final alturaBarra = comMidia ? kAmBarHeightMidia : kAmBarHeight;
    final selecionadas = <String>{?selectedId, ...multi};
    final controller = ref.read(editorControllerProvider.notifier);
    final caminho = controller.caminhoDoGrupo;
    final sessao = ref.watch(editorSessionProvider);
    final selected = selectedId == null ? null : project.layerById(selectedId);
    final keyCount = selected?.keyframeTimes.length ?? 0;
    if (_revealedSelection != selectedId ||
        _revealedKeyCount != keyCount ||
        _revealedSingleLayer != widget.singleLayerId) {
      _revealedSelection = selectedId;
      _revealedKeyCount = keyCount;
      _revealedSingleLayer = widget.singleLayerId;
      final index = trilhas.indexWhere((t) => t.any((l) => l.id == selectedId));
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (!mounted ||
            !_rowsScroll.hasClients ||
            index < 0 ||
            _revealedSelection != selectedId) {
          return;
        }
        final top = index * alturaLinha;
        final position = _rowsScroll.position;
        final bottom = top + alturaLinha;
        final target = top < position.pixels
            ? top
            : bottom > position.pixels + position.viewportDimension
            ? bottom - position.viewportDimension
            : position.pixels;
        if (target != position.pixels) {
          _rowsScroll.jumpTo(target.clamp(0.0, position.maxScrollExtent));
        }
      });
    }

    return _Alturas(
      linha: alturaLinha,
      barra: alturaBarra,
      child: Listener(
      behavior: HitTestBehavior.deferToChild,
      // "Clicar na timeline fecha essa e qualquer outra aba": o toque
      // aqui fecha a barra de adicionar, sem disputar o gesto com a
      // rolagem, o arrasto da barra ou o encaixe do cabecote.
      onPointerDown: (_) {
        if (ref.read(editorSessionProvider).adding) {
          ref.read(editorSessionProvider.notifier).closeAdd();
        }
      },
      child: SizedBox(
        height: widget.height,
        child: LayoutBuilder(
          builder: (context, constraints) {
            final pad = constraints.maxWidth / 2;
            return GestureDetector(
              onScaleStart: (d) {
                if (d.pointerCount >= 2) {
                  HapticFeedback.selectionClick();
                }
                _ppsAtGestureStart = _pps;
                _scrollAtGestureStart = _scroll.hasClients ? _scroll.offset : 0;
                _focalAtGestureStart = d.localFocalPoint.dx;
              },
              onScaleUpdate: (d) {
                if (d.pointerCount < 2) return;
                setState(() {
                  // Zoom ancorado no CENTROIDE da pinca (AUREA §3.1-5):
                  // o conteudo sob os dedos fica sob os dedos.
                  final newPps = (_ppsAtGestureStart * d.scale).clamp(
                    16.0,
                    400.0,
                  );
                  final k = newPps / _ppsAtGestureStart;
                  final contentAtFocal =
                      _scrollAtGestureStart + (_focalAtGestureStart - pad);
                  final newOffset =
                      contentAtFocal * k - (_focalAtGestureStart - pad);
                  _pps = newPps;
                  WidgetsBinding.instance.addPostFrameCallback((_) {
                    if (_scroll.hasClients) {
                      // Sem suprimir o seek: o tempo sob o playhead central
                      // continua verdadeiro durante o zoom.
                      _scroll.jumpTo(
                        newOffset.clamp(0.0, _scroll.position.maxScrollExtent),
                      );
                    }
                  });
                });
              },
              child: Stack(
                children: [
                  // Conteudo rolavel: regua + linhas de camada.
                  NotificationListener<ScrollNotification>(
                    onNotification: _onScroll,
                    child: SingleChildScrollView(
                      controller: _scroll,
                      scrollDirection: Axis.horizontal,
                      // Rubber-band nas pontas em vez de parede seca
                      // (AUREA §3.1-4: "parecer iOS").
                      physics: const BouncingScrollPhysics(),
                      child: Padding(
                        padding: EdgeInsets.symmetric(horizontal: pad),
                        child: SizedBox(
                          width: totalWidth,
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              SizedBox(
                                height: 20,
                                width: totalWidth,
                                // As marcas vivem NA REGUA: e onde a pessoa
                                // olha para achar o instante.
                                child: Stack(
                                  clipBehavior: Clip.none,
                                  children: [
                                    Positioned.fill(
                                      child: GestureDetector(
                                        behavior: HitTestBehavior.opaque,
                                        // TOQUE DUPLO cria a marca onde o
                                        // dedo esta — nao no cabecote. Quem
                                        // ouve a musica aponta o lugar; ter
                                        // de levar o cabecote ate la antes
                                        // e um passo a mais no meio do
                                        // ritmo.
                                        onDoubleTapDown: (d) =>
                                            _xDoDuploToque = d.localPosition.dx,
                                        // SEGURAR NA REGUA abre o menu de
                                        // marcas e batidas: e na regua que
                                        // elas moram, entao e nela que se
                                        // pergunta por elas.
                                        onLongPress: () {
                                          HapticFeedback.mediumImpact();
                                          menuDasMarcas(
                                            context,
                                            ref,
                                            widget.playback,
                                          );
                                        },
                                        onDoubleTap: () {
                                          final t = Duration(
                                            microseconds:
                                                (_xDoDuploToque / _pps * 1e6)
                                                    .round(),
                                          );
                                          ref
                                              .read(
                                                editorControllerProvider
                                                    .notifier,
                                              )
                                              .toggleMarker(t);
                                          HapticFeedback.selectionClick();
                                        },
                                        child: CustomPaint(
                                          painter: _AmRulerPainter(pps: _pps),
                                          // BATIDAS: risquinhos finos, e nao
                                          // bandeiras. Sao centenas contra
                                          // as poucas marcas postas a mao —
                                          // desenhadas iguais, apagariam
                                          // justamente as que alguem
                                          // escolheu.
                                          foregroundPainter: _BeatsPainter(
                                            beats: project.beats,
                                            pps: _pps,
                                          ),
                                        ),
                                      ),
                                    ),
                                    for (final (rotulo, tempo) in [
                                      ('I', sessao.inPoint),
                                      ('O', sessao.outPoint),
                                    ])
                                      if (tempo != null)
                                        Positioned(
                                          key: ValueKey('marca-$rotulo'),
                                          left: _timeToPx(tempo) - 1,
                                          top: 0,
                                          bottom: 0,
                                          child: IgnorePointer(
                                            child: Row(
                                              crossAxisAlignment:
                                                  CrossAxisAlignment.start,
                                              children: [
                                                Container(
                                                  width: 2,
                                                  color: AmColors.action,
                                                ),
                                                AppText(
                                                  rotulo,
                                                  style: const TextStyle(
                                                    fontSize: 9,
                                                    fontWeight: FontWeight.w800,
                                                    color: AmColors.action,
                                                  ),
                                                ),
                                              ],
                                            ),
                                          ),
                                        ),
                                    for (final m in project.markers)
                                      _MarcaNaRegua(
                                        marca: m,
                                        pps: _pps,
                                        onMover: (dx) => ref
                                            .read(
                                              editorControllerProvider.notifier,
                                            )
                                            .moveMarker(
                                              m.time,
                                              m.time +
                                                  Duration(
                                                    microseconds:
                                                        (dx / _pps * 1e6)
                                                            .round(),
                                                  ),
                                            ),
                                        onMenu: () =>
                                            _menuDaMarca(context, ref, m),
                                      ),
                                  ],
                                ),
                              ),
                              const SizedBox(height: 18),
                              Expanded(
                                // O FUNDO E UM ALVO. As barras sao opacas
                                // e ficam com o toque delas; o que sobra
                                // — o vazio abaixo das linhas — e o toque
                                // de voltar.
                                child: GestureDetector(
                                  key: const ValueKey('timeline-fundo'),
                                  behavior: HitTestBehavior.translucent,
                                  onTap: widget.onTapBackground,
                                  child: ListView.builder(
                                  controller: _rowsScroll,
                                  padding: EdgeInsets.zero,
                                  itemExtent: alturaLinha,
                                  itemCount: trilhas.length,
                                  itemBuilder: (context, index) {
                                    final trilha = trilhas[index];
                                    return _AmLayerRow(
                                      key: ValueKey(trilha.first.id),
                                      trilha: trilha,
                                      pps: _pps,
                                      totalWidth: totalWidth,
                                      selectedIds: selecionadas,
                                      compact: widget.singleLayerId != null,
                                      playback: widget.playback,
                                      onTapLayer: widget.onTapLayer,
                                      activeTimesUs: widget.activeTimesUs,
                                      onForeignKeyframe:
                                          widget.onForeignKeyframe,
                                      onKeyframeTap: widget.onKeyframeTap,
                                      onEditStart: () => _editingBar = true,
                                      onEditEnd: _terminarEdicaoDaBarra,
                                    );
                                  },
                                ),
                                ),
                              ),
                            ],
                          ),
                        ),
                      ),
                    ),
                  ),
                  // Relogio central sob a regua.
                  Positioned(
                    top: 8,
                    left: 0,
                    right: 0,
                    child: Center(
                      child: ValueListenableBuilder<Duration>(
                        valueListenable: widget.playback.time,
                        builder: (context, t, _) => Column(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            AppText(
                              formatTimecode(t, project.fps),
                              style: const TextStyle(
                                fontSize: 13,
                                fontWeight: FontWeight.w700,
                                color: Colors.white,
                                letterSpacing: 0.5,
                                fontFeatures: [FontFeature.tabularFigures()],
                              ),
                            ),
                            const SizedBox(height: 2),
                            Container(
                              width: 58,
                              height: 1.5,
                              color: Colors.white,
                            ),
                          ],
                        ),
                      ),
                    ),
                  ),
                  // Playhead central.
                  IgnorePointer(
                    child: Center(
                      child: Container(width: 1.6, color: widget.playheadColor),
                    ),
                  ),
                  if (widget.playheadColor != Colors.white)
                    IgnorePointer(
                      child: Align(
                        alignment: Alignment.topCenter,
                        child: Container(
                          width: 8,
                          height: 8,
                          margin: const EdgeInsets.only(top: 0),
                          decoration: BoxDecoration(
                            color: widget.playheadColor,
                            borderRadius: BorderRadius.circular(2),
                          ),
                        ),
                      ),
                    ),
                  // Dividir e Congelar moram nas acoes rapidas da camada (E2);
                  // nada flutua sobre as linhas da timeline (Fase 2).
                  // CANTO DIREITO: expandir a timeline (preview vira janela).
                  // SELECIONAR: ao lado do expandir, sempre a vista.
                  if (widget.singleLayerId == null)
                    Positioned(
                      right: widget.onExpand != null ? 34 : 0,
                      top: 0,
                      height: 22,
                      child: Consumer(
                        builder: (context, ref, _) {
                          final ligado = ref.watch(modoSelecionarProvider);
                          return _BotaoDaRegua(
                            key: const ValueKey('timeline-selecionar'),
                            tooltip: ligado
                                ? 'Sair do modo Selecionar'
                                : 'Selecionar varias camadas',
                            ativo: ligado,
                            onTap: () {
                              ref.read(modoSelecionarProvider.notifier).state =
                                  !ligado;
                            },
                            child: Icon(
                              ligado
                                  ? CupertinoIcons.checkmark_square_fill
                                  : CupertinoIcons.checkmark_square,
                              size: 14,
                              color: ligado ? AmColors.action : AmColors.text,
                            ),
                          );
                        },
                      ),
                    ),
                  if (widget.onExpand != null)
                    Positioned(
                      right: 0,
                      top: 0,
                      height: 22,
                      child: _BotaoDaRegua(
                        key: const ValueKey('timeline-expandir'),
                        tooltip: widget.expanded
                            ? 'Recolher timeline'
                            : 'Expandir timeline',
                        ativo: widget.expanded,
                        onTap: widget.onExpand!,
                        child: Icon(
                          widget.expanded
                              ? CupertinoIcons.arrow_down_right_arrow_up_left
                              : CupertinoIcons.arrow_up_left_arrow_down_right,
                          size: 14,
                          color: widget.expanded
                              ? AmColors.action
                              : AmColors.text,
                        ),
                      ),
                    ),
                  // CAMINHO DO GRUPO: Projeto › Grupo. Tocar em Projeto sai.
                  if (caminho.isNotEmpty)
                    Positioned(
                      left: 4,
                      top: 25,
                      height: 24,
                      width: constraints.maxWidth / 2 - 56,
                      child: _Breadcrumb(
                        key: const ValueKey('timeline-breadcrumb'),
                        caminho: caminho,
                        onNivel: (nivel) {
                          // nivel 0 = Projeto; n = o grupo n (1-based).
                          var sair = caminho.length - nivel;
                          while (sair-- > 0) {
                            controller.exitGroup();
                          }
                        },
                      ),
                    ),
                  // COLUNA ESQUERDA FIXA: olho, cadeado, keyframe — uma por
                  // linha (Fase 2). A miniatura mora dentro da barra.
                  Positioned(
                    left: 0,
                    top: 38,
                    bottom: 0,
                    width: 66,
                    child: Container(
                      decoration: BoxDecoration(
                        gradient: LinearGradient(
                          begin: Alignment.centerLeft,
                          end: Alignment.centerRight,
                          colors: [
                            AmColors.bg,
                            AmColors.bg.withValues(alpha: 0.95),
                            AmColors.bg.withValues(alpha: 0.0),
                          ],
                          stops: const [0.0, 0.78, 1.0],
                        ),
                      ),
                      child: ListView.builder(
                        controller: _pillsScroll,
                        padding: EdgeInsets.zero,
                        itemExtent: alturaLinha,
                        itemCount: trilhas.length,
                        itemBuilder: (context, index) {
                          final trilha = trilhas[index];
                          return _ControlesDaTrilha(
                            trilha: trilha,
                            playback: widget.playback,
                            isMatteSource: trilha.any(
                              (l) => matteSourceIds.contains(l.id),
                            ),
                          );
                        },
                      ),
                    ),
                  ),
                ],
              ),
            );
          },
        ),
      ),
    ),
    );
  }
}

/// COLUNA ESQUERDA DE UMA LINHA: olho · cadeado · ◆ (Fase 2).
///
/// Numa linha empacotada (pedacos do mesmo clipe), o olho e o cadeado
/// valem para todos os pedacos — sao o mesmo clipe para quem edita.
class _ControlesDaTrilha extends ConsumerWidget {
  const _ControlesDaTrilha({
    required this.trilha,
    required this.playback,
    required this.isMatteSource,
  });
  final List<Layer> trilha;
  final PlaybackController playback;
  final bool isMatteSource;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final controller = ref.read(editorControllerProvider.notifier);
    final project = ref.watch(editorControllerProvider);
    final layer = trilha.first;
    final hidden = trilha.every((l) => project.metaOf(l.id).hidden);
    final locked = trilha.every((l) => project.metaOf(l.id).locked);
    return Align(
      alignment: Alignment.centerLeft,
      child: Container(
        height: 28,
        width: 58,
        margin: const EdgeInsets.only(left: 4),
        decoration: BoxDecoration(
          color: const Color(0xFF1E222D),
          borderRadius: BorderRadius.circular(14),
        ),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceEvenly,
          children: [
            Tooltip(
              message: hidden ? 'Mostrar camada' : 'Ocultar camada',
              child: GestureDetector(
                key: ValueKey('olho-${layer.id}'),
                behavior: HitTestBehavior.opaque,
                onTap: () {
                  HapticFeedback.lightImpact();
                  controller.runAsOneUndo(() {
                    for (final l in trilha) {
                      controller.toggleHidden(l.id);
                    }
                  });
                },
                child: SizedBox(
                  width: 26,
                  height: _Alturas.linhaDe(context),
                  child: Icon(
                    hidden ? CupertinoIcons.eye_slash : CupertinoIcons.eye,
                    size: 16,
                    color: Colors.white70,
                  ),
                ),
              ),
            ),
            Tooltip(
              message: locked
                  ? 'Bloqueada · segure para desbloquear'
                  : 'Selecionar · segure para bloquear',
              child: GestureDetector(
                key: ValueKey('kf-${layer.id}'),
                behavior: HitTestBehavior.opaque,
                onTap: () {
                  HapticFeedback.selectionClick();
                  ref.read(multiSelectProvider.notifier).state = const {};
                  ref.read(selectedLayerProvider.notifier).state = layer.id;
                },
                onLongPress: () {
                  HapticFeedback.mediumImpact();
                  controller.runAsOneUndo(() {
                    for (final l in trilha) {
                      controller.toggleLocked(l.id);
                    }
                  });
                },
                child: Container(
                  width: 18,
                  height: 18,
                  decoration: BoxDecoration(
                    color: const Color(0xFFFFE899),
                    borderRadius: BorderRadius.circular(4),
                    border: isMatteSource
                        ? Border.all(color: AmColors.accent)
                        : null,
                  ),
                  alignment: Alignment.center,
                  child: locked
                      ? const Icon(
                          CupertinoIcons.lock_fill,
                          size: 11,
                          color: Color(0xFF12151A),
                        )
                      : null,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// Botaozinho da regua (ima, busca, expandir).
class _BotaoDaRegua extends StatelessWidget {
  const _BotaoDaRegua({
    super.key,
    required this.tooltip,
    required this.onTap,
    required this.child,
    this.ativo = false,
  });

  final String tooltip;
  final VoidCallback onTap;
  final Widget child;
  final bool ativo;

  @override
  Widget build(BuildContext context) => Tooltip(
    message: tooltip,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: () {
        HapticFeedback.lightImpact();
        onTap();
      },
      child: Container(
        width: 30,
        height: 22,
        alignment: Alignment.center,
        decoration: BoxDecoration(
          color: ativo
              ? AmColors.actionDim
              : AmColors.bg.withValues(alpha: .85),
          borderRadius: BorderRadius.circular(6),
        ),
        child: child,
      ),
    ),
  );
}

/// Projeto › Grupo › Subgrupo. Cada nivel e tocavel; o ultimo e o atual.
class _Breadcrumb extends StatelessWidget {
  const _Breadcrumb({super.key, required this.caminho, required this.onNivel});

  final List<String> caminho;
  final ValueChanged<int> onNivel;

  @override
  Widget build(BuildContext context) {
    final nomes = ['Projeto', ...caminho];
    return Container(
      decoration: BoxDecoration(
        color: AmColors.bg.withValues(alpha: .9),
        borderRadius: BorderRadius.circular(7),
      ),
      padding: const EdgeInsets.symmetric(horizontal: 6),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          for (var i = 0; i < nomes.length; i++) ...[
            if (i > 0)
              const Padding(
                padding: EdgeInsets.symmetric(horizontal: 3),
                child: AppText(
                  '›',
                  style: TextStyle(color: AmColors.muted, fontSize: 12),
                ),
              ),
            Flexible(
              child: GestureDetector(
                key: ValueKey('timeline-breadcrumb-$i'),
                behavior: HitTestBehavior.opaque,
                onTap: i == nomes.length - 1 ? null : () => onNivel(i),
                child: AppText(
                  nomes[i],
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(
                    color: i == nomes.length - 1
                        ? AmColors.text
                        : AmColors.selection,
                    fontSize: 11.5,
                    fontWeight: FontWeight.w700,
                  ),
                ),
              ),
            ),
          ],
        ],
      ),
    );
  }
}

/// AS BATIDAS: risquinhos finos na base da regua.
///
/// Densidade e o ponto. Uma faixa de tres minutos a 120 bpm tem 360
/// tempos; desenhados como bandeiras, viram uma parede. Risco fino de
/// meia altura le-se como grade e nao disputa com a marca posta a mao.
class _BeatsPainter extends CustomPainter {
  const _BeatsPainter({required this.beats, required this.pps});

  final List<Duration> beats;
  final double pps;

  static final Paint _tinta = Paint()
    ..color = AmColors.teal.withValues(alpha: 0.55)
    ..strokeWidth = 1
    ..style = PaintingStyle.stroke;

  /// O TEMPO FORTE (1 de cada 4) desce mais e pesa mais: sem ele a grade
  /// e um serrilhado uniforme e o olho nao acha o compasso — que e
  /// exatamente o que um editor de AMV esta procurando na regua.
  static final Paint _tintaForte = Paint()
    ..color = AmColors.teal.withValues(alpha: 0.9)
    ..strokeWidth = 1.6
    ..style = PaintingStyle.stroke;

  @override
  void paint(Canvas canvas, Size size) {
    Perfil3D.fase('pintar.batidas', () => _pintar(canvas, size));
  }

  void _pintar(Canvas canvas, Size size) {
    if (beats.isEmpty) return;
    // Uma grade densa tem milhares de riscos; num Path so, uma chamada.
    final caminho = Path();
    final fortes = Path();
    for (var i = 0; i < beats.length; i++) {
      final x = beats[i].inMicroseconds / 1e6 * pps;
      if (x < -2 || x > size.width + 2) continue;
      if (i % 4 == 0) {
        fortes
          ..moveTo(x, size.height * 0.3)
          ..lineTo(x, size.height);
      } else {
        caminho
          ..moveTo(x, size.height * 0.55)
          ..lineTo(x, size.height);
      }
    }
    canvas.drawPath(caminho, _tinta);
    canvas.drawPath(fortes, _tintaForte);
  }

  @override
  bool shouldRepaint(_BeatsPainter old) =>
      old.pps != pps || old.beats.length != beats.length;
}

/// Uma marca na regua: bandeirinha com rotulo, que se arrasta.
///
/// E um WIDGET, nao um desenho, de proposito: so quem toca a bandeira
/// arrasta a marca. Se o gesto morasse na regua inteira, ele roubaria a
/// rolagem — e rolar a linha do tempo e o gesto mais usado que existe
/// aqui.
class _MarcaNaRegua extends StatefulWidget {
  const _MarcaNaRegua({
    required this.marca,
    required this.pps,
    required this.onMover,
    required this.onMenu,
  });

  final proj.Marker marca;
  final double pps;
  final ValueChanged<double> onMover;
  final VoidCallback onMenu;

  @override
  State<_MarcaNaRegua> createState() => _MarcaNaReguaState();
}

class _MarcaNaReguaState extends State<_MarcaNaRegua> {
  double _acumulado = 0;

  @override
  Widget build(BuildContext context) {
    final x = widget.marca.time.inMicroseconds / 1e6 * widget.pps;
    return Positioned(
      left: x - 11,
      top: 0,
      width: 22,
      height: 20,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onLongPress: () {
          HapticFeedback.mediumImpact();
          widget.onMenu();
        },
        onHorizontalDragStart: (_) {
          HapticFeedback.lightImpact();
          _acumulado = 0;
        },
        onHorizontalDragUpdate: (d) {
          _acumulado += d.delta.dx;
          widget.onMover(_acumulado);
          _acumulado = 0;
        },
        child: CustomPaint(
          painter: _UmaMarcaPainter(
            color: widget.marca.color,
            label: widget.marca.label,
          ),
        ),
      ),
    );
  }
}

class _UmaMarcaPainter extends CustomPainter {
  const _UmaMarcaPainter({required this.color, required this.label});

  final Color color;
  final String label;

  @override
  void paint(Canvas canvas, Size size) {
    final cx = size.width / 2;
    final p = Paint()..color = color;
    final path = Path()
      ..moveTo(cx - 5, 0)
      ..lineTo(cx + 5, 0)
      ..lineTo(cx, 9)
      ..close();
    canvas.drawPath(path, p);
    if (label.isEmpty) return;
    final tp = TextPainter(
      text: TextSpan(
        text: label,
        style: TextStyle(fontSize: 9, color: color),
      ),
      textDirection: TextDirection.ltr,
      maxLines: 1,
    )..layout();
    tp.paint(canvas, Offset(cx + 7, 0));
  }

  @override
  bool shouldRepaint(_UmaMarcaPainter old) =>
      old.color != color || old.label != label;
}

/// O menu da marca: nomear, pintar, apagar. Toque longo, como em tudo
/// que e destrutivo por aqui.
Future<void> _menuDaMarca(
  BuildContext context,
  WidgetRef ref,
  proj.Marker m,
) async {
  final controller = ref.read(editorControllerProvider.notifier);
  const cores = [
    Color(0xFFB8FF3D),
    Color(0xFF7C62FF),
    Color(0xFFFF6B6B),
    Color(0xFFFFC53D),
    Color(0xFF4DD0E1),
  ];
  final texto = TextEditingController(text: m.label);
  await showModalBottomSheet<void>(
    context: context,
    backgroundColor: AmColors.panel,
    isScrollControlled: true,
    builder: (sheetContext) => Padding(
      padding: EdgeInsets.only(
        bottom: MediaQuery.of(sheetContext).viewInsets.bottom,
      ),
      child: SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Padding(
              padding: const EdgeInsets.fromLTRB(14, 14, 14, 8),
              child: TextField(
                controller: texto,
                autofocus: true,
                style: const TextStyle(color: AmColors.text),
                decoration: InputDecoration(
                  hintText: translate(context, 'Nome da marca'),
                  hintStyle: TextStyle(color: AmColors.muted),
                ),
                onSubmitted: (v) {
                  controller.renameMarker(m.time, v);
                  Navigator.of(sheetContext).pop();
                },
              ),
            ),
            Padding(
              padding: const EdgeInsets.symmetric(horizontal: 10),
              child: Row(
                children: [
                  for (final c in cores)
                    GestureDetector(
                      onTap: () {
                        controller.setMarkerColor(m.time, c);
                        Navigator.of(sheetContext).pop();
                      },
                      child: Container(
                        width: 34,
                        height: 34,
                        margin: const EdgeInsets.all(5),
                        decoration: BoxDecoration(
                          color: c,
                          shape: BoxShape.circle,
                          border: m.color == c
                              ? Border.all(color: Colors.white, width: 2)
                              : null,
                        ),
                      ),
                    ),
                ],
              ),
            ),
            ListTile(
              leading: const Icon(
                CupertinoIcons.delete,
                color: AmColors.pink,
                size: 20,
              ),
              title: const AppText('Apagar a marca',
                style: TextStyle(color: AmColors.pink, fontSize: 15),
              ),
              onTap: () {
                controller.removeMarker(m.time);
                Navigator.of(sheetContext).pop();
              },
            ),
            const SizedBox(height: 6),
          ],
        ),
      ),
    ),
  );
  controller.renameMarker(m.time, texto.text);
  texto.dispose();
}

class _AmRulerPainter extends CustomPainter {
  const _AmRulerPainter({required this.pps});

  final double pps;

  // Objetos de pintura reaproveitados: `paint` roda a cada quadro
  // enquanto a linha rola.
  static final Paint _minor = Paint()
    ..color = const Color(0xFF5A6880)
    ..strokeWidth = 1
    ..style = PaintingStyle.stroke;
  static final Paint _major = Paint()
    ..color = const Color(0xFF8A97AD)
    ..strokeWidth = 1.4
    ..style = PaintingStyle.stroke;

  @override
  void paint(Canvas canvas, Size size) {
    Perfil3D.fase('pintar.regua', () => _pintar(canvas, size));
  }

  void _pintar(Canvas canvas, Size size) {
    // DOIS CAMINHOS, DUAS CHAMADAS. Antes era um drawLine por marca, e a
    // regua cobre a linha do tempo INTEIRA: num projeto de tres minutos
    // sao milhares de chamadas gravadas a cada quadro so para desenhar
    // tracinhos. Juntas num Path, viram duas.
    var step = pps / 10; // 10 marcas por segundo
    if (step < 2) step = pps; // reduzido demais: so as marcas de segundo
    if (step <= 0) return;

    final minor = Path();
    final major = Path();
    var i = 0;
    for (var x = 0.0; x < size.width; x += step) {
      final ehMajor = i % 10 == 0;
      final alvo = ehMajor ? major : minor;
      alvo
        ..moveTo(x, ehMajor ? 2 : 9)
        ..lineTo(x, size.height - 2);
      i++;
    }
    canvas.drawPath(minor, _minor);
    canvas.drawPath(major, _major);
  }

  @override
  bool shouldRepaint(_AmRulerPainter old) => old.pps != pps;
}

/// UMA LINHA DA TIMELINE: uma trilha com um ou mais pedacos (Fase 2).
///
/// O toque no vazio da linha tira a selecao — e com ela fecha as
/// ferramentas da camada e qualquer painel aberto ("clicar na timeline
/// fecha essa aba").
/// Os dois eixos do arrasto longo numa barra: deslocar no TEMPO ou
/// trocar de degrau na PILHA. Nunca os dois no mesmo gesto.
enum _EixoDoArrasto { tempo, pilha }

class _AmLayerRow extends ConsumerWidget {
  const _AmLayerRow({
    super.key,
    required this.trilha,
    required this.pps,
    required this.totalWidth,
    required this.selectedIds,
    required this.compact,
    required this.playback,
    required this.onEditStart,
    required this.onEditEnd,
    this.onTapLayer,
    this.activeTimesUs,
    this.onForeignKeyframe,
    this.onKeyframeTap,
  });

  final void Function(Layer layer, Duration kfTime)? onKeyframeTap;
  final List<Layer> trilha;
  final double pps;
  final double totalWidth;
  final Set<String> selectedIds;
  final bool compact;
  final PlaybackController playback;
  final VoidCallback onEditStart;
  final VoidCallback onEditEnd;
  final void Function(Layer layer)? onTapLayer;
  final Set<int>? activeTimesUs;
  final void Function(Duration kfTime)? onForeignKeyframe;

  @override
  Widget build(BuildContext context, WidgetRef ref) => SizedBox(
    height: _Alturas.linhaDe(context),
    width: totalWidth,
    child: Stack(
      clipBehavior: Clip.none,
      children: [
        // TOCAR NO VAZIO DA TIMELINE FECHA TUDO: tira a selecao (e com ela
        // as ferramentas da camada) e fecha a barra de adicionar.
        Positioned.fill(
          child: GestureDetector(
            behavior: HitTestBehavior.translucent,
            onTap: () {
              HapticFeedback.selectionClick();
              ref.read(multiSelectProvider.notifier).state = const {};
              ref.read(selectedLayerProvider.notifier).state = null;
              ref.read(editorSessionProvider.notifier).closeAdd();
            },
          ),
        ),
        for (final l in trilha)
          _AmBar(
            key: ValueKey('bar-${l.id}'),
            layer: l,
            pps: pps,
            totalWidth: totalWidth,
            selected: compact || selectedIds.contains(l.id),
            compact: compact,
            playback: playback,
            onEditStart: onEditStart,
            onEditEnd: onEditEnd,
            onTapLayer: onTapLayer,
            activeTimesUs: activeTimesUs,
            onForeignKeyframe: onForeignKeyframe,
            onKeyframeTap: onKeyframeTap,
          ),
      ],
    ),
  );
}

/// A BARRA de um pedaco: gestos, alcas, transicao, keyframes.
class _AmBar extends ConsumerStatefulWidget {
  const _AmBar({
    super.key,
    required this.layer,
    required this.pps,
    required this.totalWidth,
    required this.selected,
    required this.compact,
    required this.playback,
    required this.onEditStart,
    required this.onEditEnd,
    this.onTapLayer,
    this.activeTimesUs,
    this.onForeignKeyframe,
    this.onKeyframeTap,
  });

  final void Function(Layer layer, Duration kfTime)? onKeyframeTap;
  final Layer layer;
  final double pps;
  final double totalWidth;
  final bool selected;

  /// Modo pagina de ferramenta (faixa unica com setas de navegacao).
  final bool compact;
  final PlaybackController playback;
  final VoidCallback onEditStart;
  final VoidCallback onEditEnd;
  final void Function(Layer layer)? onTapLayer;
  final Set<int>? activeTimesUs;

  /// Tocaram num diamante APAGADO — de outra propriedade que nao a que
  /// esta em edicao. Quem recebe diz de quem e o keyframe e oferece o
  /// caminho ate la.
  final void Function(Duration kfTime)? onForeignKeyframe;

  @override
  ConsumerState<_AmBar> createState() => _AmBarState();
}

class _AmBarState extends ConsumerState<_AmBar> {
  /// Toque longo em andamento: mover reordena; soltar sem mover alterna
  /// a selecao multipla (o gesto antigo).
  double _ultimoDyLongo = 0;
  bool _moveuNoToqueLongo = false;

  void _alternarNaSelecaoMultipla() {
    final set = {...ref.read(multiSelectProvider)};
    final primary = ref.read(selectedLayerProvider);
    if (primary != null) set.add(primary);
    if (!set.add(layer.id)) set.remove(layer.id);
    ref.read(multiSelectProvider.notifier).state = set;
    HapticFeedback.selectionClick();
  }

  /// Quanto o dedo ja subiu ou desceu no arrasto vertical da barra
  /// selecionada, em pixels; a cada linha inteira a camada troca de
  /// degrau na pilha.
  double _acumuladoVertical = 0;

  /// O eixo que o arrasto longo escolheu no primeiro movimento. Nulo
  /// ate la. Ver o comentario em `onLongPressMoveUpdate`.
  _EixoDoArrasto? _eixoDoArrasto;

  /// SUBIR E DESCER NA PILHA PELA PROPRIA TIMELINE. "Ja que da para
  /// selecionar, tem de dar para mover para cima ou para baixo": o
  /// arrasto vertical na barra selecionada anda um degrau por linha, com
  /// um toque de vibracao a cada degrau. A linha tem chave (o id), entao
  /// o gesto continua na mesma barra depois que ela muda de lugar.
  void _reordenarPorArrasto(double dy) {
    _acumuladoVertical += dy;
    final c = ref.read(editorControllerProvider.notifier);
    while (_acumuladoVertical > _Alturas.linhaDe(context) / 2) {
      c.reorderLayer(widget.layer.id, 1);
      _acumuladoVertical -= _Alturas.linhaDe(context);
      HapticFeedback.selectionClick();
    }
    while (_acumuladoVertical < -_Alturas.linhaDe(context) / 2) {
      c.reorderLayer(widget.layer.id, -1);
      _acumuladoVertical += _Alturas.linhaDe(context);
      HapticFeedback.selectionClick();
    }
  }

  /// UM ARRASTO DE BARRA EM ANDAMENTO.
  ///
  /// Os tratadores de FIM do arrasto so existem enquanto a condicao do
  /// build vale: `selected && !compact` para mover e para as alcas de
  /// trim, `transition != null` para a transicao. Se a camada e
  /// desselecionada, a timeline vira compacta ou a transicao some NO
  /// MEIO do arrasto, o fim nunca chega — e a timeline fica presa em
  /// "editando barra".
  ///
  /// Presa assim ela deixa de seguir o relogio: o cabecote continua
  /// desenhado no centro, mas o conteudo nao acompanha mais. O keyframe
  /// cravado em seguida nasce no tempo CERTO e aparece longe do
  /// cabecote — que foi o que os testadores relataram ("olha onde eu
  /// coloquei e olha onde ele aparece").
  ///
  /// Este campo fecha o arrasto mesmo quando a condicao que criava o
  /// tratador ja nao existe.
  bool _arrastandoBarra = false;

  /// A barra de informacoes, pega no comeco do arrasto: o fim pode
  /// chegar pelo dispose, onde o `ref` ja nao responde.
  StateController<DadosDaInfobar?>? _infobar;

  void _comecarArrasto() {
    _arrastandoBarra = true;
    _infobar = ref.read(infobarProvider.notifier);
    onEditStart();
  }

  void _terminarArrasto() {
    if (!_arrastandoBarra) return;
    _arrastandoBarra = false;
    _infobar?.state = null;
    _infobar = null;
    onEditEnd();
  }

  /// Onde o item arrastado esta e quanto andou desde que o dedo pegou.
  void _informarTempo(Duration agora, Duration origem) {
    _infobar?.state = DadosDaInfobar.tempo(
      tempo: agora,
      deslocamento: agora - origem,
    );
  }

  @override
  void didUpdateWidget(_AmBar old) {
    super.didUpdateWidget(old);
    if (_arrastandoBarra && !selected) _terminarArrasto();
  }

  @override
  void dispose() {
    // A fileira pode sair da arvore no meio do arrasto (trocar de
    // painel, desfazer): o fim tem de sair mesmo assim.
    _terminarArrasto();
    _encerrarArrastoDoLosango();
    _reconhecedorDoLosango?.dispose();
    super.dispose();
  }

  // ------------------------------------------------------ o losango na mao

  /// SEGURAR E ARRASTAR O LOSANGO.
  ///
  /// "Pressionamos continuamente para o keyframe se mover, e ele fica
  /// parado" (relato do beta): o losango so tinha toque simples, e por ser
  /// opaco por cima da barra o toque longo nao chegava a lugar nenhum.
  ///
  /// O RECONHECEDOR MORA AQUI, e nao no losango. A chave do losango
  /// carrega o tempo dele, entao cada passo do arrasto troca a chave — e um
  /// GestureDetector com outra chave e outro widget: o reconhecedor antigo
  /// seria descartado no meio do gesto, e o arrasto morreria no primeiro
  /// quadro. O losango so entrega o toque ([_pousarNoLosango]); quem segue
  /// o dedo e este reconhecedor, que sobrevive a qualquer reconstrucao.
  ///
  /// E toque LONGO de proposito: deslizar o dedo logo de cara continua
  /// sendo rolar a linha do tempo, o gesto mais usado daqui.
  ///
  /// Criado no primeiro toque num losango: a maioria das barras nunca
  /// recebe um, e as fileiras nascem e morrem a cada rolagem da lista.
  LongPressGestureRecognizer get _seguraOLosango =>
      _reconhecedorDoLosango ??= (LongPressGestureRecognizer(debugOwner: this)
        ..onLongPressStart = _pegarOLosango
        ..onLongPressMoveUpdate = _moverOLosango
        ..onLongPressEnd = ((_) => _soltarOLosango())
        // Chega tanto quando o toque longo perde para a rolagem quanto
        // quando o sistema cancela o dedo com o losango na mao.
        ..onLongPressCancel = _soltarOLosango);
  LongPressGestureRecognizer? _reconhecedorDoLosango;

  /// O losango em que o dedo pousou por ultimo.
  _GrupoDeKeyframes? _losangoSobODedo;

  /// O arrasto em andamento, ou nulo.
  _ArrastoDoLosango? _arrastoDoLosango;

  void _pousarNoLosango(PointerDownEvent evento, _GrupoDeKeyframes grupo) {
    // Um losango na mao por vez: um segundo dedo nao troca o alvo.
    if (_arrastoDoLosango != null) return;
    _losangoSobODedo = grupo;
    _seguraOLosango.addPointer(evento);
  }

  void _pegarOLosango(LongPressStartDetails _) {
    final grupo = _losangoSobODedo;
    if (grupo == null || !mounted || _arrastoDoLosango != null) return;
    // JUNTOS DEMAIS PARA PEGAR UM SO. A pilula junta instantes que caem a
    // menos de 4 px; arrastar levaria qual deles? Aproximar separa.
    if (grupo.times.length > 1) {
      _avisarLosangoParado(
        'Aproxime a linha do tempo para separar os keyframes',
      );
      return;
    }
    final projeto = ref.read(editorControllerProvider);
    final camada = projeto.layerById(layer.id) ?? layer;
    // Bloqueada: nem move, nem apara — nem arrasta keyframe.
    if (projeto.metaOf(camada.id).locked) {
      _avisarLosangoParado(
        'Camada bloqueada: desbloqueie para mover o keyframe',
      );
      return;
    }
    final origem = grupo.times.single;
    final motivo = camada.porQueNaoArrastaKeyframeEm(origem);
    if (motivo == MotivoDoKeyframeParado.modulo) {
      _avisarLosangoParado(
        'Este keyframe anima forma, grade, lente ou cena 3D e ainda não se '
        'arrasta',
        duracao: const Duration(milliseconds: 2800),
      );
      return;
    }
    if (motivo != null) return;

    // OS LIMITES, EM QUADROS DO PROJETO. A marca cai sempre num quadro
    // (e onde o cabecote consegue parar), um quadro depois da vizinha de
    // tras e um antes da da frente — encostar nelas juntaria dois
    // instantes num losango so — e dentro da camada.
    final fps = projeto.fps < 1 ? 30 : projeto.fps;
    double quadroExato(Duration t) => t.inMicroseconds * fps / 1e6;
    Duration? antes;
    Duration? depois;
    for (final t in camada.keyframeTimes) {
      if (t < origem) {
        antes = t;
      } else if (t > origem) {
        depois = t;
        break;
      }
    }
    // A folga de um milesimo de quadro absorve o arredondamento para cima
    // do instante de cada quadro (ver `PlaybackController._naGrade`).
    final inicio = camada.startTime;
    var quadroMin = (quadroExato(inicio) - 1e-3).ceil();
    var quadroMax = (quadroExato(inicio + camada.duration) + 1e-3).floor();
    if (antes != null) {
      final q = (quadroExato(inicio + antes) + 1 - 1e-3).ceil();
      if (q > quadroMin) quadroMin = q;
    }
    if (depois != null) {
      final q = (quadroExato(inicio + depois) - 1 + 1e-3).floor();
      if (q < quadroMax) quadroMax = q;
    }

    HapticFeedback.mediumImpact();
    playback.pause();
    _arrastoDoLosango = _ArrastoDoLosango(
      origem: origem,
      inicioDaCamada: inicio,
      cabecote: playback.time.value,
      quadroMin: quadroMin,
      quadroMax: quadroMax,
      fps: fps,
      controller: ref.read(editorControllerProvider.notifier),
    );
    // A regua para de seguir o relogio enquanto o dedo manda: o cabecote
    // vai acompanhar a marca para a previa mostrar o quadro, e se a regua
    // rolasse junto a marca fugiria do dedo para o centro da tela.
    onEditStart();
    setState(() {});
  }

  void _moverOLosango(LongPressMoveUpdateDetails d) {
    final a = _arrastoDoLosango;
    if (a == null || !mounted || a.quadroMin > a.quadroMax) return;
    final desejado =
        a.inicioDaCamada + a.origem + _pxToDur(d.offsetFromOrigin.dx);
    // IMA DO CABECOTE: o de ANTES do arrasto, que e o traco que a pessoa
    // ve no centro — durante o arrasto o relogio acompanha a marca.
    final pertoDoCabecote =
        (desejado - a.cabecote).abs() <= _pxToDur(_kImaDoCabecotePx);
    final alvo = pertoDoCabecote ? a.cabecote : desejado;
    final quadro = (alvo.inMicroseconds * a.fps / 1e6).round().clamp(
      a.quadroMin,
      a.quadroMax,
    );
    final global = _instanteDoQuadro(quadro, a.fps);
    final encaixou = pertoDoCabecote && global == a.cabecote;
    if (encaixou && !a.presoNoCabecote) HapticFeedback.selectionClick();
    a.presoNoCabecote = encaixou;

    final para = global - a.inicioDaCamada;
    if (para == a.atual) return;
    // UM ARRASTO, UM DESFAZER. O gesto abre no primeiro passo de verdade:
    // segurar e soltar sem mover nao deixa um passo vazio na pilha.
    if (!a.gestoAberto) {
      a.controller.beginGesture();
      a.gestoAberto = true;
    }
    if (a.controller.moverKeyframe(layer.id, a.atual, para) != null) return;
    a.atual = para;
    playback.seek(global);
  }

  void _soltarOLosango() {
    if (_encerrarArrastoDoLosango() && mounted) setState(() {});
  }

  /// Fecha o gesto de desfazer e devolve a regua ao relogio. Sem setState:
  /// tambem roda no dispose. Verdadeiro quando havia um arrasto.
  bool _encerrarArrastoDoLosango() {
    _losangoSobODedo = null;
    final a = _arrastoDoLosango;
    if (a == null) return false;
    _arrastoDoLosango = null;
    if (a.gestoAberto) a.controller.endGesture();
    onEditEnd();
    return true;
  }

  void _avisarLosangoParado(
    String motivo, {
    Duration duracao = const Duration(milliseconds: 2000),
  }) {
    HapticFeedback.lightImpact();
    AureaSnack.show(context, translate(context, motivo), duration: duracao);
  }

  Layer get layer => widget.layer;
  double get pps => widget.pps;
  double get totalWidth => widget.totalWidth;
  bool get selected => widget.selected;
  bool get compact => widget.compact;
  PlaybackController get playback => widget.playback;
  VoidCallback get onEditStart => widget.onEditStart;
  VoidCallback get onEditEnd => widget.onEditEnd;
  void Function(Layer layer)? get onTapLayer => widget.onTapLayer;
  Set<int>? get activeTimesUs => widget.activeTimesUs;
  void Function(Duration)? get onForeignKeyframe => widget.onForeignKeyframe;

  // Arrasto acumulado desde o inicio do gesto: o snap nao "prende" a
  // barra, porque a posicao desejada e recalculada do ponto de origem.
  Duration _dragStart0 = Duration.zero;
  Duration _trimStart0 = Duration.zero;
  double _trimAccumPx = 0;
  Duration _transitionDuration0 = Duration.zero;
  double _transitionAccumPx = 0;

  /// Ultimo alvo de snap: o tique haptico dispara UMA vez por encaixe.
  Duration? _lastSnapTarget;

  void _hapticIfSnapped(Duration desired, Duration snapped) {
    if (snapped == desired) {
      _lastSnapTarget = null;
      return;
    }
    if (_lastSnapTarget != snapped) {
      _lastSnapTarget = snapped;
      HapticFeedback.selectionClick();
    }
  }

  Duration _pxToDur(double px) =>
      Duration(microseconds: (px / pps * 1e6).round());

  /// Snap magnetico: playhead, 0s e bordas das outras camadas.
  Duration _snap(Duration v) {
    final tolUs = (12 / pps * 1e6).round();
    final project = ref.read(editorControllerProvider);
    var best = v;
    var bestD = tolUs + 1;
    void consider(Duration target) {
      final d = (v - target).inMicroseconds.abs();
      if (d < bestD) {
        bestD = d;
        best = target;
      }
    }

    consider(Duration.zero);
    consider(playback.time.value);
    for (final l in project.layers) {
      if (l.id == layer.id) continue;
      consider(l.startTime);
      consider(l.endTime);
    }
    // MARCAS E BATIDAS tambem prendem: e o que faz o corte cair NO
    // tempo, em vez de perto dele.
    for (final m in project.markers) {
      consider(m.time);
    }
    // A grade pode ter milhares de marcas e isto roda a cada quadro de
    // arrasto: busca binaria em vez de varrer a lista inteira.
    final beats = project.beats;
    if (beats.isNotEmpty) {
      var lo = 0;
      var hi = beats.length - 1;
      while (lo < hi) {
        final mid = (lo + hi) ~/ 2;
        if (beats[mid] < v) {
          lo = mid + 1;
        } else {
          hi = mid;
        }
      }
      for (var i = lo - 1; i <= lo + 1; i++) {
        if (i >= 0 && i < beats.length) consider(beats[i]);
      }
    }
    return best;
  }

  /// Ao mover a barra, tanto o INICIO quanto o FIM podem grudar.
  Duration _snapMove(Duration desiredStart) {
    final dur = layer.duration;
    final s1 = _snap(desiredStart);
    final s2 = _snap(desiredStart + dur) - dur;
    final d1 = (s1 - desiredStart).inMicroseconds.abs();
    final d2 = (s2 - desiredStart).inMicroseconds.abs();
    return d1 <= d2 ? s1 : s2;
  }

  @override
  Widget build(BuildContext context) {
    final controller = ref.read(editorControllerProvider.notifier);
    final left = layer.startTime.inMicroseconds / 1e6 * pps;
    final width = (layer.duration.inMicroseconds / 1e6 * pps).clamp(40.0, 1e6);
    final meta = ref.watch(
      editorControllerProvider.select((p) => p.metaOf(layer.id)),
    );
    final hidden = meta.hidden;
    final locked = meta.locked;
    // Bloqueada: nem move, nem apara, nem reordena pelo arrasto.
    final podeMover = selected && !locked;

    return Positioned.fill(
      child: Stack(
        clipBehavior: Clip.none,
        children: [
          Positioned(
            left: left,
            top: 0,
            width: width.toDouble(),
            height: _Alturas.barraDe(context),
            child: GestureDetector(
              key: ValueKey('clip-content-${layer.id}'),
              behavior: HitTestBehavior.opaque,
              onTap: () {
                HapticFeedback.selectionClick();
                // No modo compacto (a timeline de um painel aberto) o
                // toque na barra e a saida do painel: quem decide e o
                // editor, pelo mesmo onTapLayer.
                if (compact) {
                  onTapLayer?.call(layer);
                  return;
                }
                // MODO SELECIONAR: o toque marca e desmarca.
                if (ref.read(modoSelecionarProvider)) {
                  final r = alternarNaSelecao(
                    ref.read(multiSelectProvider),
                    ref.read(selectedLayerProvider),
                    layer.id,
                  );
                  ref.read(multiSelectProvider.notifier).state = r.multi;
                  ref.read(selectedLayerProvider.notifier).state = r.principal;
                  return;
                }
                // Toque simples limpa a selecao multipla.
                ref.read(multiSelectProvider.notifier).state = const {};
                if (ref.read(selectedLayerProvider) == layer.id) {
                  onTapLayer?.call(layer);
                } else {
                  ref.read(selectedLayerProvider.notifier).state = layer.id;
                }
              },
              // TOQUE DUPLO num grupo entra nele (Fase 2). So em grupos:
              // o reconhecedor de toque duplo atrasa o toque simples.
              onDoubleTap: layer is GroupLayer && !compact
                  ? () => controller.enterGroup(layer.id)
                  : null,
              // TOQUE LONGO + ARRASTAR reordena (um degrau por linha);
              // toque longo SEM mover alterna a camada na selecao
              // multipla (a barra de acoes agrupa/duplica/exclui o
              // conjunto).
              onLongPressCancel: _terminarArrasto,
              onLongPressStart: locked
                  ? null
                  : (_) {
                      _moveuNoToqueLongo = false;
                      _ultimoDyLongo = 0;
                      _acumuladoVertical = 0;
                      _eixoDoArrasto = null;
                      _dragStart0 = layer.startTime;
                      HapticFeedback.mediumImpact();
                    },
              onLongPressMoveUpdate: locked
                  ? null
                  : (d) {
                      final dy = d.offsetFromOrigin.dy;
                      if (!_moveuNoToqueLongo &&
                          d.offsetFromOrigin.distance < 8) {
                        return;
                      }
                      if (locked) return;
                      if (!_moveuNoToqueLongo) _comecarArrasto();
                      _moveuNoToqueLongo = true;
                      // O EIXO SE DECIDE UMA VEZ, no primeiro movimento.
                      //
                      // Era decidido a CADA atualizacao: um tremor do
                      // dedo no meio de uma subida vertical virava um
                      // deslocamento no tempo, e a camada que so devia
                      // trocar de degrau saia do lugar. "Arrastar a
                      // camada sem modificar a posicao" e o que a
                      // referencia faz — e o que a trava garante.
                      _eixoDoArrasto ??=
                          d.offsetFromOrigin.dx.abs() > dy.abs() || compact
                          ? _EixoDoArrasto.tempo
                          : _EixoDoArrasto.pilha;
                      if (_eixoDoArrasto == _EixoDoArrasto.tempo) {
                        final desired =
                            _dragStart0 + _pxToDur(d.offsetFromOrigin.dx);
                        final novo = _snapMove(desired);
                        controller.moveLayer(layer.id, novo);
                        _informarTempo(novo, _dragStart0);
                      } else {
                        _reordenarPorArrasto(dy - _ultimoDyLongo);
                      }
                      _ultimoDyLongo = dy;
                    },
              onLongPressEnd: locked
                  ? null
                  : (_) {
                      _terminarArrasto();
                      if (!_moveuNoToqueLongo && !compact) {
                        _alternarNaSelecaoMultipla();
                      }
                    },
              child: compact
                  ? Container(
                      decoration: BoxDecoration(
                        color: Colors.white,
                        borderRadius: BorderRadius.circular(kAmBarHeight / 2),
                      ),
                      child: Row(
                        children: [
                          GestureDetector(
                            behavior: HitTestBehavior.opaque,
                            onTap: () {
                              HapticFeedback.selectionClick();
                              controller.selectNeighbor(1);
                            },
                            child: const SizedBox(
                              width: 22,
                              height: kAmBarHeight,
                              child: Icon(
                                CupertinoIcons.chevron_left,
                                size: 15,
                                color: Color(0xFF1E222D),
                              ),
                            ),
                          ),
                          Expanded(
                            child: Container(
                              height: kAmBarHeight - 4,
                              decoration: BoxDecoration(
                                color: const Color(0xFF38B6AB),
                                borderRadius: BorderRadius.circular(4),
                              ),
                              alignment: Alignment.center,
                              padding: const EdgeInsets.symmetric(
                                horizontal: 6,
                              ),
                              child: AppText(
                                layer.name,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                style: const TextStyle(
                                  fontSize: 12,
                                  fontWeight: FontWeight.w700,
                                  letterSpacing: -0.1,
                                  color: Colors.white,
                                ),
                              ),
                            ),
                          ),
                          GestureDetector(
                            behavior: HitTestBehavior.opaque,
                            onTap: () {
                              HapticFeedback.selectionClick();
                              controller.selectNeighbor(-1);
                            },
                            child: const SizedBox(
                              width: 22,
                              height: kAmBarHeight,
                              child: Icon(
                                CupertinoIcons.chevron_right,
                                size: 15,
                                color: Color(0xFF1E222D),
                              ),
                            ),
                          ),
                        ],
                      ),
                    )
                  : CustomPaint(
                      painter: _AmBarPainter(
                        selected: selected,
                        hidden: hidden,
                        color: layerTypeColor(layer),
                      ),
                      foregroundPainter: _AmBarFrentePainter(
                        selected: selected,
                        hidden: hidden,
                        color: layerTypeColor(layer),
                      ),
                      // FORMA DE ONDA e TIRA DE MINIATURAS dentro da barra:
                      // sem elas, achar o corte e tatear.
                      child: _ClipPreview(
                        layer: layer,
                        // CLIPE CURTO: um pedaco de 266 ms tem 20 px de barra. O
                        // conteudo some por ordem de importancia em vez de
                        // estourar a linha (o projeto importado esta cheio deles).
                        child: ClipRect(
                          child: Padding(
                            padding: EdgeInsets.fromLTRB(
                              // A faixa de cor ocupa os primeiros 4 px.
                              width < 46 ? 7 : 14,
                              0,
                              width < 46 ? 3 : 10,
                              // O espaco de baixo e dos keyframes.
                              kAmFaixaKeyframes,
                            ),
                            child: Row(
                              children: [
                                // ICONE DO TIPO na ponta: reconhecer sem ler.
                                if (width > 28)
                                  Padding(
                                    padding: const EdgeInsets.only(right: 6),
                                    child: Icon(
                                      layerTypeIcon(layer),
                                      size: 11,
                                      color: Colors.white.withValues(
                                        alpha: hidden ? 0.45 : 0.85,
                                      ),
                                    ),
                                  ),
                                if (locked && width > 70)
                                  const Padding(
                                    padding: EdgeInsets.only(right: 5),
                                    child: Icon(
                                      CupertinoIcons.lock_fill,
                                      size: 10,
                                      color: Colors.white70,
                                    ),
                                  ),
                                if (width > 52)
                                  Flexible(
                                    child: AppText(
                                      layer.name,
                                      maxLines: 1,
                                      overflow: TextOverflow.ellipsis,
                                      style: const TextStyle(
                                        fontSize: 12,
                                        fontWeight: FontWeight.w600,
                                        letterSpacing: -0.1,
                                        color: Colors.white,
                                      ),
                                    ),
                                  ),
                                if (layer.hasAnimation && width > 120) ...[
                                  const SizedBox(width: 6),
                                  const Icon(
                                    CupertinoIcons.rhombus,
                                    size: 12,
                                    color: Colors.white,
                                  ),
                                ],
                                // GRUPO: a contagem de filhos, e o toque duplo
                                // entra.
                                if (layer is GroupLayer && width > 100) ...[
                                  const SizedBox(width: 6),
                                  Container(
                                    key: ValueKey('grupo-contagem-${layer.id}'),
                                    padding: const EdgeInsets.symmetric(
                                      horizontal: 5,
                                      vertical: 1,
                                    ),
                                    decoration: BoxDecoration(
                                      color: Colors.black.withValues(
                                        alpha: .35,
                                      ),
                                      borderRadius: BorderRadius.circular(6),
                                    ),
                                    child: AppText(
                                      '${(layer as GroupLayer).children.length}',
                                      style: const TextStyle(
                                        color: Colors.white,
                                        fontSize: 9.5,
                                        fontWeight: FontWeight.w800,
                                      ),
                                    ),
                                  ),
                                ],
                                const Spacer(),
                                if (!compact && width > 150)
                                  const Icon(
                                    CupertinoIcons.line_horizontal_3,
                                    size: 14,
                                    color: Colors.white70,
                                  ),
                              ],
                            ),
                          ),
                        ),
                      ),
                    ),
            ),
          ),
          // A juncao e o botao da TRANSICAO. Toque longo preserva o gesto
          // antigo de juntar novamente dois pedacos da mesma fonte.
          Consumer(
            builder: (context, ref, _) {
              final ctrl = ref.read(editorControllerProvider.notifier);
              if (!isTransitionLayer(layer) ||
                  ctrl.clipAfter(layer.id) == null) {
                return const SizedBox.shrink();
              }
              final transition = ctrl.transitionAfter(layer.id);
              final x = left + (layer.duration.inMicroseconds / 1e6 * pps);
              final markerWidth = transition == null ? 40.0 : 66.0;
              return Positioned(
                left: x - markerWidth / 2,
                top: 0,
                width: markerWidth,
                height: _Alturas.barraDe(context),
                child: GestureDetector(
                  behavior: HitTestBehavior.opaque,
                  onTap: () => showTransitionSheet(context, ref, layer.id),
                  onLongPress: ctrl.hasJoinableNeighbour(layer.id)
                      ? () {
                          if (ctrl.joinWithNeighbour(layer.id)) {
                            AureaSnack.show(
                              context,
                              'Pedacos juntados',
                              actionLabel: 'Desfazer',
                              onAction: ctrl.undo,
                            );
                          }
                        }
                      : null,
                  onHorizontalDragStart: transition == null
                      ? null
                      : (_) {
                          _comecarArrasto();
                          _transitionDuration0 = transition.duration;
                          _transitionAccumPx = 0;
                        },
                  onHorizontalDragUpdate: transition == null
                      ? null
                      : (details) {
                          _transitionAccumPx += details.delta.dx;
                          final delta = _pxToDur(_transitionAccumPx);
                          var duration = _transitionDuration0 + delta;
                          if (duration < Duration.zero) {
                            duration = Duration.zero;
                          }
                          if (duration > const Duration(milliseconds: 2500)) {
                            duration = const Duration(milliseconds: 2500);
                          }
                          ctrl.setTransitionDuration(layer.id, duration);
                        },
                  onHorizontalDragEnd: transition == null
                      ? null
                      : (_) => _terminarArrasto(),
                  onHorizontalDragCancel: transition == null
                      ? null
                      : _terminarArrasto,
                  child: Center(
                    child: Container(
                      key: ValueKey('level5-transition-${layer.id}'),
                      constraints: const BoxConstraints(minWidth: 4),
                      height: _Alturas.barraDe(context) - 10,
                      padding: transition == null
                          ? EdgeInsets.zero
                          : const EdgeInsets.symmetric(horizontal: 5),
                      decoration: BoxDecoration(
                        color: AmColors.accent.withValues(alpha: 0.85),
                        borderRadius: BorderRadius.circular(6),
                      ),
                      alignment: Alignment.center,
                      child: transition == null
                          ? const Icon(
                              CupertinoIcons.timer,
                              size: 12,
                              color: Colors.black,
                            )
                          : AppText(
                              '${transition.type.shortLabel} '
                              '${transition.duration.inMilliseconds}ms',
                              maxLines: 1,
                              style: const TextStyle(
                                color: Colors.black,
                                fontSize: 8,
                                fontWeight: FontWeight.w800,
                              ),
                            ),
                    ),
                  ),
                ),
              );
            },
          ),
          // JUNTAR a vista na juncao: dois pedacos do mesmo arquivo,
          // encostados, voltam a ser um clipe (alem do toque longo).
          if (podeMover && controller.hasJoinableNeighbour(layer.id))
            Positioned(
              left: left + width - (layer is VideoLayer ? 14 : 3) - 50,
              top: 5,
              width: 48,
              height: _Alturas.barraDe(context) - 10,
              child: GestureDetector(
                key: ValueKey('juntar-${layer.id}'),
                behavior: HitTestBehavior.opaque,
                onTap: () {
                  HapticFeedback.lightImpact();
                  if (controller.joinWithNeighbour(layer.id)) {
                    AureaSnack.show(
                      context,
                      'Pedacos juntados',
                      actionLabel: 'Desfazer',
                      onAction: controller.undo,
                    );
                  }
                },
                child: Container(
                  alignment: Alignment.center,
                  decoration: BoxDecoration(
                    color: AmColors.action,
                    borderRadius: BorderRadius.circular(6),
                  ),
                  child: const AppText('Juntar',
                    style: TextStyle(
                      color: AmColors.onAction,
                      fontSize: 9.5,
                      fontWeight: FontWeight.w800,
                    ),
                  ),
                ),
              ),
            ),
          // Cues de legenda como marcas dentro da barra (§6.5).
          if (layer is CaptionLayer)
            for (final cue in (layer as CaptionLayer).cues)
              Positioned(
                left: left + (cue.start.inMicroseconds / 1e6 * pps),
                top: 6,
                width: ((cue.end - cue.start).inMicroseconds / 1e6 * pps).clamp(
                  3.0,
                  1e6,
                ),
                height: _Alturas.barraDe(context) - 12,
                child: IgnorePointer(
                  child: Container(
                    decoration: BoxDecoration(
                      color: Colors.white.withValues(alpha: 0.35),
                      borderRadius: BorderRadius.circular(3),
                    ),
                  ),
                ),
              ),
          // Alcas de trim (com snap magnetico).
          if (podeMover && !compact) ...[
            _TrimHandle(
              left: left - 3,
              onStart: () {
                _comecarArrasto();
                _trimStart0 = layer.startTime;
                _trimAccumPx = 0;
              },
              onEnd: _terminarArrasto,
              onDrag: (dx) {
                _trimAccumPx += dx;
                final desired = _trimStart0 + _pxToDur(_trimAccumPx);
                final snapped = _snap(desired);
                _hapticIfSnapped(desired, snapped);
                controller.trimLayerStart(layer.id, snapped);
                _informarTempo(snapped, _trimStart0);
              },
            ),
            _TrimHandle(
              left: left + width - 13,
              onStart: () {
                _comecarArrasto();
                _trimStart0 = layer.endTime;
                _trimAccumPx = 0;
              },
              onEnd: _terminarArrasto,
              onDrag: (dx) {
                _trimAccumPx += dx;
                final desired = _trimStart0 + _pxToDur(_trimAccumPx);
                final snapped = _snap(desired);
                _hapticIfSnapped(desired, snapped);
                controller.trimLayerEnd(layer.id, snapped);
                _informarTempo(snapped, _trimStart0);
              },
            ),
          ],
          // Diamantes de keyframe sobre a barra: acesos = propriedade
          // ativa; translúcidos = de outra propriedade, ainda visíveis.
          // FAIXA DE KEYFRAMES COM DENSIDADE.
          for (final grupo in _gruposDosLosangos())
            _losango(context, grupo, left),
          // O LOSANGO NA MAO vem por ultimo, por cima dos outros, e fora do
          // agrupamento: a um quadro da vizinha ele fica a menos de 4 px
          // dela, e sumiria dentro da pilula bem na hora de encaixar.
          if (_arrastoDoLosango case final arrasto?) ...[
            _losango(
              context,
              _GrupoDeKeyframes([arrasto.atual]),
              left,
              arrastando: true,
            ),
            _rotuloDoArrasto(context, arrasto, left),
          ],
        ],
      ),
    );
  }

  /// Os grupos de losangos a desenhar — sem o que esta na mao.
  List<_GrupoDeKeyframes> _gruposDosLosangos() {
    final arrasto = _arrastoDoLosango;
    final tempos = layer.keyframeTimes;
    return _agrupaKeyframes(
      arrasto == null
          ? tempos
          : [
              for (final t in tempos)
                if (t != arrasto.atual) t,
            ],
      pps,
    );
  }

  /// UM LOSANGO (ou a pilula de varios juntos demais) na faixa de baixo da
  /// barra. [arrastando]: o que esta na mao — maior, e sem toque proprio,
  /// porque quem segue o dedo e [_seguraOLosango].
  Widget _losango(
    BuildContext context,
    _GrupoDeKeyframes grupo,
    double left, {
    bool arrastando = false,
  }) {
    final active =
        activeTimesUs == null ||
        grupo.times.any((t) => activeTimesUs!.contains(t.inMicroseconds));
    // Dourado/Âmbar brilhante estilo After Effects para keyframes ativos; branco nítido para inativos
    final cor = active
        ? const Color(0xFFFFC107)
        : Colors.white.withValues(alpha: 0.90);
    final x0 = grupo.times.first.inMicroseconds / 1e6 * pps;
    final x1 = grupo.times.last.inMicroseconds / 1e6 * pps;

    final Widget marca = grupo.times.length == 1
        ? Transform.rotate(
            angle: 0.785398,
            child: Container(
              width: 11,
              height: 11,
              decoration: BoxDecoration(
                color: cor,
                borderRadius: BorderRadius.circular(2),
                border: Border.all(
                  color: Colors.black.withValues(alpha: 0.85),
                  width: 1.2,
                ),
                boxShadow: [
                  BoxShadow(
                    color: (active ? const Color(0xFFFFC107) : Colors.black)
                        .withValues(alpha: active ? 0.5 : 0.3),
                    blurRadius: 3,
                    spreadRadius: 0.5,
                  ),
                ],
              ),
            ),
          )
        : Container(
            height: 10,
            decoration: BoxDecoration(
              color: cor,
              borderRadius: BorderRadius.circular(5),
              border: Border.all(
                color: Colors.black.withValues(alpha: 0.85),
                width: 1.2,
              ),
            ),
          );

    final largura = grupo.times.length == 1 ? 28.0 : (x1 - x0) + 28;
    final Widget alvo = GestureDetector(
      key: ValueKey(
        'layer-keyframe-${layer.id}-${grupo.times.first.inMicroseconds}',
      ),
      behavior: HitTestBehavior.opaque,
      onTap: () {
        HapticFeedback.selectionClick();
        playback.pause();
        playback.seek(layer.startTime + grupo.times.first);
        if (active) {
          // Tocar o losango leva ao easing dele ou abre curva
          widget.onKeyframeTap?.call(layer, grupo.times.first);
        } else {
          onForeignKeyframe?.call(grupo.times.first);
        }
      },
      child: Center(
        child: SizedBox(
          key: ValueKey(
            'keyframe-glyph-${layer.id}-${grupo.times.first.inMicroseconds}',
          ),
          width: grupo.times.length == 1
              ? 16
              : (x1 - x0).clamp(16.0, double.infinity),
          height: 16,
          // NA MAO, O LOSANGO CRESCE: e o sinal de que o toque longo pegou,
          // visivel mesmo com o dedo em cima.
          child: Center(
            child: arrastando
                ? Transform.scale(scale: 1.4, child: marca)
                : marca,
          ),
        ),
      ),
    );
    return Positioned(
      left: left + x0 - 14,
      top: _topoDoLosango(context),
      width: largura,
      height: compact ? 16 : kAmFaixaKeyframes + 2,
      child: arrastando
          ? IgnorePointer(child: alvo)
          // O toque simples continua no GestureDetector; o pouso do dedo
          // tambem vai para o reconhecedor do toque longo, que mora no
          // State e sobrevive a troca de chave do losango.
          : Listener(
              onPointerDown: (evento) => _pousarNoLosango(evento, grupo),
              child: alvo,
            ),
    );
  }

  // No COMPACTO o losango desce para a borda de baixo da pilula: no
  // meio ele caia EM CIMA do nome ("Retangulo◆redondado", print do
  // beta) e a barra parecia mudar de formato do nada.
  double _topoDoLosango(BuildContext context) => compact
      ? kAmBarHeight - 13
      : _Alturas.barraDe(context) - kAmFaixaKeyframes - 1;

  /// O TEMPO DA MARCA NA MAO, num rotulo pequeno: o dedo cobre o losango e
  /// a regua, e soltar no quadro certo depende de ler onde ele esta. Fica
  /// acima do losango quando cabe; na faixa compacta (sem espaco em cima,
  /// onde a lista recorta) vai para o lado.
  Widget _rotuloDoArrasto(
    BuildContext context,
    _ArrastoDoLosango arrasto,
    double left,
  ) {
    final x = left + arrasto.atual.inMicroseconds / 1e6 * pps;
    final topo = _topoDoLosango(context);
    final acima = topo >= 16;
    return Positioned(
      key: ValueKey('keyframe-drag-label-${layer.id}'),
      left: acima ? x - 40 : x + 10,
      top: acima ? topo - 16 : topo,
      width: 80,
      height: 16,
      child: IgnorePointer(
        child: Align(
          alignment: acima ? Alignment.center : Alignment.centerLeft,
          child: DecoratedBox(
            decoration: BoxDecoration(
              color: Colors.black.withValues(alpha: 0.82),
              borderRadius: BorderRadius.circular(4),
            ),
            child: Padding(
              padding: const EdgeInsets.symmetric(horizontal: 5, vertical: 1),
              child: AppText(
                formatTimecode(
                  arrasto.inicioDaCamada + arrasto.atual,
                  arrasto.fps,
                ),
                maxLines: 1,
                style: const TextStyle(
                  fontSize: 10,
                  fontWeight: FontWeight.w700,
                  color: Colors.white,
                  fontFeatures: [FontFeature.tabularFigures()],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

/// Um punhado de keyframes perto demais para se distinguirem.
class _GrupoDeKeyframes {
  const _GrupoDeKeyframes(this.times);
  final List<Duration> times;
}

/// A que distancia do cabecote, em pixels, o losango arrastado gruda nele.
const double _kImaDoCabecotePx = 8;

/// O INSTANTE DO QUADRO [quadro], arredondado para cima — a mesma conta do
/// `PlaybackController`, para a marca cair onde o cabecote consegue parar.
Duration _instanteDoQuadro(int quadro, int fps) =>
    Duration(microseconds: (quadro * 1000000 / fps).ceil());

/// UM LOSANGO NA MAO: o que o arrasto lembra entre um passo do dedo e o
/// seguinte.
class _ArrastoDoLosango {
  _ArrastoDoLosango({
    required this.origem,
    required this.inicioDaCamada,
    required this.cabecote,
    required this.quadroMin,
    required this.quadroMax,
    required this.fps,
    required this.controller,
  }) : atual = origem;

  /// Onde a marca estava quando o dedo a pegou, no tempo da camada. Cada
  /// passo parte daqui, e nao do passo anterior: o ima nao prende o dedo.
  final Duration origem;

  /// Onde a marca esta agora, no tempo da camada.
  Duration atual;

  final Duration inicioDaCamada;

  /// O cabecote de ANTES do arrasto, no tempo do projeto.
  final Duration cabecote;

  /// Os quadros do projeto em que a marca pode cair (inclusive).
  final int quadroMin;
  final int quadroMax;
  final int fps;

  /// Guardado no comeco: fechar o gesto tem de funcionar ate no dispose,
  /// quando o `ref` ja nao pode ser lido.
  final EditorController controller;

  /// O passo de desfazer ja foi aberto ([EditorController.beginGesture]).
  bool gestoAberto = false;

  /// O cabecote esta prendendo a marca: o toque haptico sai uma vez por
  /// encaixe, e nao a cada pixel.
  bool presoNoCabecote = false;
}

/// Junta os keyframes que ficariam a menos de [minPx] um do outro.
List<_GrupoDeKeyframes> _agrupaKeyframes(
  List<Duration> times,
  double pps, {
  double minPx = 4,
}) {
  if (times.isEmpty) return const [];
  final ordenados = [...times]..sort();
  final out = <_GrupoDeKeyframes>[];
  var atual = <Duration>[ordenados.first];
  for (var i = 1; i < ordenados.length; i++) {
    final dx = (ordenados[i] - atual.last).inMicroseconds / 1e6 * pps;
    if (dx < minPx) {
      atual.add(ordenados[i]);
    } else {
      out.add(_GrupoDeKeyframes(atual));
      atual = <Duration>[ordenados[i]];
    }
  }
  out.add(_GrupoDeKeyframes(atual));
  return out;
}

/// O FUNDO DA BARRA: a cor do tipo assentada sobre o painel, e o trilho
/// dos keyframes um tom mais escuro embaixo.
///
/// O desenho antigo era um bloco chapado da cor do tipo, com listras
/// diagonais quando selecionado. Dez faixas viravam dez blocos saturados
/// brigando entre si, e a listra passava por cima do nome. Agora o corpo
/// e escuro e a cor do tipo vira uma faixa fina na borda esquerda
/// (pintada por cima, no [_AmBarFrentePainter], para aparecer mesmo
/// sobre as miniaturas): o que se destaca e o nome, a onda e a imagem.
class _AmBarPainter extends CustomPainter {
  const _AmBarPainter({
    required this.selected,
    required this.hidden,
    required this.color,
  });

  final bool selected;
  final bool hidden;

  /// A cor DO TIPO da camada: e o que deixa achar o audio no meio dos
  /// videos sem ler dez rotulos.
  final Color color;

  // Objetos de pintura reaproveitados: `paint` roda a cada quadro
  // enquanto a linha rola, e alocar aqui dentro e lixo por quadro.
  static final Paint _fundo = Paint();
  static final Paint _trilho = Paint()
    ..color = Colors.black.withValues(alpha: 0.22);

  @override
  void paint(Canvas canvas, Size size) {
    Perfil3D.fase('pintar.barra.fundo', () => _pintar(canvas, size));
  }

  void _pintar(Canvas canvas, Size size) {
    final rrect = RRect.fromRectAndRadius(
      Offset.zero & size,
      const Radius.circular(8),
    );
    final corpo = Color.lerp(
      AmColors.panelHigh,
      color,
      hidden ? 0.22 : (selected ? 0.66 : 0.46),
    )!;
    canvas.drawRRect(rrect, _fundo..color = corpo);
    canvas.save();
    canvas.clipRRect(rrect);
    // O TRILHO dos keyframes: a faixa de baixo, onde os losangos moram.
    canvas.drawRect(
      Rect.fromLTWH(
        0,
        size.height - kAmFaixaKeyframes,
        size.width,
        kAmFaixaKeyframes,
      ),
      _trilho,
    );
    canvas.restore();
  }

  @override
  bool shouldRepaint(_AmBarPainter old) =>
      old.selected != selected || old.hidden != hidden || old.color != color;
}

/// A FRENTE DA BARRA, por cima das miniaturas e da onda: a faixa da cor
/// do tipo na esquerda, um fio de luz no alto e o contorno branco da
/// selecao — o que tem de aparecer mesmo quando o clipe e uma imagem.
class _AmBarFrentePainter extends CustomPainter {
  const _AmBarFrentePainter({
    required this.selected,
    required this.hidden,
    required this.color,
  });

  final bool selected;
  final bool hidden;
  final Color color;

  static final Paint _faixa = Paint();
  static final Paint _fio = Paint()..strokeWidth = 1;
  static final Paint _contorno = Paint()
    ..style = PaintingStyle.stroke
    ..strokeWidth = 1.5
    ..color = Colors.white;

  @override
  void paint(Canvas canvas, Size size) {
    Perfil3D.fase('pintar.barra.frente', () => _pintar(canvas, size));
  }

  void _pintar(Canvas canvas, Size size) {
    final rrect = RRect.fromRectAndRadius(
      Offset.zero & size,
      const Radius.circular(8),
    );
    canvas.save();
    canvas.clipRRect(rrect);
    canvas.drawRect(
      Rect.fromLTWH(0, 0, 4, size.height),
      _faixa..color = hidden ? color.withValues(alpha: 0.5) : color,
    );
    canvas.drawLine(
      const Offset(4, 0.5),
      Offset(size.width, 0.5),
      _fio..color = Colors.white.withValues(alpha: selected ? 0.24 : 0.10),
    );
    canvas.restore();
    if (selected) canvas.drawRRect(rrect.deflate(0.75), _contorno);
  }

  @override
  bool shouldRepaint(_AmBarFrentePainter old) =>
      old.selected != selected || old.hidden != hidden || old.color != color;
}

class _TrimHandle extends StatelessWidget {
  const _TrimHandle({
    required this.left,
    required this.onDrag,
    required this.onStart,
    required this.onEnd,
  });

  final double left;
  final ValueChanged<double> onDrag;
  final VoidCallback onStart;
  final VoidCallback onEnd;

  @override
  Widget build(BuildContext context) {
    return Positioned(
      left: left,
      top: 2,
      height: _Alturas.barraDe(context) - 4,
      width: 16,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onHorizontalDragStart: (_) {
          HapticFeedback.lightImpact();
          onStart();
        },
        onHorizontalDragUpdate: (d) => onDrag(d.delta.dx),
        onHorizontalDragEnd: (_) {
          HapticFeedback.lightImpact();
          onEnd();
        },
        onHorizontalDragCancel: () {
          HapticFeedback.lightImpact();
          onEnd();
        },
        child: Container(
          decoration: BoxDecoration(
            color: const Color(0xFFF2F5F9),
            borderRadius: BorderRadius.circular(4),
          ),
          child: const Center(
            child: SizedBox(
              width: 2,
              height: 14,
              child: ColoredBox(color: Colors.black38),
            ),
          ),
        ),
      ),
    );
  }
}

/// PREVIA DENTRO DA BARRA: forma de onda para audio, tira de
/// miniaturas para video.
///
/// As duas sao caras de calcular, entao sao pedidas uma vez e chegam
/// depois — a barra aparece na hora e ganha a previa quando fica pronta,
/// em vez de segurar a interface esperando o FFmpeg.
class _ClipPreview extends StatefulWidget {
  const _ClipPreview({required this.layer, required this.child});

  final Layer layer;
  final Widget child;

  @override
  State<_ClipPreview> createState() => _ClipPreviewState();
}

class _ClipPreviewState extends State<_ClipPreview> {
  final _service = MediaPreviewService.instance;

  @override
  void initState() {
    super.initState();
    _pedir();
  }

  @override
  void didUpdateWidget(_ClipPreview old) {
    super.didUpdateWidget(old);
    if (old.layer.id != widget.layer.id ||
        old.layer.duration != widget.layer.duration ||
        old.layer is VideoLayer &&
            widget.layer is VideoLayer &&
            (old.layer as VideoLayer).sourceOffset !=
                (widget.layer as VideoLayer).sourceOffset) {
      _pedir();
    }
  }

  void _pedir() {
    final l = widget.layer;
    if (l is AudioLayer) {
      _service.ensureWaveform(l.sourcePath);
    } else if (l is VideoLayer) {
      _service.ensureWaveform(l.sourcePath);
      // A TIRA COBRE O ARQUIVO INTEIRO quando a duracao e conhecida: a
      // primeira barra a pedir definia a janela, e um video longo so
      // mostrava quadros do comeco.
      _service.ensureFilmstrip(
        l.sourcePath,
        l.sourceDuration ?? l.sourceOffset + videoSourceSpan(l),
      );
      // PROXY: pedido daqui porque a barra do clipe sempre monta —
      // pendurar no caminho de sincronia do player era fragil, ele so
      // roda quando o relogio anda.
      ProxyService.instance.ensureProxy(l.sourcePath);
    }
  }

  /// O instante do arquivo ao longo da barra, calculado uma vez por
  /// instancia da camada (a camada e imutavel: editou, e outra).
  static final Expando<Float64List> _fontes = Expando('fonte-da-onda');

  @override
  Widget build(BuildContext context) {
    final l = widget.layer;
    if (l is! AudioLayer && l is! VideoLayer) return widget.child;

    final path = l is AudioLayer ? l.sourcePath : (l as VideoLayer).sourcePath;
    final inicio = l is VideoLayer
        ? l.sourceOffset
        : (l as AudioLayer).sourceOffset;
    final fim =
        inicio +
        (l is VideoLayer ? videoSourceSpan(l) : (l as AudioLayer).sourceSpan);
    final fonte = _fontes[l] ??= fonteAoLongoDoClipe(l);
    final barra = _Alturas.barraDe(context);
    final alta = barra >= 56;
    final mudo = l is VideoLayer
        ? (l.volume <= 0 || l.audio.muted)
        : (l as AudioLayer).volume <= 0 || l.audio.muted;

    return ValueListenableBuilder<int>(
      valueListenable: _service.revision,
      builder: (context, _, child) {
        final piramide = _service.pyramidOf(path);
        final strip = l is VideoLayer ? _service.stripOf(path) : null;
        final temStrip = strip != null && strip.isNotEmpty;
        final temOnda = piramide != null && !piramide.isEmpty;
        final alturaOnda = l is AudioLayer || !temStrip
            ? barra
            : (alta ? barra * 0.58 : barra * 0.5);

        return ClipRRect(
          borderRadius: BorderRadius.circular(8),
          child: Stack(
            fit: StackFit.passthrough,
            children: [
              // MINIATURAS: em cima (barra alta) ou por baixo de tudo.
              if (temStrip)
                Positioned(
                  left: 0,
                  right: 0,
                  top: 0,
                  height: temOnda && alta ? barra - alturaOnda : barra,
                  child: CustomPaint(
                    painter: FilmstripPainter(
                      frames: strip,
                      start: inicio,
                      end: fim,
                      sourceDuration: (l as VideoLayer).sourceDuration ?? fim,
                    ),
                  ),
                ),
              if (temStrip)
                Positioned.fill(
                  child: DecoratedBox(
                    decoration: BoxDecoration(
                      gradient: LinearGradient(
                        begin: Alignment.centerLeft,
                        end: Alignment.centerRight,
                        colors: [
                          Colors.black.withValues(alpha: 0.62),
                          Colors.black.withValues(alpha: 0.22),
                        ],
                        stops: const [0.0, 0.45],
                      ),
                    ),
                  ),
                ),
              // A ONDA DO SOM: faixa escura propria, espelhada, em dB, e
              // seguindo o instante real do arquivo (corte, velocidade,
              // reverso, Time Remap).
              if (temOnda)
                Positioned(
                  key: ValueKey('onda-${l.id}'),
                  left: 0,
                  right: 0,
                  bottom: 0,
                  height: alturaOnda,
                  child: DecoratedBox(
                    decoration: BoxDecoration(
                      color: temStrip
                          ? const Color(0xE60B0E12)
                          : Colors.transparent,
                    ),
                    child: CustomPaint(
                      painter: ClipWaveformPainter(
                        pyramid: piramide,
                        fonte: fonte,
                        color: temStrip
                            ? AmColors.accent
                            : Colors.white.withValues(alpha: 0.92),
                        contorno: temStrip
                            ? const Color(0xFFB9FFF0)
                            : Colors.white,
                        gain: _service.ganhoDaOnda(path),
                        muted: mudo,
                      ),
                    ),
                  ),
                ),
              child!,
            ],
          ),
        );
      },
      child: widget.child,
    );
  }
}
