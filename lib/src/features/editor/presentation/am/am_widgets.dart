import 'package:flutter/cupertino.dart';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import '../../../../core/utils/time_format.dart';
import '../../application/playback_controller.dart';
import 'am_colors.dart';
import 'param_sheet_shell.dart';

export 'param_sheet_shell.dart'
    show ParamSheetScope, RecentSheets, closeParamSheet;

// O dono da folha é o Scaffold, nunca uma rota inferida de um contexto antigo.
final _activeParamSheets = <ScaffoldState, VoidCallback>{};

void closeActiveParamSheet(BuildContext context) {
  final host = Scaffold.maybeOf(context) ?? paramSheetHostKey.currentState;
  _activeParamSheets[host]?.call();
}

/// Mini-transporte para dentro dos sheets de parametros: play/pause,
/// voltar ao inicio e SCRUB — da para criar keyframes em tempos
/// diferentes sem fechar o painel. O sheet pai ja escuta o clock.
class SheetTransport extends StatelessWidget {
  const SheetTransport({
    super.key,
    required this.playback,
    required this.duration,
    this.fps = 30,
  });

  final PlaybackController playback;
  final Duration duration;
  final int fps;

  @override
  Widget build(BuildContext context) {
    final t = playback.time.value;
    final totalSec = duration.inMicroseconds / 1e6;
    return Row(
      children: [
        CupertinoButton(
          padding: const EdgeInsets.all(6),
          onPressed: () {
            playback.pause();
            playback.seek(Duration.zero);
          },
          child: const Icon(
            CupertinoIcons.backward_end,
            size: 18,
            color: AmColors.text,
          ),
        ),
        CupertinoButton(
          padding: const EdgeInsets.all(6),
          onPressed: playback.toggle,
          child: Icon(
            playback.playing.value
                ? CupertinoIcons.pause_fill
                : CupertinoIcons.play_fill,
            size: 20,
            color: AmColors.accent,
          ),
        ),
        Expanded(
          child: AmTickRuler(
            value: t.inMicroseconds / 1e6,
            min: 0,
            max: totalSec <= 0 ? 1 : totalSec,
            unitsPerPixel: (totalSec <= 0 ? 1 : totalSec) / 420,
            height: 38,
            onChanged: (v) {
              playback.pause();
              playback.seek(
                Duration(microseconds: (v.clamp(0, totalSec) * 1e6).round()),
              );
            },
          ),
        ),
        SizedBox(
          width: 78,
          child: Text(
            formatTimecode(t, fps),
            textAlign: TextAlign.center,
            style: const TextStyle(
              fontSize: 13,
              color: AmColors.accent,
              fontFeatures: [FontFeature.tabularFigures()],
            ),
          ),
        ),
      ],
    );
  }
}

/// Scaffold do editor que hospeda os sheets persistentes de parametros.
final GlobalKey<ScaffoldState> paramSheetHostKey = GlobalKey<ScaffoldState>();

/// Geracao do sheet de parametros: cresce a cada sheet aberto. Quem
/// espera um sheet fechar compara a geracao para saber se OUTRO sheet ja
/// tomou o lugar (e entao NAO deve reabrir nada por cima).
int paramSheetGeneration = 0;

/// Caixa do PALCO de preview no editor: os sheets medem o fundo dela
/// para JAMAIS cobrir o preview, em qualquer aparelho/aspecto.
final GlobalKey previewStageKey = GlobalKey();

/// Altura maxima de um sheet SEM invadir o preview: espaco entre o fundo
/// do palco e a base da tela. Fallback: fracao pedida.
double _sheetMaxHeight(BuildContext context, double heightFactor) {
  final size = MediaQuery.of(context).size;
  var maxH = size.height * heightFactor;
  final ctx = previewStageKey.currentContext;
  final box = ctx?.findRenderObject();
  if (box is RenderBox && box.hasSize && box.attached) {
    final bottom = box.localToGlobal(Offset.zero).dy + box.size.height;
    final available = size.height - bottom - 4;
    // Nunca cobre o palco; em telas minusculas o sheet fica compacto e
    // o conteudo rola (todos os sheets tem scroll).
    if (available > 140) {
      maxH = maxH < available ? maxH : available;
    } else {
      maxH = 180;
    }
  }
  // PISO. "Nunca cobrir o preview" virava, em tela baixa, uma folha em
  // que so cabiam o titulo e o transporte — o grafico de curva e as
  // reguas ficavam escondidos num scroll que ninguem via. Abaixo de 42%
  // da tela a folha nao serve para mexer em nada; cobrir um pedaco do
  // preview e o preco certo.
  final piso = size.height * 0.42;
  return maxH < piso ? piso : maxH;
}

