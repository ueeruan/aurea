import 'dart:async';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../editor/domain/video_project.dart';
import 'project_repository.dart';

final projectRepositoryProvider = Provider<ProjectRepository>(
  (ref) => ProjectRepository(),
);

/// Lista de projetos, espelhada em disco (um JSON por projeto). As
/// gravacoes vindas do editor chegam a cada mutacao, entao o save por
/// projeto e debounced — mas criacao/exclusao gravam na hora.
class ProjectsController extends Notifier<List<VideoProject>> {
  final Map<String, Timer> _saveTimers = {};
  bool _loaded = false;
  bool _disposed = false, _lifecycleRegistered = false;
  final Map<String, VideoProject> _pending = {};

  void _ensureLifecycle() {
    if (_lifecycleRegistered) return;
    _lifecycleRegistered = true;
    _disposed = false;
    final repository = ref.read(projectRepositoryProvider);
    ref.onDispose(() {
      _disposed = true;
      _lifecycleRegistered = false;
      for (final timer in _saveTimers.values) {
        timer.cancel();
      }
      _saveTimers.clear();
      for (final project in _pending.values) {
        unawaited(repository.save(project));
      }
      _pending.clear();
    });
  }

  Future<void> flush() async {
    _ensureLifecycle();
    for (final timer in _saveTimers.values) {
      timer.cancel();
    }
    _saveTimers.clear();
    final projects = _pending.values.toList();
    _pending.clear();
    final repository = ref.read(projectRepositoryProvider);
    for (final project in projects) {
      await repository.save(project);
    }
    await repository.flush();
  }

  @override
  List<VideoProject> build() {
    _ensureLifecycle();
    Future.microtask(_loadOnce);
    return const [];
  }

  Future<void> _loadOnce() async {
    if (_loaded) return;
    _loaded = true;
    final fromDisk = await ref.read(projectRepositoryProvider).loadAll();
    if (_disposed) return;
    // Nao sobrescreve projetos ja criados nesta sessao antes do load.
    final known = {for (final p in state) p.id};
    state = [...state, ...fromDisk.where((p) => !known.contains(p.id))];
  }

  void add(VideoProject project) {
    _ensureLifecycle();
    state = [project, ...state];
    ref.read(projectRepositoryProvider).save(project);
  }

  /// APAGA TODOS OS PROJETOS (com confirmacao na interface).
  void removeAll() {
    _ensureLifecycle();
    final ids = [for (final p in state) p.id];
    for (final t in _saveTimers.values) {
      t.cancel();
    }
    _saveTimers.clear();
    _pending.clear();
    state = const [];
    final repo = ref.read(projectRepositoryProvider);
    for (final id in ids) {
      repo.delete(id);
    }
  }

  void remove(String id) {
    _ensureLifecycle();
    _pending.remove(id);
    state = state.where((p) => p.id != id).toList();
    _saveTimers.remove(id)?.cancel();
    ref.read(projectRepositoryProvider).delete(id);
  }

  /// Mantem a lista em dia quando o editor altera o projeto aberto e
  /// agenda a gravacao em disco (debounce por projeto).
  void upsert(VideoProject project) {
    _ensureLifecycle();
    _pending[project.id] = project;
    // RECENTE QUER DIZER RECENTE. O projeto editado ficava no lugar em
    // que nasceu: depois de criar cinco, aquele em que se passou a
    // tarde continuava em quinto na Inicio. Quem mexeu por ultimo vai
    // para a frente.
    state = [project, ...state.where((p) => p.id != project.id)];
    _saveTimers[project.id]?.cancel();
    _saveTimers[project.id] = Timer(const Duration(milliseconds: 900), () {
      _saveTimers.remove(project.id);
      if (_disposed) return;
      final current = _pending.remove(project.id);
      if (current != null) {
        ref.read(projectRepositoryProvider).save(current);
      }
    });
  }
}

final projectsControllerProvider =
    NotifierProvider<ProjectsController, List<VideoProject>>(
      ProjectsController.new,
    );
