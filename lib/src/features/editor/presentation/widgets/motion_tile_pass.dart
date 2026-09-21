import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';

import '../../domain/effect.dart';

/// One source-sized capture, then UV wrapping over the expanded output.
/// The output is drawn directly: no giant transparent input surface whose
/// bounds an ImageFilter can collapse back to the original video texture.
class MotionTilePass extends StatefulWidget {
  const MotionTilePass({
    super.key,
    required this.effect,
    required this.time,
    required this.child,
    this.escalaX = 1,
    this.escalaY = 1,
    this.posicao,
    this.composicao,
    this.rotacaoGraus = 0,
  });
  final EffectInstance effect;
  final Duration time;
  final Widget child;
  final double escalaX, escalaY, rotacaoGraus;
  final Offset? posicao;
  final Size? composicao;
  static Future<ui.FragmentProgram>? _program;
  static ui.FragmentProgram? _loaded;
  static Future<ui.FragmentProgram> warmUp() =>
      _program ??= ui.FragmentProgram.fromAsset('shaders/motion_tile.frag')
          .then((p) => _loaded = p);
  @override
  State<MotionTilePass> createState() => _MotionTilePassState();
}

class _MotionTilePassState extends State<MotionTilePass> {
  ui.FragmentShader? _shader;
  @override
  void initState() {
    super.initState();
    final loaded = MotionTilePass._loaded;
    if (loaded != null) {
      _shader = loaded.fragmentShader();
      return;
    }
    MotionTilePass.warmUp().then((p) {
      if (mounted) setState(() => _shader = p.fragmentShader());
    });
  }

  @override
  void dispose() {
    _shader?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => _TileOutput(
    key: const ValueKey('motion-tile-viewport'),
    config: widget,
    shader: _shader,
    pixelRatio: MediaQuery.maybeDevicePixelRatioOf(context) ?? 1,
    child: widget.child,
  );
}

class _TileOutput extends SingleChildRenderObjectWidget {
  const _TileOutput({
    super.key,
    required this.config,
    required this.shader,
    required this.pixelRatio,
    required super.child,
  });
  final MotionTilePass config;
  final ui.FragmentShader? shader;
  final double pixelRatio;
  @override
  RenderObject createRenderObject(BuildContext context) =>
      _TileRender(config, shader, pixelRatio);
  @override
  void updateRenderObject(BuildContext context, _TileRender renderObject) {
    renderObject.config = config;
    renderObject.shader = shader;
    renderObject.pixelRatio = pixelRatio;
    renderObject.markNeedsLayout();
    renderObject.markNeedsPaint();
  }
}

class _TileRender extends RenderProxyBox {
  _TileRender(this.config, this.shader, this.pixelRatio);
  MotionTilePass config;
  ui.FragmentShader? shader;
  double pixelRatio;
  double _p(String key) => config.effect.paramAt(key, config.time);
  @override
  void performLayout() {
    child?.layout(const BoxConstraints(), parentUsesSize: true);
    final source = child?.size ?? Size.zero;
    if (source.isEmpty) {
      size = constraints.smallest;
      return;
    }
    final coverage = fatoresQueCobremMotionTile(
      pedidoX: _p('output_width') / 100,
      pedidoY: _p('output_height') / 100,
      ladoDaCamada: source,
      escalaX: config.escalaX,
      escalaY: config.escalaY,
      posicao: config.posicao,
      composicao: config.composicao,
      rotacaoGraus: config.rotacaoGraus,
    );
    size = constraints.constrain(
      Size(source.width * coverage.x, source.height * coverage.y),
    );
  }

