import 'dart:ui' show Color, Offset;

import 'effect.dart';
import 'keyframe.dart';
import 'layer.dart';
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
const int versaoDasPrevias = 1;

/// A foto da amostra (ja no pacote do app, 640x360).
const String fotoDaAmostra = 'assets/templates/campo.jpg';

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
        position: AnimatedOffset(const Offset(c - 10, c))
            .withKeyframe(Duration.zero, const Offset(c - 10, c))
            .withKeyframe(meio, const Offset(c + 10, c - 6))
            .withKeyframe(ciclo, const Offset(c - 10, c)),
        // A foto e 16:9 e a composicao quadrada: pela largura ela ocupa
        // 56% da altura; 2,0 cobre com folga para o vaivem nao mostrar
        // borda preta.
        scaleX: AnimatedDouble(2.0)
            .withKeyframe(Duration.zero, 2.0)
            .withKeyframe(meio, 2.12)
            .withKeyframe(ciclo, 2.0),
        scaleY: AnimatedDouble(2.0)
            .withKeyframe(Duration.zero, 2.0)
            .withKeyframe(meio, 2.12)
            .withKeyframe(ciclo, 2.0),
        effects: efeito(),
      ),
    ],
  );
}

/// O instante do quadro [i] da tira.
Duration instanteDoQuadro(int i) =>
    Duration(microseconds: i * 1000000 ~/ fpsDaPrevia);
