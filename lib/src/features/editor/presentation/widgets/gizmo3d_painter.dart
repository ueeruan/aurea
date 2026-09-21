import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../../domain/gizmo3d.dart';
import 'texto_no_atlas.dart';

/// O DESENHO DO GIZMO 3D — os tres eixos e os tres aneis, na viewport.
///
/// ONDE ELE DESENHA: em pixels da COMPOSICAO, porque e esse o canvas em
/// que ele e montado — o `FittedBox` do palco aplica a escala depois, como
/// ja acontece com as guias e com a grade. Multiplicar aqui por [escala]
/// (o que este pintor fazia) punha o gizmo em `origem * escala²`: com um
/// palco de 360 px para uma composicao de 1080, um objeto no centro
/// aparecia encolhido no canto de cima e a esquerda, e andava um terco do
/// que o objeto andava. O TOQUE sempre esteve certo (ele converte o dedo
/// para a composicao), entao o eixo que se pegava era invisivel e o que
/// se via nao respondia. E o "gizmo fixo no canto" relatado pelo dono.
///
/// O QUE FICA CONSTANTE NA TELA: o braco, o raio do anel, a espessura e o
/// rotulo. Eles chegam em pixels de TELA e sao divididos pela escala do
/// palco — um gizmo que encolhesse junto com o zoom sumiria justamente
/// quando a pessoa se aproxima para mirar.
///
/// A COR E A INFORMACAO: X vermelho, Y verde, Z azul — as canonicas. O
/// rotulo na ponta existe para quem nao tem a convencao na memoria, e o
/// eixo em uso fica MAIS GROSSO e com um halo escuro por baixo, senao a
/// linha fina some sobre um video claro.
class Gizmo3DPainter extends CustomPainter {
  const Gizmo3DPainter({
    required this.gizmo,
    required this.escala,
    required this.comprimento,
    required this.raio,
    this.eixoAtivo,
    this.anelAtivo,
    this.ativo = true,
    this.eixos = true,
    this.aneis = true,
    this.alcaDeEscala = false,
    this.escalaEmUso = false,
    this.pontaQuadrada = false,
    this.pixelsPorUnidade = 1,
  });

  final GizmoNaTela gizmo;

  /// O fator composicao -> tela do palco.
  final double escala;

  /// O comprimento do braco e o raio do anel, em pixels de TELA.
  final double comprimento;
  final double raio;

  /// O eixo que o dedo esta arrastando (mais grosso), e o anel em uso.
  final EixoDoGizmo? eixoAtivo;
  final EixoDoGizmo? anelAtivo;

  /// Falso quando a camada esta bloqueada: o gizmo continua na tela para
  /// dizer onde o eixo esta, mas apagado — alca viva numa camada travada
  /// promete um gesto que o cadeado recusa.
  final bool ativo;

  /// QUAL FERRAMENTA ESTA NA MAO. Num celular os tres bracos, os tres
  /// aneis e a alca de escala juntos nao cabem no dedo: as fichas do palco
  /// escolhem um conjunto por vez, e o que nao esta em uso nem aparece —
  /// desenhar o que nao responde e prometer um gesto que nao existe.
  final bool eixos;
  final bool aneis;
  final bool alcaDeEscala;
  final bool escalaEmUso;

  /// A PONTA DO BRACO E UM CUBO, E NAO UMA SETA.
  ///
  /// A seta promete DIRECAO ("ando para la"); o cubo promete TAMANHO
  /// ("estico deste lado"). E a convencao de toda ferramenta 3D, e aqui
  /// ela nao e enfeite: no modo Escalar os mesmos tres bracos respondem a
  /// outra coisa, e a ponta e o unico lugar em que isso se ve antes de o
  /// dedo descobrir arrastando.
  final bool pontaQuadrada;

  /// QUANTOS PIXELS DE COMPOSICAO VALE UMA UNIDADE DA PROPRIEDADE.
  ///
  /// O anel nao e um circulo de raio em pixels: [anelDeGiro] monta a elipse
  /// somando as DIRECOES dos outros dois eixos, que medem "pixels por
  /// unidade". No gizmo da camada isso vale ~1 (um px de composicao por
  /// unidade de posicao) e raio em px e raio em unidades sao a mesma coisa.
  /// No gizmo de um no da CENA nao: com a camera padrao uma unidade da cena
  /// vale varios pixels, e o mesmo numero desenharia um anel gigante, fora
  /// do quadro. Quem sabe a conversao e quem monta o gizmo, e passa aqui —
  /// o teste de toque usa a MESMA divisao, entao desenho e dedo nunca
  /// discordam.
  final double pixelsPorUnidade;

  double get _e => escala <= 0 ? 1 : escala;

  /// O raio do anel em UNIDADES, a partir do raio em pixels de tela.
  double get _raioEmUnidades =>
      raio / _e / (pixelsPorUnidade.abs() < 1e-6 ? 1 : pixelsPorUnidade);

