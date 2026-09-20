import 'package:flutter/material.dart';

import '../../../../core/ui/am_colors.dart';
import '../../../../core/ui/am_tick_ruler.dart';

/// A FITA DE AJUSTE — o controle que substituiu o deslizante.
///
/// Um deslizante tem comeco e fim. Escala, inclinacao e a maioria dos
/// parametros de efeito NAO tem, e quando tem o limite e arbitrario:
/// prender "escala" entre 0 e 4 e inventar um teto que o motor nunca
/// pediu. A fita e RELATIVA e INFINITA — cada pixel de dedo vale
/// [porPixel] — e por isso serve igual para 0 a 1 e para 0 a 4000, sem
/// ninguem escolher faixa (`docs/painel-de-transformacao-alight.md`,
/// secao "A fita").
///
/// E o controle mais tocado do painel novo, entao ele faz uma coisa so:
/// converte dedo em valor e avisa quem manda. Formatar, arredondar,
/// prender em faixa e escrever no projeto sao trabalho de quem usa a
/// fita, e nao dela.
///
/// AS LINHAS ROLAM COM O VALOR. Sem isso, arrastar num parametro que vai
/// a milhares nao muda nada visivel na tela — o numero anda no campo,
/// mas a superficie sob o dedo fica parada e o gesto parece morto.
///
/// DIREITA AUMENTA, ESQUERDA DIMINUI, e o desenho diz o mesmo: os riscos
/// andam com o dedo e vem com um forte a cada cinco (riscos todos iguais
/// parecem andar ao contrario num arrasto rapido — ver [riscosPorForte]).
/// A conta dos riscos e a MESMA da regua do AM ([paraCadaRisco]): dois
/// controles do mesmo painel nao podem discordar sobre para onde e "mais".
class FitaDeAjuste extends StatefulWidget {
  const FitaDeAjuste({
    required this.valor,
    required this.porPixel,
    required this.aoMudar,
    required this.rotulo,
    this.aoComecar,
    this.aoTerminar,
    this.ativa = true,
    this.altura = 74,
    this.min,
    this.max,
    super.key,
  });

  /// O valor vigente. Serve SO para desenhar a rolagem das linhas: o
  /// gesto nunca le daqui (ver [_FitaDeAjusteState._partida]).
  final double valor;

  /// Quanto o valor muda a cada pixel de dedo. Sai da faixa util do
  /// parametro, para que atravessar a fita de uma ponta a outra cubra o
  /// intervalo que interessa sem exigir dez arrastos.
  final double porPixel;

  /// O NOVO VALOR JA PRONTO, e nao o quanto o dedo andou — ao
  /// contrario da almofada ao lado, que manda deslocamento. Quem sabe
  /// de onde o arrasto partiu e a fita, entao a conta e dela e quem
  /// recebe so escreve. Vem sem arredondar e sem prender em faixa:
  /// decidir a casa decimal e o teto e de quem tem o parametro.
  final void Function(double novoValor) aoMudar;

  /// Um arrasto e UMA edicao. Quem usa liga estes dois em
  /// `beginGesture`/`endGesture` para que o desfazer volte o arrasto
  /// inteiro, e nao os quatrocentos passos que o dedo produziu.
  final VoidCallback? aoComecar;
  final VoidCallback? aoTerminar;

  /// Se esta e a fita em edicao. Num par (inclinacao X e Y), so uma
  /// esta: a outra fica com o centro branco para dizer que existe mas
  /// nao e a que o dedo mexeu por ultimo.
  final bool ativa;

  /// A ALTURA DA FAIXA QUE ACEITA O DEDO. O padrao e folgado porque
  /// este e alvo de polegar, e nao de ponteiro: mais baixo que isto o
  /// dedo encosta nos campos de valor logo acima e o arrasto comeca no
  /// controle errado. Quem empilha duas fitas (inclinacao X e Y) passa
  /// um valor menor, porque as duas dividem a altura do miolo.
  final double altura;

  /// A FAIXA DO PARAMETRO, quando ele TEM uma de verdade (opacidade de 0
  /// a 100, canal de cor de 0 a 255). SO DESENHA: com as duas pontas
  /// finitas a fita ganha o trilho de posicao na base, que enche para a
  /// direita como um deslizante comum ([leituraDePosicao]). Prender o
  /// valor na faixa continua sendo trabalho de quem recebe [aoMudar].
  ///
  /// Nulo — o caso de escala, inclinacao e de quase todo parametro — e a
  /// fita de sempre: relativa, sem comeco nem fim, e sem trilho.
  final double? min;
  final double? max;

