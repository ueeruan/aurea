import 'animador_de_texto.dart';
import 'text_animator.dart';

/// Presets de animacao de texto.
///
/// ==========================================================================
/// NAO HA MAIS UM PRESET ESCRITO A MAO
/// ==========================================================================
///
/// Cada preset era um bloco proprio: seletor montado na mao, keyframes
/// escritos linha a linha, cada um com a sua ideia de como varrer a
/// frase. Sete receitas independentes para o mesmo motor — e mexer no
/// motor deixava seis delas para tras.
///
/// Agora a lista e uma VISTA de [presetsDoAnimador]: dez receitas, um
/// construtor so ([animadorDaReceita]). Um preset e um conjunto de
/// numeros; se dois presets divergem, e porque os numeros divergem.
class TextPreset {
  const TextPreset({required this.name, required this.build});

  final String name;
  final List<TextAnimator> Function() build;
}

final textPresets = <TextPreset>[
  for (final p in presetsDoAnimador)
    TextPreset(name: p.nome, build: () => [p.construir()]),
];