/// Sheet de PARAMETROS que nunca cobre o preview e NAO bloqueia o app:
/// e um bottom sheet PERSISTENTE (sem barreira modal) — da para tocar
/// play, dar scrub e mexer no resto do editor com ele aberto. E o que
/// permite criar keyframes em TEMPOS diferentes sem fechar o painel.
Future<void> showParamSheet(
  BuildContext context, {
  required WidgetBuilder builder,
  double heightFactor = 0.45,
  String? title,
}) async {
  if (!context.mounted) return;
  final localHost = Scaffold.maybeOf(context);
  final editorHost = paramSheetHostKey.currentState;
  final callerRoute = ModalRoute.of(context);
  // O Estudio 3D tem seu proprio Scaffold: nao hospedar sua ferramenta
  // no editor que ficou atras da rota atual.
  final host =
      localHost ??
      (editorHost != null && ModalRoute.of(editorHost.context) == callerRoute
          ? editorHost
          : null);
  // A altura pedida vira a CHEIA; a casca oferece espiada e metade.
  final maxHeight = _sheetMaxHeight(context, heightFactor);
  paramSheetGeneration++;

  // ATALHO PARA VOLTAR: em motion se alterna entre dois ou tres
  // parametros, e reabrir o caminho inteiro toda vez e o que cansa.
  if (title != null) {
    RecentSheets.instance.push(
      title,
      () => showParamSheet(
        context,
        builder: builder,
        heightFactor: heightFactor,
        title: title,
      ),
    );
  }

  // O FECHAR VIAJA COM A FOLHA. Quem esta dentro dela nao tem como
  // saber se ela virou rota (o caminho modal) ou folha persistente do
  // Scaffold — e errar isso fecha o EDITOR inteiro. Por isso o escopo
  // leva o fechar certo de cada caminho junto com o conteudo.
  Widget embrulhado(VoidCallback fechar) => ParamSheetScope(
    close: fechar,
    child: ParamSheetShell(
      maxHeight: maxHeight,
      title: title,
      child: Builder(builder: builder),
    ),
  );

  if (host == null) {
    // Sem Scaffold hospedeiro: cai para modal transparente.
    await showModalBottomSheet<void>(
      context: context,
      backgroundColor: AmColors.panel,
      barrierColor: Colors.transparent,
      isScrollControlled: true,
      // NAO ARRASTA A FOLHA INTEIRA. Dentro de um painel de parametro
      // quase tudo se ajusta arrastando — o pad de mover, os sliders,
      // as reguas. Com a folha arrastavel, cada arrasto que um controle
      // nao reivindicasse puxava a folha para baixo e a fechava no meio
      // do ajuste. A altura se muda pela alca; fechar, pelo botao ou
      // deslizando para a direita.
      enableDrag: false,
      constraints: BoxConstraints(maxHeight: maxHeight),
      shape: const RoundedRectangleBorder(
        borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
      ),
      builder: (ctx) => embrulhado(() => closeParamSheet(ctx)),
    );
    return;
  }
  PersistentBottomSheetController? controller;
  var fechada = false;
  void fechar() {
    if (fechada) return;
    fechada = true;
    controller?.close();
  }

  controller = host.showBottomSheet(
    (ctx) => embrulhado(fechar),
    backgroundColor: AmColors.panel,
    // Ver o enableDrag do caminho modal, logo acima.
    enableDrag: false,
    constraints: BoxConstraints(maxHeight: maxHeight),
    shape: const RoundedRectangleBorder(
      borderRadius: BorderRadius.vertical(top: Radius.circular(20)),
    ),
  );
  _activeParamSheets[host] = fechar;
  try {
    await controller.closed;
  } finally {
    fechada = true;
    if (_activeParamSheets[host] == fechar) _activeParamSheets.remove(host);
  }
}

/// Chip escuro com valor em verde (ex.: "200,0" / "0,00°").
class AmValueChip extends StatelessWidget {
  const AmValueChip({
    super.key,
    required this.text,
    this.label,
    this.width = 120,
    this.compact = false,
  });

