import 'package:flutter/cupertino.dart';
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
import '../../domain/video_project.dart' as proj;
import '../../application/media_preview_service.dart';
import '../../application/proxy_service.dart';
import 'am_colors.dart';
import 'layer_look.dart';
import 'clip_preview_painters.dart';
import 'freeze_sheet.dart';
import 'transition_sheet.dart';

// Mais baixas do que eram (46/38): num celular, tres camadas ja
// tomavam a tela; e o que se le numa barra e nome e keyframe.
const double kAmRowHeight = 38;
const double kAmBarHeight = 30;

/// TIMELINE MAGNETICA — ligada por padrao.
///
/// Ligada: excluir fecha o buraco e o que vinha depois encosta. Sem
/// isso, apagar um pedaco deixa um vazio que a pessoa arruma na mao — e
/// era a reclamacao "corta, apaga e fica um buraco".
///
/// Desligada: cada clipe tem posicao livre, e excluir deixa o buraco.
/// E o que se quer quando outra trilha precisa continuar no mesmo lugar.
final magneticProvider = StateProvider<bool>((ref) => true);

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
    this.activeTimesUs,
    this.onScrub,
    this.onForeignKeyframe,
  });

  final PlaybackController playback;
  final String? singleLayerId;
  final Color playheadColor;
  final double height;
  final void Function(Layer layer)? onTapLayer;

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
    if (melhor != null && melhor != t) widget.playback.seek(melhor);
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
  Widget build(BuildContext context) {
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
    final totalWidth = _timeToPx(project.duration);
    final selected = selectedId == null ? null : project.layerById(selectedId);
    final keyCount = selected?.keyframeTimes.length ?? 0;
    if (_revealedSelection != selectedId ||
        _revealedKeyCount != keyCount ||
        _revealedSingleLayer != widget.singleLayerId) {
      _revealedSelection = selectedId;
      _revealedKeyCount = keyCount;
      _revealedSingleLayer = widget.singleLayerId;
      final index = layers.indexWhere((l) => l.id == selectedId);
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (!mounted ||
            !_rowsScroll.hasClients ||
            index < 0 ||
            _revealedSelection != selectedId) {
          return;
        }
        final top = index * kAmRowHeight;
        final position = _rowsScroll.position;
        final bottom = top + kAmRowHeight;
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

    return SizedBox(
      height: widget.height,
      child: LayoutBuilder(
        builder: (context, constraints) {
          final pad = constraints.maxWidth / 2;
          return GestureDetector(
            onScaleStart: (d) {
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
                                      onDoubleTap: () {
                                        final t = Duration(
                                          microseconds:
                                              (_xDoDuploToque / _pps * 1e6)
                                                  .round(),
                                        );
                                        ref
                                            .read(
                                              editorControllerProvider.notifier,
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
                                                      (dx / _pps * 1e6).round(),
                                                ),
                                          ),
                                      onMenu: () =>
                                          _menuDaMarca(context, ref, m),
                                    ),
                                ],
                              ),
                            ),
                            const SizedBox(height: 30),
                            Expanded(
                              child: ListView.builder(
                                controller: _rowsScroll,
                                padding: EdgeInsets.zero,
                                itemExtent: kAmRowHeight,
                                itemCount: layers.length,
                                itemBuilder: (context, index) {
                                  final layer = layers[index];
                                  return _AmLayerRow(
                                    key: ValueKey(layer.id),
                                    layer: layer,
                                    pps: _pps,
                                    totalWidth: totalWidth,
                                    selected:
                                        layer.id == selectedId ||
                                        multi.contains(layer.id) ||
                                        widget.singleLayerId != null,
                                    compact: widget.singleLayerId != null,
                                    playback: widget.playback,
                                    onTapLayer: widget.onTapLayer,
                                    activeTimesUs: widget.activeTimesUs,
                                    onForeignKeyframe: widget.onForeignKeyframe,
                                    onEditStart: () => _editingBar = true,
                                    onEditEnd: () => _editingBar = false,
                                  );
                                },
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
                  top: 16,
                  left: 0,
                  right: 0,
                  child: Center(
                    child: ValueListenableBuilder<Duration>(
                      valueListenable: widget.playback.time,
                      // O TEMPO NUMA CAIXA SOBRE O CABECOTE, nao num
                      // canto: ele acompanha a posicao do cabecote, e e o
                      // que faz ler a hora sem procurar.
                      builder: (context, t, _) => Container(
                        padding: const EdgeInsets.symmetric(
                          horizontal: 8,
                          vertical: 2,
                        ),
                        decoration: BoxDecoration(
                          color: AmColors.bg,
                          borderRadius: BorderRadius.circular(7),
                        ),
                        child: Text(
                          formatTimecode(t, project.fps),
                          style: const TextStyle(
                            fontSize: 17,
                            fontWeight: FontWeight.w700,
                            color: Colors.white,
                            fontFeatures: [FontFeature.tabularFigures()],
                          ),
                        ),
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
                // Corte e CONGELAR moram no cabecote: os dois usam o
                // instante que ja esta sob a mao, sem abrir nova secao.
                if (widget.singleLayerId == null)
                  Align(
                    alignment: Alignment.bottomCenter,
                    child: Padding(
                      padding: const EdgeInsets.only(bottom: 4),
                      child: Row(
                        mainAxisSize: MainAxisSize.min,
                        children: [
                          _PlayheadAction(
                            label: 'Dividir no cabecote',
                            icon: CupertinoIcons.scissors,
                            onTap: () {
                              final id = ref.read(selectedLayerProvider);
                              if (id == null) return;
                              ref
                                  .read(editorControllerProvider.notifier)
                                  .splitLayer(id, widget.playback.time.value);
                              HapticFeedback.selectionClick();
                            },
                          ),
                          const SizedBox(width: 8),
                          _PlayheadAction(
                            label: 'Congelar aqui',
                            icon: CupertinoIcons.pause_fill,
                            onTap: () {
                              final id = ref.read(selectedLayerProvider);
                              if (id == null) return;
                              final ok = ref
                                  .read(editorControllerProvider.notifier)
                                  .freezeFrame(id, widget.playback.time.value);
                              if (ok) {
                                AureaSnack.show(
                                  context,
                                  'Quadro congelado por 1 segundo',
                                );
                                HapticFeedback.selectionClick();
                              } else {
                                AureaSnack.show(
                                  context,
                                  'Selecione um video e leve o cabecote para dentro dele',
                                );
                              }
                            },
                            onLongPress: () {
                              final id = ref.read(selectedLayerProvider);
                              if (id != null) {
                                showFreezeSheet(
                                  context,
                                  ref,
                                  id,
                                  widget.playback.time.value,
                                );
                              }
                            },
                          ),
                        ],
                      ),
                    ),
                  ),
                // Pilulas fixas a esquerda (olho + miniatura), uma por linha.
                Positioned(
                  left: 0,
                  top: 50,
                  bottom: 0,
                  width: 92,
                  child: ListView.builder(
                    controller: _pillsScroll,
                    padding: EdgeInsets.zero,
                    itemExtent: kAmRowHeight,
                    itemCount: layers.length,
                    itemBuilder: (context, index) {
                      final layer = layers[index];
                      return Align(
                        alignment: Alignment.centerLeft,
                        child: _AmLayerPill(
                          layer: layer,
                          isMatteSource: matteSourceIds.contains(layer.id),
                        ),
                      );
                    },
                  ),
                ),
              ],
            ),
          );
        },
      ),
    );
  }
}

