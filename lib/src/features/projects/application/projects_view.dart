import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/storage/prefs.dart';
import '../../editor/domain/video_project.dart';

/// COMO A LISTA DE PROJETOS SE ARRUMA.
///
/// Uma lista de seis cabe na tela e nao precisa de nada; a de sessenta
/// vira um monte. Ordenar, procurar pelo nome e mexer em varios de uma
/// vez sao as tres coisas que faltavam — e a ordem escolhida fica
/// lembrada, porque quem gosta de ordem alfabetica gosta sempre.
enum OrdemDosProjetos { recentes, nome, duracao }

String rotuloDaOrdem(OrdemDosProjetos o) => switch (o) {
  OrdemDosProjetos.recentes => 'Mais recentes',
  OrdemDosProjetos.nome => 'Nome (A-Z)',
  OrdemDosProjetos.duracao => 'Mais longos',
};

class OrdemDosProjetosNotifier extends Notifier<OrdemDosProjetos> {
  static const kChave = 'projetos.ordem';

  @override
  OrdemDosProjetos build() {
    try {
      final i = ref.read(sharedPreferencesProvider).getInt(kChave);
      if (i != null && i >= 0 && i < OrdemDosProjetos.values.length) {
        return OrdemDosProjetos.values[i];
      }
    } catch (_) {}
    return OrdemDosProjetos.recentes;
  }

  void escolher(OrdemDosProjetos o) {
    state = o;
    try {
      ref.read(sharedPreferencesProvider).setInt(kChave, o.index);
    } catch (_) {}
  }
}

final ordemDosProjetosProvider =
    NotifierProvider<OrdemDosProjetosNotifier, OrdemDosProjetos>(
      OrdemDosProjetosNotifier.new,
    );

/// O que foi digitado na busca da lista (vazio = sem busca).
final buscaDeProjetosProvider = StateProvider<String>((_) => '');

/// Os projetos MARCADOS. Vazio = nao esta no modo selecao.
final selecaoDeProjetosProvider = StateProvider<Set<String>>((_) => const {});

/// A LISTA PRONTA: filtrada pela busca e na ordem escolhida.
///
/// A ordem por recentes e a que ja existia (a controladora entrega o
/// mais novo primeiro), entao ela nao reordena nada — o que evita
/// embaralhar a lista de quem nunca tocou no botao.
List<VideoProject> projetosArrumados(
  List<VideoProject> projetos,
  OrdemDosProjetos ordem,
  String busca,
) {
  final q = busca.trim().toLowerCase();
  final lista = [
    for (final p in projetos)
      if (q.isEmpty || p.name.toLowerCase().contains(q)) p,
  ];
  switch (ordem) {
    case OrdemDosProjetos.recentes:
      break;
    case OrdemDosProjetos.nome:
      lista.sort(
        (a, b) => a.name.toLowerCase().compareTo(b.name.toLowerCase()),
      );
    case OrdemDosProjetos.duracao:
      lista.sort((a, b) => b.duration.compareTo(a.duration));
  }
  return lista;
}
