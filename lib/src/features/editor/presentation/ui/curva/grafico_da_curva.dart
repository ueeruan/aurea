import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';
import 'package:flutter/widgets.dart';

import '../../../../../core/ds/tokens.dart';
import '../../../application/interacao.dart';
import '../../../domain/keyframe.dart';
import '../../am/curve_panel.dart' show AlcasParametricas;

// ===========================================================================
// O GRAFICO DO EDITOR DE CURVA
// ===========================================================================
//
// Um trecho entre dois keyframes, visto de dois jeitos que sao a MESMA
// bezier: o VALOR (quanto do caminho ja foi percorrido) e a VELOCIDADE (a
// derivada — onde a animacao corre e onde freia). Trocar de grafico nunca
// perde nada.
//
// GESTOS, sem briga na arena: um dedo na alca arrasta a alca (sem folga de
// arranque — o arrasto comeca no primeiro movimento); dois dedos sao pinca
// (zoom) e deslocamento juntos; toque duplo reenquadra. Tudo por `Listener`,
// porque a folha em que o grafico mora nao disputa gesto nenhum e o
// reconhecedor de escala come 36 px antes de largar a alca — era o "dificil
// de mexer" do editor antigo.

/// Qual dos dois graficos.
enum ModoDoGrafico { valor, velocidade }

/// A margem do desenho (`easingEditorMargin` da referencia): a bola de 12 da
/// alca em 0 ou 1 cabe inteira.
const double margemDoGrafico = 15;

/// O raio do ALVO de toque da alca: 30 = 60 de diametro, acima dos 44 do
/// toque minimo. O desenho e [AureaDims.raioDoControleDaCurva] (12).
const double raioDeToqueDaAlca = 30;

/// Teto da velocidade no grafico de velocidade, em "vezes a media" — o
/// mesmo do editor antigo.
const double velocidadeMaximaDaCurva = 4;

/// O IMA: a alca gruda num valor notavel a menos de 8 dp dele.
const double _imaDp = 8;

/// A JANELA do grafico, em unidades da curva: x e o progresso do trecho
/// (0..1), y e o valor (0..1, podendo passar com overshoot) ou a velocidade.
@immutable
class VistaDoGrafico {
  const VistaDoGrafico(this.x0, this.x1, this.y0, this.y1);

  final double x0, x1, y0, y1;

  double get largura => x1 - x0;
  double get altura => y1 - y0;

  bool get valida =>
      x0.isFinite &&
      x1.isFinite &&
      y0.isFinite &&
      y1.isFinite &&
      largura > 0 &&
      altura > 0;

  Offset paraTela(Rect area, double x, double y) => Offset(
    area.left + (x - x0) / largura * area.width,
    area.bottom - (y - y0) / altura * area.height,
  );

  (double, double) daTela(Rect area, Offset p) => (
    x0 + (p.dx - area.left) / area.width * largura,
    y0 + (area.bottom - p.dy) / area.height * altura,
  );

  /// A JANELA QUE CABE A CURVA INTEIRA — o reenquadrar do toque duplo e a
  /// vista de quem nao deu zoom. Com overshoot ligado, abre espaco para a
  /// alca passar dos limites mesmo antes de ela passar.
  static VistaDoGrafico enquadrar(
    Easing e,
    ModoDoGrafico modo, {
    bool overshoot = false,
  }) {
    var lo = 0.0, hi = 1.0;
    void ver(double v) {
      if (!v.isFinite) return;
      if (v < lo) lo = v;
      if (v > hi) hi = v;
    }

    const n = 48;
    if (modo == ModoDoGrafico.valor) {
      for (var i = 0; i <= n; i++) {
        ver(e.transform(i / n));
      }
      if (e.type == EasingType.cubicBezier) {
        ver(e.y1);
        ver(e.y2);
      }
      for (final (_, y) in AlcasParametricas.de(e)) {
        ver(y);
      }
      if (overshoot) {
        ver(-0.5);
        ver(1.5);
      }
    } else {
      hi = 1.5;
      for (var i = 0; i <= n; i++) {
        ver(e.speedAt(i / n).clamp(-velocidadeMaximaDaCurva,
            velocidadeMaximaDaCurva));
      }
    }
    final folga = (hi - lo) * 0.08;
    return VistaDoGrafico(0, 1, lo - folga, hi + folga);
  }

