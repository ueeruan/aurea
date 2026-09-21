// NENHUM EFEITO DO CATALOGO FICA SEM DESENHO.
//
// A AUDITORIA DE 19/09 achou o defeito que este arquivo previne: 31
// efeitos do catalogo caíam num grupo do `switch` do palco que nao fazia
// nada — o desenho deles acontecia ANTES, por mapa (passada de cor,
// estilo, sapphire, pixel). Se alguem acrescentar um efeito e esquecer de
// registrar o desenho, ele aparece na galeria, a pessoa adiciona, e nada
// acontece: sem erro, sem aviso, sem pista.
//
// Aqui estao os CAMINHOS DE DESENHO que existem hoje. Um efeito novo ou
// entra num deles, ou entra na lista de "desenha em outro lugar" — e essa
// lista e explicita de proposito, para o esquecimento doer no teste e nao
// no usuario.
import 'package:aurea/src/features/editor/domain/correcao_de_cor.dart';
import 'package:aurea/src/features/editor/domain/effect.dart';
import 'package:aurea/src/features/editor/domain/estilizar.dart';
import 'package:aurea/src/features/editor/domain/estilizar_lote2.dart';
import 'package:aurea/src/features/editor/domain/pixel_effect.dart';
import 'package:flutter_test/flutter_test.dart';

/// Os que NAO passam por mapa nenhum, e desenham num lugar proprio do
/// palco ou do compositor.
const _desenhaEmOutroLugar = <EffectType>{
  // O ladrilho monta um widget proprio e fotografa a camada.
  EffectType.motionTile,
  // O tempo da camada, e nao um pixel: quantiza em `Layer.localTime`.
  EffectType.posterizeTime,
  // O conteudo da camada, e nao um pixel: empilha tres quadros do video.
  EffectType.rgbTimeWarp,
  // QUAL QUADRO, e nao como pintar o quadro. A trilha do Time Remap e a
  // funcao tempo->fonte do clipe: quem a "desenha" e o decodificador,
  // por `videoSourceTimeAt` (ver cut_ops.dart e o nucleo aurea_timecore).
  // Ele voltou ao catalogo em 20/09, a pedido do dono.
  EffectType.timeRemap,
  // Nitidez: uma passada propria (`PassadaDeNitidez`), com o numero de
  // amostras escolhido pela qualidade da previa.
  EffectType.unsharpMask,
  // Preenchimento E um kernel de pixel: o caminho existe, mas depende do
  // programa do motor de pixel estar carregado. Sem ele o palco pula o
  // efeito em silencio — o programa e carregado no inicio do app, e por
  // isso o caso nao aparece na pratica.
  EffectType.preenchimento,
  // Sombra projetada tem caso proprio no switch, com corpo.
  EffectType.sombraProjetada,
};

void main() {
  test('todo efeito do catalogo tem por onde desenhar', () {
    final semCaminho = <String>[];
    for (final t in efeitosDoCatalogo) {
      final caminhos = [
        efeitosDeCorPorPixel.contains(t),
        receitasSapphire.containsKey(t),
        modoDeEstilo.containsKey(t),
        pixelKernels.containsKey(t),
        _desenhaEmOutroLugar.contains(t),
      ];
      if (!caminhos.any((c) => c)) semCaminho.add(effectSpecs[t]!.name);
    }
    expect(
      semCaminho,
      isEmpty,
      reason: 'sem caminho de desenho: ${semCaminho.join(", ")}',
    );
  });

  test('os tres efeitos novos estao registrados', () {
    expect(receitasSapphire.containsKey(EffectType.pretoEBranco), isTrue);
    expect(_desenhaEmOutroLugar.contains(EffectType.posterizeTime), isTrue);
    expect(_desenhaEmOutroLugar.contains(EffectType.rgbTimeWarp), isTrue);
  });
}
