import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/cupertino.dart';
import 'package:flutter/foundation.dart';

import '../../../../../core/ds/ds.dart';
import '../../../domain/peak_pyramid.dart';
import 'pintores_do_clipe.dart';
import 'janela_da_timeline.dart';
import 'estado_da_timeline.dart';
import 'geometria.dart';

// ===========================================================================
// OS PINTORES DAS LINHAS
// ===========================================================================
//
// A linha NAO se reconstroi quando o tempo anda: o pintor escuta a vista
// (`repaint:`) e redesenha so a JANELA visivel. Um clipe de uma hora a
// 400 dp/s tem 1,44 milhao de dp; o que se grava por quadro e o pedaco da
// tela, mais a tira de miniaturas e a onda que ja chegam recortadas pela
// janela (e a onda vem de uma gravacao em cache).

/// AS CORES DA TIMELINE, lidas da paleta a cada montagem (a paleta muda em
/// tempo de execucao — nada com cor pode ser `const`).
@immutable
class CoresDaTimeline {
  const CoresDaTimeline({
    required this.fundoEscolhida,
    required this.selecao,
    required this.keyframe,
    required this.keyframeEscolhido,
    required this.keyframeApagado,
    required this.contornoDoLosango,
    required this.alca,
    required this.texto,
    required this.trilha,
    required this.guia,
  });

  static CoresDaTimeline get atuais => CoresDaTimeline(
    fundoEscolhida: AureaCores.campo,
    // A SELECAO E ESTADO ATIVO: a cor de destaque (a `selecao` da paleta e
    // um azul escuro de fundo, que sumia no contorno de 2 dp).
    selecao: AureaCores.destaque,
    keyframe: AureaCores.keyframe,
    keyframeEscolhido: AureaCores.texto,
    keyframeApagado: AureaCores.textoSecundario.withValues(alpha: .45),
    contornoDoLosango: AureaCores.cromo,
    alca: AureaCores.destaque,
    texto: AureaCores.texto,
    trilha: AureaCores.textoSecundario.withValues(alpha: .35),
    guia: AureaCores.destaque,
  );

  final Color fundoEscolhida;
  final Color selecao;
  final Color keyframe;
  final Color keyframeEscolhido;
  final Color keyframeApagado;
  final Color contornoDoLosango;
  final Color alca;
  final Color texto;
  final Color trilha;
  final Color guia;

  @override
  bool operator ==(Object other) =>
      other is CoresDaTimeline &&
      other.fundoEscolhida == fundoEscolhida &&
      other.selecao == selecao &&
      other.keyframe == keyframe &&
      other.keyframeEscolhido == keyframeEscolhido &&
      other.keyframeApagado == keyframeApagado &&
      other.contornoDoLosango == contornoDoLosango &&
      other.alca == alca &&
      other.texto == texto &&
      other.trilha == trilha &&
      other.guia == guia;

  @override
  int get hashCode => Object.hash(
    fundoEscolhida,
    selecao,
    keyframe,
    keyframeEscolhido,
    keyframeApagado,
    contornoDoLosango,
    alca,
    texto,
    trilha,
    guia,
  );
}

/// OS LOSANGOS de uma linha.
@immutable
class LosangosDaLinha {
  const LosangosDaLinha({
    required this.temposUs,
    required this.acesos,
    required this.selecionados,
  });

  /// Instantes LOCAIS (µs), ordenados.
  final List<int> temposUs;

  /// Os acesos (da propriedade ativa). Nulo = todos acesos.
  final Set<int>? acesos;
  final Set<int> selecionados;

  @override
  bool operator ==(Object other) =>
      other is LosangosDaLinha &&
      identical(other.temposUs, temposUs) &&
      setEquals(other.acesos, acesos) &&
      setEquals(other.selecionados, selecionados);

  @override
  int get hashCode => Object.hash(
    identityHashCode(temposUs),
    acesos == null ? 0 : Object.hashAllUnordered(acesos!),
    Object.hashAllUnordered(selecionados),
  );
}

/// A MIDIA DO CLIPE: tira de miniaturas (video) ou onda (audio).
@immutable
class MidiaDoClipe {
  const MidiaDoClipe({
    this.tira,
    this.piramide,
    this.fonte,
    required this.inicio,
    required this.fim,
    required this.duracaoDaFonte,
    this.ganho = 1,
    this.mudo = false,
  });

  final List<ui.Image>? tira;
  final PeakPyramid? piramide;
  final Float64List? fonte;
  final Duration inicio;
  final Duration fim;
  final Duration duracaoDaFonte;
  final double ganho;
  final bool mudo;

