import 'dart:math' as math;

import 'package:flutter/widgets.dart';

import '../../../../core/ui/am_colors.dart';
import 'campo_de_valor.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

/// O RAIO DO BOTAO. Alvo grande de proposito: e um gesto de precisao
/// feito com um dedo que cobre o proprio alvo.
const _raioDoBotao = 15.0;

/// A FOLGA ENTRE O CIRCULO E A BORDA DA AREA.
///
/// Ela SAI do raio do botao, e nao de um numero escolhido a parte: o
/// botao anda por cima do contorno, entao o circulo so cabe se sobrar um
/// raio inteiro de cada lado — com menos que isso metade do botao fica
/// cortada em cima e embaixo, que e justo onde o dedo mais o procura. Os
/// 2 px de sobra sao a borda antisserrilhada, que passa do raio
/// geometrico. Escrito como conta para os dois numeros nao poderem
/// discordar depois.
/// A conta agora vive em [DialDeAngulo._folga], porque o raio do botao
/// depende do modo compacto — mas ela continua sendo A MESMA conta, e
/// nao dois numeros soltos que podem discordar.
double folgaParaBotao(double raioDoBotao) => raioDoBotao * 2 + 2;

/// O MIOLO NAO TEM ANGULO. Perto do centro, um pixel de tremor do dedo
/// vira dezenas de graus; ignorar esse disco e o que impede o valor de
/// disparar quando a mao passa por cima da caixa do numero.
///
/// SAIR DO MIOLO RECOMECA A CONTA (ver [_DialDeAnguloState._mover]).
/// Apenas engolir os quadros de dentro nao bastaria: o primeiro quadro
/// de fora seria comparado com o angulo de antes de entrar, e o dial
/// pagaria de uma vez a meia volta que o dedo deu por dentro do disco —
/// justamente o salto que este disco existe para evitar.
const _mioloSurdo = 22.0;

/// O LADO DE RESERVA, para quando o pai nao limita a area. O dial
/// precisa de um lado concreto para achar o centro, e um dial sem
/// centro nao tem gesto.
const _ladoDeReserva = 200.0;

/// O DIAL DO MODO GIRAR.
///
/// Angulo e circular e uma barra reta mente sobre ele: numa barra, 359 e
/// 1 caem nas duas pontas opostas da tela quando na verdade sao vizinhos
/// de dois graus. O circulo conta a verdade sobre essa vizinhanca, e a
/// mao faz o mesmo movimento que a camada vai fazer.
///
/// O ANGULO NAO ENROLA. Duas voltas nao sao a mesma animacao que meia
/// volta: quem gira o logo tres vezes precisa que o valor chegue a 1080,
/// e um dial que zera em 360 apaga a segunda volta sem avisar. Por isso
/// o gesto ACUMULA o quanto o dedo andou em vez de ler o angulo absoluto
/// onde ele esta.
///
/// O DIAL NAO GUARDA O ANGULO: ele desenha [angulo] e devolve o novo por
/// [aoMudar]. Quem tem a camada e quem escreve — e assim o desfazer e o
/// keyframe continuam com um dono so.
class DialDeAngulo extends StatefulWidget {
  const DialDeAngulo({
    required this.angulo,
    required this.aoMudar,
    this.aoComecar,
    this.aoTerminar,
    this.rotulo = 'Girar a camada',
    this.compacto = false,
    super.key,
  });

  /// O angulo em GRAUS. Passa de 360 e fica negativo de proposito: o
  /// numero de voltas e parte da animacao.
  final double angulo;

  /// O NOVO TOTAL, ja com as voltas somadas.
  ///
  /// Chega a cada quadro do gesto, e nao so ao soltar, porque a previa e
  /// a unica resposta que o dedo tem: sem ela a mao gira no escuro e so
  /// descobre onde parou depois de levantar.
  final void Function(double graus) aoMudar;

  /// O comeco e o fim do gesto, para quem precisa juntar tudo num
  /// desfazer so ou marcar um keyframe ao soltar.
  final VoidCallback? aoComecar;
  final VoidCallback? aoTerminar;

  /// O que a acessibilidade e os testes leem no alvo.
  final String rotulo;

  /// TRES DIAIS LADO A LADO, e nao um sozinho.
  ///
  /// Nao e enfeite. Num painel de 360 px cada dial de uma fileira de
  /// tres recebe uns 87 px: descontada a folga do botao (32), sobra um
  /// raio de 27 — e o miolo surdo de 22 engoliria quase todo o anel,
  /// deixando o gesto morto. Compacto encolhe o botao, o miolo e o
  /// numero na mesma medida, e o dial volta a ter onde girar.
  ///
  /// Os padroes ficam intactos: o dial unico do modo 2D nao muda um
  /// pixel.
  final bool compacto;

