import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/playback_controller.dart';
import '../../domain/layer.dart';
import '../../domain/mask.dart';
import '../../domain/path_edit.dart';
import '../widgets/mask_node_editor.dart';
import 'am_colors.dart';
import 'am_widgets.dart';

/// O que o dedo faz no trackpad.
enum PointsMode { move, handle, add }

final pathEditModeProvider = StateProvider<PointsMode>(
  (ref) => PointsMode.move,
);

/// EDIT POINTS (o editor de pontos do Alight Motion, em retrato).
///
/// A descoberta que muda tudo: o dedo NUNCA cobre o desenho. O painel e
/// um TRACKPAD — "deslize aqui para posicionar o proximo ponto, depois
/// toque aqui para crava-lo". Um cursor em cruz (⊹) anda no preview
/// enquanto se desliza; o toque no trackpad crava. Os pontos cravados
/// aparecem como ● no preview (o MaskNodeEditor desenha).
///
/// Trilho esquerdo: `←` voltar, `◌` selecionar/mover, `⌁` alca, `⊕`
/// adicionar, `⋯` (apagar ponto, fechar/abrir, canto/suave). No
/// cabecalho da tela: `◈` keyframe dos pontos (e assim que a AM faz
/// morph) e `⊕` adicionar no cursor.
class PointsPanel extends ConsumerStatefulWidget {
  const PointsPanel({
    super.key,
    required this.playback,
    required this.layerId,
    required this.itemId,
    required this.onBack,
  });

  final PlaybackController playback;
  final String layerId;
  final String itemId;
  final VoidCallback onBack;

  @override
  ConsumerState<PointsPanel> createState() => PointsPanelState();
}

class PointsPanelState extends ConsumerState<PointsPanel> {
  /// Raio de "perto" em px do caminho.
  static const double _raio = 26;

  Duration get _t => widget.playback.time.value;

  Layer? get _layer =>
      ref.read(editorControllerProvider).layerById(widget.layerId);

  bool get _editaForma => ref.read(pathEditTargetProvider)?.forma ?? true;

  AnimatedPath? _trilha() {
    final l = _layer;
    if (l == null) return null;
    if (_editaForma) {
      return ref
          .read(editorControllerProvider.notifier)
          .shapeBezierOf(widget.layerId, widget.itemId)
          ?.path;
    }
    for (final m in l.masks) {
      if (m.id == widget.itemId) return m.path;
    }
    return null;
  }

  BezierPath? _caminho() {
    final l = _layer;
    if (l == null) return null;
    return _trilha()?.valueAt(l.localTime(_t));
  }

  void _editar(BezierPath Function(BezierPath) fn) {
    final controller = ref.read(editorControllerProvider.notifier);
    if (_editaForma) {
      controller.editShapeBezier(widget.layerId, widget.itemId, _t, fn);
    } else {
      controller.editMaskPath(widget.layerId, widget.itemId, _t, fn);
    }
  }

  Offset _cursor() {
    final c = ref.read(pathEditCursorProvider);
    if (c != null) return c;
    final caminho = _caminho();
    final sel = ref.read(pathEditSelectedProvider);
    if (caminho != null && sel != null && sel < caminho.vertices.length) {
      return caminho.vertices[sel].p;
    }
    if (caminho != null && caminho.vertices.isNotEmpty) {
      return caminho.vertices.last.p;
    }
    return Offset.zero;
  }

  void _setCursor(Offset p) =>
      ref.read(pathEditCursorProvider.notifier).state = p;

  /// Delta do dedo no trackpad -> delta no espaco do caminho (desfaz a
  /// escala e o giro da camada, para o cursor andar junto com o dedo).
  Offset _paraCaminho(Offset delta) {
    final l = _layer;
    if (l == null) return delta;
    final local = l.localTime(_t);
    final sx = l.scaleX.valueAt(local), sy = l.scaleY.valueAt(local);
    final giro = -l.rotation.valueAt(local) * math.pi / 180;
    final c = math.cos(giro), s = math.sin(giro);
    final r = Offset(delta.dx * c - delta.dy * s, delta.dx * s + delta.dy * c);
    return Offset(
      sx.abs() < 1e-6 ? r.dx : r.dx / sx,
      sy.abs() < 1e-6 ? r.dy : r.dy / sy,
    );
  }

  // ---------------------------------------------------------- acoes

  /// `◈` do cabecalho: keyframe nos pontos no tempo de agora.
  void toggleKeyframe() {
    final controller = ref.read(editorControllerProvider.notifier);
    if (_editaForma) {
      controller.toggleShapeBezierKeyframe(widget.layerId, widget.itemId, _t);
    } else {
      controller.toggleMaskPathKeyframe(widget.layerId, widget.itemId, _t);
    }
  }

