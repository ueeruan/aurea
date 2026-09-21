import 'dart:math' as math;

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/audio_render_service.dart';
import '../../../application/editor_controller.dart';
import '../../../application/media_preview_service.dart';
import '../../../domain/audio_effect.dart';
import '../../../domain/audio_ops.dart';
import '../../../domain/layer.dart';
import '../../am/audio_sheet.dart'
    show somCopiado, volumeComKeyframeAlternado, volumeEditado;
import '../../am/beats_sheet.dart' show showBeatsSheet;
import '../shell/contrato.dart';
import 'comum.dart';
import 'pecas_centrais.dart';

/// O SOM de uma camada que tem audio (camada de audio ou video).
AudioSpec? somDa(Layer? l) => switch (l) {
  AudioLayer a => a.audio,
  VideoLayer v => v.audio,
  _ => null,
};

/// AUDIO — o "Volume" da referencia, e o resto do som da camada:
///
///   Volume (com losango: o envelope)  ·  Mudo  ·  Ganho
///   Fade de entrada · Fade de saida
///   Abaixar pela voz (ducking): qual faixa manda e quanto desce
///   Efeitos de audio (eco, compressor, EQ...) em cartoes
///   Voz e EQ (limpeza, presenca, de-esser, tres bandas)
///   Normalizar · Remover silencio · Copiar/colar som · Batidas
///
/// O VOLUME ANIMA pelo envelope do som (`AudioSpec.volumeAnimado`, em
/// tempo de clipe): parado, a linha muda o numero; com marcas, so a marca
/// que estiver no cabecote (quem crava e o losango) — a mesma regra de
/// todo numero do editor. A conta e a do editor antigo
/// (`volumeEditado`/`volumeComKeyframeAlternado`), nao uma segunda.
class PainelAudio extends ConsumerWidget {
  const PainelAudio({super.key, required this.layerId});

  final String layerId;