  @override
  bool operator ==(Object other) =>
      other is MidiaDoClipe &&
      identical(other.tira, tira) &&
      identical(other.piramide, piramide) &&
      identical(other.fonte, fonte) &&
      other.inicio == inicio &&
      other.fim == fim &&
      other.duracaoDaFonte == duracaoDaFonte &&
      other.ganho == ganho &&
      other.mudo == mudo;

  @override
  int get hashCode => Object.hash(
    identityHashCode(tira),
    identityHashCode(piramide),
    identityHashCode(fonte),
    inicio,
    fim,
    duracaoDaFonte,
    ganho,
    mudo,
  );
}

/// A janela de um quadro so, para os pintores de miniatura e onda (eles
/// leem `.value`; quem manda repintar e o pintor da linha).
class _JanelaFixa implements ValueListenable<JanelaDaTimeline> {
  _JanelaFixa(this.value);

  @override
  final JanelaDaTimeline value;

  @override
  void addListener(VoidCallback listener) {}

  @override
  void removeListener(VoidCallback listener) {}
}

/// Desenha um losango de raio [AureaDims.raioDoKeyframe] em ([x], [y]).
void pintarLosango(
  Canvas canvas,
  double x,
  double y, {
  required Color preenchimento,
  Color? contorno,
  double escala = 1,
}) {
  final r = AureaDims.raioDoKeyframe * escala;
  final p = Path()
    ..moveTo(x, y - r)
    ..lineTo(x + r, y)
    ..lineTo(x, y + r)
    ..lineTo(x - r, y)
    ..close();
  canvas.drawPath(p, Paint()..color = preenchimento);
  if (contorno != null) {
    canvas.drawPath(
      p,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = AureaDims.tracoDoKeyframe
        ..color = contorno,
    );
  }
}

