import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/interacao.dart';
import '../../../application/playback_controller.dart';
import '../../../domain/keyframe.dart';
import '../../../domain/layer.dart';
import '../../../domain/layer_meta.dart';
import 'comum.dart';

// AS PECAS DOS PAINEIS DE TEXTO, FORMA E OBJETOS (texto, fonte, estilo,
// animar, legendas, forma, pontos, particulas, clonar, grupo).
//
// Tudo o que aqui mora e REGRA, nao aparencia: um gesto e um passo de
// desfazer, cada passo avisa o palco que o dedo esta mexendo, e a trilha
// animada nao ganha marca sem o losango. A aparencia e toda do DS.

/// O losango pronto de uma linha: estado e as setas.
typedef Losango = ({
  KeyframeState estado,
  VoidCallback? anterior,
  VoidCallback? proximo,
});

/// UM PASSO DE GESTO: avisa o palco que ha dedo mexendo (ele troca
/// qualidade por resposta enquanto isso) e aplica o valor.
ValueChanged<double> aCadaPasso(ValueChanged<double> aplicar) => (v) {
  Interacao.marcar();
  aplicar(v);
};

/// A LINHA NUMERICA de todo painel destes: arrasto = um passo de desfazer
/// (`beginGesture`/`endGesture`), cada passo marca a interacao, e o
/// losango so aparece quando a propriedade anima.
AureaPropertyRow linhaNumerica(
  WidgetRef ref, {
  required String rotulo,
  required double valor,
  required ValueChanged<double> aoMudar,
  double min = double.negativeInfinity,
  double max = double.infinity,
  int casas = 0,
  String unidade = '',
  Losango? losango,
  VoidCallback? aoResetar,
  String? chave,
  double? sensibilidade,
  bool habilitado = true,
}) {
  final c = ref.read(editorControllerProvider.notifier);
  final limitado = min.isFinite && max.isFinite
      ? valor.clamp(min, max).toDouble()
      : valor;
  return AureaPropertyRow(
    rotulo: rotulo,
    chave: chave,
    valor: limitado,
    aoMudar: aCadaPasso(aoMudar),
    min: min,
    max: max,
    casas: casas,
    unidade: unidade,
    sensibilidade: sensibilidade,
    keyframe: losango?.estado,
    aoAnterior: losango?.anterior,
    aoProximo: losango?.proximo,
    aoResetar: aoResetar,
    aoComecarGesto: c.beginGesture,
    aoTerminarGesto: () {
      c.endGesture();
      Interacao.soltar();
    },
    habilitado: habilitado,
  );
}

/// O losango de uma TRILHA qualquer ([AnimatedDouble]) da camada
/// [gravada]: as marcas vem da trilha GRAVADA (o losango nao mente sobre
/// edicao pendente) e o toque chama [aoAlternar]. Com [aoCurva], o toque
/// longo no losango abre o editor de curva do trecho (so com duas marcas
/// ou mais — com uma nao ha trecho).
Losango losangoDaTrilha({
  required AnimatedDouble? trilha,
  required Layer gravada,
  required Duration t,
  required PlaybackController playback,
  required VoidCallback aoAlternar,
  VoidCallback? aoCurva,
}) => losangoDasMarcas(
  marcasUs: [
    for (final k in trilha?.keyframes ?? const []) k.time.inMicroseconds,
  ],
  camada: gravada,
  t: t,
  playback: playback,
  aoAlternar: aoAlternar,
  aoCurva: aoCurva,
);

// ------------------------------------------------ acabamento (LayerStyles)

/// O ACABAMENTO QUE SE VE (contorno, sombra, brilho, degrade): com a
/// edicao pendente, quando ha. Observa so o acabamento desta camada.
LayerStyles estilosVisiveis(WidgetRef ref, String id) =>
    ref.watch(projetoVisivelProvider.select((p) => p.metaOf(id).styles));