  bool get temKeyframeAqui {
    final l = _layer;
    if (l == null) return false;
    return _trilha()?.hasKeyframeAt(l.localTime(_t)) ?? false;
  }

  bool get animado => _trilha()?.isAnimated ?? false;

  /// `⊕`: crava um ponto no cursor. Caminho aberto: perto do primeiro
  /// ponto fecha; senao anexa no fim. Caminho fechado: entra no
  /// segmento mais proximo.
  void addPoint() {
    final caminho = _caminho();
    if (caminho == null) return;
    final p = _cursor();
    final n = caminho.vertices.length;
    if (!caminho.closed &&
        n >= 3 &&
        (caminho.vertices.first.p - p).distance <= _raio) {
      _editar((c) => BezierPath(vertices: c.vertices, closed: true));
      ref.read(pathEditSelectedProvider.notifier).state = 0;
      return;
    }
    if (caminho.closed && n >= 2) {
      final hit = nearestOnPath(caminho, p);
      if (hit != null) {
        _editar(
          (c) => moveVertex(
            insertVertex(c, hit.segment, hit.t),
            hit.segment + 1,
            p,
          ),
        );
        ref.read(pathEditSelectedProvider.notifier).state = hit.segment + 1;
        return;
      }
    }
    _editar(
      (c) => BezierPath(
        vertices: [
          ...c.vertices,
          PathVertex(p: p, corner: false),
        ],
        closed: c.closed,
      ),
    );
    ref.read(pathEditSelectedProvider.notifier).state = n;
  }

  void _selecionarNoCursor() {
    final caminho = _caminho();
    if (caminho == null) return;
    final no = vertexAt(caminho, _cursor(), _raio);
    ref.read(pathEditSelectedProvider.notifier).state = no;
    if (no != null) _setCursor(caminho.vertices[no].p);
  }

  void _toggleCanto() {
    final sel = ref.read(pathEditSelectedProvider);
    if (sel == null) return;
    _editar((c) => toggleCorner(c, sel));
  }

  void _apagar() {
    final sel = ref.read(pathEditSelectedProvider);
    if (sel == null) return;
    _editar((c) => removeVertex(c, sel));
    ref.read(pathEditSelectedProvider.notifier).state = null;
  }

  void _fecharAbrir() {
    _editar((c) => BezierPath(vertices: c.vertices, closed: !c.closed));
  }

  // -------------------------------------------------------- trackpad

  void _arrasto(Offset delta) {
    final modo = ref.read(pathEditModeProvider);
    final caminho = _caminho();
    final sel = ref.read(pathEditSelectedProvider);
    final d = _paraCaminho(delta);
    final temSel =
        caminho != null && sel != null && sel < caminho.vertices.length;
    if (modo == PointsMode.move && temSel) {
      final alvo = caminho.vertices[sel].p + d;
      _editar((c) => moveVertex(c, sel, alvo));
      _setCursor(alvo);
      return;
    }
    if (modo == PointsMode.handle && temSel) {
      final v = caminho.vertices[sel];
      final alvo = v.p + v.outT + d;
      _editar((c) => moveHandle(c, sel, Handle.saida, alvo));
      _setCursor(alvo);
      return;
    }
    _setCursor(_cursor() + d);
  }

  void _toque() {
    switch (ref.read(pathEditModeProvider)) {
      case PointsMode.add:
        addPoint();
      case PointsMode.move:
      case PointsMode.handle:
        _selecionarNoCursor();
    }
  }