  /// A janela que poe o ponto [ancora] (unidades da curva) debaixo do
  /// ponto de tela [foco], com [largura]/[altura] novas. E a conta da
  /// pinca: zoom e deslocamento de uma vez, sem a curva escorregar.
  static VistaDoGrafico ancorada({
    required Rect area,
    required (double, double) ancora,
    required Offset foco,
    required double largura,
    required double altura,
  }) {
    final x0 = ancora.$1 - (foco.dx - area.left) / area.width * largura;
    final y0 = ancora.$2 - (area.bottom - foco.dy) / area.height * altura;
    return VistaDoGrafico(x0, x0 + largura, y0, y0 + altura);
  }

  @override
  bool operator ==(Object other) =>
      other is VistaDoGrafico &&
      other.x0 == x0 &&
      other.x1 == x1 &&
      other.y0 == y0 &&
      other.y1 == y1;

  @override
  int get hashCode => Object.hash(x0, x1, y0, y1);

  @override
  String toString() => 'VistaDoGrafico($x0..$x1, $y0..$y1)';
}

/// ONDE ESTAO AS ALCAS de [e] no grafico [modo], em unidades da curva.
///
/// Valor: as duas alcas da bezier, ou as amarelas das familias
/// parametricas (quique, elastico, degraus...). Velocidade: a velocidade de
/// SAIDA e a de CHEGADA, cada uma na sua influencia (x1 e x2).
List<(double, double)> alcasDaCurva(Easing e, ModoDoGrafico modo) {
  if (modo == ModoDoGrafico.velocidade) {
    if (e.type != EasingType.cubicBezier) return const [];
    return [(e.x1, velocidadeDeSaida(e)), (e.x2, velocidadeDeChegada(e))];
  }
  if (e.type == EasingType.cubicBezier) return [(e.x1, e.y1), (e.x2, e.y2)];
  return AlcasParametricas.de(e);
}

double _velocidadeNoGrafico(double v) => v.isNaN
    ? 1
    : v.clamp(0.0, velocidadeMaximaDaCurva).toDouble();

/// A velocidade com que o trecho SAI do primeiro keyframe.
double velocidadeDeSaida(Easing e) => _velocidadeNoGrafico(e.speedAt(0));

/// A velocidade com que o trecho CHEGA no segundo keyframe.
double velocidadeDeChegada(Easing e) => _velocidadeNoGrafico(e.speedAt(1));

/// O que o ima fez num movimento de alca (para o haptico e as guias).
typedef ImaDaAlca = ({bool x, bool y, bool linear});

const ImaDaAlca _semIma = (x: false, y: false, linear: false);

