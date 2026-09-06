import 'dart:async';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../editor/domain/video_project.dart';
import 'project_repository.dart';

final projectRepositoryProvider =
    Provider<ProjectRepository>((ref) => ProjectRepository());

/// Lista de projetos, espelhada em disco (um JSON por projeto). As
/// gravacoes vindas do editor chegam a cada mutacao, entao o save por
/// projeto e debounced — mas criacao/exclusao gravam na hora.
class ProjectsController extends Notifier<List<VideoProject>> {
  final Map<String, Timer> _saveTimers = {};
  bool _loaded = false;

  @override
  List<VideoProject> build() {
    Future.microtask(_loadOnce);
    return const [];
  }

  Future<void> _loadOnce() async {
    if (_loaded) return;
    _loaded = true;
    final fromDisk = await ref.read(projectRepositoryProvider).loadAll();
    // Nao sobrescreve projetos ja criados nesta sessao antes do load.
    final known = {for (final p in state) p.id};
    state = [...state, ...fromDisk.where((p) => !known.contains(p.id))];
  }

  void add(VideoProject project) {
    state = [project, ...state];
    ref.read(projectRepositoryProvider).save(project);
  }

  void remove(String id) {
    state = state.where((p) => p.id != id).toList();
    _saveTimers.remove(id)?.cancel();
    ref.read(projectRepositoryProvider).delete(id);
  }

  /// Mantem a lista em dia quando o editor altera o projeto aberto e
  /// agenda a gravacao em disco (debounce por projeto).
  void upsert(VideoProject project) {
    // RECENTE QUER DIZER RECENTE. O projeto editado ficava no lugar em
    // que nasceu: depois de criar cinco, aquele em que se passou a
    // tarde continuava em quinto na Inicio. Quem mexeu por ultimo vai
    // para a frente.
    state = [project, ...state.where((p) => p.id != project.id)];
    _saveTimers[project.id]?.cancel();
    _saveTimers[project.id] = Timer(const Duration(milliseconds: 900), () {
      _saveTimers.remove(project.id);
      final current = state.where((p) => p.id == project.id).firstOrNull;
      if (current != null) {
        ref.read(projectRepositoryProvider).save(current);
      }
    });
  }
}

final projectsControllerProvider =
    NotifierProvider<ProjectsController, List<VideoProject>>(
        ProjectsController.new);