  @override
  void paint(PaintingContext context, Offset offset) {
    final input = child;
    final fx = shader;
    if (input == null || input.size.isEmpty || size.isEmpty) return;
    if (fx == null) {
      context.paintChild(
        input,
        offset +
            Offset(
              (size.width - input.size.width) / 2,
              (size.height - input.size.height) / 2,
            ),
      );
      return;
    }
    final bounds = Offset.zero & input.size;
    // SO A FONTE E RASTERIZADA. Repetir e barato: a foto da camada tem o
    // tamanho DA CAMADA, e nao o da regiao ladrilhada — crescer a saida
    // acrescenta ladrilhos, e nao pixels de textura.
    final ratio = ratioDaCapturaMotionTile(
      camada: input.size,
      pixelRatio: pixelRatio,
    );
    final layer = OffsetLayer();
    final capture = PaintingContext(layer, bounds);
    capture.paintChild(input, Offset.zero);
    // ignore: invalid_use_of_protected_member
    capture.stopRecordingIfNeeded();
    final image = layer.toImageSync(bounds, pixelRatio: ratio);
    layer.dispose();
    context.canvas.save();
    context.canvas.translate(offset.dx, offset.dy);
    pintarMotionTile(
      canvas: context.canvas,
      shader: fx,
      textura: image,
      fonte: input.size,
      saida: size,
      parametros: ParametrosDoMotionTile.de(config.effect, config.time),
    );
    context.canvas.restore();
    image.dispose();
  }

  @override
  void applyPaintTransform(RenderBox child, Matrix4 transform) {}
}

/// ==========================================================================
/// O QUE O LADRILHO FAZ — SEM ARVORE DE WIDGETS E SEM RENDER OBJECT.
/// ==========================================================================
///
/// O RELATO DO DONO: "aplico Motion Tile numa imagem 1:1 e a imagem
/// comprime, parece voltar, diminui de tamanho e perde qualidade".
///
/// As duas metades desse relato moram aqui, e cada uma virou uma funcao
/// que um teste consegue chamar direto:
///
///   [ParametrosDoMotionTile] + [pintarMotionTile] — A GEOMETRIA. O
///   ladrilho do meio tem de sair do tamanho EXATO da camada, no lugar
///   exato dela, e as copias nascem para FORA. Nada de escala.
///
///   [ratioDaCapturaMotionTile] — A RESOLUCAO. A foto da camada nao pode
///   ter menos pixel do que a camada: menos pixel e perda PERMANENTE, que
///   sai no arquivo exportado e nao se recupera.
///
/// POR QUE FUNCOES SOLTAS: `_TileRender.paint` tira uma foto da camada com
/// `Layer.toImageSync`, e isso precisa do rasterizador de verdade — num
/// teste de widget ele nunca devolve. Com o desenho separado da foto, o
/// teste entrega a textura pronta e mede o pixel que sai. Ver
/// `test/motion_tile_escala_test.dart`.

/// OS NUMEROS DO LADRILHO, ja nas unidades do shader.
///
/// O que a pessoa ve em porcentagem e grau chega aqui em fator e volta:
/// converter num lugar so evita que o passe e o teste divirjam por 360.
class ParametrosDoMotionTile {
  const ParametrosDoMotionTile({
    this.ladrilhoX = 1,
    this.ladrilhoY = 1,
    this.centroX = .5,
    this.centroY = .5,
    this.espelhar = false,
    this.esticar = false,
    this.faseHorizontal = false,
    this.fase = 0,
  });

  /// A LEITURA DA FICHA no instante pedido.
  factory ParametrosDoMotionTile.de(EffectInstance efeito, Duration instante) {
    double p(String chave) => efeito.paramAt(chave, instante);
    return ParametrosDoMotionTile(
      ladrilhoX: (p('tile_width') / 100).clamp(.01, 3),
      ladrilhoY: (p('tile_height') / 100).clamp(.01, 3),
      centroX: p('tile_center'),
      centroY: p('tile_center_y'),
      espelhar: p('mirror_edges') >= .5,
      esticar: p('clamp_edges') >= .5,
      faseHorizontal: p('horizontal_phase_shift') >= .5,
      fase: p('phase') / 360,
    );
  }

  /// O tamanho de cada ladrilho, em fracao da camada. 1 = a camada inteira.
  final double ladrilhoX, ladrilhoY;

  /// ONDE FICA O CENTRO DO LADRILHO DO MEIO, em fracao da camada. Mover
  /// isto DESLIZA a grade; nao escala, nao encolhe, nao corta.
  final double centroX, centroY;

