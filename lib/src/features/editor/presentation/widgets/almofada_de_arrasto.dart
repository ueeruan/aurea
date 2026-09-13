import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../../../../core/ui/am_colors.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

/// A FOLGA ENTRE O L E A BORDA DA AREA: o canto precisa de ar do lado
/// de fora para ser lido como marca. Colado na borda ele vira o canto
/// de uma caixa — o desenho que esta reforma existe para tirar da tela.
const _folgaDoCanto = 12.0;

/// O BRACO DO L: curto o bastante para nunca fechar o retangulo, longo
/// o bastante para o olho continuar as duas linhas sozinho. A
/// referencia nao publica esta medida — ela so descreve "quatro cantos
/// em L" —, entao o numero e nosso e pode ser discutido.
const _bracoDoCanto = 26.0;

/// A FORCA DO TRACO DO CANTO. Mais aceso que as linhas da fita, que sao
/// 25%: aqui sao quatro tracos curtos e la e um campo repetido dezenas
/// de vezes, e marca esparsa precisa de mais contraste para pesar o
/// mesmo no olho.
const _forcaDoCanto = .45;

/// O LADO DE RESERVA, para quando o pai nao limita a area.
///
/// Sem ele a almofada nasce do tamanho da propria dica: uma tira de uma
/// linha de texto, onde a especificacao pede "area grande" — e com
/// menos de 24 px de altura os quatro cantos nem chegam a ser
/// desenhados. O numero e o que sobra do painel de 260 px abaixo da
/// linha de campos; quem monta o painel manda a altura de verdade, e
/// entao esta aqui nem e consultada.
const _ladoDeReserva = 160.0;

/// A SUPERFICIE DO MODO MOVER: uma almofada, e nao dois deslizantes.
///
/// Posicao e 2D. Dois deslizantes separados obrigam a pensar em eixos —
/// quem quer arrastar a camada para o canto de cima precisa decompor o
/// gesto em X e depois Y, que e exatamente o trabalho que o dedo ja
/// sabia fazer sozinho. A almofada devolve esse gesto inteiro
/// (`docs/painel-de-transformacao-alight.md`, secao "A almofada").
///
/// O ARRASTO E RELATIVO: 1 px de dedo vale 1 px de composicao na escala
/// 1. Nao ha "onde no intervalo", porque posicao nao tem intervalo.
///
/// [aoMover] recebe o deslocamento ACUMULADO desde o inicio do arrasto,
/// e nao o delta do quadro. E o que permite ao integrador guardar a
/// posicao no `aoComecar` e escrever `inicio + delta` a cada quadro:
/// somar deltas quadro a quadro arredondaria em cada soma, e a camada
/// terminaria alguns pixels longe de onde o dedo parou.
class AlmofadaDeArrasto extends StatefulWidget {
  const AlmofadaDeArrasto({
    required this.aoMover,
    this.aoComecar,
    this.aoTerminar,
    this.dica = 'Deslize aqui para mover a camada',
    this.rotulo = 'Mover a camada',
    this.cabecalho,
    super.key,
  });

  /// Conteudo opcional no topo (ex: campos X, Y, Z), emoldurado pelos cantos.
  final Widget? cabecalho;

  /// O deslocamento acumulado desde o inicio do arrasto, em pixels de
  /// dedo.
  final void Function(Offset delta) aoMover;

  /// Chamado no PRIMEIRO PIXEL ANDADO, e nao no toque: um dedo que
  /// encosta e sai nao editou nada. E aqui que o integrador tira a foto
  /// da posicao inicial, sempre antes do primeiro [aoMover].
  final VoidCallback? aoComecar;

  /// Chamado quando o dedo sai, inclusive quando o gesto e cancelado —
  /// um arrasto interrompido tem que fechar o lote de desfazer igual a
  /// um arrasto concluido.
  final VoidCallback? aoTerminar;

  /// A instrucao no centro. Some enquanto o dedo esta na almofada.
  final String dica;

  /// O rotulo de acessibilidade, e o ancoradouro dos testes.
  final String rotulo;

  @override
  State<AlmofadaDeArrasto> createState() => _AlmofadaDeArrastoState();
}

class _AlmofadaDeArrastoState extends State<AlmofadaDeArrasto> {
  /// O ACUMULADO DO ARRASTO VIGENTE. Vive no estado, e nao numa local
  /// do gesto, porque o `onPanUpdate` chega quadro a quadro. E e a SOMA
  /// DOS DELTAS, e nao a distancia ate o ponto de partida, para o dedo
  /// poder sair da almofada e voltar sem que o gesto se perca.
  Offset _acumulado = Offset.zero;

  /// SE O DEDO ESTA NA ALMOFADA. Existe so para a dica sumir, e por
  /// isso vale desde o toque: a especificacao manda o texto sair
  /// enquanto o dedo esta ali, ande ele ou nao.
  bool _dedoNaAlmofada = false;

  /// SE O LOTE DE EDICAO JA FOI ABERTO. Ele nasce no primeiro
  /// movimento, e nao no toque: quando a almofada e o unico alvo do
  /// gesto, o reconhecedor de arrasto ganha a arena no proprio toque e
  /// um tap chega aqui como comecar mais terminar sem um pixel andado.
  /// Sem esta separacao o integrador abriria e fecharia um lote vazio,
  /// e o desfazer ficaria com um passo que nao desfaz nada.
  bool _loteAberto = false;

