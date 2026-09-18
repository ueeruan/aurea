import 'dart:math' as math;
import 'dart:typed_data';
import 'dart:ui' show PointMode;

import 'package:aurea_render/aurea_render.dart';
import 'package:flutter/material.dart';

import '../../domain/layer.dart';

/// PINTA O LOTE QUE O MOTOR EM C++ DEVOLVEU.
///
/// ============================ O QUE ELE NAO FAZ =======================
/// ELE NAO SIMULA. Nao ha uma linha de fisica aqui: a posicao, o tamanho,
/// a cor, o giro e a forma de cada particula chegam PRONTOS no lote. Este
/// pintor e a ultima milha — pegar o que o motor calculou e por na tela.
/// Se algum dia aparecer aqui um `sin(t)` decidindo onde a particula esta,
/// a simulacao voltou a ter dois lugares e os dois vao discordar.
///
/// ============================ AS COLUNAS DO LOTE ======================
/// Elas sao contrato com o C++ (`InstanciaDeParticula`), e o indice esta
/// em [ColunaDaInstancia]. Um campo novo la desloca tudo o que vem depois:
/// o teste de ABI existe exatamente para isso.
///
/// ============================ O CAMINHO RAPIDO ========================
/// UM CAMPO DE PONTOS PEQUENOS NAO MERECE UMA CHAMADA POR PONTO. Pontos da
/// mesma cor e do mesmo tamanho se somam comutativamente em `srcOver`, e
/// o Flutter tem `drawRawPoints`, que leva milhares de pontos numa
/// chamada. E o caso do campo de estrelas, que e o preset padrao: ele cai
/// inteiro no caminho rapido, e o resto do catalogo paga o desenho
/// individual que a forma dele exige.
class ParticulasPainter extends CustomPainter {
  const ParticulasPainter({
    required this.lote,
    required this.centroDoQuadro,
    this.corFim = false,
  });

  /// O lote ja simulado. Nulo = nao ha o que desenhar (biblioteca
  /// indisponivel, ou lote vazio).
  final LoteDeParticulas? lote;

  /// Onde fica o CENTRO DA NUVEM dentro do quadro em que ela e desenhada.
  /// A receita guardada na camada tem centro zero — a nuvem nasce no
  /// centro da camada, e quem a posiciona na composicao e o transform.
  final Offset centroDoQuadro;

  /// Se a receita tem cor final: desliga o caminho rapido, porque nesse
  /// caso a cor varia por particula e o balde unico mentiria.
  final bool corFim;

  /// ACIMA DESTE RAIO O PONTO VIRA UM DESENHO PROPRIO. Abaixo dele o
  /// ganho do balde e grande e a diferenca visual e nula.
  static const double _raioDoCaminhoRapido = 4.0;

  /// AS COLUNAS DE UMA INSTANCIA. Espelho de `InstanciaDeParticula`.
  static const int colunaX = 0;
  static const int colunaY = 1;
  static const int colunaTamanho = 2;
  static const int colunaAngulo = 3;
  static const int colunaR = 4;
  static const int colunaG = 5;
  static const int colunaB = 6;
  static const int colunaA = 7;
  static const int colunaForma = 9;
  static const int colunaCaudaX = 10;
  static const int colunaCaudaY = 11;
  static const int colunaVariacao = 14;

  /// Quantos floats tem uma instancia. Contrato com o C++.
  static const int flutuantes = LoteDeParticulas.flutuantesPorInstancia;

