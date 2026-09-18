import 'dart:ui' as ui;

import 'package:flutter/widgets.dart';
import 'package:flutter/rendering.dart';

import '../../domain/effect.dart';
import '../../domain/sombra_projetada.dart';

/// A SOMBRA PROJETADA — a arvore de widgets, e nao um shader.
///
/// ==========================================================================
/// POR QUE AQUI NAO TEM SHADER, quando todo o resto do catalogo tem
/// ==========================================================================
///
/// O Passe de pixel recebe a camada ja rasterizada e devolve pixels. Isso
/// serve para Motion Tile, para o Preenchimento, para o desfoque — efeitos
/// que OLHAM a imagem e respondem com outra imagem do mesmo tamanho.
///
/// A sombra projetada nao e desse tipo, por duas razoes que se somam:
///
///   1. ELA SAI DA CAIXA. Um shader de filtro so escreve dentro do
///      retangulo que recebeu; a sombra precisa escrever FORA dele, e um
///      shader de tela cheia por camada e o caminho que ja congelou o
///      palco neste projeto.
///   2. ELA PRECISA DA CAMADA DUAS VEZES — uma como silhueta tingida e
///      desfocada, outra inteira por cima. Um shader de uma entrada so
///      faria isso amostrando o desfoque a mao, com dezenas de leituras
///      por pixel, e o desfoque do Impeller e duas passadas com mipmap e
///      sai de graca comparado.
///
/// Entao o desenho e arvore mesmo, com a composicao do proprio motor:
/// `ColorFiltered` (a silhueta na cor da sombra) dentro de
/// `ImageFiltered` (o desfoque gaussiano do Impeller) dentro de
/// `Transform` (o deslocamento medido), e a camada inteira por cima.
///
/// ==========================================================================
/// O ENGANO QUE CUSTOU TRES TENTATIVAS
/// ==========================================================================
///
/// As versoes anteriores nao pintavam nada, ou pintavam branco, ou
/// pintavam com alfa zero — e o suspeito era cada filtro, testado um a
/// um. O culpado nao era nenhum deles: era a FOTO da camada. A bancada
/// montava a imagem de teste por `ImageDescriptor.raw` e descartava o
/// codec logo depois; o desenho deslocado dessa imagem saia com alfa 0, e
/// eu li aquilo como "`drawImage` deslocado nao compoe".
///
/// O teste `test/drawimage_deslocado_test.dart` refez a mesma pergunta com
/// uma imagem vinda de BYTES PNG por `instantiateImageCodec` — o caminho
/// que o app usa — em tres superficies diferentes, incluindo um
/// `RepaintBoundary` de verdade. Seis testes, alfa 255 nos seis. A
/// bancada estava errada; o motor nunca teve esse defeito.
///
/// ==========================================================================
/// A CONTA DA CAIXA
/// ==========================================================================
///
/// A camada cresce exatamente o que a sombra precisa, e por lado — ver
/// [margemDaSombra]. A silhueta fica exatamente na posicao da camada
/// dentro da caixa aberta, e e a caixa inteira que anda sob o
/// `Transform`: assim o desfoque acontece ANTES do deslocamento e nao
/// encontra a borda da area util no caminho.
class SombraProjetadaPass extends StatefulWidget {
  const SombraProjetadaPass({
    super.key,
    required this.effect,
    required this.time,
    required this.child,
  });

  final EffectInstance effect;
  final Duration time;
  final Widget child;

  @override
  State<SombraProjetadaPass> createState() => _SombraProjetadaPassState();
}

class _SombraProjetadaPassState extends State<SombraProjetadaPass> {
  Size? tamanho;