  void _comecar(DragStartDetails _) {
    _acumulado = Offset.zero;
    setState(() => _dedoNaAlmofada = true);
  }

  void _atualizar(DragUpdateDetails d) {
    if (!_loteAberto) {
      _loteAberto = true;
      // A FOTO DA POSICAO INICIAL VEM ANTES DO PRIMEIRO PASSO: quem
      // escreve `inicio + delta` precisa do inicio ja guardado quando o
      // primeiro delta chegar, ou o primeiro quadro do arrasto sai de
      // uma posicao que ninguem anotou.
      widget.aoComecar?.call();
    }
    _acumulado += d.delta;
    widget.aoMover(_acumulado);
  }

  void _terminar() {
    if (_dedoNaAlmofada) setState(() => _dedoNaAlmofada = false);
    if (!_loteAberto) return;
    _loteAberto = false;
    widget.aoTerminar?.call();
  }

  @override
  Widget build(BuildContext context) => Semantics(
        container: true,
        excludeSemantics: widget.cabecalho == null,
        button: true,
        label: widget.rotulo,
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onPanStart: _comecar,
          onPanUpdate: _atualizar,
          onPanEnd: (_) => _terminar(),
          onPanCancel: _terminar,
          child: LayoutBuilder(
            // A ALMOFADA PRECISA DE UM TAMANHO PROPRIO. O desenho e o
            // gesto sao a area inteira, e nao o texto do meio: sem isto
            // um pai que so afrouxa as restricoes (uma coluna, uma
            // lista) daria a ela a altura da dica.
            builder: (context, restricoes) => SizedBox(
              width: restricoes.hasBoundedWidth
                  ? restricoes.maxWidth
                  : _ladoDeReserva,
              height: restricoes.hasBoundedHeight
                  ? restricoes.maxHeight
                  : _ladoDeReserva,
              child: Stack(
                alignment: Alignment.center,
                children: [
                  const Positioned.fill(
                    child: CustomPaint(painter: _PintorDosCantos()),
                  ),
                  if (widget.cabecalho != null)
                    Positioned(
                      top: 10,
                      left: 16,
                      right: 16,
                      child: Center(child: widget.cabecalho!),
                    ),
                  Padding(
                    padding: EdgeInsets.only(
                      top: widget.cabecalho != null ? 36.0 : 0.0,
                    ),
                    child: AppText(
                      widget.dica,
                      textAlign: TextAlign.center,
                      style: TextStyle(
                        fontSize: 12,
                        // A INSTRUCAO SOME DEPOIS DE OBEDECIDA: com o
                        // dedo na almofada ela so disputa atencao com a
                        // previa, que e onde o olho precisa estar.
                        //
                        // Some pela COR, e nao saindo da arvore: assim
                        // nada e medido de novo no comeco e no fim de
                        // cada arrasto, e a dica volta exatamente onde
                        // estava.
                        color: _dedoNaAlmofada
                            ? Colors.transparent
                            : AmColors.muted,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      );
}

/// OS QUATRO CANTOS EM L, e nao uma borda fechada.
///
/// A borda fechada vira uma caixa, e caixa e o que o desenho do editor
/// evita em todo lugar. O L diz onde a area comeca sem cercar o dedo:
/// quatro tracos curtos bastam para o olho fechar o retangulo sozinho.
class _PintorDosCantos extends CustomPainter {
  const _PintorDosCantos();

  @override
  void paint(Canvas canvas, Size size) {
    final esq = _folgaDoCanto;
    final dir = size.width - _folgaDoCanto;
    final topo = _folgaDoCanto;
    final base = size.height - _folgaDoCanto;
    if (dir <= esq || base <= topo) return;

    // O BRACO ENCOLHE EM AREA PEQUENA: numa almofada estreita dois
    // bracos inteiros se cruzariam no meio e os quatro L virariam duas
    // barras — o desenho passaria a cercar a area em vez de marca-la.
    final braco = math.min(
      _bracoDoCanto,
      math.min((dir - esq) / 2, (base - topo) / 2),
    );

    final tinta = Paint()
      ..color = AmColors.muted.withValues(alpha: _forcaDoCanto)
      ..strokeWidth = 2
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round
      ..style = PaintingStyle.stroke;

    final caminho = Path()
      ..moveTo(esq + braco, topo)
      ..lineTo(esq, topo)
      ..lineTo(esq, topo + braco)
      ..moveTo(dir - braco, topo)
      ..lineTo(dir, topo)
      ..lineTo(dir, topo + braco)
      ..moveTo(esq + braco, base)
      ..lineTo(esq, base)
      ..lineTo(esq, base - braco)
      ..moveTo(dir - braco, base)
      ..lineTo(dir, base)
      ..lineTo(dir, base - braco);

    canvas.drawPath(caminho, tinta);
  }

  /// O DESENHO E SEMPRE O MESMO: os cantos nao dependem de valor
  /// nenhum, so do tamanho — e mudanca de tamanho ja repinta sozinha.
  @override
  bool shouldRepaint(_PintorDosCantos o) => false;
}
