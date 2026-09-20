import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import 'am_colors.dart';

// A REGUA DE ARRASTO, O GESTO DELA E A CONTA DOS RISCOS — UMA ORIGEM SO.
//
// Este arquivo ja guardou uma COPIA MORTA da regua, com o sinal antigo
// (`valor no toque - deslocamento`: direita DIMINUIA) e os riscos andando
// contra o dedo. Ninguem a importava, mas o nome das classes era identico
// ao das vivas em `am/am_widgets.dart`: qualquer tela nova que importasse
// "a regua do core" nascia invertida. Agora as classes vivas MORAM aqui, e
// `am/am_widgets.dart` so reexporta — o mesmo desenho de `am/am_colors.dart`.
//
// A REGRA DO DONO, que vale para todo controle de arrasto do app:
// arrastar para a DIREITA aumenta, para a ESQUERDA diminui. Ela esta
// escrita em tres lugares deste arquivo, e os tres tem de concordar:
//
//   1. a CONTA    — [AmArrastoDeValor]: valor no toque + dedo x sensibilidade;
//   2. os RISCOS  — [paraCadaRisco]: andam para o mesmo lado que o dedo;
//   3. a LEITURA  — [leituraDePosicao]: o preenchimento cresce para a direita.
//
// `test/sentido_dos_controles_test.dart` cobra os tres.

/// O ESPACO ENTRE DOIS RISCOS: 9 px, medido na referencia.
///
/// E o mesmo numero na regua e na fita porque nenhuma das duas tem escala
/// propria — elas mostram "quanto andou". Se cada parametro espacasse os
/// riscos do seu jeito, o mesmo gesto pareceria mais rapido num campo do
/// que no outro sem que nada tivesse mudado.
const double passoDosRiscos = 9;

/// DE QUANTOS EM QUANTOS RISCOS VEM UM FORTE.
///
/// RISCOS TODOS IGUAIS PARECEM ANDAR AO CONTRARIO. Um padrao periodico de
/// 9 px, amostrado uma vez por quadro, sofre o efeito roda-de-carroca: o
/// olho casa cada risco com o vizinho mais proximo do quadro seguinte, e
/// quando o dedo anda entre 4,5 e 9 px por quadro (270 a 540 px/s a 60 Hz —
/// um arrasto comum de polegar) o vizinho mais proximo e o de TRAS. O
/// numero subia, o desenho parecia descer, e o relato foi "todos os
/// sliders estao invertidos" com a conta certa.
///
/// Com um risco forte a cada cinco, o padrao so se repete a cada 45 px: o
/// limite do engano sobe de 270 para 1350 px/s.
const int riscosPorForte = 5;

/// A ALTURA DO TRILHO DA LEITURA DE POSICAO.
const double alturaDoTrilho = 3;

/// Um risco da fita: onde ele cai na tela e se e dos fortes.
typedef RiscoDaFita = ({double x, bool forte});

/// OS RISCOS DE UMA FITA DE [largura] px, para um [valor].
///
/// OS RISCOS SEGUEM O DEDO: `valor / porPixel` e o quanto de dedo aquele
/// valor representa, e ele entra SOMANDO na posicao. Como a direita
/// aumenta o valor, valor maior = riscos mais a direita — o papel desliza
/// junto com a mao. Com o sinal trocado o numero sobe e o desenho desce,
/// que foi o defeito de 12/09 a 20/09.
///
/// O INDICE E ABSOLUTO: o risco forte e sempre o MESMO risco do papel, e
/// nao "o quinto a partir da borda". Contado pela borda, o forte ficaria
/// parado na tela enquanto os fracos passam por ele.
///
/// [origem] e onde cai o risco zero quando o valor e zero — os pintores
/// passam o centro, para o forte do zero passar por baixo do indicador.
///
/// Entrega por funcao, sem lista: roda dentro de `paint`, a cada quadro
/// do arrasto, e uma lista de registros por quadro e lixo que o coletor
/// teria de varrer no controle mais repintado do painel.
void paraCadaRisco({
  required double valor,
  required double porPixel,
  required double largura,
  required void Function(double x, bool forte) desenhar,
  double origem = 0,
}) {
  if (!(largura > 0)) return;
  // VALOR QUEBRADO OU SENSIBILIDADE ZERO NAO SOMEM COM A FITA: a conta
  // nao tem resposta, entao ela so nao rola.
  var emPixels = valor / porPixel;
  if (!emPixels.isFinite) emPixels = 0;
  final base = emPixels + (origem.isFinite ? origem : 0);
  final inteiro = (base / passoDosRiscos).floor();
  final fase = base - inteiro * passoDosRiscos;
  // Comeca um passo antes de zero para o risco que esta entrando pela
  // esquerda ja aparecer no lugar certo.
  var k = 0;
  for (
    var x = fase - passoDosRiscos;
    x <= largura;
    x += passoDosRiscos, k++
  ) {
    // x = base + indice * passo  =>  indice = k - 1 - inteiro.
    final indice = k - 1 - inteiro;
    desenhar(x, indice % riscosPorForte == 0);
  }
}