  void medir(Size size) {
    if (size == tamanho || size.isEmpty || !size.isFinite) return;
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted && tamanho != size) setState(() => tamanho = size);
    });
  }

  @override
  Widget build(BuildContext context) {
    final lado = tamanho;
    if (lado == null) {
      return _Medida(onSize: medir, child: widget.child);
    }

    double p(String k) => widget.effect.paramAt(k, widget.time);

    final deslocamento = deslocamentoDaSombra(
      distancia: p('distancia'),
      direcao: p('direcao'),
    );
    final sigma = sigmaDaSombra(p('suavidade'));
    final margem = margemDaSombra(
      distancia: p('distancia'),
      direcao: p('direcao'),
      suavidade: p('suavidade'),
    );

    // IDENTIDADE: sem distancia e sem desfoque a sombra fica exatamente
    // debaixo da camada, coberta por ela — nada muda na tela. E o mesmo
    // cuidado do Motion Tile: passar por aqui assim mesmo custaria a
    // camada desenhada duas vezes por quadro, de graca.
    if (margem.vazia) return widget.child;

    // A OPACIDADE MULTIPLICA o alfa da cor, em vez de substituir: uma cor
    // de sombra ja translucida continua translucida com opacidade 100.
    final base = widget.effect.color;
    final cor = base.withValues(
      alpha: (base.a * _alfaDaSombra(p('opacidade'))).clamp(0.0, 1.0),
    );
    final soSombra = p('somente_sombra') >= 0.5;

    Widget silhueta = ColorFiltered(
      colorFilter: ColorFilter.mode(cor, BlendMode.srcIn),
      child: Stack(
        fit: StackFit.expand,
        children: [
          Positioned(
            left: margem.esquerda,
            top: margem.topo,
            width: lado.width,
            height: lado.height,
            child: widget.child,
          ),
        ],
      ),
    );
    // SEM DESFOQUE NAO HA FILTRO: suavidade 0 no AE e uma borda dura, e
    // nao uma gaussiana de raio zero. Pedir o filtro assim mesmo custaria
    // um `saveLayer` por quadro para nao mudar um pixel.
    if (sigma > 0) {
      silhueta = ImageFiltered(
        imageFilter: ui.ImageFilter.blur(sigmaX: sigma, sigmaY: sigma),
        child: silhueta,
      );
    }
    silhueta = Transform.translate(offset: deslocamento, child: silhueta);

    return SizedBox(
      width: lado.width,
      height: lado.height,
      child: OverflowBox(
        minWidth: lado.width + margem.largura,
        maxWidth: lado.width + margem.largura,
        minHeight: lado.height + margem.altura,
        maxHeight: lado.height + margem.altura,
        alignment: _alinhamento(margem, lado),
        child: Stack(
          clipBehavior: Clip.none,
          children: [
            Positioned.fill(child: silhueta),
            if (!soSombra)
              Positioned(
                left: margem.esquerda,
                top: margem.topo,
                width: lado.width,
                height: lado.height,
                child: widget.child,
              ),
          ],
        ),
      ),
    );
  }
}

/// O PAR DE EIXOS DO DOMINIO VIRA O `Alignment` DO MOTOR.
Alignment _alinhamento(MargemDaSombra margem, Size lado) {
  final a = alinhamentoDaSombra(
    margem: margem,
    larguraDaCamada: lado.width,
    alturaDaCamada: lado.height,
  );
  return Alignment(a.x, a.y);
}

/// A OPACIDADE DO AE E 0..255 NUMA SILHUETA JA TINGIDA — aqui ela entra
/// pelo ALFA DA COR, e nao por um `Opacity` por cima. Um `Opacity` a mais
/// seria mais um `saveLayer` por quadro para o mesmo resultado.
double _alfaDaSombra(double opacidade) {
  if (!opacidade.isFinite) return 1;
  return (opacidade / 100).clamp(0.0, 1.0);
}

class _Medida extends SingleChildRenderObjectWidget {
  const _Medida({required this.onSize, required super.child});
  final ValueChanged<Size> onSize;
  @override
  RenderObject createRenderObject(BuildContext context) => _CaixaDeMedida(onSize);
  @override
  void updateRenderObject(BuildContext context, _CaixaDeMedida renderObject) {
    renderObject.onSize = onSize;
  }
}

class _CaixaDeMedida extends RenderProxyBox {
  _CaixaDeMedida(this.onSize);
  ValueChanged<Size> onSize;
  @override
  void performLayout() {
    super.performLayout();
    onSize(size);
  }
}
