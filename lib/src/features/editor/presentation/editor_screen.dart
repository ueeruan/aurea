import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../core/theme/tokens.dart';
import '../../projects/application/projects_controller.dart';
import '../application/desempenho/aurea_performance_manager.dart';
import '../application/editor_controller.dart';
import '../application/perfil3d.dart';
import '../application/playback_controller.dart';
import '../application/ui/editor_session.dart';
import '../application/video_layer_manager.dart';
import '../domain/grupo_ops.dart';
import '../domain/layer.dart';
import 'am/am_widgets.dart' show RecentSheets, closeActiveParamSheet;
import 'am/points_panel.dart' show editPointsRequestProvider;
import 'shell/cromo_editor.dart' show zoomDoPalcoProvider;
import 'ui/paineis/pontos.dart'
    show abrirEditarPontosDaForma, fecharEditarPontos;
import 'ui/shell/contrato.dart' show painelAbertoProvider;
import 'ui/shell/editor_shell.dart';

/// O EDITOR — o dono da SESSAO de edicao, sem desenho proprio.
///
/// A tela que se ve e a [EditorShell] (`ui/shell/editor_shell.dart`, a UI
/// nova). Este widget ficou com o que NAO e visual e que so pode existir
/// uma vez por editor aberto — e por isso continua sendo a porta de
/// entrada: a Home, criar projeto e o remix da Comunidade abrem
/// `EditorScreen()` como sempre, com o projeto ja no controlador.
///
///  * o RELOGIO ([PlaybackController]) e o GERENTE DE VIDEO
///    ([VideoLayerManager]): criados ao abrir, descartados ao fechar; a
///    sincronia de video a cada tique, a cada play/pausa e a cada mutacao;
///  * o SALVAMENTO AUTOMATICO: toda mutacao vai para a lista de projetos
///    (`upsert` do projeto completo — dentro de um grupo, o todo);
///  * o CICLO DE VIDA: segundo plano pausa (tocadores devolvem o
///    decodificador), tela cheia devolve as barras do sistema ao sair;
///  * o GERENTE DE DESEMPENHO ligado enquanto o editor esta aberto;
///  * entrar e sair de GRUPO movem o cabecote;
///  * trocar de camada fecha painel e folhas da camada anterior.
class EditorScreen extends ConsumerStatefulWidget {
  const EditorScreen({super.key, this.playback});

  /// Relogio injetado (testes). Nulo: o editor cria e descarta o seu.
  final PlaybackController? playback;

  @override
  ConsumerState<EditorScreen> createState() => _EditorScreenState();
}

