import 'package:flutter_riverpod/flutter_riverpod.dart';

enum PreviewResolution {
  full('Full', 1),
  half('1/2', .5),
  quarter('1/4', .25),
  eighth('1/8', .125);

  const PreviewResolution(this.label, this.scale);
  final String label;
  final double scale;
}

/// Session preference only: never changes project dimensions or export settings.
final previewResolutionProvider = StateProvider<PreviewResolution>(
  (ref) => PreviewResolution.full,
);

/// O NIVEL DE QUALIDADE DAS PARTICULAS (0..3), tirado da RESOLUCAO DA
/// PREVIA.
///
/// NAO E UM BOTAO NOVO, E DE PROPOSITO: a resolucao da previa ja e o
/// botao que a pessoa gira quando o aparelho esta sofrendo — quem baixa
/// para 1/4 esta dizendo "este celular nao esta dando conta". A nuvem de
/// particulas entra na mesma conversa, em vez de ter o proprio controle
/// que ninguem sabe que existe.
///
/// O TETO E GENEROSO (ate 4096): ele limita o caso patologico, e nao o
/// uso normal. Um campo de mil particulas nao muda nada em nivel cheio.
final nivelDasParticulasProvider = Provider<int>((ref) {
  final r = ref.watch(previewResolutionProvider);
  return switch (r) {
    PreviewResolution.full => 3,
    PreviewResolution.half => 2,
    PreviewResolution.quarter => 1,
    PreviewResolution.eighth => 0,
  };
});

/// Guides from the transform pad, in composition coordinates.
final transformGuidesProvider = StateProvider<({double? x, double? y})>(
  (ref) => (x: null, y: null),
);