/// A CURVA COM A ALCA [alca] (0 ou 1) LEVADA a ([x], [y]), em unidades da
/// curva — a conta inteira de um passo do arrasto, pura para poder ser
/// testada sem dedo.
///
/// [tolX]/[tolY] sao o alcance do ima em unidades da curva (8 dp na tela).
/// [yInicial] e onde a alca estava ao ser agarrada: sem overshoot o valor
/// fica em 0..1, MAS uma alca que ja estava fora (uma curva que veio de um
/// preset de overshoot) nao e puxada para dentro so por ser tocada.
///
/// NENHUM PASSO CORROMPE A CURVA: entrada nao finita devolve a curva como
/// estava, e toda saida e finita.
({Easing curva, ImaDaAlca ima}) moverAlcaDaCurva(
  Easing e,
  ModoDoGrafico modo,
  int alca,
  double x,
  double y, {
  double tolX = 0,
  double tolY = 0,
  bool overshoot = false,
  double? yInicial,
}) {
  if (!x.isFinite || !y.isFinite) return (curva: e, ima: _semIma);
  var ix = false, iy = false, il = false;
  Easing nova;
  if (modo == ModoDoGrafico.velocidade) {
    if (e.type != EasingType.cubicBezier) return (curva: e, ima: _semIma);
    var px = x.clamp(0.02, 0.98).toDouble();
    var v = y.clamp(0.0, velocidadeMaximaDaCurva).toDouble();
    // O ima da velocidade: parada (0) e a velocidade MEDIA (1); e a
    // influencia de um terco, que e a do linear. Tudo junto = linear.
    for (final alvo in const [0.0, 1.0]) {
      if ((v - alvo).abs() <= tolY) {
        v = alvo;
        iy = true;
      }
    }
    final influencia = alca == 0 ? 1 / 3 : 2 / 3;
    if ((px - influencia).abs() <= tolX) {
      px = influencia;
      ix = true;
    }
    if (alca == 0) {
      // influencia = x1; velocidade de saida = y1 / x1.
      final x1 = math.min(px, e.x2 - 0.02).clamp(0.0, 1.0).toDouble();
      nova = e.copyWith(x1: x1, y1: (v * x1).clamp(-2.0, 2.0).toDouble());
    } else {
      // influencia = 1 - x2; velocidade de chegada = (1 - y2) / (1 - x2).
      final x2 = math.max(px, e.x1 + 0.02).clamp(0.0, 1.0).toDouble();
      nova = e.copyWith(
        x2: x2,
        y2: (1 - v * (1 - x2)).clamp(-1.0, 3.0).toDouble(),
      );
    }
  } else if (e.type == EasingType.cubicBezier) {
    final y0 = yInicial ?? (alca == 0 ? e.y1 : e.y2);
    final lo = overshoot ? -1.5 : math.min(0.0, y0);
    final hi = overshoot ? 2.5 : math.max(1.0, y0);
    var px = x.clamp(0.0, 1.0).toDouble();
    var py = y.clamp(lo, hi).toDouble();
    for (final alvo in const [0.0, 1.0]) {
      if ((px - alvo).abs() <= tolX) {
        px = alvo;
        ix = true;
      }
      if ((py - alvo).abs() <= tolY) {
        py = alvo;
        iy = true;
      }
    }
    // LINEAR: a alca em cima da diagonal (y = x). A distancia e medida na
    // TELA (a elipse dos dois alcances), porque o grafico nao e quadrado.
    if (!ix && !iy && tolX > 0 && tolY > 0) {
      final m = (px + py) / 2;
      final dx = (px - m) / tolX, dy = (py - m) / tolY;
      if (dx * dx + dy * dy <= 1) {
        px = m.clamp(0.0, 1.0).toDouble();
        py = px;
        il = true;
      }
    }
    nova = alca == 0 ? e.copyWith(x1: px, y1: py) : e.copyWith(x2: px, y2: py);
  } else {
    nova = alca == 0
        ? AlcasParametricas.comAlcaA(e, x)
        : AlcasParametricas.comAlcaB(e, y);
  }
  final finita =
      nova.x1.isFinite &&
      nova.y1.isFinite &&
      nova.x2.isFinite &&
      nova.y2.isFinite &&
      nova.intensity.isFinite &&
      nova.smooth.isFinite;
  if (!finita) return (curva: e, ima: _semIma);
  return (curva: nova, ima: (x: ix, y: iy, linear: il));
}

/// O GRAFICO: desenho, alcas, zoom, deslocamento e reenquadrar.
class GraficoDaCurva extends StatefulWidget {
  const GraficoDaCurva({
    super.key,
    required this.curva,
    required this.aoMudar,
    this.modo = ModoDoGrafico.valor,
    this.aoComecar,
    this.aoTerminar,
    this.percorrido,
    this.overshoot = false,
    this.valorDoInicio,
    this.valorDoFim,
    this.tempoDoInicio,
    this.tempoDoFim,
  });

  final Easing curva;
  final ModoDoGrafico modo;