  @override
  void paint(Canvas canvas, Size size) {
    final l = lote;
    if (l == null || l.quantas == 0) return;

    final base = centroDoQuadro;

    // ---- CAMINHO RAPIDO: campo de pontos pequenos ----
    if (!corFim && _soPontosPequenos(l, base)) {
      _pintarPontos(canvas, l, base);
      return;
    }

    for (var i = 0; i < l.quantas; i++) {
      final o = i * flutuantes;
      final a = l.floats[o + colunaA];
      if (a <= 0.004) continue;
      final r = l.floats[o + colunaTamanho];
      if (r <= 0.2) continue;
      final p = Offset(l.floats[o + colunaX], l.floats[o + colunaY]) + base;
      final cor = Color.from(
        alpha: a.clamp(0.0, 1.0),
        red: l.floats[o + colunaR].clamp(0.0, 1.0),
        green: l.floats[o + colunaG].clamp(0.0, 1.0),
        blue: l.floats[o + colunaB].clamp(0.0, 1.0),
      );
      final brilho = l.floats[o + 13];
      if (brilho > 0.004) {
        canvas.drawCircle(
          p,
          r * (1.8 + brilho * 1.5),
          Paint()..color = cor.withValues(alpha: cor.a * 0.9 * brilho),
        );
      }
      final forma = FormaDaParticula.values[(l.floats[o + colunaForma] + 0.5)
          .toInt()
          .clamp(0, FormaDaParticula.values.length - 1)];
      switch (forma) {
        case FormaDaParticula.estrela:
          _estrela(canvas, p, r, cor, l.floats[o + colunaVariacao],
              l.floats[o + colunaAngulo]);
        case FormaDaParticula.risco:
          _risco(canvas, l, o, p, r, cor, base);
        case FormaDaParticula.nuvem:
          _nuvem(canvas, p, r, cor);
        case FormaDaParticula.quadrado:
          canvas.save();
          canvas.translate(p.dx, p.dy);
          canvas.rotate(l.floats[o + colunaAngulo] * math.pi / 180);
          canvas.drawRect(
            Rect.fromCenter(
              center: Offset.zero,
              width: r * 2,
              height: r * 2,
            ),
            Paint()..color = cor,
          );
          canvas.restore();
        case FormaDaParticula.anel:
          canvas.drawCircle(
            p,
            r,
            Paint()
              ..style = PaintingStyle.stroke
              ..strokeWidth = math.max(0.8, r * 0.35)
              ..color = cor,
          );
        case FormaDaParticula.esfera:
          canvas.drawCircle(p, r, Paint()..color = cor);
      }
    }
  }

  /// TODAS AS INSTANCIAS SAO PONTOS PEQUENOS E DA MESMA COR?
  bool _soPontosPequenos(LoteDeParticulas l, Offset base) {
    final f = l.floats;
    final r0 = f[colunaR], g0 = f[colunaG], b0 = f[colunaB];
    for (var i = 0; i < l.quantas; i++) {
      final o = i * flutuantes;
      if (f[o + colunaForma] != 0) return false;
      if (f[o + colunaTamanho] > _raioDoCaminhoRapido) return false;
      if (f[o + 13] > 0.004) return false;
      if (f[o + colunaR] != r0 ||
          f[o + colunaG] != g0 ||
          f[o + colunaB] != b0) {
        return false;
      }
    }
    return true;
  }

  /// O BALDE: um `drawRawPoints` por (raio, alfa) arredondados.
  ///
  /// ARREDONDAR E O QUE FAZ O BALDE EXISTIR. Sem arredondar, cada
  /// particula com o proprio alfa seria um balde de um ponto so — mais
  /// caro do que desenhar direto. Com o raio em passos de meio pixel e o
  /// alfa em passos de 1/15, o olho nao distingue e as chamadas caem de
  /// milhares para dezenas.
  void _pintarPontos(Canvas canvas, LoteDeParticulas l, Offset base) {
    final f = l.floats;
    final baldes = <int, List<double>>{};
    final cor = Color.from(
      alpha: 1,
      red: f[colunaR].clamp(0.0, 1.0),
      green: f[colunaG].clamp(0.0, 1.0),
      blue: f[colunaB].clamp(0.0, 1.0),
    );
    for (var i = 0; i < l.quantas; i++) {
      final o = i * flutuantes;
      final a = f[o + colunaA];
      final r = f[o + colunaTamanho];
      if (a <= 0.004 || r <= 0.2) continue;
      final chave = ((r * 4).round().clamp(1, 16) << 8) |
          (a * 15).round().clamp(1, 15);
      (baldes[chave] ??= <double>[])
        ..add(f[o + colunaX] + base.dx)
        ..add(f[o + colunaY] + base.dy);
    }
    final paint = Paint()
      ..strokeCap = StrokeCap.round
      ..isAntiAlias = true;
    for (final entrada in baldes.entries) {
      final raio = (entrada.key >> 8) / 4;
      final alfa = (entrada.key & 0xFF) / 15;
      paint
        ..strokeWidth = raio * 2
        ..color = cor.withValues(alpha: cor.a * alfa);
      canvas.drawRawPoints(
        PointMode.points,
        Float32List.fromList(entrada.value),
        paint,
      );
    }
  }