  double get _raio => compacto ? 10 : _raioDoBotao;
  double get _folga => folgaParaBotao(_raio);
  double get _miolo => compacto ? 12 : _mioloSurdo;

  @override
  State<DialDeAngulo> createState() => _DialDeAnguloState();
}

class _DialDeAnguloState extends State<DialDeAngulo> {
  /// SE HA UM DEDO NA TELA. Vive separado de [_cruAnterior] porque a
  /// referencia se perde no miolo sem o gesto acabar: sem este campo,
  /// um "terminar" solto nao teria como se distinguir de um real.
  bool _arrastando = false;

  /// O angulo CRU do dedo no quadro anterior, de -180 a 180. NULO
  /// quando nao ha de onde medir: antes do primeiro quadro fora do
  /// miolo, e de novo cada vez que o dedo volta para dentro dele.
  double? _cruAnterior;

  /// O total com as voltas somadas. Ele nasce de [DialDeAngulo.angulo]
  /// no inicio do gesto e e a verdade ate o dedo soltar: enquanto o
  /// dedo esta na tela, ler de fora seria discutir com quem esta
  /// mandando.
  double _total = 0;

  void _comecar(Offset toque, Offset centro) {
    _arrastando = true;
    // COMECAR EM CIMA DO NUMERO NAO DA REFERENCIA: a caixa do valor
    // fica no centro do dial, e o angulo de um toque a dois pixels do
    // centro e ruido. Nulo aqui adia a partida para o primeiro quadro
    // que valha alguma coisa.
    _cruAnterior = _foraDoMiolo(toque, centro) ? _cru(toque, centro) : null;
    _total = widget.angulo.isFinite ? widget.angulo : 0;
    widget.aoComecar?.call();
  }

  void _mover(Offset toque, Offset centro) {
    if (!_arrastando) return;
    if (!_foraDoMiolo(toque, centro)) {
      _cruAnterior = null;
      return;
    }
    final cru = _cru(toque, centro);
    final anterior = _cruAnterior;
    _cruAnterior = cru;
    // O PRIMEIRO QUADRO FORA DO MIOLO SO MARCA A PARTIDA. Nao ha passo
    // a somar quando o quadro anterior foi engolido pelo disco surdo, e
    // inventar um passo aqui seria cobrar do valor o caminho que o dedo
    // fez por dentro do disco — onde nao havia angulo nenhum.
    if (anterior == null) return;
    var passo = cru - anterior;
    // MAIS DE MEIA VOLTA NUM QUADRO E UMA VOLTA, e nao um salto de 350
    // graus para tras: o angulo cru pula de 180 para -180 na esquerda
    // do circulo, e nenhum dedo atravessa meio circulo entre dois
    // quadros. Corrigir aqui e o que permite o total passar de 360.
    if (passo > 180) passo -= 360;
    if (passo < -180) passo += 360;
    _total += passo;
    widget.aoMudar(_total);
  }

  void _terminar() {
    if (!_arrastando) return;
    _arrastando = false;
    _cruAnterior = null;
    widget.aoTerminar?.call();
  }

  bool _foraDoMiolo(Offset toque, Offset centro) =>
      (toque - centro).distance >= widget._miolo;

  /// SEM CONVERSAO DE EIXO: no canvas o y cresce para baixo, entao o
  /// `atan2` ja nasce com o zero as 3 horas e crescendo no sentido
  /// horario — que e o sentido em que a rotacao positiva gira a camada
  /// na composicao. Qualquer sinal trocado aqui faria o dial girar ao
  /// contrario da previa.
  double _cru(Offset toque, Offset centro) {
    final d = toque - centro;
    return math.atan2(d.dy, d.dx) * 180 / math.pi;
  }

  /// A CASA DECIMAL SO APARECE QUANDO EXISTE: "45°" e o caso de todo
  /// dia, e escrever "45,0°" nele so gasta largura e atencao no unico
  /// numero grande da tela — e e assim que a referencia medida escreve
  /// o angulo (`docs/painel-de-transformacao-alight.md`, "O dial").
  ///
  /// QUEM FORMATA E [numeroPtBr], e nao um `toInt()` local: e ele que
  /// ja sabe trocar o ponto pela virgula e apagar o "-0" que aparece
  /// quando um valor negativo minusculo arredonda para zero.
  String _texto(double graus) {
    final v = graus.isFinite ? graus : 0.0;
    final umaCasa = (v * 10).roundToDouble() / 10;
    final inteiro = umaCasa == umaCasa.roundToDouble();
    return '${numeroPtBr(umaCasa, casas: inteiro ? 0 : 1)}°';
  }

