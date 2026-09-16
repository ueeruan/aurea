import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';

import '../../domain/caption_highlight.dart';
import 'texto_no_atlas.dart';

/// DESENHA A FRASE COM DESTAQUE.
///
/// O ponto inteiro deste pintor e uma coisa so: A DIAGRAMACAO NAO DEPENDE
/// DA PALAVRA ATIVA. As posicoes saem de uma medicao feita com todas as
/// palavras no TAMANHO DE CONTEXTO; a palavra dita e so escalada em cima
/// da posicao dela, a partir do proprio centro.
///
/// Se a medicao usasse o tamanho grande da palavra ativa, o texto inteiro
/// se reorganizaria a cada palavra — as vizinhas pulariam, e a legenda
/// ficaria ilegivel. E o defeito classico desta animacao.
class CaptionHighlightPainter extends CustomPainter {
  CaptionHighlightPainter({
    required this.frase,
    required this.tempo,
    required this.estilo,
    required this.corpo,
  });

  final CaptionPhrase frase;
  final Duration tempo;
  final CaptionHighlightStyle estilo;

  /// O tamanho do texto de CONTEXTO. O destaque e um multiplo dele.
  final double corpo;

  @override
  void paint(Canvas canvas, Size size) {
    if (estilo.layout == HighlightLayout.viral) {
      _pintarViral(canvas, size);
      return;
    }
    final ativa = frase.ativaEm(tempo);
    final visiveis = _visiveis(ativa);
    if (visiveis.isEmpty) return;

    // 1. MEDIR, sempre no tamanho de contexto.
    final medidos = [
      for (final i in visiveis)
        (indice: i, paragrafo: _paragrafo(i, corpo, estilo.corContexto)),
    ];

    // 2. DIAGRAMAR. As linhas saem da medicao, nao do que esta ativo.
    final linhas = _linhas(medidos, size);

    // 3. DESENHAR, escalando cada palavra no lugar dela.
    var y = _yInicial(linhas, size);
    for (final linha in linhas) {
      final larguraDaLinha =
          linha.fold<double>(
            0,
            (a, m) => a + m.paragrafo.maxIntrinsicWidth + _espaco,
          ) -
          _espaco;
      var x = _xInicial(larguraDaLinha, size);
      var alturaDaLinha = 0.0;

      for (final m in linha) {
        final largura = m.paragrafo.maxIntrinsicWidth;
        final altura = m.paragrafo.height;
        alturaDaLinha = math.max(alturaDaLinha, altura);

        final escala = escalaDaPalavra(
          indice: m.indice,
          ativa: ativa,
          frase: frase,
          t: tempo,
          destaque: estilo.destaque,
          duracao: estilo.duracaoInflar,
        );
        final cor = corDaPalavra(
          indice: m.indice,
          ativa: ativa,
          frase: frase,
          t: tempo,
          contexto: estilo.corContexto,
          destaque: estilo.corDestaque,
          duracao: estilo.duracaoInflar,
        );

        // A PALAVRA QUE NAO CABE encolhe ate a margem segura. Sem isto,
        // "INFINITAS POSSIBILIDADES" em corpo grande sai da tela.
        final margem = size.width * 0.94;
        final cabe = fatorParaCaber(
          larguraDoTexto: largura * escala,
          larguraDisponivel: margem,
        );
        final s = escala * cabe;

        final p = escala == 1.0 && cor == estilo.corContexto
            ? m.paragrafo
            : _paragrafo(m.indice, corpo, cor);

        canvas.save();
        // CRESCE A PARTIR DO PROPRIO CENTRO: crescer pela esquerda
        // empurraria as vizinhas.
        final cx = x + largura / 2;
        final cy = y + altura / 2;
        canvas.translate(cx, cy);
        canvas.scale(s);
        canvas.translate(-cx, -cy);
        // Pelo corpo de desenho: a palavra ativa infla e a camada amplia, e
        // um glifo gigante corromperia o atlas do Impeller.
        final k = reducaoDoCorpo(corpo);
        final reduzido = k > 1 ? _paragrafo(m.indice, corpo / k, cor, k) : null;
        desenharParagrafoNoAtlas(canvas, p, reduzido, k, Offset(x, y));
        reduzido?.dispose();
        canvas.restore();

        x += largura + _espaco;
      }
      y += alturaDaLinha * estilo.entrelinha;
    }
  }

  static const double _espaco = 14;

  // ----------------------------------------------------------- VIRAL