  static const _titulo = 'Áudio';

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final chave = 'painel-${PainelId.audio.name}';
    final som = somDa(camada);
    if (som == null) {
      return AureaPanel(
        titulo: _titulo,
        chave: chave,
        aoFechar: escopo.fecharPainel,
        filhos: const [
          AureaAvisoDoPainel(texto: 'Esta camada não tem som.'),
        ],
      );
    }
    final caminho = switch (camada) {
      AudioLayer a => a.sourcePath,
      VideoLayer v => v.sourcePath,
      _ => null,
    };
    if (caminho != null &&
        MediaPreviewService.instance.estadoDaOnda(caminho) ==
            EstadoDaOnda.semAudio) {
      return AureaPanel(
        titulo: _titulo,
        chave: chave,
        aoFechar: escopo.fecharPainel,
        filhos: const [
          AureaAvisoDoPainel(texto: 'Esta camada não tem áudio.'),
        ],
      );
    }
    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      aoFechar: escopo.fecharPainel,
      corpo: NoCabecote(
        construir: (context, t) => ListView(
          padding: respiroDoPainel,
          children: _linhas(context, ref, camada, som, t),
        ),
      ),
    );
  }

  List<Widget> _linhas(
    BuildContext context,
    WidgetRef ref,
    Layer camada,
    AudioSpec som,
    Duration t,
  ) {
    final escopo = EscopoDoEditor.of(context);
    final c = ref.read(editorControllerProvider.notifier);
    final id = layerId;

    // O TEMPO DO ENVELOPE e o do clipe, cru e preso a ele.
    Duration localDe(Duration global) {
      final bruto = global - camada.startTime;
      if (bruto < Duration.zero) return Duration.zero;
      return bruto > camada.duration ? camada.duration : bruto;
    }

    final local = localDe(t);
    void editar(AudioSpec Function(AudioSpec) f) => c.updateAudioSpec(id, f);

    final trilha = som.volumeAnimado;
    final kf = losangoNoInstante(
      marcasUs: marcasDe(trilha),
      agoraUs: local.inMicroseconds,
      inicio: camada.startTime,
      playback: escopo.playback,
      aoAlternar: () => umPasso(
        ref,
        () => editar(
          (a) => volumeComKeyframeAlternado(
            a,
            localDe(escopo.playback.time.value),
          ),
        ),
      ),
    );
    final recusaAqui = trilha != null && !trilha.aceitaEdicaoEm(local);
    final segundos = camada.duration.inMilliseconds / 1000.0;
    final teto = segundos.clamp(.5, 10.0).toDouble();

    AureaPropertyRow numero(
      String rotulo,
      double valor,
      double min,
      double max,
      ValueChanged<double> mudar, {
      int casas = 0,
      String unidade = '',
      double? padrao,
      String? chave,
    }) => AureaPropertyRow(
      rotulo: rotulo,
      chave: chave,
      valor: valor.clamp(min, max).toDouble(),
      min: min,
      max: max,
      casas: casas,
      unidade: unidade,
      aoMudar: aCadaPasso((v) => mudar(v.clamp(min, max).toDouble())),
      aoResetar: padrao == null ? null : () => umPasso(ref, () => mudar(padrao)),
      aoComecarGesto: c.beginGesture,
      aoTerminarGesto: c.endGesture,
    );

    final proc = som.processing;

    return [
      AureaPropertyRow(
        rotulo: 'Volume',
        valor: som.volumeEm(local) * 100,
        min: 0,
        max: 400,
        casas: 0,
        unidade: '%',
        keyframe: kf.estado,
        aoAnterior: kf.anterior,
        aoProximo: kf.proximo,
        aoComecarGesto: c.beginGesture,
        aoTerminarGesto: c.endGesture,
        aoResetar: () =>
            umPasso(ref, () => editar((a) => a.copyWith(clearVolumeAnimado: true))),
        aoMudar: aCadaPasso(
          (v) => editar(
            (a) => volumeEditado(a, localDe(escopo.playback.time.value), v / 100),
          ),
        ),
      ),
      if (recusaAqui)
        const AureaAvisoDoPainel(
          texto:
              'O volume tem keyframes: toque no losango para marcar este '
              'instante.',
        ),
      AureaPropertyRow.personalizada(
        rotulo: 'Mudo',
        filho: AureaToggle(
          valor: som.muted,
          aoMudar: (v) => umPasso(ref, () => editar((a) => a.copyWith(muted: v))),
        ),
      ),
      numero(
        'Ganho',
        som.gain,
        0,
        4,
        (v) => editar((a) => a.copyWith(gain: v)),
        casas: 2,
        unidade: '×',
        padrao: 1,
      ),
      numero(
        'Fade de entrada',
        som.fadeIn.inMilliseconds / 1000.0,
        0,
        teto,
        (v) => editar(
          (a) => a.copyWith(fadeIn: Duration(milliseconds: (v * 1000).round())),
        ),
        casas: 2,
        unidade: 's',
        padrao: 0,
      ),
      numero(
        'Fade de saída',
        som.fadeOut.inMilliseconds / 1000.0,
        0,
        teto,
        (v) => editar(
          (a) =>
              a.copyWith(fadeOut: Duration(milliseconds: (v * 1000).round())),
        ),
        casas: 2,
        unidade: 's',
        padrao: 0,
      ),
      AureaSection(
        titulo: 'Abaixar pela voz',
        chave: 'audio-ducking',
        filhos: [
          _EscolhaDaVoz(
            layerId: id,
            atual: som.duckAgainstId,
            aoEscolher: (voz) => umPasso(
              ref,
              () => editar(
                (a) => voz == null
                    ? a.copyWith(clearDuck: true)
                    : a.copyWith(duckAgainstId: voz),
              ),
            ),
          ),
          if (som.duckAgainstId != null)
            numero(
              'Quanto desce',
              som.duckAmount * 100,
              0,
              100,
              (v) => editar((a) => a.copyWith(duckAmount: v / 100)),
              unidade: '%',
              padrao: 70,
            ),
        ],
      ),
      SecaoDeEfeitosDeAudio(layerId: id),
      AureaSection(
        titulo: 'Voz e EQ',
        chave: 'audio-voz-eq',
        inicialmenteAberta: false,
        filhos: [
          numero(
            'Limpar ruído',
            proc.denoise * 100,
            0,
            100,
            (v) => editar(
              (a) => a.copyWith(
                processing: a.processing.copyWith(denoise: v / 100),
              ),
            ),
            unidade: '%',
            padrao: 0,
          ),
          numero(
            'Voz',
            proc.voice * 100,
            0,
            100,
            (v) => editar(
              (a) => a.copyWith(
                processing: a.processing.copyWith(voice: v / 100),
              ),
            ),
            unidade: '%',
            padrao: 0,
          ),
          numero(
            'De-esser',
            proc.deEsser * 100,
            0,
            100,
            (v) => editar(
              (a) => a.copyWith(
                processing: a.processing.copyWith(deEsser: v / 100),
              ),
            ),
            unidade: '%',
            padrao: 0,
          ),
          for (final (rotulo, valor, gravar) in [
            (
              'Graves',
              proc.lowDb,
              (AudioProcessing x, double v) => x.copyWith(lowDb: v),
            ),
            (
              'Médios',
              proc.midDb,
              (AudioProcessing x, double v) => x.copyWith(midDb: v),
            ),
            (
              'Agudos',
              proc.highDb,
              (AudioProcessing x, double v) => x.copyWith(highDb: v),
            ),
          ])
            numero(
              rotulo,
              valor,
              -12,
              12,
              (v) => editar(
                (a) => a.copyWith(processing: gravar(a.processing, v)),
              ),
              casas: 1,
              unidade: 'dB',
              padrao: 0,
            ),
        ],
      ),
      FileiraDeAcoes(
        acoes: [
          AureaChip(
            key: const ValueKey('audio-normalizar'),
            rotulo: 'Normalizar',
            icone: CupertinoIcons.speedometer,
            aoTocar: () {
              final g = c.normalizeAudio(id);
              AureaSnack.show(
                context,
                g == null
                    ? translate(context, 'A forma de onda ainda está sendo lida')
                    : '${translate(context, 'Ganho ajustado para')} '
                          '${gainToDb(g).toStringAsFixed(1)} dB',
              );
            },
          ),
          AureaChip(
            key: const ValueKey('audio-remover-silencio'),
            rotulo: 'Remover silêncio',
            icone: CupertinoIcons.scissors,
            aoTocar: () {
              final n = c.removeSilence(id);
              AureaSnack.show(
                context,
                switch (n) {
                  null => translate(
                    context,
                    'A forma de onda ainda está sendo lida',
                  ),
                  <= 1 => translate(context, 'Não achei pausa longa o bastante'),
                  _ => '$n ${translate(context, 'pedaços, sem as pausas')}',
                },
                actionLabel: n != null && n > 1
                    ? translate(context, 'Desfazer')
                    : null,
                onAction: c.undo,
              );
            },
          ),
          AureaChip(
            key: const ValueKey('audio-copiar'),
            rotulo: 'Copiar som',
            icone: CupertinoIcons.doc_on_doc,
            aoTocar: () {
              somCopiado = som;
              AureaSnack.show(context, translate(context, 'Som copiado'));
            },
          ),
          if (somCopiado != null)
            AureaChip(
              key: const ValueKey('audio-colar'),
              rotulo: 'Colar som',
              icone: CupertinoIcons.doc_on_clipboard,
              aoTocar: () {
                final copia = somCopiado!;
                // A voz que manda nao pode ser a propria camada.
                umPasso(
                  ref,
                  () => editar(
                    (_) => copia.duckAgainstId == id
                        ? copia.copyWith(clearDuck: true)
                        : copia,
                  ),
                );
              },
            ),
          AureaChip(
            key: const ValueKey('audio-batidas'),
            rotulo: 'Batidas e BPM',
            icone: CupertinoIcons.metronome,
            aoTocar: () {
              escopo.playback.pause();
              showBeatsSheet(context, ref, id);
            },
          ),
        ],
      ),
    ];
  }
}