  final String text;
  final String? label;
  final double width;
  final bool compact;

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        Container(
          width: width,
          padding: EdgeInsets.symmetric(vertical: compact ? 6 : 10),
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: AmColors.chip,
            borderRadius: BorderRadius.circular(10),
          ),
          child: Text(
            text,
            style: TextStyle(
              fontSize: 19,
              height: compact ? 1.1 : null,
              fontWeight: FontWeight.w600,
              color: AmColors.accent,
            ),
          ),
        ),
        if (label != null) ...[
          const SizedBox(height: 4),
          Text(
            label!,
            style: TextStyle(
              fontSize: 11,
              height: compact ? 1.1 : null,
              color: AmColors.muted,
            ),
          ),
        ],
      ],
    );
  }
}

/// Regua de ticks arrastavel (scrub fino de valor): os ticks deslizam com o
/// valor e o indicador central fica fixo.
///
/// O arrasto ACUMULA desde o inicio do gesto: valor = valor no toque -
/// deslocamento total x sensibilidade. Aplicar cada delta em cima de
/// [value] parece igual, mas [value] so muda quando o dono reconstroi —
/// e chegam dois ou tres eventos de movimento por quadro. Cada evento
/// a mais no mesmo quadro era descartado: um arrasto rapido de 300 px
/// virava o ultimo deltazinho de 3 px, e a superficie de arrasto
/// parecia "nao pegar".
class AmTickRuler extends StatefulWidget {
  const AmTickRuler({
    super.key,
    required this.value,
    required this.onChanged,
    this.unitsPerPixel = 0.5,
    this.height = 64,
    this.accentCenter = true,
    this.min = double.negativeInfinity,
    this.max = double.infinity,
  });

  final double value;
  final ValueChanged<double> onChanged;

  /// Sensibilidade do arrasto.
  final double unitsPerPixel;
  final double height;
  final bool accentCenter;
  final double min;
  final double max;

  @override
  State<AmTickRuler> createState() => _AmTickRulerState();
}

class _AmTickRulerState extends State<AmTickRuler> {
  double _inicio = 0;
  double _acumulado = 0;

  /// UMA ENTREGA POR QUADRO. Chegam dois ou tres eventos de movimento
  /// por quadro, e cada entrega reconstroi o projeto inteiro (preview,
  /// timeline, painel). Entregar todos e pagar a reconstrucao tres
  /// vezes para mostrar um quadro so — e o que se sentia como
  /// microtravamento ao arrastar. O primeiro evento do quadro sai na
  /// hora; os seguintes ficam guardados e o ultimo sai logo depois do
  /// quadro pintar. Nada se perde: o valor final e sempre entregue.
  double? _pendente;
  bool _agendado = false;

  void _entregar(double v) {
    widget.onChanged(v);
  }

  void _descarregar() {
    final p = _pendente;
    _pendente = null;
    if (p != null) _entregar(p);
  }

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onHorizontalDragStart: (_) {
        _inicio = widget.value;
        _acumulado = 0;
      },
      onHorizontalDragUpdate: (d) {
        _acumulado += d.delta.dx;
        final v = (_inicio - _acumulado * widget.unitsPerPixel).clamp(
          widget.min,
          widget.max,
        );
        if (_agendado) {
          _pendente = v;
          return;
        }
        _agendado = true;
        _entregar(v);
        SchedulerBinding.instance.addPostFrameCallback((_) {
          _agendado = false;
          if (mounted) _descarregar();
        });
      },
      onHorizontalDragEnd: (_) => _descarregar(),
      onHorizontalDragCancel: _descarregar,
      child: SizedBox(
        height: widget.height,
        width: double.infinity,
        child: CustomPaint(
          painter: _TickRulerPainter(
            value: widget.value,
            unitsPerPixel: widget.unitsPerPixel,
            accentCenter: widget.accentCenter,
          ),
        ),
      ),
    );
  }
}

class _TickRulerPainter extends CustomPainter {
  const _TickRulerPainter({
    required this.value,
    required this.unitsPerPixel,
    required this.accentCenter,
  });

  final double value;
  final double unitsPerPixel;
  final bool accentCenter;