  /// A CRUZ DE QUATRO PONTAS: dois losangos finos e um nucleo claro.
  void _estrela(
    Canvas canvas,
    Offset p,
    double r,
    Color cor,
    double variacao,
    double angulo,
  ) {
    final comprimento = r * (2.4 + variacao * 1.6);
    final comprimentoH = comprimento * 0.72;
    final w = r * 0.40;
    canvas.save();
    canvas.translate(p.dx, p.dy);
    canvas.rotate(angulo * math.pi / 180);
    final caminho = Path()
      ..moveTo(0, -comprimento)
      ..lineTo(w, 0)
      ..lineTo(0, comprimento)
      ..lineTo(-w, 0)
      ..close()
      ..moveTo(-comprimentoH, 0)
      ..lineTo(0, -w)
      ..lineTo(comprimentoH, 0)
      ..lineTo(0, w)
      ..close();
    canvas.drawPath(caminho, Paint()..color = cor);
    canvas.drawCircle(
      Offset.zero,
      w * 0.95,
      Paint()..color = Color.from(
        alpha: cor.a * 0.85,
        red: 1,
        green: 1,
        blue: 1,
      ),
    );
    canvas.restore();
  }

  /// O RISCO: a linha da cauda ate a posicao, com a ponta redonda.
  void _risco(
    Canvas canvas,
    LoteDeParticulas l,
    int o,
    Offset p,
    double r,
    Color cor,
    Offset base,
  ) {
    final cauda = Offset(
          l.floats[o + colunaCaudaX],
          l.floats[o + colunaCaudaY],
        ) +
        base;
    final dir = p - cauda;
    final dist = dir.distance;
    final alvo = dist < 0.5
        ? p - Offset(0, r * 2)
        : p - dir / dist * math.max(r * 2.5, dist * 3);
    canvas.drawLine(
      alvo,
      p,
      Paint()
        ..strokeCap = StrokeCap.round
        ..strokeWidth = math.max(0.8, r * 0.9)
        ..color = cor,
    );
    canvas.drawCircle(p, r * 0.55, Paint()..color = cor);
  }

  /// A NUVEM: tres discos concentricos bem transparentes.
  void _nuvem(Canvas canvas, Offset p, double r, Color cor) {
    for (final (escala, alfa) in const [(2.6, 0.10), (1.8, 0.16), (1.1, 0.28)]) {
      canvas.drawCircle(
        p,
        r * escala,
        Paint()..color = cor.withValues(alpha: cor.a * alfa),
      );
    }
  }

  @override
  bool shouldRepaint(ParticulasPainter old) =>
      old.lote != lote || old.centroDoQuadro != centroDoQuadro;
}

/// A NUVEM NO PALCO — quem guarda o lote e o regenera a cada quadro.
///
/// ============================ POR QUE UM `StatefulWidget` =============
/// O LOTE E MEMORIA NATIVA RESERVADA UMA VEZ. Cria-lo no `paint` seria
/// alocar 30 KB e chama-lo de `gerar` a cada quadro, que e exatamente o
/// "pouca alocacao por quadro" que o motor existe para cumprir. Aqui ele
/// vive enquanto a camada estiver na tela, e so e refeito quando a
/// RECEITA muda — nao quando o tempo anda.
///
/// ============================ O QUE VAI PARA O MOTOR =================
/// A receita guardada na camada e BOX-LOCAL: a nuvem nasce no centro da
/// camada. A LENTE, as ROTACOES e o TETO DE QUALIDADE sao do instante, e
/// nao da receita — por isso sao aplicados aqui, sobre uma copia. Sem
/// isso, mexer no zoom da camera reescreveria o projeto.
class ParticulasDoPalco extends StatefulWidget {
  const ParticulasDoPalco({
    super.key,
    required this.layer,
    required this.tempo,
    required this.focal,
    required this.rotX,
    required this.rotY,
    required this.teto,
  });