/// O ACABAMENTO GRAVADO: a verdade dos losangos.
LayerStyles estilosGravados(WidgetRef ref, String id) =>
    ref.watch(editorControllerProvider.select((p) => p.metaOf(id).styles));

/// Como ler e como escrever UMA trilha do acabamento.
typedef LerTrilha = AnimatedDouble? Function(LayerStyles s);
typedef GravarTrilha = LayerStyles Function(LayerStyles s, AnimatedDouble v);

/// EDITA UMA TRILHA DO ACABAMENTO com a regra do keyframe explicito
/// (`docs/keyframe-explicito.md`), a mesma que o controlador aplica as
/// propriedades de transformacao:
///
///   estatica ................ muda a base;
///   animada, sobre a marca .. muda aquela marca;
///   animada, fora da marca .. fica PENDENTE (a previa mostra, o losango
///                             crava).
///
/// O controlador nao tem essa porta para o acabamento (so
/// `updateLayerStyles`, que grava cru), entao a pendencia e montada aqui
/// com o mesmo [EdicaoPendente] que ele usa.
void editarTrilhaDoEstilo(
  WidgetRef ref, {
  required String layerId,
  required Duration t,
  required LerTrilha ler,
  required GravarTrilha gravar,
  required double valor,
}) {
  final c = ref.read(editorControllerProvider.notifier);
  final projeto = ref.read(editorControllerProvider);
  final camada = projeto.layerById(layerId);
  if (camada == null) return;
  final local = camada.localTime(t);
  final estilos = projeto.metaOf(layerId).styles;
  final trilha = ler(estilos);
  if (trilha == null) return;
  if (trilha.aceitaEdicaoEm(local)) {
    c.updateLayerStyles(layerId, (s) {
      final atual = ler(s);
      return atual == null ? s : gravar(s, atual.edited(local, valor));
    });
    return;
  }
  final comMarca = trilha.comMarcaInserida(local, valor);
  if (ref.read(autoKeyframeProvider)) {
    c.updateLayerStyles(layerId, (s) => gravar(s, comMarca));
    return;
  }
  final meta = projeto
      .metaOf(layerId)
      .copyWith(styles: gravar(estilos, comMarca));
  ref.read(edicaoPendenteProvider.notifier).state = EdicaoPendente(
    projeto: projeto.copyWith(meta: {...projeto.meta, layerId: meta}),
    camadaId: layerId,
    tempoLocal: local,
  );
}

/// O LOSANGO de uma trilha do acabamento: crava a pendencia desta camada
/// neste instante, quando ha; senao poe ou tira a marca (a marca nova
/// parte a curva do trecho, como no controlador).
void alternarMarcaDoEstilo(
  WidgetRef ref, {
  required String layerId,
  required Duration t,
  required LerTrilha ler,
  required GravarTrilha gravar,
}) {
  final c = ref.read(editorControllerProvider.notifier);
  final camada = ref.read(editorControllerProvider).layerById(layerId);
  if (camada == null) return;
  final local = camada.localTime(t);
  final pendente = ref.read(edicaoPendenteProvider);
  if (pendente != null &&
      pendente.camadaId == layerId &&
      pendente.tempoLocal == local) {
    // CRAVAR E UM PASSO SO: o arrasto que veio antes nao gerou mutacao.
    ref.read(edicaoPendenteProvider.notifier).state = null;
    final estilos = pendente.projeto.metaOf(layerId).styles;
    c.runAsOneUndo(() => c.setLayerStyles(layerId, estilos));
    return;
  }
  c.updateLayerStyles(layerId, (s) {
    final trilha = ler(s);
    if (trilha == null) return s;
    return gravar(
      s,
      trilha.hasKeyframeAt(local)
          ? trilha.withoutKeyframe(local)
          : trilha.comMarcaInserida(local),
    );
  });
}

// ------------------------------------------------------------ pecas visuais

