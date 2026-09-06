import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';

import '../../domain/caption_highlight.dart';

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
      final larguraDaLinha = linha.fold<double>(
        0,
        (a, m) => a + m.paragrafo.maxIntrinsicWidth + _espaco,
      ) - _espaco;
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
        canvas.drawParagraph(p, Offset(x, y));
        canvas.restore();

        x += largura + _espaco;
      }
      y += alturaDaLinha * estilo.entrelinha;
    }
  }

  static const double _espaco = 14;

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

  ui.Paragraph _paragrafo(int indice, double tamanho, Color cor) {
    final texto = estilo.maiusculas
        ? frase.palavras[indice].text.toUpperCase()
        : frase.palavras[indice].text.toLowerCase();
    final destaque = estilo.fonteDestaque;
    final construtor = ui.ParagraphBuilder(ui.ParagraphStyle(
      textAlign: TextAlign.left,
      fontSize: tamanho,
      fontWeight: FontWeight.w800,
      fontFamily: destaque,
      height: estilo.entrelinha,
    ))
      ..pushStyle(ui.TextStyle(
        color: cor,
        fontSize: tamanho,
        fontWeight: FontWeight.w800,
        fontFamily: destaque,
        letterSpacing: estilo.tracking,
      ))
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
        return [for (final m in medidos) [m]];
      case HighlightLayout.atravessada:
      case HighlightLayout.costura:
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
      (a, l) => a +
          l.fold<double>(0, (b, m) => math.max(b, m.paragrafo.height)) *
              estilo.entrelinha,
    );
    return switch (estilo.layout) {
      // Empilhada mora num canto, nao no meio.
      HighlightLayout.empilhada => size.height * 0.16,
      _ => (size.height - alturaTotal) / 2,
    };
  }

  double _xInicial(double larguraDaLinha, Size size) =>
      switch (estilo.layout) {
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