  @override
  Widget build(BuildContext context) => LayoutBuilder(
    builder: (context, restricoes) {
      final largura = restricoes.hasBoundedWidth
          ? restricoes.maxWidth
          : _ladoDeReserva;
      final altura = restricoes.hasBoundedHeight
          ? restricoes.maxHeight
          : _ladoDeReserva;
      final centro = Offset(largura / 2, altura / 2);
      final raio = math.max(
        0.0,
        (math.min(largura, altura) - widget._folga) / 2,
      );
      final texto = _texto(widget.angulo);
      return Semantics(
        container: true,
        excludeSemantics: true,
        button: true,
        label: widget.rotulo,
        value: texto,
        child: GestureDetector(
          behavior: HitTestBehavior.opaque,
          onPanStart: (d) => _comecar(d.localPosition, centro),
          onPanUpdate: (d) => _mover(d.localPosition, centro),
          onPanEnd: (_) => _terminar(),
          onPanCancel: _terminar,
          child: SizedBox(
            width: largura,
            height: altura,
            child: Stack(
              alignment: Alignment.center,
              children: [
                Positioned.fill(
                  child: CustomPaint(
                    painter: _PinturaDoDial(
                      graus: widget.angulo.isFinite ? widget.angulo : 0,
                      raio: raio,
                      raioDoBotao: widget._raio,
                    ),
                  ),
                ),
                _CaixaDoAngulo(texto: texto, compacto: widget.compacto),
              ],
            ),
          ),
        ),
      );
    },
  );
}

/// O NUMERO NO CENTRO, na mesma caixa dos campos de valor: o dial nao
/// tem rotulo escrito, entao a caixa e o que diz que aquilo ali e um
/// valor e nao um enfeite do desenho.
class _CaixaDoAngulo extends StatelessWidget {
  const _CaixaDoAngulo({required this.texto, this.compacto = false});

  final String texto;
  final bool compacto;

  @override
  Widget build(BuildContext context) => Container(
    padding: compacto
        ? const EdgeInsets.symmetric(horizontal: 8, vertical: 3)
        : const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
    decoration: BoxDecoration(
      color: const Color(0xFF242436),
      borderRadius: BorderRadius.circular(8),
    ),
    child: AppText(texto,
      textAlign: TextAlign.center,
      style: TextStyle(
        color: AmColors.accent,
        fontSize: compacto ? 13 : 24,
        fontWeight: FontWeight.w700,
        fontFeatures: const [FontFeature.tabularFigures()],
      ),
    ),
  );
}

/// O CIRCULO, O ARCO VERDE E O BOTAO.
class _PinturaDoDial extends CustomPainter {
  const _PinturaDoDial({
    required this.graus,
    required this.raio,
    this.raioDoBotao = _raioDoBotao,
  });

  final double graus;
  final double raio;
  final double raioDoBotao;

  @override
  void paint(Canvas canvas, Size size) {
    if (raio <= 0) return;
    final centro = Offset(size.width / 2, size.height / 2);
    // Círculo escuro de trilha
    canvas.drawCircle(
      centro,
      raio,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2.0
        ..color = const Color(0xFF2E3548),
    );

    final radianos = graus * math.pi / 180;
    // Arco verde contínuo conectando de zero até a posição do botão
    if (graus.abs() > 0.5) {
      final sweep = (graus.abs() > 360 ? 360.0 : graus) * math.pi / 180;
      canvas.drawArc(
        Rect.fromCircle(center: centro, radius: raio),
        0,
        sweep,
        false,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 3.5
          ..strokeCap = StrokeCap.round
          ..color = AmColors.accent,
      );
    }

    final botao =
        centro + Offset(math.cos(radianos), math.sin(radianos)) * raio;
    // Manípulo branco na borda do anel
    canvas.drawCircle(botao, raioDoBotao, Paint()..color = const Color(0xFFFFFFFF));
  }

  @override
  bool shouldRepaint(_PinturaDoDial anterior) =>
      anterior.graus != graus ||
      anterior.raio != raio ||
      anterior.raioDoBotao != raioDoBotao;
}