class _PlayheadAction extends StatelessWidget {
  const _PlayheadAction({
    required this.label,
    required this.icon,
    required this.onTap,
    this.onLongPress,
  });

  final String label;
  final IconData icon;
  final VoidCallback onTap;
  final VoidCallback? onLongPress;

  @override
  Widget build(BuildContext context) => Semantics(
    label: label,
    button: true,
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      onLongPress: onLongPress,
      child: Container(
        width: 34,
        height: 34,
        decoration: BoxDecoration(
          color: AmColors.panelHigh,
          shape: BoxShape.circle,
          border: Border.all(color: AmColors.hairline),
        ),
        child: Icon(icon, size: 17, color: AmColors.text),
      ),
    ),
  );
}

/// Pilula fixa: olho + miniatura da camada.
class _AmLayerPill extends StatelessWidget {
  const _AmLayerPill({required this.layer, required this.isMatteSource});

  final Layer layer;
  final bool isMatteSource;

  Color get _thumbColor => switch (layer) {
    ShapeLayer l => l.primaryColor,
    TextLayer _ => Colors.white,
    VideoLayer _ => const Color(0xFF3D6BB3),
    ImageLayer _ => const Color(0xFF8A5BA0),
    AudioLayer _ => const Color(0xFF2E8B62),
    CaptionLayer _ => const Color(0xFFE8B93E),
    GroupLayer _ => const Color(0xFF6B7A94),
    NullLayer _ => const Color(0xFF9F8CFF),
    ParticlesLayer l => l.color,
    Element3DLayer l => l.color,
    AdjustmentLayer _ => const Color(0xFF56D1C4),
    Scene3DLayer _ => const Color(0xFF35C4E7),
  };

