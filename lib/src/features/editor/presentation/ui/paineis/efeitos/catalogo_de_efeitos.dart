import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../../core/ds/ds.dart';
import '../../../../../../core/l10n/app_language.dart';
import '../../../../../../core/storage/prefs.dart';
import '../../../../../../core/ui/tocavel.dart';
import '../../../../application/editor_controller.dart';
import '../../../../application/ui/effect_favorites.dart';
import '../../../../application/ui/effect_recents.dart';
import '../../../../domain/animador_de_texto.dart';
import '../../../../domain/effect.dart';
import '../../../../domain/layer.dart';
import 'previa_do_efeito.dart';
import '../../shell/contrato.dart';
import '../pecas_centrais.dart';

/// ABRE O CATALOGO DE EFEITOS da camada [layerId] numa folha grande.
///
/// O "Effect Browser" da referencia: busca por nome e sinonimo, filtros
/// (Todos, Recentes, Favoritos e as categorias), grade com a previa
/// animada de cada efeito ou lista compacta. Um toque aplica e fecha; o
/// cartao do efeito novo abre sozinho na pilha.
Future<void> abrirCatalogoDeEfeitos(
  BuildContext context, {
  required String layerId,
  required EscopoDoEditor escopo,
}) {
  escopo.playback.pause();
  final altura = math.min(560.0, MediaQuery.sizeOf(context).height * .62);
  return mostrarAureaFolha<void>(
    context,
    titulo: 'Adicionar efeito',
    grande: true,
    altura: altura,
    construtor: (_) => CatalogoDeEfeitos(
      layerId: layerId,
      aoAbrirPainel: escopo.abrirPainel,
    ),
  );
}

/// UMA ENTRADA DO CATALOGO: um efeito da pilha de pixel, ou uma das duas
/// portas que moram no catalogo sem ser passe de pixel — o Animador de
/// Texto (efeito da pilha, motor de texto) e o Camera Tracker (o
/// rastreio da camera do video).
sealed class _Entrada {
  const _Entrada();
}

class _Efeito extends _Entrada {
  const _Efeito(this.tipo);
  final EffectType tipo;
}

class _AnimadorDeTexto extends _Entrada {
  const _AnimadorDeTexto();
}

class _CameraTracker extends _Entrada {
  const _CameraTracker();
}

/// O filtro ativo: `todos`, `recentes`, `favoritos`, `Text`, `Camera` ou
/// uma categoria de [effectCategories].
const _todos = 'todos';
const _recentes = 'recentes';
const _favoritos = 'favoritos';
const _texto = 'Text';
const _camera = 'Camera';

/// O que acha as portas na busca (sem acento e sem caixa, como a busca
/// dos efeitos).
const _buscaDoAnimador =
    'animador de texto animacao letra por letra palavra typewriter '
    'maquina de escrever offset seletor range selector text animator';
const _buscaDoRastreio =
    'camera tracker rastrear rastreio camera 3d tracking track solve '
    'rastreador';

/// A pref da grade/lista: conveniencia do aparelho, nao do projeto.
const _chaveDaLista = 'efeitos.catalogo.lista';

class CatalogoDeEfeitos extends ConsumerStatefulWidget {
  const CatalogoDeEfeitos({
    super.key,
    required this.layerId,
    required this.aoAbrirPainel,
  });

  final String layerId;

  /// Abre um painel da casca (o Camera Tracker abre o painel Rastrear).
  final void Function(PainelId id) aoAbrirPainel;

  @override
  ConsumerState<CatalogoDeEfeitos> createState() => _CatalogoDeEfeitosState();
}

class _CatalogoDeEfeitosState extends ConsumerState<CatalogoDeEfeitos> {
  final _busca = TextEditingController();
  String _consulta = '';
  String _filtro = _todos;
  late bool _lista = _lerLista();

  bool _lerLista() {
    try {
      return ref.read(sharedPreferencesProvider).getBool(_chaveDaLista) ??
          false;
    } catch (_) {
      return false;
    }
  }