  /// Cada passo do arrasto de uma alca.
  final ValueChanged<Easing> aoMudar;

  /// Comeco e fim do ARRASTO — o editor abre e fecha o gesto de desfazer
  /// aqui, e um arrasto vira um passo so.
  final VoidCallback? aoComecar;
  final VoidCallback? aoTerminar;

  /// Onde o cabecote esta dentro do trecho (0..1); nulo = fora. Escutado,
  /// e nao reconstruido: o cabecote anda sessenta vezes por segundo e so o
  /// pintor precisa saber.
  final ValueListenable<double?>? percorrido;

  final bool overshoot;

  /// Rotulos dos eixos (numeros, sem traducao): o valor em cada keyframe e
  /// o instante de cada um.
  final String? valorDoInicio;
  final String? valorDoFim;
  final String? tempoDoInicio;
  final String? tempoDoFim;

  @override
  State<GraficoDaCurva> createState() => GraficoDaCurvaState();
}

class GraficoDaCurvaState extends State<GraficoDaCurva>
    with SingleTickerProviderStateMixin {
  /// A vista escolhida pela pinca (nula = enquadrada na curva).
  VistaDoGrafico? _vistaDoUsuario;

  /// A vista CONGELADA durante um arrasto: sem isto, a alca que passa do
  /// limite reenquadraria o grafico debaixo do dedo, e o dedo perderia a
  /// alca.
  VistaDoGrafico? _vistaDoArrasto;

  Size _tamanho = Size.zero;

  final Map<int, Offset> _dedos = {};

  // O arrasto de alca.
  int? _dedoDaAlca;
  int? _alca;
  Offset? _dedoInicial;
  (double, double)? _pontoInicial;
  bool _arrastando = false;
  ImaDaAlca _ima = _semIma;

  // A pinca.
  VistaDoGrafico? _vistaDaPinca;
  (double, double)? _ancoraDaPinca;
  double _vaoInicial = 1;

  /// A BOLINHA DE PREVIA: corre a curva de ponta a ponta enquanto a alca
  /// e arrastada — o tempo da animacao sentido, e nao so desenhado.
  late final AnimationController _previa = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 1100),
  );

  /// A vista em vigor.
  VistaDoGrafico get vista =>
      _vistaDoUsuario ??
      _vistaDoArrasto ??
      VistaDoGrafico.enquadrar(
        widget.curva,
        widget.modo,
        overshoot: widget.overshoot,
      );

  /// A alca na mao (nula = nenhuma).
  int? get alcaAgarrada => _alca;

  Rect get _area => (Offset.zero & _tamanho).deflate(margemDoGrafico);

  /// O centro da alca [i] em coordenadas LOCAIS do grafico — para teste e
  /// para quem quiser apontar para ela.
  @visibleForTesting
  Offset? centroDaAlca(int i) {
    final alcas = alcasDaCurva(widget.curva, widget.modo);
    if (i < 0 || i >= alcas.length || _tamanho.isEmpty) return null;
    return vista.paraTela(_area, alcas[i].$1, alcas[i].$2);
  }

  /// REENQUADRAR (o toque duplo): volta a vista que cabe a curva.
  void reenquadrar() => setState(() => _vistaDoUsuario = null);

  @override
  void didUpdateWidget(GraficoDaCurva oldWidget) {
    super.didUpdateWidget(oldWidget);
    // Outro grafico, outra escala: o zoom do de valor nao serve ao de
    // velocidade.
    if (oldWidget.modo != widget.modo) {
      _vistaDoUsuario = null;
      _vistaDoArrasto = null;
    }
  }

  @override
  void dispose() {
    // O WIDGET SAIU COM O DEDO NA ALCA: o gesto de desfazer tem de fechar,
    // senao nenhuma edicao seguinte entra no historico.
    if (_arrastando) widget.aoTerminar?.call();
    _previa.dispose();
    super.dispose();
  }

  int? _alcaSob(Offset p) {
    final alcas = alcasDaCurva(widget.curva, widget.modo);
    int? melhor;
    var menor = raioDeToqueDaAlca * raioDeToqueDaAlca;
    for (var i = 0; i < alcas.length; i++) {
      final c = vista.paraTela(_area, alcas[i].$1, alcas[i].$2);
      final d = (c - p).distanceSquared;
      if (d <= menor) {
        menor = d;
        melhor = i;
      }
    }
    return melhor;
  }

  void _aoTocar(PointerDownEvent e) {
    _dedos[e.pointer] = e.localPosition;
    if (_dedos.length == 1) {
      final alca = _alcaSob(e.localPosition);
      if (alca == null) return;
      final pontos = alcasDaCurva(widget.curva, widget.modo);
      setState(() {
        _dedoDaAlca = e.pointer;
        _alca = alca;
        _dedoInicial = e.localPosition;
        _pontoInicial = pontos[alca];
        _vistaDoArrasto = _vistaDoUsuario == null ? vista : null;
      });
      return;
    }
    // Segundo dedo: vira pinca. A alca que estava na mao e largada ONDE
    // ESTA — o passo de desfazer fecha com o que ja foi feito.
    _largarAlca();
    _comecarPinca();
  }

  void _aoMover(PointerMoveEvent e) {
    if (!_dedos.containsKey(e.pointer)) return;
    _dedos[e.pointer] = e.localPosition;
    if (_dedos.length >= 2 && _vistaDaPinca != null) {
      _atualizarPinca();
    } else if (_dedoDaAlca == e.pointer) {
      _moverAlca(e.localPosition);
    }
  }

  void _aoSoltar(PointerEvent e) {
    _dedos.remove(e.pointer);
    if (_dedoDaAlca == e.pointer) _largarAlca();
    // De dois para um dedo a pinca acaba, e o dedo que sobrou NAO agarra
    // alca nenhuma: ele nao comecou em cima dela.
    if (_dedos.length < 2) _vistaDaPinca = null;
    if (_dedos.length >= 2) _comecarPinca();
  }

  void _moverAlca(Offset dedo) {
    final alca = _alca, inicio = _dedoInicial, ponto = _pontoInicial;
    if (alca == null || inicio == null || ponto == null) return;
    final area = _area;
    if (area.width <= 0 || area.height <= 0) return;
    final v = vista;
    // RELATIVO: a alca anda o que o dedo andou. Agarrar a bola pela borda
    // nao a faz pular para debaixo do dedo — e o dedo nao a cobre.
    final alvo = v.paraTela(area, ponto.$1, ponto.$2) + (dedo - inicio);
    final (x, y) = v.daTela(area, alvo);
    final r = moverAlcaDaCurva(
      widget.curva,
      widget.modo,
      alca,
      x,
      y,
      tolX: _imaDp / area.width * v.largura,
      tolY: _imaDp / area.height * v.altura,
      overshoot: widget.overshoot,
      yInicial: ponto.$2,
    );
    if (!_arrastando) {
      _arrastando = true;
      widget.aoComecar?.call();
      _previa.repeat();
    }
    Interacao.marcar();
    final grudou = r.ima.x || r.ima.y || r.ima.linear;
    final grudava = _ima.x || _ima.y || _ima.linear;
    // HAPTICO LEVE SO NA CHEGADA ao ima, e nao a cada passo em cima dele.
    if (grudou && !grudava) HapticFeedback.lightImpact();
    if (r.ima != _ima) setState(() => _ima = r.ima);
    widget.aoMudar(r.curva);
  }

  void _largarAlca() {
    final estava = _arrastando;
    if (_alca == null && !estava) return;
    setState(() {
      _dedoDaAlca = null;
      _alca = null;
      _dedoInicial = null;
      _pontoInicial = null;
      _arrastando = false;
      _ima = _semIma;
      _vistaDoArrasto = null;
    });
    _previa
      ..stop()
      ..value = 0;
    if (estava) widget.aoTerminar?.call();
  }

  (Offset, double) _focoEVao() {
    final pontos = _dedos.values.take(2).toList();
    final foco = (pontos[0] + pontos[1]) / 2;
    return (foco, (pontos[0] - pontos[1]).distance);
  }

  void _comecarPinca() {
    if (_dedos.length < 2) return;
    final (foco, vao) = _focoEVao();
    final v = vista;
    _vistaDaPinca = v;
    _ancoraDaPinca = v.daTela(_area, foco);
    _vaoInicial = math.max(vao, 1);
  }

  void _atualizarPinca() {
    final base = _vistaDaPinca, ancora = _ancoraDaPinca;
    if (base == null || ancora == null) return;
    final (foco, vao) = _focoEVao();
    // Zoom igual nos dois eixos, com teto nas duas pontas: de 1/50 do
    // trecho ate 20 trechos inteiros.
    var s = vao / _vaoInicial;
    if (!s.isFinite || s <= 0) return;
    s = s.clamp(base.largura / 20, base.largura / 0.02).toDouble();
    final nova = VistaDoGrafico.ancorada(
      area: _area,
      ancora: ancora,
      foco: foco,
      largura: base.largura / s,
      altura: base.altura / s,
    );
    if (!nova.valida) return;
    Interacao.marcar();
    setState(() => _vistaDoUsuario = nova);
  }

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, c) {
        _tamanho = Size(c.maxWidth, c.maxHeight);
        return GestureDetector(
          behavior: HitTestBehavior.opaque,
          onDoubleTap: reenquadrar,
          child: Listener(
            behavior: HitTestBehavior.opaque,
            onPointerDown: _aoTocar,
            onPointerMove: _aoMover,
            onPointerUp: _aoSoltar,
            onPointerCancel: _aoSoltar,
            child: CustomPaint(
              size: _tamanho,
              painter: PintorDaCurva(
                curva: widget.curva,
                modo: widget.modo,
                vista: vista,
                alcaAgarrada: _alca,
                ima: _ima,
                percorrido: widget.percorrido,
                previa: _previa,
                mostrarPrevia: _arrastando,
                valorDoInicio: widget.valorDoInicio,
                valorDoFim: widget.valorDoFim,
                tempoDoInicio: widget.tempoDoInicio,
                tempoDoFim: widget.tempoDoFim,
              ),
            ),
          ),
        );
      },
    );
  }
}

