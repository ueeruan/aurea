import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../application/editor_controller.dart';
import '../../application/ui/editor_session.dart';
import '../../domain/layer.dart';
import '../../domain/mask.dart';
import '../../domain/shape.dart';
import '../../domain/path_edit.dart';
import '../../../../core/ui/am_colors.dart';

/// Que mascara esta sendo editada no a no. Nulo = ninguem, e a camada
/// volta a se mover com o dedo normalmente.
class PathEditTarget {
  const PathEditTarget(this.layerId, this.maskId, {this.forma = false});
  final String layerId;

  /// O id da mascara — ou, com [forma], o id do item ShapeBezier.
  final String maskId;

  /// O CAMINHO E DA FORMA, nao de uma mascara. O editor de nos e o
  /// mesmo: um contorno bezier no espaco da camada, com o dedo. So muda
  /// de onde o caminho vem e para onde volta.
  final bool forma;
  @override
  bool operator ==(Object other) =>
      other is PathEditTarget &&
      other.layerId == layerId &&
      other.maskId == maskId &&
      other.forma == forma;
  @override
  int get hashCode => Object.hash(layerId, maskId, forma);
}

final pathEditTargetProvider = StateProvider<PathEditTarget?>((ref) => null);

/// O que o dedo faz no trackpad do Edit Points.
enum PointsMode { move, handle, add }

/// O modo vigente. Mora aqui, e nao no painel, porque quem decide se um
/// toque no palco CRIA um ponto e este modo — o painel so desenha o
/// botao. Enquanto ele morava no painel, os dois lados discordavam.
final pathEditModeProvider = StateProvider<PointsMode>(
  (ref) => PointsMode.move,
);

/// Qual no esta selecionado (o unico que mostra alcas).
final pathEditSelectedProvider = StateProvider<int?>((ref) => null);

/// O EDITOR DE NOS ESTA ATIVO NESTE PALCO?
///
/// A decisao mora numa funcao pura porque ela e O ponto onde o editor
/// ficava aceso fora do painel. O alvo (`pathEditTargetProvider`) e um
/// provider global que ninguem limpa quando a sessao muda de painel por
/// outro caminho que nao o Voltar; a sessao e quem abre e fecha a tela.
/// Sem esta pergunta, tocar num objeto do palco INSERIA um no no contorno
/// antigo em vez de selecionar a camada.
bool editorDeNosAtivo({
  required EditorPanel painel,
  required String? itemDaSessao,
  required PathEditTarget? alvo,
}) =>
    alvo != null &&
    painel == EditorPanel.editPoints &&
    itemDaSessao == alvo.maskId;

/// O CURSOR do trackpad (Edit Points), em coordenadas do caminho: onde
/// o proximo ponto cai. Desenhado como ⊹ no preview; null = sem cursor.
final pathEditCursorProvider = StateProvider<Offset?>((ref) => null);

/// EDITOR DE NOS, EM CIMA DA COMPOSICAO.
///
/// Desenhar a mascara em volta de uma pessoa nao acontece num formulario
/// — acontece com o dedo, olhando a imagem. Por isso os nos vivem aqui,
/// sobrepostos ao preview, e nao numa tela separada onde nao da para ver
/// o que se esta recortando.
///
/// A mascara mora no espaco da CAMADA (origem no centro do conteudo); a
/// tela e o espaco da COMPOSICAO. As duas contas de ida e volta estao em
/// [_paraComp] e [_paraMascara] — sem elas, arrastar um no numa camada
/// girada puxaria para o lado errado.
class MaskNodeEditor extends ConsumerStatefulWidget {
  const MaskNodeEditor({
    super.key,
    required this.time,
    required this.stageScale,
  });

  final ValueListenable<Duration> time;

  /// Quanto a composicao esta encolhida na tela: o raio do dedo em
  /// pixels de tela vira raio em pixels de composicao.
  final double Function() stageScale;

