import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/storage/prefs.dart';
import '../../domain/effect.dart';

/// EFEITOS RECENTES: os ultimos que a pessoa aplicou, do mais novo para
/// o mais velho, lembrados por aparelho — nao sao do projeto.
///
/// A galeria tem busca, categoria e favorito, mas nada disso ajuda quem
/// acabou de usar um efeito em outra camada e quer o mesmo de novo: era
/// procurar tudo outra vez. Aqui a lista guarda sozinha o que foi usado.
class EffectRecentsNotifier extends Notifier<List<String>> {
  static const kChave = 'efeitos.recentes';

  /// Quantos cabem na lista. Passou disso, o mais velho sai.
  static const maximo = 12;

  @override
  List<String> build() {
    try {
      final lista = ref.read(sharedPreferencesProvider).getStringList(kChave);
      return List.unmodifiable(lista ?? const <String>[]);
    } catch (_) {
      return const [];
    }
  }

  void registrar(EffectType tipo) {
    final id = effectSpecs[tipo]?.id;
    if (id == null) return;
    final novo = [id, ...state.where((e) => e != id)];
    state = List.unmodifiable(
      novo.length > maximo ? novo.sublist(0, maximo) : novo,
    );
    try {
      ref.read(sharedPreferencesProvider).setStringList(kChave, state);
    } catch (_) {}
  }

  void limpar() {
    state = const [];
    try {
      ref.read(sharedPreferencesProvider).setStringList(kChave, const []);
    } catch (_) {}
  }

  /// Os recentes que ainda existem no catalogo, ja como tipo.
  List<EffectType> get tipos => [for (final id in state) ?effectTypeFromId(id)];
}

final effectRecentsProvider =
    NotifierProvider<EffectRecentsNotifier, List<String>>(
      EffectRecentsNotifier.new,
    );

/// RECOMENDADOS: o que vale oferecer antes de a pessoa procurar.
///
/// Nao e um ranking do aplicativo inteiro: e o que combina com ESTA
/// camada. Video ganha as ferramentas de imagem, texto e forma ganham as
/// de estilo, e todo mundo ganha as tres que resolvem a maior parte dos
/// pedidos (aparecer/sumir, brilho e tremor). O que a pessoa acabou de
/// usar entra na frente — recomendar o que ela ja escolheu e melhor
/// palpite do que qualquer lista fixa.
List<EffectType> efeitosRecomendados({
  required bool ehMidia,
  List<EffectType> recentes = const [],
  int quantos = 8,
}) {
  final base = <EffectType>[
    ...recentes,
    EffectType.aparecerSumir,
    if (ehMidia) ...[
      EffectType.corrections,
      EffectType.curves,
      EffectType.lightGlow,
      EffectType.filmGrain,
      EffectType.gaussianBlur,
      EffectType.vignette,
      EffectType.tremor,
    ] else ...[
      EffectType.lightGlow,
      EffectType.repetirEmCirculo,
      EffectType.pena,
      EffectType.oscillate,
      EffectType.gradientMap,
      EffectType.tremor,
      EffectType.raios,
    ],
  ];
  final vistos = <EffectType>{};
  final saida = <EffectType>[];
  for (final t in base) {
    if (!effectSpecs.containsKey(t) || !vistos.add(t)) continue;
    saida.add(t);
    if (saida.length >= quantos) break;
  }
  return saida;
}