class _EditorScreenState extends ConsumerState<EditorScreen>
    with SingleTickerProviderStateMixin, WidgetsBindingObserver {
  late final PlaybackController _playback;
  final VideoLayerManager _videos = VideoLayerManager();

  @override
  void initState() {
    super.initState();
    RecentSheets.instance.clear();
    WidgetsBinding.instance.addObserver(this);
    // O EDITOR ABRIU: o gerente de desempenho liga as sondas (temperatura,
    // memoria, tempo de quadro) e passa a publicar a politica da previa.
    // Fora do editor ele nao custa nada — ver [editorFechou].
    AureaPerformanceManager.instancia.editorAbriu();
    _playback =
        widget.playback ??
        PlaybackController(
          vsync: this,
          durationOf: () => ref.read(editorControllerProvider).duration,
          // Com midia na cena o relogio espera o tocador comecar a andar;
          // sem midia ele anda direto (composicoes so de formas).
          temMidiaAtiva: () => _videos.temMidiaAtiva,
        );
    _playback.time.addListener(_syncVideos);
    _playback.playing.addListener(_syncVideos);
    // ENTRAR E SAIR DE GRUPO movem o cabecote junto: la dentro o tempo
    // conta do inicio do grupo.
    _controladorDoNivel = ref.read(editorControllerProvider.notifier);
    _controladorDoNivel!.aoMudarDeNivel = (d) {
      final alvo = _playback.time.value + d;
      _playback.seek(alvo < Duration.zero ? Duration.zero : alvo);
    };
    // ABRIR UM PROJETO precisa montar os tocadores AGORA: o relogio esta
    // parado no zero e o sync so aconteceria quando ele andasse.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) return;
      _syncVideos();
      // O zoom do palco e o painel aberto sao da sessao de edicao: entram
      // sempre zerados (o provider do painel e global e sobreviveria ao
      // editor anterior).
      ref.read(zoomDoPalcoProvider.notifier).state = 1.0;
      ref.read(painelAbertoProvider.notifier).state = null;
    });
  }

  EditorController? _controladorDoNivel;

  /// Midias com os grupos abertos, recalculadas so quando a pilha muda:
  /// o gerenciador de video compara a lista por identidade.
  List<Layer>? _pilhaDasMidias;
  List<Layer> _midias = const [];

  void _syncVideos() {
    final project = ref.read(editorControllerProvider);
    if (!identical(project.layers, _pilhaDasMidias)) {
      _pilhaDasMidias = project.layers;
      _midias = midiasAchatadas(project.layers);
    }
    final master = _videos.sync(
      _midias,
      _playback.time.value,
      _playback.playing.value,
      seekRevision: _playback.seekRevision,
    );
    if (master != null) _playback.anchorToMedia(master);
  }

  @override
  void dispose() {
    // Saiu do editor em tela cheia: devolve as barras do sistema.
    if (_telaCheiaAtiva) _modoDeSistema(false);
    WidgetsBinding.instance.removeObserver(this);
    AureaPerformanceManager.instancia.editorFechou();
    RecentSheets.instance.clear();
    // Sem ref no dispose: o controlador foi guardado na montagem.
    _controladorDoNivel?.aoMudarDeNivel = null;
    _playback.time.removeListener(_syncVideos);
    _playback.playing.removeListener(_syncVideos);
    if (widget.playback == null) {
      _playback.dispose();
    }
    _videos.dispose();
    super.dispose();
  }

  /// O APP FOI PARA O SEGUNDO PLANO: para de tocar.
  ///
  /// O `Ticker` do relogio parava sozinho por falta de vsync, mas os
  /// TOCADORES de video e audio nao: ao trocar de app com a previa tocando,
  /// o som continuava e a midia seguia andando — e ao voltar, o relogio
  /// parado e a midia adiantada davam um salto. Pausar de verdade tambem
  /// devolve o decodificador ao sistema.
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    if (state == AppLifecycleState.paused ||
        state == AppLifecycleState.hidden ||
        state == AppLifecycleState.detached) {
      if (_playback.playing.value) _playback.pause();
    }
  }

  bool _telaCheiaAtiva = false;

  void _modoDeSistema(bool telaCheia) {
    _telaCheiaAtiva = telaCheia;
    try {
      SystemChrome.setEnabledSystemUIMode(
        telaCheia ? SystemUiMode.immersiveSticky : SystemUiMode.edgeToEdge,
      );
    } catch (_) {
      // Plataforma sem controle de barras (testes, desktop): segue.
    }
  }

  @override
  Widget build(BuildContext context) {
    // A REGUA DAS RECONSTRUCOES. Desligada custa um `if` (ver Perfil3D);
    // ligada, prova se um passo de slider refaz o editor inteiro.
    Perfil3D.contar('build.editor');

    // TELA CHEIA DE VERDADE: some a barra de status e a de navegacao
    // enquanto a previa ocupa a tela; voltam ao sair.
    ref.listen<bool>(editorSessionProvider.select((s) => s.previewExpanded), (
      antes,
      agora,
    ) {
      if (antes == agora) return;
      _modoDeSistema(agora);
    });

    ref.listen<String?>(selectedLayerProvider, (previous, next) {
      if (previous == next) return;
      // Um painel/atalho capturado para A nao pode editar A depois de
      // selecionar B.
      RecentSheets.instance.clear();
      closeActiveParamSheet(context);
      fecharEditarPontos(ref);
      if (ref.read(painelAbertoProvider) != null) {
        ref.read(painelAbertoProvider.notifier).state = null;
      }
    });

    ref.listen(editorControllerProvider, (previous, updated) {
      if (previous?.id != updated.id) {
        RecentSheets.instance.clear();
        closeActiveParamSheet(context);
      }
      // Mutacao com o dedo ARRASTANDO na tela e passo de gesto: o gerente
      // liga [Interacao.agora], e a lista de projetos (e a Inicio atras da
      // rota) para de ser refeita a cada evento de ponteiro.
      AureaPerformanceManager.instancia.houveMutacao();
      // O SALVAMENTO AUTOMATICO. Dentro de um grupo o estado e o grupo; o
      // que se salva e o todo.
      ref
          .read(projectsControllerProvider.notifier)
          .upsert(ref.read(editorControllerProvider.notifier).projetoCompleto);
      _syncVideos();
    });

    // Desenho vetorial pelo menu de adicionar: abre o Editar pontos na
    // camada recem-criada.
    ref.listen<String?>(editPointsRequestProvider, (_, id) {
      if (id == null) return;
      ref.read(editPointsRequestProvider.notifier).state = null;
      abrirEditarPontosDaForma(context, ref, _playback, id);
    });

    // O relogio compoe na taxa da COMPOSICAO, nao na da tela.
    _playback.compositionFps = ref.watch(
      editorControllerProvider.select((p) => p.fps),
    );

    return AureaTheme(
      tokens: AureaTokens.motion,
      child: EditorShell(playback: _playback, videos: _videos),
    );
  }
}