  @override
  void paint(Canvas canvas, Size size) {
    const spacing = 9.0;
    final tick = Paint()
      ..color = const Color(0xFF43516A)
      ..strokeWidth = 1.6;
    final center = size.width / 2;
    // Ticks deslizam conforme o valor.
    final phase = (value / unitsPerPixel) % spacing;
    final pad = size.height * 0.18;
    for (var x = -phase; x < size.width; x += spacing) {
      canvas.drawLine(Offset(x, pad), Offset(x, size.height - pad), tick);
    }
    final indicator = Paint()
      ..color = accentCenter ? AmColors.accent : Colors.white
      ..strokeWidth = 3;
    canvas.drawLine(
      Offset(center, pad * 0.4),
      Offset(center, size.height - pad * 0.4),
      indicator,
    );
  }

  @override
  bool shouldRepaint(_TickRulerPainter old) =>
      old.value != value || old.accentCenter != accentCenter;
}

/// Botao dos trilhos laterais dos paineis.
class AmRailButton extends StatelessWidget {
  const AmRailButton({
    super.key,
    required this.child,
    this.selected = false,
    this.onTap,
    this.tooltip,
  });

  final Widget child;
  final bool selected;
  final VoidCallback? onTap;
  final String? tooltip;

  @override
  Widget build(BuildContext context) {
    final button = GestureDetector(
      behavior: HitTestBehavior.opaque,
      onTap: onTap,
      child: Container(
        width: 52,
        height: 52,
        margin: const EdgeInsets.symmetric(vertical: 8),
        decoration: BoxDecoration(
          color: selected ? AmColors.accentDim : Colors.transparent,
          borderRadius: BorderRadius.circular(12),
        ),
        child: Center(child: child),
      ),
    );
    return tooltip == null ? button : Tooltip(message: tooltip!, child: button);
  }
}

/// Diamante com "+" (adicionar keyframe), como no trilho esquerdo.
class AmDiamondAdd extends StatelessWidget {
  const AmDiamondAdd({super.key, this.active = false, this.filled = false});

  final bool active;
  final bool filled;

  @override
  Widget build(BuildContext context) {
    final color = active ? AmColors.accent : AmColors.text;
    return Stack(
      alignment: Alignment.center,
      children: [
        Icon(
          filled ? CupertinoIcons.rhombus_fill : CupertinoIcons.rhombus,
          size: 26,
          color: color,
        ),
        Icon(
          filled ? CupertinoIcons.minus : CupertinoIcons.plus,
          size: 11,
          color: filled ? AmColors.bg : color,
        ),
      ],
    );
  }
}

/// Icone de curva (mini bezier) para o trilho esquerdo.
class AmCurveIcon extends StatelessWidget {
  const AmCurveIcon({super.key, this.color = AmColors.text});

  final Color color;

  @override
  Widget build(BuildContext context) {
    return CustomPaint(
      size: const Size(26, 26),
      painter: _CurveIconPainter(color: color),
    );
  }
}

class _CurveIconPainter extends CustomPainter {
  const _CurveIconPainter({required this.color});

  final Color color;

  @override
  void paint(Canvas canvas, Size size) {
    final dash = Paint()
      ..color = color.withValues(alpha: 0.5)
      ..strokeWidth = 1
      ..style = PaintingStyle.stroke;
    // Cantos pontilhados.
    const c = 5.0;
    canvas.drawLine(Offset.zero, const Offset(c, 0), dash);
    canvas.drawLine(Offset.zero, const Offset(0, c), dash);
    canvas.drawLine(
      Offset(size.width, size.height),
      Offset(size.width - c, size.height),
      dash,
    );
    canvas.drawLine(
      Offset(size.width, size.height),
      Offset(size.width, size.height - c),
      dash,
    );
    final curve = Paint()
      ..color = color
      ..strokeWidth = 2
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round;
    final path = Path()
      ..moveTo(2, size.height - 3)
      ..cubicTo(
        size.width * 0.7,
        size.height - 3,
        size.width * 0.3,
        3,
        size.width - 2,
        3,
      );
    canvas.drawPath(path, curve);
  }

  @override
  bool shouldRepaint(_CurveIconPainter old) => old.color != color;
}

/// Formata numero no padrao pt-BR com decimais fixos (200,0 / 0,00).
String amNumber(double v, [int decimals = 1]) =>
    v.toStringAsFixed(decimals).replaceAll('.', ',');
