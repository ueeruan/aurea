import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../../domain/gizmo3d.dart';
import 'texto_no_atlas.dart';

/// O DESENHO DO GIZMO 3D — os tres eixos e os tres aneis, na viewport.
///
/// ONDE ELE DESENHA: nas coordenadas da COMPOSICAO multiplicadas por
/// [escala]. A caixa do gizmo na tela nao muda com o zoom do palco do
/// mesmo jeito que o resto? Muda: [comprimento] e [raio] chegam ja
/// divididos pela escala do palco (ver `kComprimentoDoGizmo`), entao o
/// braco tem sempre os mesmos pixels de dedo — um gizmo que encolhesse
/// junto com o zoom sumiria justamente quando se aproxima para mirar.
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
  });

  final GizmoNaTela gizmo;

  /// O fator composicao -> tela do palco.
  final double escala;

  /// O comprimento do braco e o raio do anel, JA em pixels de tela.
  final double comprimento;
  final double raio;

  /// O eixo que o dedo esta arrastando (mais grosso), e o anel em uso.
  final EixoDoGizmo? eixoAtivo;
  final EixoDoGizmo? anelAtivo;

  /// Falso quando a camada esta bloqueada: o gizmo continua na tela para
  /// dizer onde o eixo esta, mas apagado — alca viva numa camada travada
  /// promete um gesto que o cadeado recusa.
  final bool ativo;

  Offset _naTela(Offset comp) => comp * escala;

  @override
  void paint(Canvas canvas, Size size) {
    final origem = _naTela(gizmo.origem);
    final braco = comprimento;

    // OS ANEIS PRIMEIRO, os bracos por cima: o braco e o que se pega com
    // mais frequencia, e desenha-lo por ultimo evita que a elipse passe
    // na frente da ponta que o dedo esta mirando.
    for (final e in EixoDoGizmo.values) {
      // ANEL DE PERFIL NAO SE DESENHA. Uma circunferencia vista de lado e
      // um traco — desenha-la por cima do braco do eixo pareceria um
      // segundo braco, e pior, prometeria um gesto que o teste de toque
      // recusa. So aparece o anel que tem area.
      if (!anelVisivel(gizmo, e)) continue;
      final pontos = anelDeGiro(gizmo, e, raio / escala);
      final emUso = anelAtivo == e;
      final visivel = eixoVisivel(gizmo, e);
      final cor = corDoEixo(e);
      final alfa = !ativo
          ? 0.25
          : visivel
          ? (emUso ? 0.95 : 0.5)
          : 0.18;
      final caminho = Path()
        ..moveTo(_naTela(pontos.first).dx, _naTela(pontos.first).dy);
      for (final p in pontos.skip(1)) {
        final q = _naTela(p);
        caminho.lineTo(q.dx, q.dy);
      }
      // O HALO ESCURO POR BAIXO: a elipse fina de uma cor clara sobre um
      // video claro desaparecia. Duas passadas resolvem sem opacificar a
      // cor (que e a informacao do eixo).
      canvas.drawPath(
        caminho,
        Paint()
          ..style = PaintingStyle.stroke
          ..color = Colors.black.withValues(alpha: alfa * 0.55)
          ..strokeWidth = emUso ? 6 : 4
          ..isAntiAlias = true,
      );
      canvas.drawPath(
        caminho,
        Paint()
          ..style = PaintingStyle.stroke
          ..color = cor.withValues(alpha: alfa)
          ..strokeWidth = emUso ? 3.4 : 1.8
          ..isAntiAlias = true,
      );
    }

    for (final e in EixoDoGizmo.values) {
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
        ..strokeWidth = emUso ? 11 : 8;
      canvas.drawLine(origem, ponta, traco);
      traco
        ..color = cor.withValues(alpha: alfa)
        ..strokeWidth = emUso ? 5.5 : 3.6;
      canvas.drawLine(origem, ponta, traco);

      // O CONE DA PONTA: diz o SENTIDO do eixo. Sem ele, um eixo so de
      // traco nao distingue +X de -X — e a pessoa arrasta para o lado
      // errado.
      if (visivel && ativo) {
        final u = _direcaoNaTela(e);
        final n = Offset(-u.dy, u.dx);
        canvas.drawPath(
          Path()
            ..moveTo(ponta.dx + u.dx * 9, ponta.dy + u.dy * 9)
            ..lineTo(ponta.dx + n.dx * 5.5, ponta.dy + n.dy * 5.5)
            ..lineTo(ponta.dx - n.dx * 5.5, ponta.dy - n.dy * 5.5)
            ..close(),
          Paint()
            ..color = cor.withValues(alpha: alfa)
            ..isAntiAlias = true,
        );
      }

      if (visivel) {
        _rotulo(canvas, ponta + _direcaoNaTela(e) * 12, cor, alfa, e);
      }
    }

    // O PONTO CENTRAL: o que se ve quando o eixo inteiro aponta para o
    // olho. Sem ele, uma camada de frente com a camera parada parece nao
    // ter gizmo nenhum — e o Z existe, so nao tem comprimento.
    canvas.drawCircle(
      origem,
      4.5,
      Paint()..color = Colors.white.withValues(alpha: ativo ? 0.85 : 0.3),
    );
    canvas.drawCircle(
      origem,
      2.4,
      Paint()
        ..color = Colors.black.withValues(alpha: ativo ? 0.75 : 0.3),
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
    final corpo = 13 / (escala <= 0 ? 1 : escala);
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
      old.ativo != ativo;
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