  void _alternarLista() {
    setState(() => _lista = !_lista);
    try {
      ref.read(sharedPreferencesProvider).setBool(_chaveDaLista, _lista);
    } catch (_) {}
  }

  @override
  void dispose() {
    _busca.dispose();
    super.dispose();
  }

  void _aplicar(_Entrada e) {
    final c = ref.read(editorControllerProvider.notifier);
    final id = widget.layerId;
    switch (e) {
      case _Efeito(:final tipo):
        umPasso(
          ref,
          () => c.addEffect(id, tipo, pronto: prontoAoAplicar(tipo)),
        );
        ref.read(effectRecentsProvider.notifier).registrar(tipo);
        Navigator.of(context).maybePop();
      case _AnimadorDeTexto():
        umPasso(ref, () => c.addTextAnimator(id));
        Navigator.of(context).maybePop();
      case _CameraTracker():
        Navigator.of(context).maybePop();
        widget.aoAbrirPainel(PainelId.rastrear);
    }
  }

  @override
  Widget build(BuildContext context) {
    final camada = ref.watch(
      editorControllerProvider.select((p) => p.layerById(widget.layerId)),
    );
    final favoritos = ref.watch(effectFavoritesProvider);
    ref.watch(effectRecentsProvider);
    final recentes = ref.read(effectRecentsProvider.notifier).tipos;
    final ehVideo = camada is VideoLayer;
    final ehTexto = camada is TextLayer;

    // SO EM VIDEO: o fluxo optico e o Time Remap trabalham na FONTE do
    // clipe. Texto e forma nao tem fonte para remapear, e um tile que nao
    // aplica nada e pior que tile nenhum.
    bool disponivel(EffectType t) =>
        ehVideo || (t != EffectType.opticalFlow && t != EffectType.timeRemap);
    List<_Entrada> efeitos(Iterable<EffectType> tipos) => [
      for (final t in tipos)
        if (effectSpecs.containsKey(t) && disponivel(t)) _Efeito(t),
    ];

    final consulta = normalizarBusca(_consulta.trim());
    final List<_Entrada> entradas;
    if (consulta.isNotEmpty) {
      entradas = [
        if (ehTexto && _buscaDoAnimador.contains(consulta))
          const _AnimadorDeTexto(),
        if (ehVideo && _buscaDoRastreio.contains(consulta))
          const _CameraTracker(),
        ...efeitos(searchEffects(_consulta)),
      ];
    } else {
      entradas = switch (_filtro) {
        _recentes => efeitos(recentes),
        _favoritos => efeitos([
          for (final t in efeitosDoCatalogo)
            if (favoritos.contains(effectSpecs[t]!.id)) t,
        ]),
        _texto => [if (ehTexto) const _AnimadorDeTexto()],
        _camera => [if (ehVideo) const _CameraTracker()],
        _todos => [
          if (ehTexto) const _AnimadorDeTexto(),
          if (ehVideo) const _CameraTracker(),
          ...efeitos(efeitosDoCatalogo),
        ],
        final categoria => efeitos(effectsInCategory(categoria)),
      };
    }

    Widget chip(String filtro, String rotulo) => Padding(
      padding: const EdgeInsets.only(right: AureaDims.e6),
      child: AureaChip(
        key: ValueKey(switch (filtro) {
          _todos || _recentes || _favoritos => 'catalogo-$filtro',
          _ => 'catalogo-cat-$filtro',
        }),
        rotulo: rotulo,
        ativo: consulta.isEmpty && _filtro == filtro,
        aoTocar: () => setState(() {
          _filtro = filtro;
          if (_consulta.isNotEmpty) {
            _consulta = '';
            _busca.clear();
          }
        }),
      ),
    );

    final vazio = switch (consulta.isNotEmpty ? '' : _filtro) {
      _favoritos => 'Nenhum favorito ainda. Toque na estrela de um efeito.',
      _recentes => 'Os efeitos que você aplicar aparecem aqui.',
      _ => 'Nada encontrado. Tente "glow", "rgb", "pixel" ou "shake".',
    };

    return Padding(
      padding: const EdgeInsets.fromLTRB(
        AureaDims.margemDoPainel,
        0,
        AureaDims.margemDoPainel,
        AureaDims.e10,
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Row(
            children: [
              Expanded(
                child: CupertinoSearchTextField(
                  key: const ValueKey('catalogo-busca'),
                  controller: _busca,
                  placeholder: translate(context, 'Buscar efeito'),
                  style: AureaEstilos.corpo,
                  backgroundColor: AureaCores.campo,
                  onChanged: (v) => setState(() => _consulta = v),
                ),
              ),
              AcaoDoCabecalho(
                key: const ValueKey('catalogo-modo'),
                icone: _lista
                    ? CupertinoIcons.square_grid_2x2
                    : CupertinoIcons.list_bullet,
                aoTocar: _alternarLista,
              ),
            ],
          ),
          const SizedBox(height: AureaDims.e8),
          SizedBox(
            height: 28,
            child: ListView(
              key: const ValueKey('catalogo-filtros'),
              scrollDirection: Axis.horizontal,
              children: [
                chip(_todos, 'Todos'),
                // O QUE SO ESTA CAMADA TEM vem logo depois de Todos: e o
                // filtro que muda de camada para camada, e o primeiro que
                // se procura nela.
                if (ehTexto) chip(_texto, 'Texto'),
                if (ehVideo) chip(_camera, 'Câmera'),
                chip(_recentes, 'Recentes'),
                chip(_favoritos, 'Favoritos'),
                for (final c in effectCategories)
                  if (effectsInCategory(c).any(disponivel))
                    chip(c, categoriaDoEfeito(c)),
              ],
            ),
          ),
          const SizedBox(height: AureaDims.e10),
          Expanded(
            child: entradas.isEmpty
                ? Center(
                    child: AppText(
                      vazio,
                      textAlign: TextAlign.center,
                      style: AureaEstilos.propriedade,
                    ),
                  )
                : RelogioDasPrevias(
                    child: _lista
                        ? ListView.builder(
                            key: const ValueKey('catalogo-lista'),
                            itemCount: entradas.length,
                            itemBuilder: (_, i) => _item(
                              entradas[i],
                              favoritos,
                              lista: true,
                            ),
                          )
                        : LayoutBuilder(
                            builder: (context, c) {
                              final colunas = (c.maxWidth / 104).floor().clamp(
                                3,
                                6,
                              );
                              return GridView.builder(
                                key: const ValueKey('catalogo-grade'),
                                gridDelegate:
                                    SliverGridDelegateWithFixedCrossAxisCount(
                                      crossAxisCount: colunas,
                                      mainAxisSpacing: AureaDims.e10,
                                      crossAxisSpacing: AureaDims.e8,
                                      childAspectRatio: .74,
                                    ),
                                itemCount: entradas.length,
                                itemBuilder: (_, i) => _item(
                                  entradas[i],
                                  favoritos,
                                  lista: false,
                                ),
                              );
                            },
                          ),
                  ),
          ),
        ],
      ),
    );
  }

  Widget _item(_Entrada e, Set<String> favoritos, {required bool lista}) {
    final (String id, String nome, String detalhe, IconData? icone) =
        switch (e) {
          _Efeito(:final tipo) => (
            effectSpecs[tipo]!.id,
            effectSpecs[tipo]!.name,
            categoriaDoEfeito(effectSpecs[tipo]!.category),
            null,
          ),
          _AnimadorDeTexto() => (
            'animador_de_texto',
            nomeDoAnimadorDeTexto,
            'Texto',
            CupertinoIcons.textformat,
          ),
          _CameraTracker() => (
            'camera_tracker',
            'Camera Tracker',
            'Câmera',
            CupertinoIcons.viewfinder,
          ),
        };
    final tipo = e is _Efeito ? e.tipo : null;
    final favorito = favoritos.contains(id);
    Widget miniatura(double lado) => tipo != null
        ? PreviaDoEfeito(tipo: tipo, lado: lado)
        : Container(
            width: lado,
            height: lado,
            decoration: BoxDecoration(
              color: AureaCores.campo,
              borderRadius: BorderRadius.circular(AureaDims.raioXl),
            ),
            child: Icon(
              icone,
              size: AureaDims.iconeXl,
              color: AureaCores.destaque,
            ),
          );
    final estrela = tipo == null
        ? null
        : Tocavel(
            key: ValueKey('catalogo-favorito-$id'),
            onTap: () =>
                ref.read(effectFavoritesProvider.notifier).toggle(id),
            child: SizedBox(
              width: 32,
              height: 32,
              child: Icon(
                favorito ? CupertinoIcons.star_fill : CupertinoIcons.star,
                size: AureaDims.iconeSm,
                color: favorito
                    ? AureaCores.destaque
                    : AureaCores.textoSecundario,
              ),
            ),
          );
    final nomeEstilo = AureaEstilos.corpo.copyWith(
      fontSize: 12,
      fontWeight: FontWeight.w600,
    );

    if (lista) {
      return Tocavel(
        key: ValueKey('catalogo-efeito-$id'),
        encolhe: 1,
        onTap: () => _aplicar(e),
        child: SizedBox(
          height: AureaDims.blocoDePainel,
          child: Row(
            children: [
              ClipRRect(
                borderRadius: BorderRadius.circular(AureaDims.raioMd),
                child: miniatura(40),
              ),
              const SizedBox(width: AureaDims.e10),
              Expanded(
                child: Column(
                  mainAxisAlignment: MainAxisAlignment.center,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    AppText(
                      nome,
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: nomeEstilo,
                    ),
                    AppText(
                      detalhe,
                      maxLines: 1,
                      style: AureaEstilos.rotulo,
                    ),
                  ],
                ),
              ),
              ?estrela,
            ],
          ),
        ),
      );
    }
    return Tocavel(
      key: ValueKey('catalogo-efeito-$id'),
      onTap: () => _aplicar(e),
      child: LayoutBuilder(
        builder: (context, c) {
          final lado = c.maxWidth;
          return Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Stack(
                children: [
                  ClipRRect(
                    borderRadius: BorderRadius.circular(AureaDims.raioXl),
                    child: miniatura(lado),
                  ),
                  if (estrela != null)
                    Positioned(right: 0, top: 0, child: estrela),
                ],
              ),
              const SizedBox(height: AureaDims.e4),
              AppText(
                nome,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: nomeEstilo,
              ),
              AppText(detalhe, maxLines: 1, style: AureaEstilos.rotulo),
            ],
          );
        },
      ),
    );
  }
}

/// O QUE O TOQUE NO CATALOGO APLICA NA CAMADA.
///
/// Em regra, o mesmo preset que a miniatura mostrou: quem toca na previa
/// espera ver aquele efeito, e muitos efeitos com os valores iniciais nao
/// mudam nada visivel.
///
/// O MOTION TILE E A EXCECAO, e foi relato de testador: "a imagem comprime,
/// diminui de tamanho, parece voltar". A miniatura dele mostra o preset
/// Tijolos (mosaico 50% x 25%), e aplicar esse preset ENCOLHIA a camada na
/// hora — o passe estava certo, o que chegava era uma demonstracao. O
/// Motion Tile nao pode mexer no tamanho da imagem: ele nasce com o
/// mosaico em 100% (a copia do centro igual a camada) e a SAIDA em 300%,
/// para as copias aparecerem ao redor sem tocar na original.
EffectPronto? prontoAoAplicar(EffectType tipo) {
  if (tipo == EffectType.motionTile) {
    return const EffectPronto('Ao redor', {
      'tile_width': 100,
      'tile_height': 100,
      'output_width': 300,
      'output_height': 300,
    });
  }
  return PreviasDosEfeitos.instance.prontoDaPrevia(tipo);
}