  @override
  Widget build(BuildContext context) {
    return Container(
      height: kAmRowHeight,
      padding: const EdgeInsets.only(bottom: 8),
      child: Container(
        padding: const EdgeInsets.fromLTRB(10, 4, 12, 4),
        decoration: const BoxDecoration(
          color: AmColors.panel,
          borderRadius: BorderRadius.only(
            topRight: Radius.circular(20),
            bottomRight: Radius.circular(20),
          ),
        ),
        child: Row(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(CupertinoIcons.eye, size: 18, color: AmColors.text),
            const SizedBox(width: 8),
            if (isMatteSource) ...[
              Semantics(
                label: 'Fonte de recorte por camada',
                child: const Icon(
                  CupertinoIcons.scope,
                  size: 15,
                  color: AmColors.accent,
                ),
              ),
              const SizedBox(width: 7),
            ],
            Container(
              width: 26,
              height: 26,
              decoration: BoxDecoration(
                color: _thumbColor,
                borderRadius: BorderRadius.circular(
                  layer is ShapeLayer ? 13 : 5,
                ),
              ),
              child: layer is TextLayer
                  ? const Center(
                      child: Text(
                        'T',
                        style: TextStyle(
                          color: Colors.black,
                          fontSize: 14,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    )
                  : null,
            ),
          ],
        ),
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

  @override
  void paint(Canvas canvas, Size size) {
    if (beats.isEmpty) return;
    // Uma grade densa tem milhares de riscos; num Path so, uma chamada.
    final caminho = Path();
    for (final b in beats) {
      final x = b.inMicroseconds / 1e6 * pps;
      if (x < -2 || x > size.width + 2) continue;
      caminho
        ..moveTo(x, size.height * 0.55)
        ..lineTo(x, size.height);
    }
    canvas.drawPath(caminho, _tinta);
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
        onLongPress: widget.onMenu,
        onHorizontalDragStart: (_) => _acumulado = 0,
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
                decoration: const InputDecoration(
                  hintText: 'Nome da marca',
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
              title: const Text(
                'Apagar a marca',
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

class _AmLayerRow extends ConsumerStatefulWidget {
  const _AmLayerRow({
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
  });

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
  ConsumerState<_AmLayerRow> createState() => _AmLayerRowState();
}

class _AmLayerRowState extends ConsumerState<_AmLayerRow> {
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

  void _comecarArrasto() {
    _arrastandoBarra = true;
    onEditStart();
  }

  void _terminarArrasto() {
    if (!_arrastandoBarra) return;
    _arrastandoBarra = false;
    onEditEnd();
  }

  @override
  void didUpdateWidget(_AmLayerRow old) {
    super.didUpdateWidget(old);
    if (_arrastandoBarra && !(selected && !compact)) _terminarArrasto();
  }

  @override
  void dispose() {
    // A fileira pode sair da arvore no meio do arrasto (trocar de
    // painel, desfazer): o fim tem de sair mesmo assim.
    _terminarArrasto();
    super.dispose();
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
  double _accumPx = 0;
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

    return SizedBox(
      height: kAmRowHeight,
      width: totalWidth,
      child: Stack(
        clipBehavior: Clip.none,
        children: [
          Positioned(
            left: left,
            top: 0,
            width: width.toDouble(),
            height: kAmBarHeight,
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: () {
                if (compact) return;
                // Toque simples limpa a selecao multipla.
                ref.read(multiSelectProvider.notifier).state = const {};
                if (ref.read(selectedLayerProvider) == layer.id) {
                  onTapLayer?.call(layer);
                } else {
                  ref.read(selectedLayerProvider.notifier).state = layer.id;
                }
              },
              // Toque LONGO alterna a camada na selecao multipla (a
              // barra de acoes agrupa/duplica/exclui o conjunto).
              onLongPress: compact
                  ? null
                  : () {
                      final set = {...ref.read(multiSelectProvider)};
                      final primary = ref.read(selectedLayerProvider);
                      if (primary != null) set.add(primary);
                      if (!set.add(layer.id)) set.remove(layer.id);
                      ref.read(multiSelectProvider.notifier).state = set;
                      HapticFeedback.selectionClick();
                    },
              onHorizontalDragStart: selected && !compact
                  ? (_) {
                      _comecarArrasto();
                      _dragStart0 = layer.startTime;
                      _accumPx = 0;
                    }
                  : null,
              onHorizontalDragUpdate: selected && !compact
                  ? (d) {
                      _accumPx += d.delta.dx;
                      final desired = _dragStart0 + _pxToDur(_accumPx);
                      final snapped = _snapMove(desired);
                      _hapticIfSnapped(desired, snapped);
                      controller.moveLayer(layer.id, snapped);
                    }
                  : null,
              onHorizontalDragEnd: selected && !compact
                  ? (_) => _terminarArrasto()
                  : null,
              onHorizontalDragCancel: selected && !compact
                  ? _terminarArrasto
                  : null,
              child: CustomPaint(
                painter: _AmBarPainter(
                  selected: selected,
                  color: layerTypeColor(layer),
                  stripeColor: layerTypeStripe(layer),
                ),
                // FORMA DE ONDA e TIRA DE MINIATURAS dentro da barra:
                // sem elas, achar o corte e tatear.
                child: _ClipPreview(
                  layer: layer,
                  child: Padding(
                    padding: const EdgeInsets.symmetric(horizontal: 12),
                    child: Row(
                      children: [
                        // ICONE DO TIPO na ponta: reconhecer sem ler.
                        Padding(
                          padding: const EdgeInsets.only(right: 6),
                          child: Icon(
                            layerTypeIcon(layer),
                            size: 11,
                            color: Colors.white.withValues(alpha: 0.85),
                          ),
                        ),
                        Flexible(
                          child: Text(
                            layer.name,
                            maxLines: 1,
                            overflow: TextOverflow.ellipsis,
                            style: const TextStyle(
                              fontSize: 12.5,
                              fontWeight: FontWeight.w700,
                              color: Colors.white,
                            ),
                          ),
                        ),
                        if (layer.hasAnimation) ...[
                          const SizedBox(width: 6),
                          const Icon(
                            CupertinoIcons.rhombus,
                            size: 12,
                            color: Colors.white,
                          ),
                        ],
                        const Spacer(),
                        if (!compact && width > 90)
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
          // A juncao e o botao da TRANSICAO. Toque longo preserva o gesto
          // antigo de juntar novamente dois pedacos da mesma fonte.
          Consumer(
            builder: (context, ref, _) {
              final ctrl = ref.read(editorControllerProvider.notifier);
              if (layer is! VideoLayer || ctrl.clipAfter(layer.id) == null) {
                return const SizedBox.shrink();
              }
              final transition = ctrl.transitionAfter(layer.id);
              final x = left + (layer.duration.inMicroseconds / 1e6 * pps);
              final markerWidth = transition == null ? 24.0 : 66.0;
              return Positioned(
                left: x - markerWidth / 2,
                top: 0,
                width: markerWidth,
                height: kAmBarHeight,
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
                      height: kAmBarHeight - 10,
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
                          : Text(
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
                height: kAmBarHeight - 12,
                child: IgnorePointer(
                  child: Container(
                    decoration: BoxDecoration(
                      color: Colors.white.withValues(alpha: 0.35),
                      borderRadius: BorderRadius.circular(3),
                    ),
                  ),
                ),
              ),
          // Setas de navegacao entre camadas (paginas de ferramenta).
          if (compact) ...[
            Positioned(
              left: left - 40,
              top: 0,
              height: kAmBarHeight,
              child: _NavArrow(
                icon: CupertinoIcons.chevron_left,
                onTap: () => controller.selectNeighbor(1),
              ),
            ),
            Positioned(
              left: left + width + 4,
              top: 0,
              height: kAmBarHeight,
              child: _NavArrow(
                icon: CupertinoIcons.chevron_right,
                onTap: () => controller.selectNeighbor(-1),
              ),
            ),
          ],
          // Alcas de trim (com snap magnetico).
          if (selected && !compact) ...[
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
              },
            ),
          ],
          // Diamantes de keyframe sobre a barra: acesos = propriedade
          // ativa; translúcidos = de outra propriedade, ainda visíveis.
          // FAIXA DE KEYFRAMES COM DENSIDADE.
          //
          // Vinte keyframes a 2 px de distancia desenhados um a um viram
          // uma mancha de losangos sobrepostos — que informa menos que
          // uma barra lisa, e custa vinte widgets. Aqui o que esta junto
          // demais para se distinguir vira barra, e o que da para
          // distinguir continua losango.
          for (final grupo in _agrupaKeyframes(layer.keyframeTimes, pps))
            Builder(
              builder: (context) {
                final active =
                    activeTimesUs == null ||
                    grupo.times.any(
                      (t) => activeTimesUs!.contains(t.inMicroseconds),
                    );
                final cor = active
                    ? Colors.white
                    : Colors.white.withValues(alpha: 0.65);
                final x0 = grupo.times.first.inMicroseconds / 1e6 * pps;
                final x1 = grupo.times.last.inMicroseconds / 1e6 * pps;

                final Widget marca = grupo.times.length == 1
                    ? Transform.rotate(
                        angle: 0.785398,
                        child: Container(
                          width: 10,
                          height: 10,
                          decoration: BoxDecoration(
                            color: cor,
                            borderRadius: BorderRadius.circular(2),
                            border: Border.all(color: Colors.black87),
                          ),
                        ),
                      )
                    : Container(
                        height: 8,
                        decoration: BoxDecoration(
                          color: cor,
                          borderRadius: BorderRadius.circular(4),
                          border: active
                              ? Border.all(color: Colors.black38)
                              : null,
                        ),
                      );

                final largura = grupo.times.length == 1 ? 22.0 : (x1 - x0) + 22;
                return Positioned(
                  left: left + x0 - 11,
                  top: kAmBarHeight / 2 - 11,
                  width: largura,
                  height: 22,
                  // O APAGADO RESPONDE AO TOQUE. Marca que se ve e nao se
                  // consegue tocar vira enigma: de quem e essa? So `onTap`
                  // — arrastar continua movendo o clipe, porque um
                  // reconhecedor de toque perde a arena para um de arrasto.
                  // O diamante ACESO leva o cabecote ate ele: e como se
                  // navega de keyframe em keyframe no Alight Motion, tocando
                  // na barra. Sem botao de anterior/proximo, sem menu.
                  child: GestureDetector(
                    key: ValueKey(
                      'layer-keyframe-${layer.id}-${grupo.times.first.inMicroseconds}',
                    ),
                    behavior: HitTestBehavior.opaque,
                    onTap: () {
                      if (active) {
                        playback.pause();
                        playback.seek(layer.startTime + grupo.times.first);
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
                            ? 10
                            : (x1 - x0).clamp(10.0, double.infinity),
                        height: 10,
                        child: marca,
                      ),
                    ),
                  ),
                );
              },
            ),
        ],
      ),
    );
  }
}

class _NavArrow extends StatelessWidget {
  const _NavArrow({required this.icon, required this.onTap});

  final IconData icon;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Container(
        width: 36,
        decoration: BoxDecoration(
          color: const Color(0xFFE9EDF2),
          borderRadius: BorderRadius.circular(19),
        ),
        child: Icon(icon, size: 18, color: const Color(0xFF12151A)),
      ),
    );
  }
}

/// Barra teal; selecionada ganha listras diagonais claras.
/// Um punhado de keyframes perto demais para se distinguirem.
class _GrupoDeKeyframes {
  const _GrupoDeKeyframes(this.times);
  final List<Duration> times;
}

/// Junta os keyframes que ficariam a menos de [minPx] um do outro.
///
/// O criterio e em PIXEL, nao em tempo: o mesmo par de keyframes se
/// distingue com a linha ampliada e some quando ela e reduzida, e o
/// desenho tem de acompanhar isso.
List<_GrupoDeKeyframes> _agrupaKeyframes(
  List<Duration> times,
  double pps, {
  double minPx = 9,
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

class _AmBarPainter extends CustomPainter {
  const _AmBarPainter({
    required this.selected,
    required this.color,
    required this.stripeColor,
  });

  final bool selected;

  /// A cor DO TIPO da camada. Antes era o mesmo violeta para tudo, e uma
  /// linha do tempo com dez faixas exigia ler dez rotulos para achar o
  /// audio no meio dos videos.
  final Color color;
  final Color stripeColor;

  // Objetos de pintura reaproveitados: `paint` roda a cada quadro
  // enquanto a linha rola, e alocar aqui dentro e lixo por quadro.
  static final Paint _fundo = Paint();
  static final Paint _listra = Paint()..strokeWidth = 7;

  @override
  void paint(Canvas canvas, Size size) {
    final rrect = RRect.fromRectAndRadius(
      Offset.zero & size,
      const Radius.circular(8),
    );
    canvas.drawRRect(rrect, _fundo..color = color);
    if (selected) {
      canvas.save();
      canvas.clipRRect(rrect);
      _listra.color = stripeColor.withValues(alpha: 0.55);
      for (var x = -size.height; x < size.width + size.height; x += 22) {
        canvas.drawLine(
          Offset(x, size.height + 4),
          Offset(x + size.height + 8, -4),
          _listra,
        );
      }
      canvas.restore();
    }
  }

  @override
  bool shouldRepaint(_AmBarPainter old) =>
      old.selected != selected || old.color != color;
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
      height: kAmBarHeight - 4,
      width: 16,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onHorizontalDragStart: (_) => onStart(),
        onHorizontalDragUpdate: (d) => onDrag(d.delta.dx),
        onHorizontalDragEnd: (_) => onEnd(),
        onHorizontalDragCancel: onEnd,
        child: Container(
          decoration: BoxDecoration(
            color: Colors.white,
            borderRadius: BorderRadius.circular(5),
          ),
          child: const Center(
            child: SizedBox(
              width: 2,
              height: 14,
              child: ColoredBox(color: Colors.black26),
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
      _service.ensureFilmstrip(
        l.sourcePath,
        l.sourceOffset + videoSourceSpan(l),
      );
      // PROXY: pedido daqui porque a barra do clipe sempre monta —
      // pendurar no caminho de sincronia do player era fragil, ele so
      // roda quando o relogio anda.
      ProxyService.instance.ensureProxy(l.sourcePath);
    }
  }

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

    return ValueListenableBuilder<int>(
      valueListenable: _service.revision,
      builder: (context, _, child) {
        final peaks = _service.peaksOf(path);
        final piramide = _service.pyramidOf(path);
        final strip = l is VideoLayer ? _service.stripOf(path) : null;

        final temStrip = strip != null && strip.isNotEmpty;
        final temOnda = peaks != null && peaks.isNotEmpty;

        return ClipRRect(
          borderRadius: BorderRadius.circular(8),
          child: Stack(
            fit: StackFit.passthrough,
            children: [
              // MINIATURAS em cima da barra, opacas: meia opacidade
              // sobre o violeta lava a imagem e ela deixa de informar.
              if (temStrip)
                Positioned.fill(
                  child: CustomPaint(
                    painter: FilmstripPainter(
                      frames: strip,
                      start: inicio,
                      end: fim,
                      sourceDuration: fim,
                    ),
                  ),
                ),
              // Veu escuro so onde o nome do clipe passa, para o texto
              // continuar legivel sobre qualquer cena.
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
              // Audio do proprio video: faixa fina embaixo, para nao
              // brigar com a imagem.
              if (temOnda)
                Positioned(
                  left: 0,
                  right: 0,
                  bottom: 0,
                  height: temStrip ? kAmBarHeight * 0.34 : null,
                  top: temStrip ? null : 0,
                  child: CustomPaint(
                    // PIRAMIDE: contorno de pico e miolo de RMS, com o
                    // nivel escolhido pelo zoom. Ampliar troca de nivel
                    // e nunca recalcula.
                    painter: piramide != null && !piramide.isEmpty
                        ? PyramidWaveformPainter(
                            pyramid: piramide,
                            start: inicio,
                            end: fim,
                            color: temStrip
                                ? AmColors.accent.withValues(alpha: 0.85)
                                : Colors.white.withValues(alpha: 0.8),
                          )
                        : WaveformPainter(
                            peaks: peaks,
                            start: inicio,
                            end: fim,
                            color: temStrip
                                ? AmColors.accent.withValues(alpha: 0.85)
                                : Colors.white.withValues(alpha: 0.8),
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
