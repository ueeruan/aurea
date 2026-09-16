import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../domain/estilo_preset.dart';
import '../domain/project_store.dart';

/// OS ESTILOS DA PESSOA, guardados FORA do projeto — mesma regra dos
/// presets de efeito: o que se salvou editando um projeto aparece em
/// todos os outros e sobrevive a apagar aquele projeto.
class EstiloPresetStore {
  EstiloPresetStore._();

  static final EstiloPresetStore instance = EstiloPresetStore._();

  /// Sobe a cada mudanca na lista.
  final ValueNotifier<int> revisao = ValueNotifier<int>(0);

  List<EstiloPreset> _estilos = const [];
  bool _carregado = false;
  Future<void>? _carregando;

  /// Para testes: guarda em memoria, sem arquivo.
  @visibleForTesting
  static bool semArquivo = false;

  List<EstiloPreset> get estilos => List.unmodifiable(_estilos);

  Future<File> _arquivo() async {
    final docs = await getApplicationDocumentsDirectory();
    return File('${docs.path}/estilos.json');
  }

  Future<void> load() {
    if (_carregado) return Future.value();
    return _carregando ??= _load().whenComplete(() {
      _carregado = true;
      _carregando = null;
    });
  }

  Future<void> _load() async {
    if (semArquivo) return;
    try {
      final f = await _arquivo();
      if (!f.existsSync()) return;
      final raw = jsonDecode(await f.readAsString());
      if (raw is! List) return;
      _estilos = [
        for (final m in raw)
          if (m is Map<String, dynamic>) estiloPresetFromJson(m),
      ];
      revisao.value++;
    } catch (_) {
      // Arquivo corrompido: melhor lista vazia do que app sem abrir.
      _estilos = const [];
    }
  }

  Future<void> _gravar() async {
    revisao.value++;
    if (semArquivo) return;
    try {
      final f = await _arquivo();
      await f.writeAsString(
        jsonEncode([for (final e in _estilos) estiloPresetToJson(e)]),
        flush: true,
      );
    } catch (_) {}
  }

  Future<void> add(EstiloPreset estilo) async {
    await load();
    _estilos = [estilo, ..._estilos.where((e) => e.id != estilo.id)];
    await _gravar();
  }

  Future<void> remove(String id) async {
    await load();
    _estilos = [
      for (final e in _estilos)
        if (e.id != id) e,
    ];
    await _gravar();
  }

  Future<void> renomear(String id, String nome) async {
    await load();
    _estilos = [for (final e in _estilos) e.id == id ? e.comNome(nome) : e];
    await _gravar();
  }

  @visibleForTesting
  void reset() {
    _estilos = const [];
    _carregado = false;
    _carregando = null;
  }
}

Map<String, dynamic> estiloPresetToJson(EstiloPreset e) => {
  'id': e.id,
  'nome': e.nome,
  'criadoEm': e.criadoEm.toIso8601String(),
  'estilos': layerStylesToJson(e.estilos),
};

EstiloPreset estiloPresetFromJson(Map<String, dynamic> m) => EstiloPreset(
  id: m['id'] as String?,
  nome: m['nome'] as String? ?? m['name'] as String? ?? 'Estilo',
  criadoEm: DateTime.tryParse(m['criadoEm'] as String? ?? ''),
  estilos: layerStylesFromJson(
    (m['estilos'] as Map?)?.cast<String, dynamic>() ?? const {},
  ),
);