/// [paraCadaRisco] em lista — para os testes e para quem nao esta num
/// `paint`.
List<RiscoDaFita> riscosDaFita({
  required double valor,
  required double porPixel,
  required double largura,
  double origem = 0,
}) {
  final riscos = <RiscoDaFita>[];
  paraCadaRisco(
    valor: valor,
    porPixel: porPixel,
    largura: largura,
    origem: origem,
    desenhar: (x, forte) => riscos.add((x: x, forte: forte)),
  );
  return riscos;
}

/// ONDE NO INTERVALO — o trecho preenchido do trilho, em pixels.
///
/// Riscos rolando dizem "andou", e nao "onde estou": com o indicador fixo
/// no centro, nada na regua mostrava para que lado fica o MAIS. Quando o
/// parametro tem comeco e fim de verdade, o trilho responde como um
/// deslizante comum: o preenchimento CRESCE PARA A DIREITA com o valor.
///
/// FAIXA QUE CRUZA O ZERO CRESCE A PARTIR DO ZERO (-100..100 em 0 e trilho
/// vazio; em 50 enche do meio para a direita; em -50, do meio para a
/// esquerda) — encher desde a ponta esquerda diria que "0" ja e metade de
/// alguma coisa.
///
/// NULO quando nao ha intervalo (min ou max infinito, faixa vazia, valor
/// quebrado): fita relativa nao tem posicao, e desenhar uma seria mentir.
({double de, double ate})? leituraDePosicao({
  required double valor,
  required double min,
  required double max,
  required double largura,
}) {
  if (!min.isFinite || !max.isFinite || !valor.isFinite) return null;
  if (!(max > min) || !(largura > 0)) return null;
  double xDe(double v) => (v.clamp(min, max) - min) / (max - min) * largura;
  final noValor = xDe(valor);
  final naOrigem = (min < 0 && max > 0) ? xDe(0) : 0.0;
  return noValor >= naOrigem
      ? (de: naOrigem, ate: noValor)
      : (de: noValor, ate: naOrigem);
}

/// PINTA O TRILHO E O PREENCHIMENTO na base da faixa. Devolve se pintou,
/// para o pintor saber que tem de encurtar os riscos.
///
/// DOIS `drawRRect` E MAIS NADA: sem `saveLayer`, sem sombra, sem mascara.
/// No Impeller cada um desses e um passe de render por quadro, e este e o
/// desenho que fica sob o dedo.
bool pintarLeituraDePosicao(
  Canvas canvas,
  Size size, {
  required double valor,
  required double min,
  required double max,
  bool ativa = true,
}) {
  final leitura = leituraDePosicao(
    valor: valor,
    min: min,
    max: max,
    largura: size.width,
  );
  if (leitura == null || size.height <= alturaDoTrilho) return false;
  final topo = size.height - alturaDoTrilho;
  const raio = Radius.circular(alturaDoTrilho / 2);
  canvas.drawRRect(
    RRect.fromLTRBR(0, topo, size.width, size.height, raio),
    Paint()..color = AmColors.muted.withValues(alpha: .18),
  );
  if (leitura.ate - leitura.de > .5) {
    canvas.drawRRect(
      RRect.fromLTRBR(leitura.de, topo, leitura.ate, size.height, raio),
      Paint()..color = ativa ? AmColors.accent : AmColors.cabecote,
    );
  }
  return true;
}

/// Regua de ticks arrastavel (scrub fino de valor): os ticks deslizam com o
/// valor e o indicador central fica fixo.
///
/// O arrasto ACUMULA desde o inicio do gesto: valor = valor no toque +
/// deslocamento total x sensibilidade (DIREITA AUMENTA). Aplicar cada
/// delta em cima de [value] parece igual, mas [value] so muda quando o
/// dono reconstroi — e chegam dois ou tres eventos de movimento por
/// quadro. Cada evento a mais no mesmo quadro era descartado: um arrasto
/// rapido de 300 px virava o ultimo deltazinho de 3 px, e a superficie de
/// arrasto parecia "nao pegar".
class AmTickRuler extends StatelessWidget {
  const AmTickRuler({
    super.key,
    required this.value,
    required this.onChanged,
    this.unitsPerPixel = 0.5,
    this.height = 64,
    this.accentCenter = true,
    this.min = double.negativeInfinity,
    this.max = double.infinity,
    this.arrastavel = true,
  });

  final double value;
  final ValueChanged<double> onChanged;

  /// Sensibilidade do arrasto.
  final double unitsPerPixel;
  final double height;

  /// A COR do indicador central (realce ou branco). Nao diz nada sobre a
  /// faixa: de onde o preenchimento cresce sai de [min] e [max].
  final bool accentCenter;

  /// Com os dois FINITOS a regua desenha tambem a leitura de posicao
  /// ([leituraDePosicao]).
  final double min;
  final double max;