  @override
  ConsumerState<MaskNodeEditor> createState() => _MaskNodeEditorState();
}

class _MaskNodeEditorState extends ConsumerState<MaskNodeEditor> {
  /// O que o dedo pegou no comeco do arrasto.
  int? _arrastandoNo;
  (int, Handle)? _arrastandoAlca;

  double get _raio => 22 / math.max(widget.stageScale(), 0.05);

  Layer? _camada(PathEditTarget alvo) =>
      ref.read(editorControllerProvider).layerById(alvo.layerId);

  /// O caminho ANIMAVEL sob edicao: de uma mascara ou de um item de
  /// forma, conforme o alvo.
  AnimatedPath? _caminhoAnimado(PathEditTarget alvo) {
    final l = _camada(alvo);
    if (l == null) return null;
    if (alvo.forma) {
      if (l is! ShapeLayer) return null;
      for (final i in l.contents) {
        if (i.id == alvo.maskId && i is ShapeBezier) return i.path;
      }
      return null;
    }
    for (final m in l.masks) {
      if (m.id == alvo.maskId) return m.path;
    }
    return null;
  }

  /// (centro da camada na composicao, escala, giro em radianos).
  (Offset, double, double, double) _pose(Layer l, Duration t) {
    final local = l.localTime(t);
    return (
      l.position.valueAt(local),
      l.scaleX.valueAt(local),
      l.scaleY.valueAt(local),
      l.rotation.valueAt(local) * math.pi / 180,
    );
  }

  Offset _paraComp(Offset p, (Offset, double, double, double) pose) {
    final (centro, sx, sy, giro) = pose;
    final e = Offset(p.dx * sx, p.dy * sy);
    final c = math.cos(giro), s = math.sin(giro);
    return centro + Offset(e.dx * c - e.dy * s, e.dx * s + e.dy * c);
  }

  Offset _paraMascara(Offset p, (Offset, double, double, double) pose) {
    final (centro, sx, sy, giro) = pose;
    final d = p - centro;
    final c = math.cos(-giro), s = math.sin(-giro);
    final r = Offset(d.dx * c - d.dy * s, d.dx * s + d.dy * c);
    return Offset(
      sx.abs() < 1e-6 ? r.dx : r.dx / sx,
      sy.abs() < 1e-6 ? r.dy : r.dy / sy,
    );
  }

  void _editar(PathEditTarget alvo, BezierPath Function(BezierPath) fn) {
    final c = ref.read(editorControllerProvider.notifier);
    if (alvo.forma) {
      c.editShapeBezier(alvo.layerId, alvo.maskId, widget.time.value, fn);
    } else {
      c.editMaskPath(alvo.layerId, alvo.maskId, widget.time.value, fn);
    }
  }