/// QUAL FAIXA MANDA nesta (o ducking): qualquer outra com som.
///
/// Observa so a LISTA DE CANDIDATAS, reduzida a um texto: o painel nao
/// acorda quando outra camada muda um numero, so quando uma faixa com som
/// entra, sai ou muda de nome.
class _EscolhaDaVoz extends ConsumerWidget {
  const _EscolhaDaVoz({
    required this.layerId,
    required this.atual,
    required this.aoEscolher,
  });

  final String layerId;
  final String? atual;
  final ValueChanged<String?> aoEscolher;

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final lista = ref.watch(
      editorControllerProvider.select(
        (p) => [
          for (final l in p.layers)
            if (l.id != layerId && (l is AudioLayer || l is VideoLayer))
              '${l.id}\u0001${l.name}',
        ].join('\u0000'),
      ),
    );
    final vozes = <String?, String>{
      null: 'Nenhuma',
      if (lista.isNotEmpty)
        for (final par in lista.split('\u0000'))
          par.split('\u0001').first: par.split('\u0001').last,
    };
    return AureaPropertyRow.personalizada(
      rotulo: 'Voz',
      chave: 'audio-voz',
      filho: AureaDropdown<String?>(
        valor: vozes.containsKey(atual) ? atual : null,
        opcoes: vozes.keys.toList(),
        rotuloDe: (v) =>
            v == null ? translate(context, 'Nenhuma') : vozes[v] ?? '',
        // Nome de camada e conteudo da pessoa: nao vai ao catalogo. O
        // "Nenhuma" e traduzido na hora, pelo mesmo motivo inverso.
        traduzir: false,
        titulo: 'Abaixar pela voz',
        aoMudar: aoEscolher,
      ),
    );
  }
}