  @override
  void paint(Canvas canvas, Size size) {
    final origem = gizmo.origem;
    final braco = comprimento / _e;

    // OS ANEIS PRIMEIRO, os bracos por cima: o braco e o que se pega com
    // mais frequencia, e desenha-lo por ultimo evita que a elipse passe
    // na frente da ponta que o dedo esta mirando.
    for (final e in aneis ? EixoDoGizmo.values : const <EixoDoGizmo>[]) {
      // ANEL DE PERFIL NAO SE DESENHA. Uma circunferencia vista de lado e
      // um traco — desenha-la por cima do braco do eixo pareceria um
      // segundo braco, e pior, prometeria um gesto que o teste de toque
      // recusa. So aparece o anel que tem area.
      if (!anelVisivel(gizmo, e)) continue;
      final pontos = anelDeGiro(gizmo, e, _raioEmUnidades);
      final emUso = anelAtivo == e;
      final visivel = eixoVisivel(gizmo, e);
      final cor = corDoEixo(e);
      final alfa = !ativo
          ? 0.25
          : visivel
          ? (emUso ? 0.95 : 0.5)
          : 0.18;
      final caminho = Path()..moveTo(pontos.first.dx, pontos.first.dy);
      for (final p in pontos.skip(1)) {
        caminho.lineTo(p.dx, p.dy);
      }
      // O HALO ESCURO POR BAIXO: a elipse fina de uma cor clara sobre um
      // video claro desaparecia. Duas passadas resolvem sem opacificar a
      // cor (que e a informacao do eixo).
      canvas.drawPath(
        caminho,
        Paint()
          ..style = PaintingStyle.stroke
          ..color = Colors.black.withValues(alpha: alfa * 0.55)
          ..strokeWidth = (emUso ? 6 : 4) / _e
          ..isAntiAlias = true,
      );
      canvas.drawPath(
        caminho,
        Paint()
          ..style = PaintingStyle.stroke
          ..color = cor.withValues(alpha: alfa)
          ..strokeWidth = (emUso ? 3.4 : 1.8) / _e
          ..isAntiAlias = true,
      );
    }

    for (final e in eixos ? EixoDoGizmo.values : const <EixoDoGizmo>[]) {
      final emUso = eixoAtivo == e;
      final visivel = eixoVisivel(gizmo, e);
      final cor = corDoEixo(e);
      final alfa = !ativo
          ? 0.3
          : visivel
          ? 1.0
          : 0.28;
      final ponta = origem + _direcaoNaTela(e) * braco;
      final traco = Paint()
        ..style = PaintingStyle.stroke
        ..strokeCap = StrokeCap.round
        ..isAntiAlias = true;

      // HALO: a linha passa por cima de qualquer conteudo.
      traco
        ..color = Colors.black.withValues(alpha: alfa * 0.6)
        ..strokeWidth = (emUso ? 11 : 8) / _e;
      canvas.drawLine(origem, ponta, traco);
      traco
        ..color = cor.withValues(alpha: alfa)
        ..strokeWidth = (emUso ? 5.5 : 3.6) / _e;
      canvas.drawLine(origem, ponta, traco);

      // O CONE DA PONTA: diz o SENTIDO do eixo. Sem ele, um eixo so de
      // traco nao distingue +X de -X — e a pessoa arrasta para o lado
      // errado. No modo Escalar a ponta vira CUBO (ver [pontaQuadrada]).
      if (visivel && ativo) {
        final u = _direcaoNaTela(e);
        final tinta = Paint()
          ..color = cor.withValues(alpha: alfa)
          ..isAntiAlias = true;
        if (pontaQuadrada) {
          final lado = (emUso ? 13.0 : 11.0) / _e;
          canvas.drawRect(
            Rect.fromCenter(
              center: ponta + u * (lado / 2),
              width: lado,
              height: lado,
            ).inflate(1.5 / _e),
            Paint()..color = Colors.black.withValues(alpha: alfa * 0.6),
          );
          canvas.drawRect(
            Rect.fromCenter(
              center: ponta + u * (lado / 2),
              width: lado,
              height: lado,
            ),
            tinta,
          );
        } else {
          final n = Offset(-u.dy, u.dx);
          final c = 9 / _e;
          final l = 5.5 / _e;
          canvas.drawPath(
            Path()
              ..moveTo(ponta.dx + u.dx * c, ponta.dy + u.dy * c)
              ..lineTo(ponta.dx + n.dx * l, ponta.dy + n.dy * l)
              ..lineTo(ponta.dx - n.dx * l, ponta.dy - n.dy * l)
              ..close(),
            tinta,
          );
        }
      }

      if (visivel) {
        _rotulo(canvas, ponta + _direcaoNaTela(e) * (12 / _e), cor, alfa, e);
      }
    }

    // A ALCA DA ESCALA UNIFORME: um quadrado BRANCO na diagonal, fora dos
    // bracos (que ficam nos eixos) e fora do ponto central, para o dedo
    // nao disputar com nenhum dos dois. Branco porque ela nao e de eixo
    // nenhum — e a unica alca do gizmo que mexe nos tres de uma vez.
    //
    // MAIOR QUE AS PONTAS DE EIXO (14 px de tela contra 11): no modo
    // Escalar os quatro alvos convivem, e o que faz mais coisa tem de ser
    // o mais facil de acertar.
    if (alcaDeEscala) {
      final p = pontoDaAlcaDeEscala(gizmo, comprimento, escala);
      final lado = (escalaEmUso ? 18.0 : 14.0) / _e;
      final caixa = Rect.fromCenter(center: p, width: lado, height: lado);
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          caixa.inflate(2 / _e),
          Radius.circular(3 / _e),
        ),
        Paint()..color = Colors.black.withValues(alpha: ativo ? 0.6 : 0.25),
      );
      canvas.drawRRect(
        RRect.fromRectAndRadius(caixa, Radius.circular(2.5 / _e)),
        Paint()
          ..color = Colors.white.withValues(alpha: ativo ? 0.95 : 0.3),
      );
    }

    // O PONTO CENTRAL: o que se ve quando o eixo inteiro aponta para o
    // olho. Sem ele, uma camada de frente com a camera parada parece nao
    // ter gizmo nenhum — e o Z existe, so nao tem comprimento.
    canvas.drawCircle(
      origem,
      4.5 / _e,
      Paint()..color = Colors.white.withValues(alpha: ativo ? 0.85 : 0.3),
    );
    canvas.drawCircle(
      origem,
      2.4 / _e,
      Paint()..color = Colors.black.withValues(alpha: ativo ? 0.75 : 0.3),
    );
  }

  /// A direcao UNITARIA do eixo, em pixels de tela.
  Offset _direcaoNaTela(EixoDoGizmo e) {
    final d = gizmo.direcao(e);
    if (d.distance < 1e-9) return Offset.zero;
    return d / d.distance;
  }

  void _rotulo(
    Canvas canvas,
    Offset onde,
    Color cor,
    double alfa,
    EixoDoGizmo e,
  ) {
    // O CORPO DO ROTULO E CONSTANTE NA TELA (13 px), e nao na composicao:
    // num palco ampliado ele encolheria ate sumir. E o texto vai pelo
    // [TextoNoAtlas], que e a regra do projeto para texto no palco — o
    // atlas de glifos do Impeller corrompe quando um glifo e pedido muito
    // maior do que o corpo de desenho.
    final corpo = 13 / _e;
    final t = TextoNoAtlas(
      texto: nomeDoEixo(e),
      estilo: TextStyle(
        fontSize: corpo,
        fontWeight: FontWeight.w800,
        color: cor.withValues(alpha: alfa),
        height: 1,
        shadows: const [Shadow(color: Colors.black87, blurRadius: 3)],
      ),
    );
    t.layout(soMedir: true);
    t.cheio.paint(
      canvas,
      onde - Offset(t.cheio.width / 2, t.cheio.height / 2),
    );
  }

  @override
  bool shouldRepaint(Gizmo3DPainter old) =>
      old.gizmo.origem != gizmo.origem ||
      old.gizmo.x != gizmo.x ||
      old.gizmo.y != gizmo.y ||
      old.gizmo.z != gizmo.z ||
      old.escala != escala ||
      old.comprimento != comprimento ||
      old.raio != raio ||
      old.eixoAtivo != eixoAtivo ||
      old.anelAtivo != anelAtivo ||
      old.ativo != ativo ||
      old.eixos != eixos ||
      old.aneis != aneis ||
      old.alcaDeEscala != alcaDeEscala ||
      old.escalaEmUso != escalaEmUso ||
      old.pontaQuadrada != pontaQuadrada ||
      old.pixelsPorUnidade != pixelsPorUnidade;
}