  final bool espelhar, esticar, faseHorizontal;

  /// A fase em VOLTAS (o grau da ficha dividido por 360).
  final double fase;
}

/// O ORCAMENTO DE TEXELS da foto da camada, em texels.
///
/// Ele e um TETO, e nunca um piso: serve para uma camada gigante nao virar
/// uma textura gigante. Ver [ratioDaCapturaMotionTile].
const double orcamentoDeTexelsDoMotionTile = 2e6;

/// A RESOLUCAO DA FOTO DA CAMADA, em pixels de textura por pixel de camada.
///
/// ==========================================================================
/// NUNCA ABAIXO DE 1 — E ESTE E O DEFEITO QUE ESTA FUNCAO FECHA.
/// ==========================================================================
///
/// A conta antiga era `min(pixelRatio, sqrt(2e6 / area))`. Numa camada
/// grande a raiz cai abaixo de 1 e a foto sai com MENOS pixel do que a
/// camada: uma camada de 1080x1920 virava uma textura de 1060x1885, e um
/// quadro 4K (3840x2160) virava METADE — 1886x1061 esticados de volta ao
/// tamanho cheio.
///
/// Isso e perda PERMANENTE. Nao e a previa cedendo ao dedo: e a textura
/// que o efeito desenha, e ela sai igual no arquivo exportado. Era este o
/// "perde qualidade" do relato.
///
/// AGORA O TETO NUNCA DESCE ABAIXO DE 1. Numa camada dentro do orcamento,
/// ainda se pode gastar mais que 1 (ate o `pixelRatio` do aparelho) porque
/// ali a foto e barata e a borda do ladrilho agradece. Numa camada acima
/// do orcamento, para-se em 1 — a camada inteira, nem um pixel a menos.
double ratioDaCapturaMotionTile({
  required Size camada,
  required double pixelRatio,
}) {
  final area = camada.width * camada.height;
  if (!area.isFinite || area <= 0) return 1;
  final pedido = pixelRatio.isFinite && pixelRatio > 0 ? pixelRatio : 1.0;
  final folga = math.sqrt(orcamentoDeTexelsDoMotionTile / area);
  final teto = math.min(math.max(1.0, folga), 3.0);
  return pedido.clamp(1.0, teto).toDouble();
}

/// DESENHA A REPETICAO NA REGIAO [saida], com a camada ja fotografada.
///
/// [fonte] e o tamanho LOGICO da camada — o ladrilho do meio sai
/// exatamente desse tamanho, centrado em [saida]. [textura] e a foto dela,
/// que pode ter mais pixel que [fonte] (ver [ratioDaCapturaMotionTile]) sem
/// que isso mude UM PIXEL da geometria: o shader amostra em coordenada
/// normalizada.
///
/// A ORDEM DOS `setFloat` E A DA DECLARACAO NO SHADER, e nao a do byte do
/// std140 (ver a nota em `shaders/motion_tile.frag`). Uniforme novo entra
/// no FIM, nunca no meio.
void pintarMotionTile({
  required ui.Canvas canvas,
  required ui.FragmentShader shader,
  required ui.Image textura,
  required Size fonte,
  required Size saida,
  required ParametrosDoMotionTile parametros,
}) {
  if (fonte.isEmpty || saida.isEmpty) return;
  final p = parametros;
  shader
    ..setFloat(0, saida.width)
    ..setFloat(1, saida.height)
    ..setFloat(2, saida.width / fonte.width)
    ..setFloat(3, saida.height / fonte.height)
    ..setFloat(4, p.ladrilhoX)
    ..setFloat(5, p.ladrilhoY)
    ..setFloat(6, p.centroX)
    ..setFloat(7, p.centroY)
    ..setFloat(8, p.espelhar ? 1 : 0)
    ..setFloat(9, p.fase)
    ..setFloat(10, p.faseHorizontal ? 1 : 0)
    ..setFloat(11, 0)
    ..setFloat(12, p.esticar ? 1 : 0)
    // MEIO TEXEL DA TEXTURA DE VERDADE, e nao meio pixel da camada.
    //
    // O shader trava a amostra a meio texel da borda do ladrilho para a
    // interpolacao nao lamber o ladrilho vizinho. A conta antiga usava
    // meio pixel DA CAMADA; com a foto em 3x (um celular comum), isso
    // achatava UM PIXEL E MEIO em cada emenda — e tambem na borda da
    // copia central, que e justamente a que tem de sair intacta.
    ..setFloat(13, .5 / math.max(1, textura.width))
    ..setFloat(14, .5 / math.max(1, textura.height))
    ..setImageSampler(0, textura);
  canvas.drawRect(Offset.zero & saida, Paint()..shader = shader);
}

/// O FATOR PELA LARGURA (ou altura) QUE FAZ A REPETICAO COBRIR A COMPOSICAO.
///
/// A CONTA, em uma linha: depois da escala, a camada ocupa
/// `[posicao - H*escala, posicao + H*escala]` na composicao, onde `H` e a
/// meia-extensao da regiao ladrilhada em espaco de camada. Para cobrir
/// `[0, lado]` do quadro, `H` tem de alcancar os DOIS lados:
///
///   H >= max(posicao, lado - posicao) / escala
///
/// e o fator e `2*H / ladoDaCamada`. O maior entre isso e o que a pessoa
/// pediu em "Largura da saida".
///
/// OS DOIS LADOS, e nao a distancia ao centro: uma camada encostada na
/// direita precisa de quase nada a mais do lado esquerdo, e de muito do
/// lado direito. Calcular pelo centro daria uma regiao simetrica cheia de
/// ladrilho que ninguem ve — e cada pixel a mais e area que o shader
/// preenche por quadro.
///
/// SEM POSICAO OU SEM COMPOSICAO, devolve o pedido: nao saber e motivo
/// para nao crescer, e nao para chutar.
double fatorQueCobreMotionTile({
  required double pedido,
  required double ladoDaCamada,
  required double escala,
  required double? posicao,
  required double? ladoDaComposicao,
}) {
  if (posicao == null || ladoDaComposicao == null) return pedido;
  if (!ladoDaCamada.isFinite || ladoDaCamada <= 0) return pedido;
  if (!escala.isFinite || escala <= 0.001) return pedido;
  if (!ladoDaComposicao.isFinite || ladoDaComposicao <= 0) return pedido;
  final preciso =
      2 *
      math.max(posicao, ladoDaComposicao - posicao) /
      (escala * ladoDaCamada);
  if (!preciso.isFinite || preciso <= 0) return pedido;
  // O TETO EXISTE porque cada fator a mais e area por quadro. Seis vezes a
  // camada ja cobre uma composicao com a camada em 1/6 do quadro — abaixo
  // disso nao ha o que salvar sem custo desproporcional.
  return math.max(pedido, preciso).clamp(0.01, 24.0);
}

/// ==========================================================================
/// A REGIAO QUE COBRE O QUADRO QUANDO A CAMADA ESTA GIRADA.
/// ==========================================================================
///
/// O defeito que este calculo fecha: girar a camada num efeito de ladrilho
/// deixava os QUATRO CANTOS com buraco. A conta anterior olhava so a escala
/// e a posicao, e por isso tratava a area coberta como um retangulo alinhado
/// aos eixos do quadro. A camada girada cobre um losango — e o losango nao
/// alcanca os cantos do retangulo.
///
/// A CONTA E A INVERSA DA TRANSFORMACAO, e nao uma aproximacao.
///
/// A camada vai ao quadro por `q = R(theta) * S(escala) * d + posicao`, onde
/// `d` e o deslocamento em espaco de camada. Para a regiao ladrilhada cobrir
/// TODO o quadro, basta que os QUATRO CANTOS do quadro caibam dentro dela.
/// Entao inverte-se a conta: para cada canto, `d = S^-1 * R(-theta) * (canto
/// - posicao)`, e a regiao tem de alcancar o maior `|d|` que aparecer.
///
/// SAO OS CANTOS, E NAO O CENTRO. Um canto e sempre o pior caso de uma
/// transformacao linear: a distancia maxima a origem de um retangulo esta
/// num vertice. Medir pelo centro subestimaria justamente a quina onde o
/// buraco aparece.
///
/// COM ROTACAO ZERO ISTO E A CONTA ANTIGA. `max(|0-p|, |w-p|)` e o mesmo que
/// `max(p, w-p)` — as duas formulas concordam na diagonal, e por isso a
/// funcao de um eixo so ([fatorQueCobreMotionTile]) continua valendo e
/// continua testada.
({double x, double y}) fatoresQueCobremMotionTile({
  required double pedidoX,
  required double pedidoY,
  required Size ladoDaCamada,
  required double escalaX,
  required double escalaY,
  required Offset? posicao,
  required Size? composicao,
  double rotacaoGraus = 0,
}) {
  final pedido = (x: pedidoX.clamp(0.01, 6.0), y: pedidoY.clamp(0.01, 6.0));
  if (!ladoDaCamada.width.isFinite || ladoDaCamada.width <= 0) return pedido;
  if (!ladoDaCamada.height.isFinite || ladoDaCamada.height <= 0) return pedido;
  if (!escalaX.isFinite || escalaX <= 0.001) return pedido;
  if (!escalaY.isFinite || escalaY <= 0.001) return pedido;

  double fator(double meio, double lado) {
    if (lado <= 0) return 0;
    final f = 2 * meio / lado;
    return f.isFinite && f > 0 ? f : 0;
  }

  double junto(double a, double b) => (a > b ? a : b).clamp(0.01, 24.0);

  // O PISO QUE NAO DEPENDE DE SABER ONDE A CAMADA CAI.
  //
  // Sem a posicao, a conta dos cantos nao existe — e o efeito ficava
  // ladrilhando SO A CAIXA DA CAMADA. Numa camada menor que o quadro
  // isso e a imagem pequena no meio do preto, que e o relato. Com o
  // tamanho do quadro conhecido, da para cobri-lo mesmo sem saber onde a
  // camada esta: a regiao tem de ser, no minimo, o quadro inteiro
  // dividido pela escala. E o caso do centro, que e o pior que a conta
  // dos cantos tambem cobre.
  if (composicao == null ||
      !composicao.width.isFinite ||
      !composicao.height.isFinite ||
      composicao.width <= 0 ||
      composicao.height <= 0) {
    return pedido;
  }
  final pisoX = composicao.width / (ladoDaCamada.width * escalaX);
  final pisoY = composicao.height / (ladoDaCamada.height * escalaY);
  if (posicao == null) {
    return (x: junto(pedido.x, pisoX), y: junto(pedido.y, pisoY));
  }
  final theta = (rotacaoGraus.isFinite ? rotacaoGraus : 0) * math.pi / 180;
  final cos = math.cos(theta), sen = math.sin(theta);
  var maxX = 0.0, maxY = 0.0;
  for (final canto in [
    Offset.zero,
    Offset(composicao.width, 0),
    Offset(0, composicao.height),
    Offset(composicao.width, composicao.height),
  ]) {
    final v = canto - posicao;
    // R(-theta) * v, e depois desfaz a escala: e a volta completa, em
    // espaco de camada.
    final dx = (v.dx * cos + v.dy * sen) / escalaX;
    final dy = (-v.dx * sen + v.dy * cos) / escalaY;
    if (dx.abs() > maxX) maxX = dx.abs();
    if (dy.abs() > maxY) maxY = dy.abs();
  }
  // O TETO E O MESMO DO CASO SEM GIRO, pelo mesmo motivo: cada fator a mais
  // e area que o shader preenche por quadro.
  return (
    x: junto(junto(pedido.x, pisoX), fator(maxX, ladoDaCamada.width)),
    y: junto(junto(pedido.y, pisoY), fator(maxY, ladoDaCamada.height)),
  );
}
