import 'dart:ui' show Color, Offset;

import 'effect.dart';
import 'keyframe.dart';
import 'ajuste_da_midia.dart';
import 'layer.dart';
import 'shape.dart';
import 'video_project.dart';

/// A PREVIA DE CADA EFEITO NA GALERIA e o efeito de verdade sobre uma foto.
///
/// Era uma cartela sintetica (degrade, disco laranja e um traco), e os
/// testadores do beta 1.0.5 disseram que o "circulo" nao explicava efeito
/// nenhum: pediram uma imagem animada do efeito. As previas saem do proprio
/// motor (tool/previas_dos_efeitos_test.dart) sobre esta amostra e vao para
/// os assets como uma tira de quadros; a galeria so toca a tira.

/// Quantos quadros cada tira tem, o lado de cada quadro e a cadencia.
const int quadrosDaPrevia = 8;
const int ladoDaPrevia = 200;
const int fpsDaPrevia = 8;

/// Sobe quando a amostra, a receita ou o motor mudam: o teste-guarda cobra
/// que o manifesto foi gerado com esta versao.
const int versaoDasPrevias = 2;

/// Retrato fornecido para as previas, incluido no pacote do app.
const String fotoDaAmostra = 'assets/efeitos/modelo.png';

/// Onde as tiras moram.
const String pastaDasPrevias = 'assets/efeitos/previas';

/// O LADO DA COMPOSICAO da amostra: maior que o quadro final para o efeito
/// ter pixel com que trabalhar, e a captura reduz.
const int ladoDaAmostra = 400;

/// A composicao da amostra: a foto em tela cheia com um vaivem de zoom e
/// deslocamento (efeito de tempo precisa de movimento para aparecer) que
/// volta ao comeco no fim do ciclo. (Um texto por cima saia como blocos:
/// o gerador roda sem as fontes do app.)
///
/// [caminhoDaFoto] vem de fora porque o gerador le a foto do disco.
VideoProject amostraDoEfeito(
  EffectType? tipo, {
  EffectPronto? pronto,
  String caminhoDaFoto = fotoDaAmostra,
  bool silhueta = false,
}) {
  const lado = ladoDaAmostra;
  const c = lado / 2;
  const ciclo = Duration(milliseconds: 1000 * quadrosDaPrevia ~/ fpsDaPrevia);
  final meio = Duration(microseconds: ciclo.inMicroseconds ~/ 2);
  List<EffectInstance> efeito() {
    if (tipo == null) return const [];
    var fx = EffectInstance(type: tipo);
    if (pronto != null) fx = fx.withPreset(pronto);
    return [fx];
  }

  return VideoProject(
    name: 'amostra',
    createdAt: DateTime(2026, 9, 14),
    aspectRatio: 1,
    resolutionHeight: lado,
    fps: fpsDaPrevia,
    backgroundColor: const Color(0xFF000000),
    layers: [
      ImageLayer(
        name: 'foto',
        startTime: Duration.zero,
        duration: const Duration(seconds: 4),
        sourcePath: caminhoDaFoto,
        // O enquadramento LEGADO, cravado: as previas dos efeitos ja
        // foram geradas com a caixa pela largura, e o padrao novo
        // (cobrir) mudaria a geometria de 74 tiras prontas.
        ajuste: AjusteDaMidia.largura,
        position: AnimatedOffset(const Offset(c - 10, c))
            .withKeyframe(Duration.zero, const Offset(c - 10, c))
            .withKeyframe(meio, const Offset(c + 10, c - 6))
            .withKeyframe(ciclo, const Offset(c - 10, c)),
        // A foto quadrada cobre a composicao durante o movimento.
        scaleX: AnimatedDouble(1.08)
            .withKeyframe(Duration.zero, 1.08)
            .withKeyframe(meio, 1.15)
            .withKeyframe(ciclo, 1.08),
        scaleY: AnimatedDouble(1.08)
            .withKeyframe(Duration.zero, 1.08)
            .withKeyframe(meio, 1.15)
            .withKeyframe(ciclo, 1.08),
        effects: silhueta ? const [] : efeito(),
      ),
      if (silhueta)
        ShapeLayer(
          name: 'estrela',
          startTime: Duration.zero,
          duration: const Duration(seconds: 4),
          position: AnimatedOffset(const Offset(c, c)),
          scaleX: AnimatedDouble(.62),
          scaleY: AnimatedDouble(.62),
          rotation: AnimatedDouble(0)
              .withKeyframe(Duration.zero, 0)
              .withKeyframe(ciclo, 24),
          contents: ShapePresets.paramStar(),
          effects: efeito(),
        ),
    ].reversed.toList(),
  );
}

/// EFEITOS QUE PRECISAM DE SILHUETA para a previa dizer alguma coisa.
///
/// Numa foto de tela cheia o alfa e 1 em todo lugar: contorno, brilho
/// por dentro, pena, borda aspera e aperto de recorte nao tem beirada
/// onde trabalhar, e a repeticao poe as copias fora do quadro. Para
/// estes, a amostra ganha uma ESTRELA por cima da foto, e o efeito vai
/// na estrela — que e onde eles moram na vida real (texto, forma,
/// recorte).
const efeitosComSilhueta = <String>{
  'feather',
  'matte_choker',
  'outline',
  'inner_glow',
  'roughen_edges',
  'repeat_line',
  'repeat_grid',
  'repeat_radial',
  'repeat_scatter',
  'adbe_drop_shadow',
};

/// O instante do quadro [i] da tira.
Duration instanteDoQuadro(int i) =>
    Duration(microseconds: i * 1000000 ~/ fpsDaPrevia);