/// ONDE FICA A ALCA DE ESCALA, em pixels de COMPOSICAO.
///
/// Na diagonal de cima e a direita, a [comprimento] pixels de TELA da
/// origem: fora dos bracos (que ficam nos eixos) e fora do ponto central,
/// para o dedo nao disputar com nenhum dos dois.
Offset pontoDaAlcaDeEscala(
  GizmoNaTela gizmo,
  double comprimento,
  double escala,
) {
  final e = escala <= 0 ? 1.0 : escala;
  const diagonal = Offset(0.7071, -0.7071);
  return gizmo.origem + diagonal * (comprimento / e);
}

/// O HALO QUE O DEDO VE AO PEGAR UM EIXO: o braco em uso ganha um circulo
/// na ponta, para o gesto ter resposta antes do primeiro pixel de
/// movimento — num eixo quase sem comprimento (apontando para o olho) a
/// cor sozinha nao diz que o toque pegou.
void pintarPulsoDoEixo(Canvas canvas, Offset ponta, Color cor) {
  canvas.drawCircle(
    ponta,
    9,
    Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..color = cor.withValues(alpha: 0.9),
  );
}

/// O ANGULO EM GRAUS entre dois pontos, para o teste do sinal do giro.
double anguloNaTela(Offset centro, Offset p) =>
    math.atan2(p.dy - centro.dy, p.dx - centro.dx) * 180 / math.pi;