  /// Palavra-chave do estilo viral: a longa (8+ letras) e a ultima da frase
  /// (4+ letras) — o que o olho procura num edit, sem pedir marcacao.
  bool _chave(int i) {
    final letras = frase.palavras[i].text.replaceAll(
      RegExp(r'[^A-Za-zÀ-ÿ0-9]'),
      '',
    );
    return letras.length >= 8 ||
        (i == frase.palavras.length - 1 && letras.length >= 4);
  }

  /// O ESTILO VIRAL: a frase inteira e diagramada de uma vez (nada pula
  /// quando uma palavra chega), cada palavra aparece no instante em que e
  /// dita com fade e subida, todas com brilho e a chave com brilho forte na
  /// cor de destaque. Terminada a ultima palavra, a frase apaga em fade.
  void _pintarViral(Canvas canvas, Size size) {
    final palavras = frase.palavras;
    if (palavras.isEmpty) return;
    const entrada = 220000, saida = 280000;
    final depois = (tempo - palavras.last.end).inMicroseconds;
    final some = depois <= 0 ? 1.0 : 1 - (depois / saida).clamp(0.0, 1.0);
    if (some <= 0) return;

    final corpoV = corpo * 0.62;
    final espaco = corpoV * 0.55;
    final k = reducaoDoCorpo(corpoV);

    ui.Paragraph construir(int i, double reducao, Color cor, double brilho) {
      final texto = frase.palavras[i].text.toUpperCase();
      final chave = _chave(i);
      final b =
          ui.ParagraphBuilder(
              ui.ParagraphStyle(
                fontSize: corpoV / reducao,
                fontWeight: FontWeight.w700,
                fontFamily: estilo.fonteContexto,
              ),
            )
            ..pushStyle(
              ui.TextStyle(
                color: cor,
                fontSize: corpoV / reducao,
                fontWeight: FontWeight.w700,
                fontFamily: estilo.fonteContexto,
                letterSpacing: corpoV * 0.2 / reducao,
                shadows: brilho <= 0
                    ? null
                    : [
                        Shadow(
                          color: cor.withValues(alpha: .9 * brilho),
                          blurRadius: corpoV * .35 / reducao,
                        ),
                        if (chave)
                          Shadow(
                            color: cor.withValues(alpha: .85 * brilho),
                            blurRadius: corpoV * 1.1 / reducao,
                          ),
                      ],
              ),
            )
            ..addText(texto);
      return b.build()
        ..layout(const ui.ParagraphConstraints(width: double.infinity));
    }

    // 1. MEDIR a frase inteira e quebrar em linhas (80% da largura).
    final medidos = [
      for (var i = 0; i < palavras.length; i++)
        (indice: i, paragrafo: construir(i, 1, const Color(0xFFFFFFFF), 0)),
    ];
    final linhas = <List<({int indice, ui.Paragraph paragrafo})>>[];
    var atual = <({int indice, ui.Paragraph paragrafo})>[];
    var largura = 0.0;
    for (final m in medidos) {
      final w = m.paragrafo.maxIntrinsicWidth + espaco;
      if (atual.isNotEmpty && largura + w > size.width * .8) {
        linhas.add(atual);
        atual = [];
        largura = 0;
      }
      atual.add(m);
      largura += w;
    }
    if (atual.isNotEmpty) linhas.add(atual);
    final alturaLinha = medidos.first.paragrafo.height * 1.05;
    var y = size.height * .5 - linhas.length * alturaLinha / 2;

    // 2. DESENHAR o que ja foi dito.
    for (final linha in linhas) {
      final larguraDaLinha =
          linha.fold<double>(
            0,
            (a, m) => a + m.paragrafo.maxIntrinsicWidth + espaco,
          ) -
          espaco;
      var x = (size.width - larguraDaLinha) / 2;
      for (final m in linha) {
        final w = m.paragrafo.maxIntrinsicWidth;
        final entrou =
            (tempo - palavras[m.indice].start).inMicroseconds / entrada;
        if (entrou > 0) {
          final p = Curves.easeOutCubic.transform(entrou.clamp(0.0, 1.0));
          final alfa = p * some;
          final base = _chave(m.indice)
              ? estilo.corDestaque
              : estilo.corContexto;
          final cor = base.withValues(alpha: base.a * alfa);
          final cheio = construir(m.indice, 1, cor, alfa);
          final reduzido = k > 1 ? construir(m.indice, k, cor, alfa) : null;
          desenharParagrafoNoAtlas(
            canvas,
            cheio,
            reduzido,
            k,
            Offset(x, y + (1 - p) * corpoV * .3),
          );
          reduzido?.dispose();
          cheio.dispose();
        }
        x += w + espaco;
      }
      y += alturaLinha;
    }
    for (final m in medidos) {
      m.paragrafo.dispose();
    }
  }

