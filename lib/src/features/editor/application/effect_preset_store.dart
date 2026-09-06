import 'dart:convert';
import 'dart:io';

import 'package:flutter/foundation.dart';
import 'package:path_provider/path_provider.dart';

import '../domain/effect_preset.dart';
import '../domain/project_store.dart';

/// PRESETS DE EFEITO DA PESSOA, guardados FORA do projeto.
///
/// Um preset que so existe dentro do projeto em que nasceu nao e preset,
/// e receita perdida. Aqui a lista vive num arquivo proprio na pasta do
/// app: o que se salvou editando um projeto aparece em todos os outros,
/// e sobrevive a apagar o projeto.
///
/// O arquivo e um JSON simples; os efeitos usam a mesma serializacao do
/// projeto, entao qualquer efeito que o projeto sabe guardar o preset
/// tambem sabe.
class EffectPresetStore {
  EffectPresetStore._();

  static final EffectPresetStore instance = EffectPresetStore._();

  /// Sobe a cada mudanca na lista.
  final ValueNotifier<int> revision = ValueNotifier<int>(0);

  List<EffectPreset> _presets = const [];
  bool _carregado = false;
  Future<void>? _carregando;

  /// Para testes: guarda em memoria, sem arquivo.
  @visibleForTesting
  static bool semArquivo = false;

  List<EffectPreset> get presets => List.unmodifiable(_presets);

  Future<File> _arquivo() async {
    final docs = await getApplicationDocumentsDirectory();
    return File('${docs.path}/effect_presets.json');
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
      _presets = [
        for (final m in raw)
          if (m is Map<String, dynamic>) effectPresetFromJson(m),
      ];
      revision.value++;
    } catch (_) {
      // Arquivo corrompido: melhor lista vazia do que app sem abrir.
      _presets = const [];
    }
  }

  Future<void> _persist() async {
    revision.value++;
    if (semArquivo) return;
    try {
      final f = await _arquivo();
      await f.writeAsString(
          jsonEncode([for (final p in _presets) effectPresetToJson(p)]),
          flush: true);
    } catch (_) {}
  }

  Future<void> add(EffectPreset preset) async {
    await load();
    _presets = [preset, ..._presets.where((p) => p.id != preset.id)];
    await _persist();
  }

  Future<void> remove(String id) async {
    await load();
    _presets = [for (final p in _presets) if (p.id != id) p];
    await _persist();
  }

  Future<void> rename(String id, String name) async {
    await load();
    _presets = [
      for (final p in _presets) p.id == id ? p.copyWith(name: name) : p,
    ];
    await _persist();
  }

  @visibleForTesting
  void reset() {
    _presets = const [];
    _carregado = false;
    _carregando = null;
  }
}

Map<String, dynamic> effectPresetToJson(EffectPreset p) => {
      'id': p.id,
      'name': p.name,
      'tags': p.tags,
      'category': p.category,
      'durationUs': p.suggestedDuration.inMicroseconds,
      'createdAt': p.createdAt.toIso8601String(),
      'author': p.author,
      'effects': [for (final e in p.effects) effectToJson(e)],
    };

EffectPreset effectPresetFromJson(Map<String, dynamic> m) => EffectPreset(
      id: m['id'] as String?,
      name: m['name'] as String? ?? 'Preset',
      tags: [for (final t in (m['tags'] as List? ?? const [])) t.toString()],
      category: m['category'] as String? ?? 'Meus',
      suggestedDuration:
          Duration(microseconds: (m['durationUs'] as num?)?.toInt() ?? 2000000),
      createdAt: DateTime.tryParse(m['createdAt'] as String? ?? ''),
      author: m['author'] as String? ?? '',
      effects: [
        for (final e in (m['effects'] as List? ?? const []))
          if (e is Map<String, dynamic>) effectFromJson(e),
      ],
    );