/// Os losangos de [l] na altura [y], so os da tela.
void _pintarLosangos(
  Canvas canvas,
  Size size,
  EstadoDaTimeline e,
  int inicioUs,
  LosangosDaLinha l,
  double y,
  CoresDaTimeline cores,
) {
  final tempos = l.temposUs;
  if (tempos.isEmpty) return;
  // BUSCA BINARIA DO PRIMEIRO VISIVEL: centenas de marcas numa camada
  // longa, e so as da tela sao gravadas.
  const r = AureaDims.raioDoKeyframe + 1;
  final deUs = e.tempoDoX(-r) - inicioUs;
  final ateUs = e.tempoDoX(size.width + r) - inicioUs;
  var lo = 0;
  var hi = tempos.length;
  while (lo < hi) {
    final mid = (lo + hi) >> 1;
    if (tempos[mid] < deUs) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  // Os apagados primeiro, os acesos por cima, os escolhidos por ultimo:
  // numa pilha apertada, o que importa fica a vista.
  for (var passada = 0; passada < 3; passada++) {
    for (var i = lo; i < tempos.length; i++) {
      final us = tempos[i];
      if (us > ateUs) break;
      final escolhido = l.selecionados.contains(us);
      final aceso = l.acesos == null || l.acesos!.contains(us);
      final nivel = escolhido ? 2 : (aceso ? 1 : 0);
      if (nivel != passada) continue;
      pintarLosango(
        canvas,
        e.xDoTempo(inicioUs + us),
        y,
        preenchimento: switch (nivel) {
          2 => cores.keyframeEscolhido,
          1 => cores.keyframe,
          _ => cores.keyframeApagado,
        },
        contorno: switch (nivel) {
          2 => cores.selecao,
          1 => cores.contornoDoLosango,
          _ => null,
        },
        escala: escolhido ? 1.25 : 1,
      );
    }
  }
}

/// A LINHA DE UMA CAMADA: o clipe (23, recuo 2,5, raio 1,5), a midia
/// dentro, o nome, a selecao, as alcas de trim e os losangos.
class PintorDaLinha extends CustomPainter {
  PintorDaLinha({
    required this.estado,
    required this.inicioUs,
    required this.fimUs,
    required this.nome,
    required this.estiloDoNome,
    required this.cor,
    required this.cores,
    required this.oculta,
    required this.travada,
    required this.escolhida,
    required this.naMulti,
    required this.comAlcas,
    this.losangos,
    this.midia,
    Listenable? revisaoDaMidia,
  }) : super(
         repaint: revisaoDaMidia == null
             ? estado.vista
             : Listenable.merge([estado.vista, revisaoDaMidia]),
       );

  final EstadoDaTimeline estado;
  final int inicioUs;
  final int fimUs;
  final String nome;
  final TextStyle estiloDoNome;

  /// A cor DO TIPO da camada.
  final Color cor;
  final CoresDaTimeline cores;
  final bool oculta;
  final bool travada;
  final bool escolhida;

  /// Na selecao multipla: traco de 1,5 em vez de 2.
  final bool naMulti;

  /// As alcas de trim aparecem (escolhida, destravada, fora do lote).
  final bool comAlcas;
  final LosangosDaLinha? losangos;
  final MidiaDoClipe? midia;

  TextPainter? _rotulo;

  TextPainter get _textoDoNome => _rotulo ??= TextPainter(
    text: TextSpan(
      children: [
        if (travada)
          TextSpan(
            text: '${String.fromCharCode(CupertinoIcons.lock_fill.codePoint)} ',
            style: estiloDoNome.copyWith(
              fontFamily: CupertinoIcons.lock_fill.fontFamily,
              package: CupertinoIcons.lock_fill.fontPackage,
            ),
          ),
        TextSpan(text: nome, style: estiloDoNome),
      ],
    ),
    maxLines: 1,
    ellipsis: '…',
    textDirection: TextDirection.ltr,
  )..layout();

  @override
  void paint(Canvas canvas, Size size) {
    final e = estado;
    final escolhidaOuMulti = escolhida || naMulti;
    if (escolhidaOuMulti) {
      // DESTAQUE POR TOM: a linha inteira fica um degrau mais clara.
      canvas.drawRect(
        Offset.zero & size,
        Paint()..color = cores.fundoEscolhida.withValues(alpha: .55),
      );
    }
    final x0 = e.xDoTempo(inicioUs);
    final x1 = e.xDoTempo(fimUs);
    if (x1 < -AureaDims.alcaDeTrim || x0 > size.width + AureaDims.alcaDeTrim) {
      return;
    }
    // A caixa e cortada perto da tela: um retangulo de um milhao de dp nao
    // ajuda ninguem, e o canto arredondado de fora da tela nao se ve.
    final esq = math.max(x0, -8.0);
    final dir = math.min(x1, size.width + 8);
    final caixa = GeometriaDaLinha.caixa(esq, dir);
    final rr = RRect.fromRectAndRadius(
      caixa,
      const Radius.circular(AureaDims.raioDoClipe),
    );
    final corDoCorpo = escolhidaOuMulti
        ? Color.lerp(cor, cores.texto, .16)!
        : cor;
    canvas.drawRRect(
      rr,
      Paint()..color = corDoCorpo.withValues(alpha: oculta ? .35 : 1),
    );

    final m = midia;
    if (m != null && dir > esq) _pintarMidia(canvas, rr, x0, x1, m);

    // O NOME acompanha o comeco VISIVEL do clipe: com o inicio escondido
    // atras dos cabecalhos, ele gruda logo depois deles em vez de sumir.
    final rotulo = _textoDoNome;
    final xNome = math.max(
      x0 + AureaDims.recuoDoRotuloDoClipe,
      AureaDims.cabecalhoDaCamada + AureaDims.e6,
    );
    if (x1 - xNome > 12) {
      final comLosangos = losangos != null && losangos!.temposUs.isNotEmpty;
      final y = comLosangos
          ? caixa.top + 1
          : caixa.center.dy - rotulo.height / 2;
      canvas.save();
      canvas.clipRect(caixa);
      rotulo.paint(canvas, Offset(xNome, y));
      canvas.restore();
    }

    if (escolhidaOuMulti) {
      final traco = naMulti
          ? AureaDims.tracoDeMultisselecao
          : AureaDims.tracoDeSelecao;
      canvas.drawRRect(
        RRect.fromRectAndRadius(
          caixa.deflate(traco / 2),
          const Radius.circular(AureaDims.raioDoClipe),
        ),
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = traco
          ..color = cores.selecao,
      );
    }

    if (comAlcas) {
      // Ponta escondida (atras do cabecalho, alem da borda) nao tem alca:
      // a mesma regra da zona de toque.
      if (x0 >= AureaDims.cabecalhoDaCamada) {
        _pintarAlca(canvas, x0, caixa.center.dy, LadoDaAlca.inicio, size);
      }
      if (x1 <= size.width) {
        _pintarAlca(canvas, x1, caixa.center.dy, LadoDaAlca.fim, size);
      }
    }

    final l = losangos;
    if (l != null) {
      _pintarLosangos(
        canvas,
        size,
        e,
        inicioUs,
        l,
        GeometriaDaLinha.yDoLosangoNoClipe,
        cores,
      );
    }
  }

  void _pintarMidia(
    Canvas canvas,
    RRect rr,
    double x0,
    double x1,
    MidiaDoClipe m,
  ) {
    final janela = _JanelaFixa(estado.janela);
    final esquerdaPx = inicioUs / 1e6 * estado.pps.value;
    final tamanho = Size(x1 - x0, rr.height);
    canvas.save();
    canvas.clipRRect(rr);
    canvas.translate(x0, rr.top);
    final tira = m.tira;
    final piramide = m.piramide;
    final fonte = m.fonte;
    if (tira != null && tira.isNotEmpty) {
      FilmstripPainter(
        frames: tira,
        start: m.inicio,
        end: m.fim,
        sourceDuration: m.duracaoDaFonte,
        janela: janela,
        esquerdaPx: esquerdaPx,
      ).paint(canvas, tamanho);
      // O NOME PRECISA SER LIDO sobre qualquer imagem: um veu escuro.
      canvas.drawRect(
        Offset.zero & tamanho,
        Paint()..color = const Color(0xFF000000).withValues(alpha: .38),
      );
    } else if (piramide != null && !piramide.isEmpty && fonte != null) {
      ClipWaveformPainter(
        pyramid: piramide,
        fonte: fonte,
        color: cores.texto.withValues(alpha: .85),
        gain: m.ganho,
        muted: m.mudo,
        janela: janela,
        esquerdaPx: esquerdaPx,
      ).paint(canvas, tamanho);
    }
    canvas.restore();
  }

  /// A ALCA DE TRIM, desenhada FORA do clipe (17 x 15), colada na ponta —
  /// ou dentro dele, com a ponta colada na borda (`GeometriaDaLinha`).
  void _pintarAlca(
    Canvas canvas,
    double x,
    double cy,
    LadoDaAlca lado,
    Size size,
  ) {
    final r = GeometriaDaLinha.desenhoDaAlca(
      x,
      cy,
      lado,
      largura: size.width,
    );
    canvas.drawRRect(
      RRect.fromRectAndRadius(r, const Radius.circular(AureaDims.raioSm)),
      Paint()..color = cores.alca,
    );
    // O risco do meio diz "puxe daqui".
    canvas.drawLine(
      Offset(r.center.dx, r.top + 4),
      Offset(r.center.dx, r.bottom - 4),
      Paint()
        ..strokeWidth = 1.5
        ..color = cores.contornoDoLosango,
    );
  }

  @override
  bool shouldRepaint(PintorDaLinha old) =>
      old.estado != estado ||
      old.inicioUs != inicioUs ||
      old.fimUs != fimUs ||
      old.nome != nome ||
      old.estiloDoNome != estiloDoNome ||
      old.cor != cor ||
      old.cores != cores ||
      old.oculta != oculta ||
      old.travada != travada ||
      old.escolhida != escolhida ||
      old.naMulti != naMulti ||
      old.comAlcas != comAlcas ||
      old.losangos != losangos ||
      old.midia != midia;
}

/// A LINHA DE UMA PROPRIEDADE ANIMADA (camada expandida): o trilho do
/// tempo da camada e os losangos daquela propriedade, no meio da linha.
class PintorDaTrilha extends CustomPainter {
  PintorDaTrilha({
    required this.estado,
    required this.inicioUs,
    required this.fimUs,
    required this.losangos,
    required this.cores,
    required this.ativa,
  }) : super(repaint: estado.vista);

  final EstadoDaTimeline estado;
  final int inicioUs;
  final int fimUs;
  final LosangosDaLinha losangos;
  final CoresDaTimeline cores;

  /// E a trilha da propriedade ativa: o trilho acende.
  final bool ativa;

  @override
  void paint(Canvas canvas, Size size) {
    final e = estado;
    final y = GeometriaDaLinha.yDoLosangoNaTrilha;
    final x0 = math.max(e.xDoTempo(inicioUs), -4.0);
    final x1 = math.min(e.xDoTempo(fimUs), size.width + 4);
    if (x1 > x0) {
      canvas.drawLine(
        Offset(x0, y),
        Offset(x1, y),
        Paint()
          ..strokeWidth = ativa ? 1.5 : 1
          ..color = ativa ? cores.keyframe.withValues(alpha: .6) : cores.trilha,
      );
    }
    _pintarLosangos(canvas, size, e, inicioUs, losangos, y, cores);
  }

  @override
  bool shouldRepaint(PintorDaTrilha old) =>
      old.estado != estado ||
      old.inicioUs != inicioUs ||
      old.fimUs != fimUs ||
      old.losangos != losangos ||
      old.cores != cores ||
      old.ativa != ativa;
}