  @override
  Widget build(BuildContext context) {
    final alvo = ref.watch(pathEditTargetProvider);
    if (alvo == null) return const SizedBox.shrink();

    // O EDITOR DE NOS SÓ EXISTE COM O PAINEL DE PONTOS ABERTO.
    //
    // Sem esta trava ele ficava LIGADO no palco depois de sair do painel
    // por qualquer caminho que nao fosse o Voltar — tocar numa barra da
    // timeline, abrir outra ferramenta, o "+". E o alvo sobrevive porque
    // `pathEditTargetProvider` e um provider global: ninguem o limpa
    // quando a sessao muda de painel.
    //
    // O resultado era o pior possivel: com o alvo velho ainda de pe, o
    // palco inteiro virava editor de nos. Tocar num objeto INSERIA um no
    // no contorno antigo em vez de selecionar a camada, e arrastar mexia
    // num no invisivel em vez de mover a camada — "o editor vetorial esta
    // bugado", que foi o relato. Ele nao estava errado: estava aceso.
    final sessao = ref.watch(editorSessionProvider);
    if (!editorDeNosAtivo(
      painel: sessao.panel,
      itemDaSessao: sessao.pointsItemId,
      alvo: alvo,
    )) {
      return const SizedBox.shrink();
    }

    // Redesenha quando a mascara muda.
    ref.watch(editorControllerProvider);
    final selecionado = ref.watch(pathEditSelectedProvider);
    final cursor = ref.watch(pathEditCursorProvider);

    return ValueListenableBuilder<Duration>(
      valueListenable: widget.time,
      builder: (context, t, _) {
        final camada = _camada(alvo);
        final animado = _caminhoAnimado(alvo);
        if (camada == null || animado == null) {
          return const SizedBox.shrink();
        }
        final pose = _pose(camada, t);
        final caminho = animado.valueAt(camada.localTime(t));

        return GestureDetector(
          behavior: HitTestBehavior.opaque,
          onTapDown: (d) {
            final p = _paraMascara(d.localPosition, pose);
            final alca = handleAt(caminho, selecionado, p, _raio);
            if (alca != null) return;

            final no = vertexAt(caminho, p, _raio);
            if (no != null) {
              ref.read(pathEditSelectedProvider.notifier).state = no;
              return;
            }
            // Toque EM CIMA da linha insere um no ali — mas SO no modo
            // Adicionar, que e o modo que diz isso.
            //
            // Antes, qualquer modo inseria. O painel e um TRACKPAD: o
            // dedo nunca cobre o desenho, e o ponto nasce onde o cursor
            // esta, cravado pelo botao de adicionar. Com o toque
            // inserindo em todo modo, encostar na linha para SELECIONAR
            // um ponto vizinho acrescentava um ponto novo na curva, e a
            // curva ia ficando cheia de nos que ninguem pediu — o
            // "criacao de pontos" do relato, do lado de la do que se
            // queria.
            final hit = nearestOnPath(caminho, p);
            if (hit != null &&
                hit.distance <= _raio &&
                ref.read(pathEditModeProvider) == PointsMode.add) {
              _editar(alvo, (c) => insertVertex(c, hit.segment, hit.t));
              ref.read(pathEditSelectedProvider.notifier).state =
                  hit.segment + 1;
              return;
            }
            ref.read(pathEditSelectedProvider.notifier).state = null;
          },
          onPanStart: (d) {
            final p = _paraMascara(d.localPosition, pose);
            _arrastandoAlca = handleAt(caminho, selecionado, p, _raio);
            if (_arrastandoAlca != null) {
              _arrastandoNo = null;
              return;
            }
            _arrastandoNo = vertexAt(caminho, p, _raio);
            if (_arrastandoNo != null) {
              ref.read(pathEditSelectedProvider.notifier).state = _arrastandoNo;
            }
          },
          onPanUpdate: (d) {
            final p = _paraMascara(d.localPosition, pose);
            final alca = _arrastandoAlca;
            if (alca != null) {
              _editar(alvo, (c) => moveHandle(c, alca.$1, alca.$2, p));
              return;
            }
            final no = _arrastandoNo;
            if (no != null) {
              _editar(alvo, (c) => moveVertex(c, no, p));
            }
          },
          onPanEnd: (_) {
            _arrastandoNo = null;
            _arrastandoAlca = null;
          },
          child: CustomPaint(
            painter: _NodePainter(
              path: caminho,
              selected: selecionado,
              cursor: cursor,
              toComp: (p) => _paraComp(p, pose),
              scale: widget.stageScale(),
            ),
          ),
        );
      },
    );
  }
}

class _NodePainter extends CustomPainter {
  const _NodePainter({
    required this.path,
    required this.selected,
    required this.toComp,
    required this.scale,
    this.cursor,
  });

  final BezierPath path;
  final int? selected;

  /// Cursor do trackpad (⊹), no espaco do caminho.
  final Offset? cursor;
  final Offset Function(Offset) toComp;
  final double scale;