  /// Quais palavras entram na tela: a ativa mais o contexto que o arranjo
  /// comporta, nunca mais que o teto.
  List<int> _visiveis(int? ativa) {
    final n = frase.palavras.length;
    if (n == 0) return const [];
    final lados = math.min(
      estilo.contextoPorLado,
      estilo.layout.contextoPorLado,
    );
    if (ativa == null) {
      return [for (var i = 0; i < math.min(n, 1 + lados * 2); i++) i];
    }
    final de = math.max(0, ativa - lados);
    final ate = math.min(n - 1, ativa + lados);
    return [for (var i = de; i <= ate; i++) i];
  }

  /// [reducao] divide as medidas absolutas (o espacamento) junto com o
  /// corpo, para o paragrafo no corpo de desenho.
  ui.Paragraph _paragrafo(
    int indice,
    double tamanho,
    Color cor, [
    double reducao = 1,
  ]) {
    final texto = estilo.maiusculas
        ? frase.palavras[indice].text.toUpperCase()
        : frase.palavras[indice].text.toLowerCase();
    final destaque = estilo.fonteDestaque;
    final construtor =
        ui.ParagraphBuilder(
            ui.ParagraphStyle(
              textAlign: TextAlign.left,
              fontSize: tamanho,
              fontWeight: FontWeight.w800,
              fontFamily: destaque,
              height: estilo.entrelinha,
            ),
          )
          ..pushStyle(
            ui.TextStyle(
              color: cor,
              fontSize: tamanho,
              fontWeight: FontWeight.w800,
              fontFamily: destaque,
              letterSpacing: estilo.tracking / reducao,
            ),
          )
          ..addText(texto);
    final p = construtor.build()
      ..layout(const ui.ParagraphConstraints(width: double.infinity));
    return p;
  }

  /// Como as palavras se quebram em linhas, por arranjo.
  List<List<({int indice, ui.Paragraph paragrafo})>> _linhas(
    List<({int indice, ui.Paragraph paragrafo})> medidos,
    Size size,
  ) {
    switch (estilo.layout) {
      case HighlightLayout.sozinha:
        return [medidos];
      case HighlightLayout.dupla:
        return [
          for (var i = 0; i < medidos.length; i += 2)
            medidos.sublist(i, math.min(i + 2, medidos.length)),
        ];
      case HighlightLayout.empilhada:
        // Uma palavra por linha, entrelinha apertada: e o arranjo em que
        // as linhas quase se tocam.
        return [
          for (final m in medidos) [m],
        ];
      case HighlightLayout.atravessada:
      case HighlightLayout.costura:
      case HighlightLayout.viral:
        // Uma fileira so, quebrando quando estoura a largura.
        final out = <List<({int indice, ui.Paragraph paragrafo})>>[];
        var atual = <({int indice, ui.Paragraph paragrafo})>[];
        var largura = 0.0;
        for (final m in medidos) {
          final w = m.paragrafo.maxIntrinsicWidth + _espaco;
          if (atual.isNotEmpty && largura + w > size.width * 0.94) {
            out.add(atual);
            atual = [];
            largura = 0;
          }
          atual.add(m);
          largura += w;
        }
        if (atual.isNotEmpty) out.add(atual);
        return out;
    }
  }

  double _yInicial(
    List<List<({int indice, ui.Paragraph paragrafo})>> linhas,
    Size size,
  ) {
    final alturaTotal = linhas.fold<double>(
      0,
      (a, l) =>
          a +
          l.fold<double>(0, (b, m) => math.max(b, m.paragrafo.height)) *
              estilo.entrelinha,
    );
    return switch (estilo.layout) {
      // Empilhada mora num canto, nao no meio.
      HighlightLayout.empilhada => size.height * 0.16,
      _ => (size.height - alturaTotal) / 2,
    };
  }

  double _xInicial(double larguraDaLinha, Size size) => switch (estilo.layout) {
    HighlightLayout.empilhada => size.width * 0.08,
    _ => (size.width - larguraDaLinha) / 2,
  };

  @override
  bool shouldRepaint(covariant CaptionHighlightPainter old) =>
      old.tempo != tempo ||
      old.frase != frase ||
      old.corpo != corpo ||
      old.estilo != estilo;
}