/// UMA ACAO DO PAINEL com nome: item de lista (37 + respiro), icone no
/// destaque a esquerda. Diferente de [LinhaDePorta], nao promete abrir
/// outra tela (sem a seta).
class LinhaDeAcao extends StatelessWidget {
  const LinhaDeAcao({
    super.key,
    required this.rotulo,
    required this.icone,
    required this.aoTocar,
    this.destrutiva = false,
    this.habilitada = true,
  });

  final String rotulo;
  final IconData icone;
  final VoidCallback aoTocar;
  final bool destrutiva;
  final bool habilitada;

  @override
  Widget build(BuildContext context) {
    final cor = !habilitada
        ? AureaCores.textoSecundario
        : destrutiva
        ? AureaCores.perigo
        : AureaCores.destaque;
    return Tocavel(
      onTap: habilitada ? aoTocar : null,
      encolhe: 1,
      child: SizedBox(
        height: AureaDims.itemDeLista + AureaDims.e6,
        child: Row(
          children: [
            Icon(icone, size: AureaDims.iconeSm + 2, color: cor),
            const SizedBox(width: AureaDims.e10),
            Expanded(
              child: AppText(
                rotulo,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: AureaEstilos.corpo.copyWith(
                  color: habilitada
                      ? (destrutiva ? AureaCores.perigo : AureaCores.texto)
                      : AureaCores.textoSecundario,
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

/// A FILEIRA DE PILULAS de uma escolha curta (modo, tipo, posicao): rola
/// de lado quando nao cabe, em vez de estourar a linha num celular de 360.
class FileiraDePilulas<T> extends StatelessWidget {
  const FileiraDePilulas({
    super.key,
    required this.opcoes,
    required this.atual,
    required this.rotuloDe,
    required this.aoEscolher,
    this.chave = 'opcao',
    this.chaveDe,
    this.traduzir = true,
  });

  final List<T> opcoes;
  final T? atual;
  final String Function(T) rotuloDe;
  final ValueChanged<T> aoEscolher;

  /// Chave de cada pilula: `<chave>-<chaveDe(opcao)>` (padrao: o indice).
  final String chave;
  final String Function(T)? chaveDe;
  final bool traduzir;

  @override
  Widget build(BuildContext context) => SizedBox(
    height: AureaDims.toqueConfortavel,
    child: ListView.separated(
      scrollDirection: Axis.horizontal,
      itemCount: opcoes.length,
      separatorBuilder: (_, _) => const SizedBox(width: AureaDims.e6),
      itemBuilder: (context, i) {
        final o = opcoes[i];
        return Center(
          child: AureaChip(
            key: ValueKey('$chave-${chaveDe?.call(o) ?? i}'),
            rotulo: rotuloDe(o),
            ativo: o == atual,
            traduzir: traduzir,
            aoTocar: () => aoEscolher(o),
          ),
        );
      },
    ),
  );
}

/// O INTERRUPTOR DE UMA SECAO que liga e desliga (contorno, sombra,
/// traco): a linha de propriedade com o [AureaToggle].
AureaPropertyRow linhaDeLigar({
  required String rotulo,
  required bool valor,
  required ValueChanged<bool> aoMudar,
  String? chave,
}) => AureaPropertyRow.personalizada(
  rotulo: rotulo,
  chave: chave,
  filho: AureaToggle(valor: valor, aoMudar: aoMudar),
);

/// O AVISO DE CAMADA ERRADA: o painel abriu numa camada que nao e do tipo
/// dele (a casca abre qualquer painel em qualquer camada nos testes, e o
/// menu pode sobrar aberto depois de um desfazer).
class PainelDeTipoErrado extends StatelessWidget {
  const PainelDeTipoErrado({
    super.key,
    required this.titulo,
    required this.chave,
    required this.aviso,
  });

  final String titulo;
  final String chave;
  final String aviso;

  @override
  Widget build(BuildContext context) => PainelDePortas(
    titulo: titulo,
    chave: chave,
    aviso: aviso,
    portas: const [],
  );
}
