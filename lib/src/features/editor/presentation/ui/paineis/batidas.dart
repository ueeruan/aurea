import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../../../core/ui/tocavel.dart';
import '../../../application/editor_controller.dart';
import '../../../application/media_preview_service.dart';
import '../../../domain/audio_ops.dart';
import '../../../domain/layer.dart';
import 'comum_de_objetos.dart' show FileiraDePilulas, LinhaDeAcao;
import 'pecas_centrais.dart'
    show AcaoDoCabecalho, FileiraDeAcoes, respiroDoPainel;

// ===========================================================================
// BATIDAS E PULSO — as duas folhas do ritmo
// ===========================================================================
//
// Vieram de `am/beats_sheet.dart` e `am/beat_pulse_sheet.dart` com as
// mesmas chamadas ao controlador e a mesma regra de desfazer: cada comando
// e o passo que o controlador ja define (detectar = um passo, cortar nas
// batidas = um passo, aplicar o pulso = um passo). O que mudou foi so a
// casca: folha do DS, pilulas, linha de propriedade e avisos do painel.

/// DETECTAR BATIDAS numa trilha.
///
/// O que sai daqui nao sao os ataques crus: e a GRADE regular que nasce
/// do andamento. Ataque treme alguns milissegundos, e corte encaixado em
/// ataque herda o tremor — soa fora do tempo mesmo caindo "onde a musica
/// bateu". Por isso o BPM aparece e da para corrigir: quem edita musica
/// muitas vezes sabe o andamento, e ajustar acerta mais rapido do que
/// reanalisar.
///
/// [ref] fica na assinatura porque todo chamador (barra do projeto, menu
/// das marcas, painel de audio) ja o passa; a folha observa o projeto pelo
/// proprio `Consumer`, que vive enquanto ela estiver aberta.
Future<void> showBeatsSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) {
  return mostrarAureaFolha<void>(
    context,
    titulo: 'Batidas',
    construtor: (_) => _FolhaDasBatidas(layerId: layerId),
  );
}

class _FolhaDasBatidas extends ConsumerStatefulWidget {
  const _FolhaDasBatidas({required this.layerId});

  final String layerId;

  @override
  ConsumerState<_FolhaDasBatidas> createState() => _FolhaDasBatidasState();
}

class _FolhaDasBatidasState extends ConsumerState<_FolhaDasBatidas> {
  var _faixa = BeatBand.grave;
  var _sensibilidade = 50.0;
  var _denominador = 4;
  var _rodando = false;

  // Marcar na timeline assim que detectar (o pedido do beta 89).
  var _marcarAoDetectar = true;

  Future<void> _analisar() async {
    final c = ref.read(editorControllerProvider.notifier);
    setState(() => _rodando = true);
    final n = await c.detectBeatsInto(
      widget.layerId,
      band: _faixa,
      sensitivity: _sensibilidade,
      denominador: _denominador,
    );
    // A folha pode ter fechado enquanto o arquivo era ouvido: sem ela nao
    // ha a quem avisar, e as marcas ficam para quem pedir de novo.
    if (!mounted) return;
    // As marcas entram LOGO depois da grade: o controlador junta as duas
    // mutacoes (menos de 450 ms, nenhuma estrutural) no passo da deteccao,
    // como na folha antiga — um desfazer tira as duas.
    final marcados = n != null && _marcarAoDetectar
        ? c.batidasViramMarcadores()
        : 0;
    setState(() => _rodando = false);
    AureaSnack.show(
      context,
      n == null
          ? translate(context, 'Não achei ritmo nessa faixa')
          : (_marcarAoDetectar
                ? moldar(context, '{0} batidas, {1} marcadores na timeline', [
                    n,
                    marcados,
                  ])
                : moldar(context, '{0} marcas de batida', [n])),
    );
  }