  @override
  void paint(Canvas canvas, Size size) {
    _pintaCursor(canvas, 1 / math.max(scale, 0.05));
    if (path.vertices.isEmpty) return;
    // Espessuras em pixels de TELA: o no tem o mesmo tamanho aparente
    // com a composicao encolhida ou ampliada.
    final k = 1 / math.max(scale, 0.05);

    // O contorno, em duas passadas: escura embaixo para nao sumir sobre
    // imagem clara.
    final contorno = Path();
    final n = path.vertices.length;
    final v0 = toComp(path.vertices.first.p);
    contorno.moveTo(v0.dx, v0.dy);
    final total = path.closed ? n : n - 1;
    for (var i = 0; i < total; i++) {
      final a = path.vertices[i];
      final b = path.vertices[(i + 1) % n];
      final c1 = toComp(a.p + a.outT);
      final c2 = toComp(b.p + b.inT);
      final p3 = toComp(b.p);
      contorno.cubicTo(c1.dx, c1.dy, c2.dx, c2.dy, p3.dx, p3.dy);
    }
    canvas.drawPath(
      contorno,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 3.5 * k
        ..color = const Color(0xCC000000),
    );
    canvas.drawPath(
      contorno,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.5 * k
        ..color = AmColors.accent,
    );

    // Alcas so do no selecionado — mostrar todas vira um monte de
    // bolinha sobreposta e nao da para pegar nenhuma.
    final sel = selected;
    if (sel != null && sel >= 0 && sel < n) {
      final v = path.vertices[sel];
      final centro = toComp(v.p);
      for (final rel in [v.inT, v.outT]) {
        if (rel == Offset.zero) continue;
        final ponta = toComp(v.p + rel);
        canvas.drawLine(
          centro,
          ponta,
          Paint()
            ..strokeWidth = 1.2 * k
            ..color = AmColors.tealBright,
        );
        canvas.drawCircle(ponta, 6 * k, Paint()..color = AmColors.tealBright);
      }
    }

    for (var i = 0; i < n; i++) {
      final v = path.vertices[i];
      final p = toComp(v.p);
      final aceso = i == selected;
      final r = (aceso ? 7.5 : 5.5) * k;
      // Canto e quadrado, curva e redondo: a forma diz o tipo sem
      // precisar selecionar para descobrir.
      if (v.corner) {
        final quad = Rect.fromCenter(center: p, width: r * 2, height: r * 2);
        canvas.drawRect(quad, Paint()..color = const Color(0xCC000000));
        canvas.drawRect(
          quad.deflate(1.2 * k),
          Paint()..color = aceso ? AmColors.accent : Colors.white,
        );
      } else {
        canvas.drawCircle(p, r, Paint()..color = const Color(0xCC000000));
        canvas.drawCircle(
          p,
          r - 1.2 * k,
          Paint()..color = aceso ? AmColors.accent : Colors.white,
        );
      }
    }
  }

  @override
  bool shouldRepaint(_NodePainter old) => true;

  /// ⊹: cruz aberta com um anel no meio, sempre por cima e sempre do
  /// mesmo tamanho na tela.
  void _pintaCursor(Canvas canvas, double k) {
    final c0 = cursor;
    if (c0 == null) return;
    final c = toComp(c0);
    final sombra = Paint()
      ..color = const Color(0xCC000000)
      ..strokeWidth = 4 * k
      ..style = PaintingStyle.stroke;
    final tinta = Paint()
      ..color = AmColors.accent
      ..strokeWidth = 1.8 * k
      ..style = PaintingStyle.stroke;
    final braco = 16 * k, furo = 6 * k;
    for (final p in [sombra, tinta]) {
      canvas.drawLine(c - Offset(braco, 0), c - Offset(furo, 0), p);
      canvas.drawLine(c + Offset(furo, 0), c + Offset(braco, 0), p);
      canvas.drawLine(c - Offset(0, braco), c - Offset(0, furo), p);
      canvas.drawLine(c + Offset(0, furo), c + Offset(0, braco), p);
      canvas.drawCircle(c, furo, p);
    }
  }
}