  /// FALSO quando quem arrasta e a linha inteira, e nao so esta faixa.
  ///
  /// Dois detectores de arrasto horizontal encaixados brigam na arena de
  /// gestos, e quem ganha e o de dentro — que e justamente o mais
  /// estreito. Desligando este, o dedo pega a linha toda.
  final bool arrastavel;

  @override
  Widget build(BuildContext context) {
    final visual = SizedBox(
      height: height,
      width: double.infinity,
      child: CustomPaint(
        painter: _TickRulerPainter(
          value: value,
          unitsPerPixel: unitsPerPixel,
          accentCenter: accentCenter,
          min: min,
          max: max,
        ),
      ),
    );
    if (!arrastavel) return visual;
    return AmArrastoDeValor(
      value: value,
      min: min,
      max: max,
      unitsPerPixel: unitsPerPixel,
      onChanged: onChanged,
      child: visual,
    );
  }
}

/// A SUPERFICIE DE ARRASTO de um numero — o gesto, sem desenho nenhum.
///
/// Separada da regua porque a superficie precisa ser MAIOR do que ela.
/// Numa tela de 375 px, a faixa de riscos da linha "Largura" sobrava com
/// vinte e tres pixels depois do losango, do rotulo e do valor: o beta
/// relatou "nao da pra mexer no botao de largura, so no de altura", e
/// estava certo — nao havia onde pegar. Envolvendo a linha inteira, o
/// alvo passa a ser a linha, e o rotulo e o espaco vazio tambem puxam.
class AmArrastoDeValor extends StatefulWidget {
  const AmArrastoDeValor({
    super.key,
    required this.value,
    required this.onChanged,
    required this.child,
    this.unitsPerPixel = 0.5,
    this.min = double.negativeInfinity,
    this.max = double.infinity,
  });

  final double value;
  final ValueChanged<double> onChanged;
  final Widget child;
  final double unitsPerPixel;
  final double min;
  final double max;

  @override
  State<AmArrastoDeValor> createState() => _AmArrastoDeValorState();
}

class _AmArrastoDeValorState extends State<AmArrastoDeValor> {
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
        // MAIS, e nao menos: dedo para a direita AUMENTA. E o sinal que
        // o vigia de fonte em `sentido_dos_controles_test.dart` guarda.
        final v = (_inicio + _acumulado * widget.unitsPerPixel).clamp(
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
      child: widget.child,
    );
  }
}

class _TickRulerPainter extends CustomPainter {
  const _TickRulerPainter({
    required this.value,
    required this.unitsPerPixel,
    required this.accentCenter,
    required this.min,
    required this.max,
  });

  final double value;
  final double unitsPerPixel;
  final bool accentCenter;
  final double min;
  final double max;

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.width / 2;
    final pad = size.height * 0.18;

    // A LEITURA DE POSICAO vai primeiro e por baixo: o trilho na base, e
    // os riscos param antes dele.
    final temLeitura = pintarLeituraDePosicao(
      canvas,
      size,
      valor: value,
      min: min,
      max: max,
    );
    final fundo = temLeitura
        ? size.height - alturaDoTrilho - 2
        : size.height;

    final fraco = Paint()
      ..color = const Color(0xFF43516A)
      ..strokeWidth = 1.6;
    final forte = Paint()
      ..color = const Color(0xFF7485A3)
      ..strokeWidth = 2;
    // A regua acompanha o dedo: arrastar para a direita faz os riscos
    // irem para a direita ([paraCadaRisco]).
    paraCadaRisco(
      valor: value,
      porPixel: unitsPerPixel,
      largura: size.width,
      origem: center,
      desenhar: (x, eForte) {
        final folga = eForte ? pad * 0.55 : pad;
        final base = size.height - folga;
        canvas.drawLine(
          Offset(x, folga),
          Offset(x, base < fundo ? base : fundo),
          eForte ? forte : fraco,
        );
      },
    );

    // O INDICADOR E O ULTIMO TRACO, por cima de tudo (os testes do pintor
    // contam com isso para separa-lo dos riscos).
    final indicator = Paint()
      ..color = accentCenter ? AmColors.accent : Colors.white
      ..strokeWidth = 3;
    final baseDoIndicador = size.height - pad * 0.4;
    canvas.drawLine(
      Offset(center, pad * 0.4),
      Offset(center, baseDoIndicador < fundo ? baseDoIndicador : fundo),
      indicator,
    );
  }

  // `unitsPerPixel`, `min` e `max` TAMBEM MUDAM O DESENHO: a mesma regua
  // troca de sensibilidade quando o painel troca de parametro sem trocar
  // o valor (dois parametros em 0), e sem eles aqui ela ficava com os
  // riscos do parametro anterior.
  @override
  bool shouldRepaint(_TickRulerPainter old) =>
      old.value != value ||
      old.unitsPerPixel != unitsPerPixel ||
      old.min != min ||
      old.max != max ||
      old.accentCenter != accentCenter;
}
