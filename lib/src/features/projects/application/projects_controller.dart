import 'dart:async';

import 'package:flutter/widgets.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../editor/application/interacao.dart';
import '../../editor/domain/video_project.dart';
import 'project_repository.dart';

final projectRepositoryProvider = Provider<ProjectRepository>(
  (ref) => ProjectRepository(),
);

/// Lista de projetos, espelhada em disco (um JSON por projeto). As
/// gravacoes vindas do editor chegam a cada mutacao, entao o save por
/// projeto e debounced — mas criacao/exclusao gravam na hora.
class ProjectsController extends Notifier<List<VideoProject>>
    with WidgetsBindingObserver {
  final Map<String, Timer> _saveTimers = {};
  bool _loaded = false;
  bool _disposed = false, _lifecycleRegistered = false;
  final Map<String, VideoProject> _pending = {};

  /// Projetos que mudaram DURANTE um gesto e ainda nao foram para a lista
  /// (ver [upsert]). Saem daqui quando o gesto acaba, no Timer ou no
  /// [flush] — o que vier primeiro.
  final Set<String> _foraDaLista = {};

  void _ensureLifecycle() {
    if (_lifecycleRegistered) return;
    _lifecycleRegistered = true;
    _disposed = false;
    // O APP INDIO PARA SEGUNDO PLANO: GRAVA AGORA.
    //
    // A gravacao e debounced em 900 ms por projeto, e o unico caminho que
    // esvaziava a fila era o `onDispose` abaixo — que so roda quando o
    // container do Riverpod e destruido, ou seja numa saida ORDEIRA. O
    // sistema operacional nao avisa antes de matar: no Android e no iOS a
    // suspensao em segundo plano e o preludio comum do fim do processo, e
    // o que estivesse na janela de 900 ms ia embora — a ultima edicao, que
    // e justamente a que a pessoa acabou de fazer.
    //
    // `paused` e o ultimo estado em que ainda da para escrever. `hidden`
    // cobre o caminho do iOS, que passa por ele antes de `paused` em
    // algumas versoes; gravar duas vezes e barato porque a segunda nao
    // acha nada pendente.
    WidgetsBinding.instance.addObserver(this);
    // O gesto acabou: a lista recebe, de uma vez, o que o arrasto mudou.
    Interacao.agora.addListener(_aoMudarAInteracao);
    final repository = ref.read(projectRepositoryProvider);
    ref.onDispose(() {
      _disposed = true;
      _lifecycleRegistered = false;
      WidgetsBinding.instance.removeObserver(this);
      Interacao.agora.removeListener(_aoMudarAInteracao);
      _foraDaLista.clear();
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
    // Quem ficou fora da lista por causa de um gesto entra agora: depois
    // do flush nao sobra `_pending` de onde tirar o projeto.
    publicarPendentes();
    final projects = _pending.values.toList();
    _pending.clear();
    final repository = ref.read(projectRepositoryProvider);
    for (final project in projects) {
      await repository.save(project);
    }
    await repository.flush();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    // So `paused` e `hidden` importam: sao os ultimos instantes em que o
    // processo ainda tem CPU garantida. Em `resumed` nao ha o que fazer —
    // a fila ja foi esvaziada antes de sair.
    if (state != AppLifecycleState.paused && state != AppLifecycleState.hidden) {
      return;
    }
    unawaited(flush());
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
    _foraDaLista.clear();
    state = const [];
    final repo = ref.read(projectRepositoryProvider);
    for (final id in ids) {
      repo.delete(id);
    }
  }

  void remove(String id) {
    _ensureLifecycle();
    _pending.remove(id);
    _foraDaLista.remove(id);
    state = state.where((p) => p.id != id).toList();
    _saveTimers.remove(id)?.cancel();
    ref.read(projectRepositoryProvider).delete(id);
  }

  /// Mantem a lista em dia quando o editor altera o projeto aberto e
  /// agenda a gravacao em disco (debounce por projeto).
  ///
  /// DURANTE UM GESTO A LISTA NAO E TOCADA.
  ///
  /// O editor chama isto a CADA mutacao, e um arrasto sao dezenas por
  /// segundo. Cada chamada montava uma lista nova (O(n projetos)) e
  /// notificava a Inicio — que continua montada atras da rota do editor e
  /// refazia a grade inteira, escondida, a cada passo do dedo. O debounce
  /// de 900 ms protegia o DISCO, nao a notificacao.
  ///
  /// Com [Interacao.agora] ligado, so o rascunho e guardado e o Timer
  /// rearmado; a lista recebe o projeto UMA vez, quando o gesto acaba
  /// ([_aoMudarAInteracao]), no Timer ou no [flush]. Fora de gesto
  /// (renomear pela Inicio, um toque num botao) nada muda: publica na hora.
  void upsert(VideoProject project) {
    _ensureLifecycle();
    _pending[project.id] = project;
    if (Interacao.agora.value) {
      _foraDaLista.add(project.id);
    } else {
      _foraDaLista.remove(project.id);
      _publicar(project);
    }
    _armarGravacao(project.id);
  }

  void _armarGravacao(String id) {
    _saveTimers[id]?.cancel();
    _saveTimers[id] = Timer(const Duration(milliseconds: 900), () {
      _saveTimers.remove(id);
      if (_disposed) return;
      // O DEDO AINDA ESTA NA TELA: gravar agora poria a serializacao do
      // projeto (e a abertura do isolate de escrita) em cima do arrasto.
      // Espera mais uma janela; `paused`/`flush` continuam gravando na hora.
      if (Interacao.agora.value) {
        _armarGravacao(id);
        return;
      }
      publicarPendentes();
      final current = _pending.remove(id);
      if (current != null) {
        ref.read(projectRepositoryProvider).save(current);
      }
    });
  }

  /// RECENTE QUER DIZER RECENTE. O projeto editado ficava no lugar em que
  /// nasceu: depois de criar cinco, aquele em que se passou a tarde
  /// continuava em quinto na Inicio. Quem mexeu por ultimo vai para a
  /// frente.
  void _publicar(VideoProject project) {
    state = [project, ...state.where((p) => p.id != project.id)];
  }

  /// Poe na lista o que um gesto deixou de fora. Barato quando nao ha
  /// nada: o editor chama ao fechar, para a Inicio ja voltar em dia.
  void publicarPendentes() {
    if (_foraDaLista.isEmpty || _disposed) return;
    final ids = _foraDaLista.toList();
    _foraDaLista.clear();
    for (final id in ids) {
      final p = _pending[id];
      if (p != null) _publicar(p);
    }
  }

  void _aoMudarAInteracao() {
    if (Interacao.agora.value) return;
    publicarPendentes();
  }
}

final projectsControllerProvider =
    NotifierProvider<ProjectsController, List<VideoProject>>(
      ProjectsController.new,
    );