  @override
  Widget build(BuildContext context) {
    ref.watch(editorControllerProvider);
    final modo = ref.watch(pathEditModeProvider);
    final sel = ref.watch(pathEditSelectedProvider);
    final caminho = _caminho();
    final n = caminho?.vertices.length ?? 0;
    final temSel = sel != null && sel < n;

    return ColoredBox(
      color: AmColors.panel,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          SizedBox(
            width: 56,
            child: Column(
              children: [
                AmRailButton(
                  onTap: widget.onBack,
                  child: const Icon(
                    CupertinoIcons.chevron_back,
                    size: 24,
                    color: AmColors.text,
                  ),
                ),
                _ModoBotao(
                  ativo: modo == PointsMode.move,
                  icon: CupertinoIcons.smallcircle_circle,
                  onTap: () => ref.read(pathEditModeProvider.notifier).state =
                      PointsMode.move,
                ),
                _ModoBotao(
                  ativo: modo == PointsMode.handle,
                  icon: CupertinoIcons.arrow_up_right_diamond,
                  onTap: () => ref.read(pathEditModeProvider.notifier).state =
                      PointsMode.handle,
                ),
                _ModoBotao(
                  ativo: modo == PointsMode.add,
                  icon: CupertinoIcons.plus_circle,
                  onTap: () => ref.read(pathEditModeProvider.notifier).state =
                      PointsMode.add,
                ),
                // OS COMANDOS DO PONTO, VISIVEIS.
                //
                // Eram uma lista dentro de um tres pontinhos. Sao os tres
                // que se usa o tempo todo ao desenhar, e cada um custava
                // dois toques a mais por estar escondido. Sem ponto
                // selecionado ficam esmaecidos, nao somem.
                _ModoBotao(
                  ativo: false,
                  icon: CupertinoIcons.slider_horizontal_below_rectangle,
                  onTap: temSel ? _toggleCanto : null,
                ),
                _ModoBotao(
                  ativo: false,
                  icon: CupertinoIcons.trash,
                  onTap: temSel ? _apagar : null,
                ),
                _ModoBotao(
                  ativo: false,
                  icon: (caminho?.closed ?? true)
                      ? CupertinoIcons.lock_open
                      : CupertinoIcons.lock,
                  onTap: _fecharAbrir,
                ),
              ],
            ),
          ),
          Expanded(
            child: Padding(
              padding: const EdgeInsets.fromLTRB(4, 8, 12, 10),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  Text(
                    switch (modo) {
                      PointsMode.move =>
                        temSel
                            ? 'Ponto ${sel + 1} de $n: deslize para mover. Toque duplo: canto/suave.'
                            : 'Deslize ate um ponto e toque para selecionar.',
                      PointsMode.handle =>
                        temSel
                            ? 'Deslize para puxar a alca do ponto ${sel + 1}.'
                            : 'Selecione um ponto antes de puxar a alca.',
                      PointsMode.add => 'Deslize aqui para posicionar o proximo ponto, depois toque aqui para crava-lo.',
                    },
                    style: const TextStyle(
                      fontSize: 12.5,
                      color: AmColors.muted,
                    ),
                  ),
                  const SizedBox(height: 8),
                  Expanded(
                    child: GestureDetector(
                      behavior: HitTestBehavior.opaque,
                      onPanUpdate: (d) => _arrasto(d.delta),
                      onTap: _toque,
                      onDoubleTap: _toggleCanto,
                      child: CustomPaint(
                        painter: _TrackpadPainter(modo: modo),
                        child: const SizedBox.expand(),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

}

class _ModoBotao extends StatelessWidget {
  const _ModoBotao({
    required this.ativo,
    required this.icon,
    required this.onTap,
  });

  final bool ativo;
  final IconData icon;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    return AmRailButton(
      onTap: onTap,
      selected: ativo,
      child: Icon(
        icon,
        size: 22,
        color: ativo ? AmColors.accent : AmColors.text,
      ),
    );
  }
}


/// O trackpad: cantos marcados, e um ⊹ no meio como lembrete.
class _TrackpadPainter extends CustomPainter {
  const _TrackpadPainter({required this.modo});

  final PointsMode modo;

  @override
  void paint(Canvas canvas, Size size) {
    final fundo = Paint()..color = AmColors.chip;
    final rr = RRect.fromRectAndRadius(
      Offset.zero & size,
      const Radius.circular(14),
    );
    canvas.drawRRect(rr, fundo);
    final traco = Paint()
      ..color = AmColors.muted.withValues(alpha: 0.7)
      ..strokeWidth = 2
      ..style = PaintingStyle.stroke;
    const m = 12.0, l = 22.0;
    final w = size.width, h = size.height;
    for (final (x, y, sx, sy) in [
      (m, m, 1.0, 1.0),
      (w - m, m, -1.0, 1.0),
      (m, h - m, 1.0, -1.0),
      (w - m, h - m, -1.0, -1.0),
    ]) {
      canvas.drawLine(Offset(x, y), Offset(x + l * sx, y), traco);
      canvas.drawLine(Offset(x, y), Offset(x, y + l * sy), traco);
    }
    final c = Offset(w / 2, h / 2);
    final cor = modo == PointsMode.add ? AmColors.accent : AmColors.muted;
    final cruz = Paint()
      ..color = cor.withValues(alpha: 0.55)
      ..strokeWidth = 1.5;
    canvas.drawLine(c - const Offset(14, 0), c + const Offset(14, 0), cruz);
    canvas.drawLine(c - const Offset(0, 14), c + const Offset(0, 14), cruz);
    canvas.drawCircle(c, 5, cruz..style = PaintingStyle.stroke);
  }

  @override
  bool shouldRepaint(_TrackpadPainter old) => old.modo != modo;
}

/// Pedido de abrir o Edit Points numa camada (o "Desenho vetorial" do
/// menu de adicionar): o editor escuta, abre e zera.
final editPointsRequestProvider = StateProvider<String?>((ref) => null);