/// O DESENHO do grafico. Cores so do tema em vigor (getters).
class PintorDaCurva extends CustomPainter {
  PintorDaCurva({
    required this.curva,
    required this.modo,
    required this.vista,
    required this.previa,
    this.alcaAgarrada,
    this.ima = _semIma,
    this.percorrido,
    this.mostrarPrevia = false,
    this.valorDoInicio,
    this.valorDoFim,
    this.tempoDoInicio,
    this.tempoDoFim,
  }) : super(
         repaint: Listenable.merge([previa, ?percorrido]),
       );

  final Easing curva;
  final ModoDoGrafico modo;
  final VistaDoGrafico vista;
  final Animation<double> previa;
  final int? alcaAgarrada;
  final ImaDaAlca ima;
  final ValueListenable<double?>? percorrido;
  final bool mostrarPrevia;
  final String? valorDoInicio, valorDoFim, tempoDoInicio, tempoDoFim;

  double _y(double t) {
    final v = modo == ModoDoGrafico.valor ? curva.transform(t) : curva.speedAt(t);
    if (!v.isFinite) return v.isNegative ? -1e3 : 1e3;
    return v.clamp(-1e3, 1e3).toDouble();
  }

  /// O passo da grade que deixa pelo menos 28 dp entre linhas.
  static double _passo(double unidadesPorDp) {
    for (final p in const [0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0]) {
      if (p / unidadesPorDp >= 28) return p;
    }
    return 20;
  }