  /// O que o leitor de tela anuncia — e por onde os testes acham a
  /// fita, ja que ela nao tem texto nenhum.
  final String rotulo;

  @override
  State<FitaDeAjuste> createState() => _FitaDeAjusteState();
}

class _FitaDeAjusteState extends State<FitaDeAjuste> {
  /// O VALOR DE ONDE O DEDO PARTIU, congelado no inicio do arrasto.
  ///
  /// Ler `widget.valor` a cada quadro parece mais simples e escorrega:
  /// o motor arredonda (uma casa decimal no campo, quantizacao no
  /// parametro), devolve um numero ligeiramente diferente do que
  /// pedimos, e no quadro seguinte o proximo delta parte desse valor
  /// arredondado. O erro acumula, e o desenho descola do dedo — poucos
  /// pixels por segundo de arrasto, o bastante para parecer defeito.
  ///
  /// NULO QUER DIZER "NAO HA ARRASTO", e e para isso que ele e
  /// anulavel. O Flutter avisa o cancelamento de arrastos que nunca
  /// comecaram: um toque simples, ou um dedo que a area rolavel de cima
  /// roubou, sai pelo `onHorizontalDragCancel` sem ter passado pelo
  /// `Start`. Sem esta marca, esse toque fecharia um lote de desfazer
  /// que ninguem abriu — e, com dois dedos nas duas fitas do par,
  /// fecharia o lote do arrasto que ainda esta acontecendo na outra.
  double? _partida;

  /// A SOMA DOS DELTAS deste arrasto. Somar os deltas e diferente de
  /// olhar a posicao atual: o dedo pode sair da fita e voltar, e o
  /// gesto continua o mesmo.
  double _andado = 0;

  void _comecar(DragStartDetails _) {
    // UM VALOR QUEBRADO NAO CONTAMINA O GESTO: se o parametro chegou
    // NaN ou infinito, partir dele faria o arrasto inteiro escrever
    // NaN, e o parametro nunca mais sairia de la nem arrastando de
    // volta. O pintor ja se defende disso; o gesto tem de se defender
    // igual, porque le o mesmo numero.
    _partida = widget.valor.isFinite ? widget.valor : 0;
    _andado = 0;
    widget.aoComecar?.call();
  }

  void _andar(DragUpdateDetails d) {
    final partida = _partida;
    if (partida == null) return;
    _andado += d.delta.dx;
    widget.aoMudar(partida + _andado * widget.porPixel);
  }

  void _terminar() {
    if (_partida == null) return;
    _partida = null;
    widget.aoTerminar?.call();
  }

  @override
  Widget build(BuildContext context) => Semantics(
    container: true,
    excludeSemantics: true,
    button: true,
    // "AJUSTAR X", e nao so "X": o chip da linha ja leva o nome cru, e
    // dois nos com o mesmo rotulo deixam quem le a tela — e quem escreve
    // teste — sem saber em qual dos dois esta encostando.
    label: 'Ajustar ${widget.rotulo}',
    child: GestureDetector(
      behavior: HitTestBehavior.opaque,
      onHorizontalDragStart: _comecar,
      onHorizontalDragUpdate: _andar,
      onHorizontalDragEnd: (_) => _terminar(),
      onHorizontalDragCancel: _terminar,
      child: SizedBox(
        height: widget.altura.isFinite ? widget.altura : null,
        child: CustomPaint(
          size: Size.infinite,
          painter: _PintorDaFita(
            valor: widget.valor,
            porPixel: widget.porPixel,
            min: widget.min ?? double.negativeInfinity,
            max: widget.max ?? double.infinity,
            ativa: widget.ativa,
          ),
        ),
      ),
    ),
  );
}

/// A FOLGA EM CIMA E EMBAIXO. As linhas nao encostam nas pontas da
/// faixa: encostadas, a fita vira uma caixa com borda, e a reforma toda
/// e sobre nao ter caixas.
const double _folga = 8;

/// ONDE AS LINHAS COMECAM A SUMIR, contado de cada borda para dentro.
///
/// Na referencia a fita nao termina: ela some. Um corte duro nas pontas
/// contaria a mentira de que existe um comeco e um fim do intervalo — o
/// que e justamente o que a fita nao tem.
const double _bordaSuave = 24;