/// OS EFEITOS DE AUDIO da camada em cartoes do DS: olho liga e desliga,
/// ⋯ sobe, desce, reseta e apaga, e cada parametro e uma linha.
///
/// Mora aqui e aparece em dois lugares: no painel Audio e no painel
/// Efeitos de uma camada de audio (onde "efeito" quer dizer isto).
class SecaoDeEfeitosDeAudio extends ConsumerWidget {
  const SecaoDeEfeitosDeAudio({
    super.key,
    required this.layerId,
    this.comTitulo = true,
  });

  final String layerId;
  final bool comTitulo;

  void _gravar(WidgetRef ref, List<AudioEffect> efeitos) => ref
      .read(editorControllerProvider.notifier)
      .updateAudioSpec(
        layerId,
        (a) => a.copyWith(processing: a.processing.copyWith(effects: efeitos)),
      );

  Future<void> _adicionar(BuildContext botao, WidgetRef ref) async {
    final tipo = await mostrarAureaMenu<AudioEffectType>(
      botao,
      titulo: 'Efeito de áudio',
      itens: [
        for (final e in audioEffectSpecs.entries)
          AureaMenuItem(valor: e.key, rotulo: e.value.name, chave: e.key.name),
      ],
    );
    if (tipo == null) return;
    final atuais =
        somDa(ref.read(editorControllerProvider).layerById(layerId))
            ?.processing
            .effects ??
        const <AudioEffect>[];
    umPasso(ref, () => _gravar(ref, [...atuais, AudioEffect(tipo)]));
  }

