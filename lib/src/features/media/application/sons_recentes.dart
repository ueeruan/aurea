import 'dart:convert';
import 'dart:io';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/storage/prefs.dart';

/// UM SOM QUE JA ENTROU NUM PROJETO: o caminho copiado para o app e o
/// nome como a pessoa conhece.
class SomRecente {
  const SomRecente({required this.caminho, required this.nome});

  final String caminho;
  final String nome;

  Map<String, dynamic> toJson() => {'c': caminho, 'n': nome};

  static SomRecente? fromJson(Object? raw) {
    if (raw is! Map) return null;
    final c = raw['c'];
    final n = raw['n'];
    if (c is! String || n is! String) return null;
    return SomRecente(caminho: c, nome: n);
  }
}

/// OS SONS RECENTES do aparelho: quem poe a mesma trilha em varios
/// projetos nao deveria abrir o seletor de arquivos toda vez. A lista
/// guarda os ultimos sons importados (ja copiados para o app) e some
/// com o que foi apagado do disco.
class SonsRecentesNotifier extends Notifier<List<SomRecente>> {
  static const kChave = 'audio.recentes';
  static const maximo = 10;

  @override
  List<SomRecente> build() {
    try {
      final bruto = ref.read(sharedPreferencesProvider).getString(kChave);
      if (bruto == null) return const [];
      final lista = jsonDecode(bruto);
      if (lista is! List) return const [];
      return List.unmodifiable([
        for (final m in lista)
          if (SomRecente.fromJson(m) case final s?)
            if (File(s.caminho).existsSync()) s,
      ]);
    } catch (_) {
      return const [];
    }
  }

  void registrar(String caminho, String nome) {
    final novo = [
      SomRecente(caminho: caminho, nome: nome),
      ...state.where((s) => s.caminho != caminho),
    ];
    state = List.unmodifiable(
      novo.length > maximo ? novo.sublist(0, maximo) : novo,
    );
    _gravar();
  }

  void tirar(String caminho) {
    state = List.unmodifiable(state.where((s) => s.caminho != caminho));
    _gravar();
  }

  void _gravar() {
    try {
      ref
          .read(sharedPreferencesProvider)
          .setString(kChave, jsonEncode([for (final s in state) s.toJson()]));
    } catch (_) {}
  }
}

final sonsRecentesProvider =
    NotifierProvider<SonsRecentesNotifier, List<SomRecente>>(
      SonsRecentesNotifier.new,
    );