  @override
  Widget build(BuildContext context) {
    // SO O QUE A FOLHA MOSTRA do projeto: o andamento e quantas batidas.
    // Um record compara por valor, entao outra mutacao nao a refaz.
    final (:bpm, :batidas) = ref.watch(
      editorControllerProvider.select(
        (p) => (bpm: p.bpm, batidas: p.beats.length),
      ),
    );
    final c = ref.read(editorControllerProvider.notifier);

    return ListView(
      shrinkWrap: true,
      padding: respiroDoPainel,
      children: [
        AureaSection(
          titulo: 'Faixa de frequência',
          chave: 'batidas-faixa',
          recolhivel: false,
          filhos: [
            FileiraDePilulas<BeatBand>(
              opcoes: BeatBand.values,
              atual: _faixa,
              chave: 'batidas-faixa',
              chaveDe: (b) => b.name,
              rotuloDe: (b) => switch (b) {
                BeatBand.grave => 'Grave',
                BeatBand.medio => 'Médio',
                BeatBand.agudo => 'Agudo',
                BeatBand.tudo => 'Tudo',
              },
              aoEscolher: (b) => setState(() => _faixa = b),
            ),
            const AureaAvisoDoPainel(
              texto:
                  'Bumbo e chimbal atacam em instantes diferentes. Cortar no '
                  'grave é cortar no pulso; no agudo, na levada.',
            ),
          ],
        ),
        // Sensibilidade e estado da folha, nao do projeto: o arrasto nao
        // muta nada e por isso nao abre gesto de desfazer.
        AureaPropertyRow(
          rotulo: 'Sensibilidade',
          chave: 'batidas-sensibilidade',
          valor: _sensibilidade,
          min: 0,
          max: 100,
          casas: 0,
          aoMudar: (v) => setState(() => _sensibilidade = v),
        ),
        AureaSection(
          titulo: 'Subdivisão',
          chave: 'batidas-subdivisao',
          recolhivel: false,
          filhos: [
            FileiraDePilulas<int>(
              opcoes: const [1, 2, 4, 8],
              atual: _denominador,
              chave: 'batidas-subdivisao',
              chaveDe: (d) => '$d',
              traduzir: false,
              rotuloDe: (d) => '1/$d',
              aoEscolher: (d) {
                setState(() => _denominador = d);
                // Ja analisado: trocar a densidade nao precisa reler o
                // arquivo, so refazer a grade.
                if (bpm != null) c.setBpm(bpm, denominador: d);
              },
            ),
            const AureaAvisoDoPainel(
              texto:
                  'Em compasso 4/4: 1/4 põe uma marca em cada tempo, 1/8 '
                  'duas, 1/1 uma por compasso.',
            ),
          ],
        ),
        AureaPropertyRow.personalizada(
          rotulo: 'Andamento',
          chave: 'batidas-andamento',
          filho: Row(
            children: [
              Expanded(
                child: bpm == null
                    ? AppText(
                        'ainda não analisado',
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: AureaEstilos.corpo,
                      )
                    : AppTextMoldado(
                        '{0} bpm · {1} marcas',
                        [bpm.toStringAsFixed(1), batidas],
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: AureaEstilos.valor,
                      ),
              ),
              // O andamento a mao, um bpm por toque: cada toque e um
              // `setBpm`, como os botoes da folha antiga.
              if (bpm != null) ...[
                AcaoDoCabecalho(
                  key: const ValueKey('batidas-bpm-menos'),
                  icone: CupertinoIcons.minus_circle,
                  aoTocar: () => c.setBpm(bpm - 1, denominador: _denominador),
                ),
                AcaoDoCabecalho(
                  key: const ValueKey('batidas-bpm-mais'),
                  icone: CupertinoIcons.plus_circle,
                  aoTocar: () => c.setBpm(bpm + 1, denominador: _denominador),
                ),
              ],
            ],
          ),
        ),
        // A linha inteira liga e desliga (o interruptor tambem): a chave
        // e a da folha antiga, que os testes procuram.
        Tocavel(
          key: const ValueKey('batidas-marcar-ao-detectar'),
          encolhe: 1,
          onTap: () => setState(() => _marcarAoDetectar = !_marcarAoDetectar),
          child: SizedBox(
            height: AureaDims.linhaDePropriedade,
            child: Row(
              children: [
                Expanded(
                  child: AppText(
                    'Marcar as batidas na timeline',
                    maxLines: 2,
                    overflow: TextOverflow.ellipsis,
                    style: AureaEstilos.corpo,
                  ),
                ),
                AureaToggle(
                  valor: _marcarAoDetectar,
                  aoMudar: (v) => setState(() => _marcarAoDetectar = v),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: AureaDims.e8),
        _BotaoPrincipal(
          chave: 'batidas-detectar',
          rotulo: _rodando
              ? 'Ouvindo a faixa...'
              : (batidas == 0 ? 'Detectar batidas' : 'Detectar de novo'),
          aoTocar: _rodando ? null : _analisar,
        ),
        if (batidas > 0)
          FileiraDeAcoes(
            acoes: [
              AureaChip(
                key: const ValueKey('batidas-virar-marcas'),
                rotulo: 'Marcar na timeline',
                icone: CupertinoIcons.flag,
                aoTocar: () {
                  final n = c.batidasViramMarcadores();
                  AureaSnack.show(
                    context,
                    moldar(context, '{0} marcadores', [n]),
                  );
                },
              ),
              AureaChip(
                key: const ValueKey('batidas-cortar'),
                rotulo: 'Cortar nas batidas',
                icone: CupertinoIcons.scissors,
                aoTocar: () {
                  // Um comando, um passo: `cutAtMarkers` ja agrupa os
                  // cortes todos.
                  final n = c.cutAtMarkers(usarBatidas: true);
                  AureaSnack.show(context, moldar(context, '{0} cortes', [n]));
                },
              ),
              AureaChip(
                key: const ValueKey('batidas-limpar'),
                rotulo: 'Limpar',
                icone: CupertinoIcons.delete,
                aoTocar: c.clearBeats,
              ),
            ],
          ),
      ],
    );
  }
}

/// PULSAR NA BATIDA.
///
/// Feito na mao, isso e um keyframe a cada meio segundo por tres minutos
/// — ninguem faz, e o video fica parado enquanto a musica anda. A conta
/// de achar a batida ja existe; o que faltava era virar keyframe.
Future<void> showBeatPulseSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) {
  return mostrarAureaFolha<void>(
    context,
    titulo: 'Pulsar na batida',
    // O contexto do EDITOR vai junto: os avisos com "Desfazer" nascem
    // depois que a folha fecha, e o contexto dela ja nao existe.
    construtor: (_) =>
        _FolhaDoPulso(layerId: layerId, contextoDoEditor: context),
  );
}

class _FolhaDoPulso extends ConsumerStatefulWidget {
  const _FolhaDoPulso({required this.layerId, required this.contextoDoEditor});

  final String layerId;
  final BuildContext contextoDoEditor;

  @override
  ConsumerState<_FolhaDoPulso> createState() => _FolhaDoPulsoState();
}

class _FolhaDoPulsoState extends ConsumerState<_FolhaDoPulso> {
  var _forca = 0.12;
  String? _fonteId;

  // AS BATIDAS DA FONTE, lembradas: achar batida percorre a forma de onda
  // inteira, e a folha se refaz a cada passo do arrasto da forca. So se
  // recalcula quando a fonte muda ou quando a onda de alguma midia ficou
  // pronta (a revisao do servico anda).
  String? _fonteDaConta;
  int? _revisaoDaConta;
  List<Duration>? _batidas;

  List<Duration>? _batidasDa(String fonteId, String? caminho, int revisao) {
    if (fonteId == _fonteDaConta && revisao == _revisaoDaConta) {
      return _batidas;
    }
    if (fonteId != _fonteDaConta && caminho != null) {
      // A forma de onda pode nao estar pronta ainda: pede, e a revisao do
      // servico acorda a folha quando ela chegar.
      MediaPreviewService.instance.ensureWaveform(caminho).ignore();
    }
    _fonteDaConta = fonteId;
    _revisaoDaConta = revisao;
    return _batidas = ref
        .read(editorControllerProvider.notifier)
        .beatsOf(fonteId);
  }

  void _aplicar() {
    final c = ref.read(editorControllerProvider.notifier);
    final n = c.applyBeatPulse(widget.layerId, _fonteId!, amount: _forca);
    if (n == null) {
      AureaSnack.show(
        context,
        translate(context, 'A forma de onda ainda não ficou pronta'),
      );
      return;
    }
    if (n == 0) {
      AureaSnack.show(
        context,
        translate(context, 'Nenhuma batida cai dentro desta camada'),
      );
      return;
    }
    Navigator.of(context).pop();
    final editor = widget.contextoDoEditor;
    if (!editor.mounted) return;
    AureaSnack.show(
      editor,
      moldar(editor, '{0} batidas viraram keyframe', [n]),
      actionLabel: translate(editor, 'Desfazer'),
      onAction: c.undo,
    );
  }

  void _tirarEscala() {
    final c = ref.read(editorControllerProvider.notifier);
    c.clearScaleKeyframes(widget.layerId);
    Navigator.of(context).pop();
    final editor = widget.contextoDoEditor;
    if (!editor.mounted) return;
    AureaSnack.show(
      editor,
      translate(editor, 'Escala voltou a ser fixa'),
      actionLabel: translate(editor, 'Desfazer'),
      onAction: c.undo,
    );
  }

  @override
  Widget build(BuildContext context) {
    // A camada e as fontes de som: a folha e modal, e o que muda o projeto
    // enquanto ela esta aberta sao as proprias acoes dela (que a fecham).
    final projeto = ref.watch(editorControllerProvider);
    final camada = projeto.layerById(widget.layerId);
    if (camada == null) {
      return const Padding(
        padding: respiroDoPainel,
        child: AureaAvisoDoPainel(texto: 'Esta camada não existe mais.'),
      );
    }
    final fontes = [
      for (final l in projeto.layers)
        if (l is AudioLayer || (l is VideoLayer && l.volume > 0.001)) l,
    ];
    _fonteId ??= fontes.isEmpty ? null : fontes.first.id;
    final nomes = {for (final f in fontes) f.id: f.name};

    return ListView(
      shrinkWrap: true,
      padding: respiroDoPainel,
      children: [
        // O nome da camada e da pessoa: entra no molde, fora do catalogo.
        Padding(
          padding: const EdgeInsets.only(bottom: AureaDims.e6),
          child: AppTextMoldado(
            '"{0}" cresce um tiquinho em cada ataque da música.',
            [camada.name],
            style: AureaEstilos.propriedade,
          ),
        ),
        if (fontes.isEmpty)
          const AureaAvisoDoPainel(texto: 'Não há faixa de som no projeto.')
        else ...[
          AureaSection(
            titulo: 'Ouvir de',
            chave: 'pulsar-fonte',
            recolhivel: false,
            filhos: [
              FileiraDePilulas<String>(
                opcoes: [for (final f in fontes) f.id],
                atual: _fonteId,
                chave: 'pulsar-fonte',
                chaveDe: (id) => id,
                traduzir: false,
                rotuloDe: (id) => nomes[id] ?? '',
                aoEscolher: (id) => setState(() => _fonteId = id),
              ),
            ],
          ),
          // A forca e estado da folha: so vira keyframe no "Aplicar".
          AureaPropertyRow(
            rotulo: 'Força',
            chave: 'pulsar-forca',
            valor: (_forca * 100).clamp(2, 60).toDouble(),
            min: 2,
            max: 60,
            casas: 0,
            unidade: '%',
            aoMudar: (v) => setState(() => _forca = v / 100),
          ),
          ValueListenableBuilder<int>(
            valueListenable: MediaPreviewService.instance.revision,
            builder: (context, revisao, _) {
              final fonte = _fonteId;
              final caminho = switch (fonte == null
                  ? null
                  : projeto.layerById(fonte)) {
                AudioLayer a => a.sourcePath,
                VideoLayer v => v.sourcePath,
                _ => null,
              };
              final batidas = fonte == null
                  ? null
                  : _batidasDa(fonte, caminho, revisao);
              return batidas == null
                  ? const AureaAvisoDoPainel(texto: 'Lendo o som...')
                  : Padding(
                      padding: const EdgeInsets.symmetric(
                        vertical: AureaDims.e10,
                      ),
                      child: AppTextMoldado(
                        '{0} batidas encontradas.',
                        [batidas.length],
                        style: AureaEstilos.propriedade,
                      ),
                    );
            },
          ),
          _BotaoPrincipal(
            chave: 'pulsar-aplicar',
            rotulo: 'Aplicar',
            aoTocar: _aplicar,
          ),
          const SizedBox(height: AureaDims.e4),
          LinhaDeAcao(
            key: const ValueKey('pulsar-tirar-escala'),
            rotulo: 'Tirar os keyframes de escala',
            icone: CupertinoIcons.arrow_counterclockwise,
            aoTocar: _tirarEscala,
          ),
        ],
      ],
    );
  }
}

/// O BOTAO DA ACAO PRINCIPAL de uma folha (Detectar, Aplicar): a largura
/// toda, 44 de toque, no tom do destaque apagado — o mesmo "aceso" da
/// pilula, sem borda. Nulo em [aoTocar] = ocupado (texto apagado).
class _BotaoPrincipal extends StatelessWidget {
  const _BotaoPrincipal({
    required this.chave,
    required this.rotulo,
    required this.aoTocar,
  });

  final String chave;
  final String rotulo;
  final VoidCallback? aoTocar;

  @override
  Widget build(BuildContext context) => Tocavel(
    key: ValueKey(chave),
    onTap: aoTocar,
    haptico: true,
    child: Container(
      height: AureaDims.toqueConfortavel,
      alignment: Alignment.center,
      decoration: BoxDecoration(
        color: AureaCores.destaqueApagado,
        borderRadius: BorderRadius.circular(AureaDims.raioXl),
      ),
      child: AppText(
        rotulo,
        maxLines: 1,
        style: AureaEstilos.corpo.copyWith(
          fontWeight: FontWeight.w600,
          color: aoTocar == null
              ? AureaCores.textoSecundario
              : AureaCores.destaque,
        ),
      ),
    ),
  );
}