  final ParticulasLayer layer;

  /// O tempo LOCAL da camada.
  final Duration tempo;

  /// A lente da camera ativa (px). 1200 e a neutra.
  final double focal;

  /// A rotacao do SISTEMA — a da camada mais o delta herdado do pai 3D.
  final double rotX;
  final double rotY;

  /// O TETO DE PARTICULAS do nivel de qualidade atual. E um teto, e nao
  /// um multiplicador: um campo pequeno nao cresce por causa dele.
  final int teto;

  /// O TAMANHO DO QUADRO ONDE A NUVEM E DESENHADA. Ela e centrada nele, e
  /// o que sair daqui continua sendo pintado (o Flutter nao recorta por
  /// padrao) — o quadro so define onde fica o CENTRO.
  static const Size tamanhoDoQuadro = Size(420, 420);

  @override
  State<ParticulasDoPalco> createState() => _ParticulasDoPalcoState();
}

class _ParticulasDoPalcoState extends State<ParticulasDoPalco> {
  LoteDeParticulas? _lote;

  /// A receita com que o lote foi montado: lente, rotacoes e teto
  /// incluidos. Comparar so os parametros guardados perderia a mudanca de
  /// camera, e comparar so o teto perderia a mudanca de forma.
  String _assinatura = '';

  ParametrosDeParticulas _receitaDoInstante() {
    final p = widget.layer.parametros.clonar()
      ..centroX = 0
      ..centroY = 0
      ..focal = widget.focal
      ..rotacaoXGraus = widget.rotX
      ..rotacaoYGraus = widget.rotY
      ..maximo = MotorDeParticulasRender.teto(
        widget.teto,
        widget.layer.parametros.maximo,
      );
    return p;
  }

  String _chaveDaReceita(ParametrosDeParticulas p) {
    // A ASSINATURA COBRE O QUE MUDA O TAMANHO DO LOTE OU O DESENHO. Sem
    // ela, trocar de preset com o projeto aberto continuaria desenhando a
    // nuvem antiga ate a camada sair da tela.
    return '${p.focal}|${p.rotacaoXGraus}|${p.rotacaoYGraus}|${p.maximo}|'
        '${p.emissor.index}|${p.forma.index}|${p.modoDeEmissao.index}|'
        '${p.taxaDeNascimento}|${p.vidaS}|${p.rastro}|${p.faiscas}|'
        '${p.semente}|${p.corInicio}|${p.corFim}|${p.temCorFim}';
  }

  void _garantir(ParametrosDeParticulas p) {
    final chave = _chaveDaReceita(p);
    if (_lote != null && chave == _assinatura) return;
    _lote?.liberar();
    _lote = LoteDeParticulas(p);
    _assinatura = chave;
  }

  @override
  void dispose() {
    _lote?.liberar();
    _lote = null;
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final p = _receitaDoInstante();
    _garantir(p);
    final lote = _lote;
    if (lote == null || lote.capacidade == 0) {
      // O AVISO E `const` NO FILHO, e nao no `SizedBox` inteiro: um
      // `const` com o widget dentro pediria a arvore inteira constante, e
      // o `Text` abaixo depende do tema.
      return const SizedBox(
        width: 420,
        height: 420,
        child: _AvisoDoMotor(),
      );
    }
    lote.gerar(widget.tempo.inMicroseconds / 1e6);
    return CustomPaint(
      size: ParticulasDoPalco.tamanhoDoQuadro,
      painter: ParticulasPainter(
        lote: lote,
        centroDoQuadro: const Offset(210, 210),
        corFim: p.temCorFim,
      ),
    );
  }
}

/// O QUE APARECE QUANDO O MOTOR NAO RESPONDE.
class _AvisoDoMotor extends StatelessWidget {
  const _AvisoDoMotor();

  @override
  Widget build(BuildContext context) => Center(
    child: Text(
      'Motor de particulas indisponivel',
      textAlign: TextAlign.center,
      style: TextStyle(
        fontSize: 12,
        color: Theme.of(context).colorScheme.error,
      ),
    ),
  );
}