/// A ESPESSURA DA LINHA CENTRAL. Dois pixels contra um das outras, para
/// o olho achar o centro sem procurar.
const double _linhaCentral = 2;

/// OS RISCOS SAO PINTADOS, E NAO WIDGETS.
///
/// Uma faixa da largura do miolo tem mais de trinta riscos, e cada um
/// viraria um objeto de render a ser refeito a cada quadro do arrasto:
/// trinta nos de layout para desenhar trinta pixels. Aqui e um laco
/// dentro de um `paint` so — no controle que existe justamente para
/// ficar sob o dedo, e por isso o mais repintado do painel.
class _PintorDaFita extends CustomPainter {
  const _PintorDaFita({
    required this.valor,
    required this.porPixel,
    required this.min,
    required this.max,
    required this.ativa,
  });

  final double valor;
  final double porPixel;
  final double min;
  final double max;
  final bool ativa;

  @override
  void paint(Canvas canvas, Size size) {
    final topo = _folga;
    var base = size.height - _folga;
    if (base <= topo || size.width <= 0) return;

    // A LEITURA DE POSICAO, so quando o parametro tem faixa: o trilho na
    // base enche PARA A DIREITA com o valor. Os riscos param antes dele.
    final temLeitura = pintarLeituraDePosicao(
      canvas,
      size,
      valor: valor,
      min: min,
      max: max,
      ativa: ativa,
    );
    if (temLeitura && base > size.height - alturaDoTrilho - 2) {
      base = size.height - alturaDoTrilho - 2;
      if (base <= topo) return;
    }

    final risco = Paint();
    // O RISCO FRACO E MAIS CURTO que o forte: altura e opacidade juntas,
    // para o forte se ler mesmo na borda, onde o degrade apaga a cor.
    final recuo = (base - topo) * .18;

    // A FITA SEGUE O DEDO: o valor entra SOMANDO na posicao dos riscos
    // ([paraCadaRisco]), para as linhas andarem para o mesmo lado que a
    // mao — como papel deslizando por baixo do dedo. Ao contrario, o
    // gesto briga.
    //
    // O DEGRADE DAS BORDAS SAI RISCO A RISCO, e nao de uma mascara.
    // Mascara aqui pediria `saveLayer`, que no Impeller custa um passe
    // de render inteiro por quadro — caro demais para um controle que
    // fica sendo arrastado o tempo todo.
    final centro = size.width / 2;
    paraCadaRisco(
      valor: valor,
      porPixel: porPixel,
      largura: size.width,
      origem: centro,
      desenhar: (x, forte) {
        final forca = _opacidadeNaBorda(x, size.width);
        if (forca <= 0) return;
        risco
          ..strokeWidth = forte ? 1.5 : 1
          ..color = AmColors.muted.withValues(
            alpha: (forte ? .6 : .25) * forca,
          );
        canvas.drawLine(
          Offset(x, forte ? topo : topo + recuo),
          Offset(x, forte ? base : base - recuo),
          risco,
        );
      },
    );

    // A LINHA CENTRAL E O ULTIMO TRACO, por cima dos riscos.
    canvas.drawLine(
      Offset(centro, topo),
      Offset(centro, base),
      Paint()
        ..strokeWidth = _linhaCentral
        // Teal na que esta em edicao, branco na outra do par: e o unico
        // sinal de qual das duas fitas o dedo vai mexer.
        ..color = ativa ? AmColors.accent : AmColors.cabecote,
    );
  }

  /// QUANTO DESTA LINHA SOBREVIVE PERTO DA BORDA, de 0 a 1.
  double _opacidadeNaBorda(double x, double largura) {
    final daBorda = x < largura - x ? x : largura - x;
    if (daBorda <= 0) return 0;
    if (daBorda >= _bordaSuave) return 1;
    return daBorda / _bordaSuave;
  }

  // O VALOR INTEIRO, e nao so a fase dentro do passo: com o risco forte
  // e o trilho, dois valores a 9 px um do outro ja nao desenham igual.
  @override
  bool shouldRepaint(_PintorDaFita o) =>
      o.valor != valor ||
      o.porPixel != porPixel ||
      o.min != min ||
      o.max != max ||
      o.ativa != ativa;
}