  @override
  void paint(Canvas canvas, Size size) {
    final area = (Offset.zero & size).deflate(margemDoGrafico);
    if (area.width <= 0 || area.height <= 0 || !vista.valida) return;
    canvas.save();
    canvas.clipRect(Offset.zero & size);
    Offset p(double x, double y) => vista.paraTela(area, x, y);

    // --- grade: fina a cada passo, forte nos limites do trecho (0 e 1).
    final fina = Paint()
      ..color = AureaCores.textoSecundario.withValues(alpha: .14)
      ..strokeWidth = .5;
    final forte = Paint()
      ..color = AureaCores.textoSecundario.withValues(alpha: .42)
      ..strokeWidth = 1;
    final passoX = _passo(vista.largura / area.width);
    for (var k = (vista.x0 / passoX).ceil(); k * passoX <= vista.x1; k++) {
      final x = k * passoX;
      final notavel = (x - 0).abs() < 1e-9 || (x - 1).abs() < 1e-9;
      final sx = p(x, 0).dx;
      canvas.drawLine(Offset(sx, 0), Offset(sx, size.height), notavel ? forte : fina);
    }
    final passoY = _passo(vista.altura / area.height);
    for (var k = (vista.y0 / passoY).ceil(); k * passoY <= vista.y1; k++) {
      final y = k * passoY;
      final notavel = (y - 0).abs() < 1e-9 || (y - 1).abs() < 1e-9;
      final sy = p(0, y).dy;
      canvas.drawLine(Offset(0, sy), Offset(size.width, sy), notavel ? forte : fina);
    }

    // --- guias do ima: a linha em que a alca grudou.
    final guia = Paint()
      ..color = AureaCores.destaque.withValues(alpha: .6)
      ..strokeWidth = 1;
    final alcas = alcasDaCurva(curva, modo);
    final agarrada = alcaAgarrada;
    if (agarrada != null && agarrada < alcas.length) {
      final (ax, ay) = alcas[agarrada];
      if (ima.x) {
        final sx = p(ax, 0).dx;
        _tracejada(canvas, Offset(sx, 0), Offset(sx, size.height), guia);
      }
      if (ima.y) {
        final sy = p(0, ay).dy;
        _tracejada(canvas, Offset(0, sy), Offset(size.width, sy), guia);
      }
      if (ima.linear) _tracejada(canvas, p(0, 0), p(1, 1), guia);
    }

    // --- a curva, traco 3.
    final traco = Paint()
      ..color = AureaCores.destaque
      ..strokeWidth = AureaDims.tracoDaCurva
      ..style = PaintingStyle.stroke
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round;
    final caminho = Path();
    if (modo == ModoDoGrafico.valor && curva.type == EasingType.cubicBezier) {
      // A BEZIER EXATA, e nao amostrada: e a mesma cubica do motor.
      final a = p(0, 0), b = p(curva.x1.clamp(0.0, 1.0), curva.y1);
      final c = p(curva.x2.clamp(0.0, 1.0), curva.y2), d = p(1, 1);
      caminho
        ..moveTo(a.dx, a.dy)
        ..cubicTo(b.dx, b.dy, c.dx, c.dy, d.dx, d.dy);
    } else {
      const n = 120;
      for (var i = 0; i <= n; i++) {
        final t = i / n;
        final q = p(t, _y(t));
        if (i == 0) {
          caminho.moveTo(q.dx, q.dy);
        } else {
          caminho.lineTo(q.dx, q.dy);
        }
      }
    }
    canvas.drawPath(caminho, traco);

    // --- alcas: tangentes finas, bola de 12.
    final tangente = Paint()
      ..color = AureaCores.texto.withValues(alpha: .55)
      ..strokeWidth = 1;
    if (curva.type == EasingType.cubicBezier && alcas.length == 2) {
      if (modo == ModoDoGrafico.valor) {
        canvas.drawLine(p(0, 0), p(alcas[0].$1, alcas[0].$2), tangente);
        canvas.drawLine(p(1, 1), p(alcas[1].$1, alcas[1].$2), tangente);
      } else {
        // A velocidade de cada ponta vale da borda ate a influencia.
        canvas.drawLine(p(0, alcas[0].$2), p(alcas[0].$1, alcas[0].$2), tangente);
        canvas.drawLine(p(alcas[1].$1, alcas[1].$2), p(1, alcas[1].$2), tangente);
      }
    }
    if (modo == ModoDoGrafico.valor) {
      final ancora = Paint()..color = AureaCores.destaque;
      canvas.drawCircle(p(0, 0), 4.5, ancora);
      canvas.drawCircle(p(1, 1), 4.5, ancora);
    }
    final parametrica = curva.type != EasingType.cubicBezier;
    for (var i = 0; i < alcas.length; i++) {
      final c = p(alcas[i].$1, alcas[i].$2);
      canvas.drawCircle(
        c,
        AureaDims.raioDoControleDaCurva,
        Paint()..color = parametrica ? AureaCores.keyframe : AureaCores.texto,
      );
      if (i == agarrada) {
        canvas.drawCircle(
          c,
          AureaDims.raioDoControleDaCurva,
          Paint()
            ..color = AureaCores.destaque
            ..style = PaintingStyle.stroke
            ..strokeWidth = 2.5,
        );
      }
    }

    // --- o cabecote dentro do trecho.
    final andando = percorrido?.value;
    if (andando != null && andando.isFinite) {
      final c = p(andando, _y(andando));
      canvas.drawLine(
        Offset(c.dx, 0),
        Offset(c.dx, size.height),
        Paint()
          ..color = AureaCores.cabecote.withValues(alpha: .45)
          ..strokeWidth = 1,
      );
      canvas.drawCircle(c, 6, Paint()..color = AureaCores.cabecote);
    }

    // --- a bolinha de previa, enquanto a alca anda.
    if (mostrarPrevia) {
      final u = previa.value;
      final c = p(u, _y(u));
      canvas.drawCircle(c, 7, Paint()..color = AureaCores.keyframe);
      canvas.drawCircle(
        c,
        7,
        Paint()
          ..color = AureaCores.palco
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.5,
      );
    }

    // --- rotulos dos eixos (numeros).
    final estilo = TextStyle(
      fontSize: 10,
      color: AureaCores.textoSecundario,
      fontFeatures: const [FontFeature.tabularFigures()],
    );
    void rotulo(String? texto, Offset onde, {bool direita = false}) {
      if (texto == null || texto.isEmpty) return;
      final tp = TextPainter(
        text: TextSpan(text: texto, style: estilo),
        textDirection: TextDirection.ltr,
        maxLines: 1,
      )..layout(maxWidth: area.width / 2);
      tp.paint(canvas, direita ? onde - Offset(tp.width, 0) : onde);
    }

    if (modo == ModoDoGrafico.valor) {
      rotulo(valorDoInicio, Offset(area.left + 4, p(0, 0).dy - 14));
      rotulo(valorDoFim, Offset(area.left + 4, p(0, 1).dy + 2));
    } else {
      rotulo('1×', Offset(area.left + 4, p(0, 1).dy - 14));
    }
    rotulo(tempoDoInicio, Offset(area.left + 2, size.height - 13));
    rotulo(
      tempoDoFim,
      Offset(area.right - 2, size.height - 13),
      direita: true,
    );
    canvas.restore();
  }

  static void _tracejada(Canvas canvas, Offset a, Offset b, Paint paint) {
    final d = b - a;
    final total = d.distance;
    if (total <= 0 || !total.isFinite) return;
    final u = d / total;
    for (var s = 0.0; s < total; s += 8) {
      canvas.drawLine(a + u * s, a + u * math.min(s + 4, total), paint);
    }
  }

  @override
  bool shouldRepaint(PintorDaCurva old) =>
      old.curva != curva ||
      old.modo != modo ||
      old.vista != vista ||
      old.alcaAgarrada != alcaAgarrada ||
      old.ima != ima ||
      old.mostrarPrevia != mostrarPrevia ||
      old.percorrido != percorrido ||
      old.valorDoInicio != valorDoInicio ||
      old.valorDoFim != valorDoFim ||
      old.tempoDoInicio != tempoDoInicio ||
      old.tempoDoFim != tempoDoFim;
}