  Future<void> _menu(
    BuildContext botao,
    WidgetRef ref,
    List<AudioEffect> efeitos,
    int i,
  ) async {
    final escolha = await mostrarAureaMenu<String>(
      botao,
      itens: [
        if (i > 0)
          const AureaMenuItem(
            valor: 'subir',
            rotulo: 'Subir',
            icone: CupertinoIcons.arrow_up,
          ),
        if (i < efeitos.length - 1)
          const AureaMenuItem(
            valor: 'descer',
            rotulo: 'Descer',
            icone: CupertinoIcons.arrow_down,
          ),
        const AureaMenuItem(
          valor: 'resetar',
          rotulo: 'Resetar',
          icone: CupertinoIcons.arrow_counterclockwise,
        ),
        const AureaMenuItem(
          valor: 'apagar',
          rotulo: 'Apagar',
          icone: CupertinoIcons.trash,
          destrutivo: true,
        ),
      ],
    );
    final lista = [...efeitos];
    switch (escolha) {
      case 'subir':
        lista.insert(i - 1, lista.removeAt(i));
      case 'descer':
        lista.insert(i + 1, lista.removeAt(i));
      case 'resetar':
        lista[i] = AudioEffect(lista[i].type, enabled: lista[i].enabled);
      case 'apagar':
        lista.removeAt(i);
      default:
        return;
    }
    umPasso(ref, () => _gravar(ref, lista));
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    // SO A LISTA DE EFEITOS de som desta camada: um fade ou o volume nao
    // refazem os cartoes.
    final efeitos = ref.watch(
      projetoVisivelProvider.select(
        (p) =>
            somDa(p.layerById(layerId))?.processing.effects ??
            const <AudioEffect>[],
      ),
    );
    final c = ref.read(editorControllerProvider.notifier);
    final filhos = <Widget>[
      ValueListenableBuilder<int>(
        valueListenable: AudioRenderService.instance.revision,
        builder: (context, _, _) {
          final l = ref.read(editorControllerProvider).layerById(layerId);
          if (l == null) return const SizedBox.shrink();
          final servico = AudioRenderService.instance;
          final erro = servico.error(l);
          if (erro == null && !servico.busy(l)) return const SizedBox.shrink();
          return AureaAvisoDoPainel(texto: erro ?? 'Preparando som...');
        },
      ),
      for (var i = 0; i < efeitos.length; i++)
        AureaEffectCard(
          key: ValueKey('efeito-audio-$i-${efeitos[i].type.name}'),
          chave: 'efeito-audio-$i',
          nome: efeitos[i].spec.name,
          ligado: efeitos[i].enabled,
          aoAlternarLigado: () {
            final lista = [...efeitos];
            lista[i] = lista[i].toggle();
            umPasso(ref, () => _gravar(ref, lista));
          },
          aoMenu: (botao) => _menu(botao, ref, efeitos, i),
          filhos: [
            for (final p in efeitos[i].spec.params.entries)
              if (p.value.options.isNotEmpty)
                AureaPropertyRow.personalizada(
                  rotulo: p.value.label,
                  chave: 'efeito-audio-$i-${p.key}',
                  filho: AureaDropdown<int>(
                    valor: efeitos[i]
                        .value(p.key)
                        .round()
                        .clamp(0, p.value.options.length - 1),
                    opcoes: [
                      for (var j = 0; j < p.value.options.length; j++) j,
                    ],
                    rotuloDe: (j) => p.value.options[j],
                    titulo: p.value.label,
                    aoMudar: (j) {
                      final lista = [...efeitos];
                      lista[i] = lista[i].edit(p.key, j.toDouble());
                      umPasso(ref, () => _gravar(ref, lista));
                    },
                  ),
                )
              else
                AureaPropertyRow(
                  rotulo: p.value.label,
                  chave: 'efeito-audio-$i-${p.key}',
                  valor: efeitos[i].value(p.key),
                  min: p.value.min,
                  max: p.value.max,
                  casas: (p.value.max - p.value.min) <= 20 ? 2 : 0,
                  aoComecarGesto: c.beginGesture,
                  aoTerminarGesto: c.endGesture,
                  aoResetar: () {
                    final lista = [...efeitos];
                    lista[i] = lista[i].edit(p.key, p.value.initial);
                    umPasso(ref, () => _gravar(ref, lista));
                  },
                  // Relido a cada passo: o arrasto nao pode gravar por
                  // cima de uma lista que o passo anterior ja trocou.
                  aoMudar: aCadaPasso((v) {
                    final atuais =
                        somDa(
                          ref.read(editorControllerProvider).layerById(layerId),
                        )?.processing.effects ??
                        const <AudioEffect>[];
                    if (i >= atuais.length) return;
                    final lista = [...atuais];
                    lista[i] = lista[i].edit(
                      p.key,
                      math.min(p.value.max, math.max(p.value.min, v)),
                    );
                    _gravar(ref, lista);
                  }),
                ),
          ],
        ),
      Builder(
        builder: (botao) => FileiraDeAcoes(
          acoes: [
            AureaChip(
              key: const ValueKey('adicionar-efeito-audio'),
              rotulo: 'Efeito de áudio',
              icone: CupertinoIcons.plus,
              aoTocar: () => _adicionar(botao, ref),
            ),
          ],
        ),
      ),
    ];
    if (!comTitulo) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: filhos,
      );
    }
    return AureaSection(
      titulo: 'Efeitos de áudio',
      chave: 'audio-efeitos',
      filhos: filhos,
    );
  }
}
