import 'dart:async';

import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/storage/prefs.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../../../application/proxy_service.dart';
import '../../../application/ui/effect_recents.dart';
import '../../../domain/cut.dart';
import '../../../domain/cut_ops.dart';
import '../../../domain/effect.dart';
import '../../../domain/layer.dart';
import '../../../domain/velocidade.dart';
import '../shell/contrato.dart';
import 'comum.dart';
import 'pecas_centrais.dart';

/// O modo de compensacao que a pessoa escolheu da ultima vez (prefs):
/// vale em toda abertura do painel.
const _chaveDaCompensacao = 'velocidade.compensacao';

/// OS EFEITOS DE TEMPO que o painel Tempo oferece de atalho. Sao efeitos
/// NORMAIS da pilha (Efeitos -> Tempo): o atalho aplica e abre a pilha,
/// nenhuma tela propria.
const efeitosDeTempo = <EffectType>[
  EffectType.timeRemap,
  EffectType.rgbTimeWarp,
  EffectType.posterizeTime,
];

/// TEMPO (video) — velocidade constante, rampas, reverso, blur de
/// velocidade, interpolacao de quadros, congelar quadro e os atalhos para
/// os efeitos de tempo. Tudo aqui, sem folha separada.
class PainelTempo extends ConsumerStatefulWidget {
  const PainelTempo({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<PainelTempo> createState() => _PainelTempoState();
}

class _PainelTempoState extends ConsumerState<PainelTempo> {
  /// Quanto dura o quadro congelado e onde ele entra: escolha do toque,
  /// nao do projeto (so vira projeto no "Congelar aqui").
  double _segundosCongelados = 1;
  FreezePlacement _ondeCongelar = FreezePlacement.separateClip;

  static const _titulo = 'Tempo';

  @override
  Widget build(BuildContext context) {
    final escopo = EscopoDoEditor.of(context);
    final camada = camadaVisivel(ref, widget.layerId);
    if (camada == null) return const PainelSemCamada(titulo: _titulo);
    final c = ref.read(editorControllerProvider.notifier);
    final id = widget.layerId;
    final video = camada is VideoLayer ? camada : null;
    final chave = 'painel-${PainelId.tempo.name}';
    if (video == null && camada is! AudioLayer) {
      return AureaPanel(
        titulo: _titulo,
        chave: chave,
        aoFechar: escopo.fecharPainel,
        filhos: const [
          AureaAvisoDoPainel(
            texto:
                'A velocidade vale para vídeo e áudio. Nas outras camadas, '
                'aproxime ou afaste os keyframes para animar mais rápido ou '
                'mais devagar.',
          ),
        ],
      );
    }

    void congelar() {
      var ok = false;
      umPasso(
        ref,
        () => ok = c.freezeFrame(
          id,
          escopo.playback.time.value,
          duration: Duration(
            milliseconds: (_segundosCongelados * 1000).round(),
          ),
          placement: _ondeCongelar,
        ),
      );
      if (!ok) {
        AureaSnack.show(
          context,
          translate(
            context,
            'Leve o cabeçote para dentro de um clipe de vídeo',
          ),
        );
      }
    }

    void efeitoDeTempo(EffectType tipo) {
      final ja = camada.effects.any((e) => e.type == tipo);
      if (!ja) {
        umPasso(ref, () => c.addEffect(id, tipo));
        ref.read(effectRecentsProvider.notifier).registrar(tipo);
      }
      escopo.abrirPainel(PainelId.efeitos);
    }

    return AureaPanel(
      titulo: _titulo,
      chave: chave,
      aoFechar: escopo.fecharPainel,
      filhos: [
        SecaoDaVelocidade(layerId: id),
        if (video != null) ...[
          AureaPropertyRow.personalizada(
            rotulo: 'Reverso',
            chave: 'tempo-reverso',
            filho: AureaToggle(
              valor: video.reverse,
              aoMudar: (v) {
                umPasso(ref, () => c.setClipReverse(id, v));
                // O cache de GOP curto so acelera os seeks seguintes; o
                // reverso ja toca da fonte na hora.
                if (v) {
                  unawaited(
                    ProxyService.instance.ensureProxy(
                      video.sourcePath,
                      force: true,
                    ),
                  );
                }
              },
            ),
          ),
          AureaPropertyRow.personalizada(
            rotulo: 'Blur de velocidade',
            chave: 'tempo-blur',
            filho: AureaToggle(
              valor: video.speedBlur,
              aoMudar: (v) => umPasso(ref, () => c.setClipSpeedBlur(id, v)),
            ),
          ),
          // INTERPOLACAO AQUI, e nao so no Time Remap: uma camera lenta de
          // velocidade CONSTANTE (0,25x sem curva) tambem escolhe como os
          // quadros do meio nascem.
          AureaPropertyRow.personalizada(
            rotulo: 'Interpolação',
            chave: 'tempo-interpolacao',
            filho: AureaDropdown<InterpolacaoDeQuadros>(
              valor: video.interpolacao,
              opcoes: InterpolacaoDeQuadros.values,
              rotuloDe: rotuloDaInterpolacao,
              titulo: 'Interpolação de quadros',
              aoMudar: (i) => umPasso(ref, () => c.setClipInterpolacao(id, i)),
            ),
          ),
          if (seloDaInterpolacao(video.interpolacao) case final selo?)
            AureaAvisoDoPainel(texto: selo),
          AureaSection(
            titulo: 'Congelar quadro',
            chave: 'tempo-congelar',
            inicialmenteAberta: false,
            filhos: [
              AureaPropertyRow(
                rotulo: 'Duração',
                chave: 'tempo-congelar-duracao',
                valor: _segundosCongelados,
                min: .1,
                max: 10,
                casas: 1,
                unidade: 's',
                aoMudar: (v) => setState(() => _segundosCongelados = v),
              ),
              AureaPropertyRow.personalizada(
                rotulo: 'Onde',
                chave: 'tempo-congelar-onde',
                filho: AureaDropdown<FreezePlacement>(
                  valor: _ondeCongelar,
                  opcoes: const [
                    FreezePlacement.separateClip,
                    FreezePlacement.insideClip,
                  ],
                  rotuloDe: (p) => switch (p) {
                    FreezePlacement.separateClip => 'Clipe separado',
                    FreezePlacement.insideClip => 'Dentro do clipe',
                  },
                  aoMudar: (p) => setState(() => _ondeCongelar = p),
                ),
              ),
              FileiraDeAcoes(
                acoes: [
                  AureaChip(
                    key: const ValueKey('tempo-congelar-aqui'),
                    rotulo: 'Congelar aqui',
                    icone: CupertinoIcons.snow,
                    aoTocar: congelar,
                  ),
                ],
              ),
            ],
          ),
          AureaSection(
            titulo: 'Efeitos de tempo',
            chave: 'tempo-efeitos',
            filhos: [
              FileiraDeAcoes(
                acoes: [
                  for (final tipo in efeitosDeTempo)
                    if (effectSpecs[tipo] case final spec?)
                      AureaChip(
                        key: ValueKey('tempo-efeito-${spec.id}'),
                        rotulo: spec.name,
                        icone: camada.effects.any((e) => e.type == tipo)
                            ? CupertinoIcons.checkmark_alt
                            : CupertinoIcons.plus,
                        ativo: camada.effects.any((e) => e.type == tipo),
                        aoTocar: () => efeitoDeTempo(tipo),
                      ),
                ],
              ),
            ],
          ),
        ],
      ],
    );
  }
}

/// A VELOCIDADE CONSTANTE do clipe (video ou audio): o numero, os valores
/// de um toque, o que acontece com a barra (compensacao), as rampas
/// prontas (video) e manter o tom. Mora aqui e serve aos paineis Tempo e
/// Velocidade — a mesma conta nos dois.
class SecaoDaVelocidade extends ConsumerStatefulWidget {
  const SecaoDaVelocidade({super.key, required this.layerId});

  final String layerId;

  @override
  ConsumerState<SecaoDaVelocidade> createState() => _SecaoDaVelocidadeState();
}

class _SecaoDaVelocidadeState extends ConsumerState<SecaoDaVelocidade> {
  late CompensacaoDaVelocidade _modo = compensacaoLembrada(ref);

  void _lembrar(CompensacaoDaVelocidade m) {
    setState(() => _modo = m);
    try {
      ref.read(sharedPreferencesProvider).setInt(_chaveDaCompensacao, m.index);
    } catch (_) {}
  }

  @override
  Widget build(BuildContext context) {
    final camada = camadaVisivel(ref, widget.layerId);
    if (camada == null) return const SizedBox.shrink();
    final c = ref.read(editorControllerProvider.notifier);
    final id = widget.layerId;
    final video = camada is VideoLayer ? camada : null;
    final velocidade = c.clipSpeedOf(id);
    final temCurva = video != null && hasTimeRemap(video);
    final som = switch (camada) {
      VideoLayer v => v.audio,
      AudioLayer a => a.audio,
      _ => null,
    };
    final temSom =
        camada is AudioLayer || (camada is VideoLayer && camada.volume > .001);
    void mudar(double v) => c.setClipSpeed(id, v, modo: _modo);

    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      mainAxisSize: MainAxisSize.min,
      children: [
        AureaPropertyRow(
          rotulo: 'Velocidade',
          chave: 'velocidade',
          valor: velocidade,
          min: velocidadeMinima,
          max: velocidadeMaxima,
          // Um centesimo por pixel: a faixa inteira (0,1x a 10x) numa
          // largura de celular pularia 0,07x por pixel e ninguem acertaria
          // 0,5x arrastando. Os valores redondos estao nas pilulas.
          sensibilidade: .01,
          casas: 2,
          unidade: '×',
          aoMudar: aCadaPasso(mudar),
          aoResetar: () => umPasso(ref, () => mudar(1)),
          aoComecarGesto: c.beginGesture,
          aoTerminarGesto: c.endGesture,
        ),
        if (temCurva)
          const AureaAvisoDoPainel(
            texto:
                'Este clipe tem Time Remap: mudar a velocidade constante '
                'desfaz a curva.',
          ),
        FileiraDeAcoes(
          acoes: [
            for (final v in const [0.25, 0.5, 1.0, 2.0, 3.0])
              AureaChip(
                key: ValueKey('velocidade-$v'),
                rotulo: '${v == v.roundToDouble() ? v.round() : v}×',
                traduzir: false,
                ativo: !temCurva && (velocidade - v).abs() < .01,
                aoTocar: () => umPasso(ref, () => mudar(v)),
              ),
          ],
        ),
        AureaPropertyRow.personalizada(
          rotulo: 'Ao mudar',
          chave: 'velocidade-compensacao',
          filho: AureaDropdown<CompensacaoDaVelocidade>(
            valor: _modo,
            opcoes: CompensacaoDaVelocidade.values,
            rotuloDe: rotuloDaCompensacao,
            titulo: 'O que acontece com a barra',
            aoMudar: _lembrar,
          ),
        ),
        if (video != null)
          AureaPropertyRow.personalizada(
            rotulo: 'Rampa',
            chave: 'velocidade-rampa',
            filho: AureaDropdown<SpeedRampPreset?>(
              valor: null,
              opcoes: SpeedRampPreset.values,
              rotuloDe: (p) => p?.label ?? 'Escolher rampa',
              titulo: 'Rampa de velocidade',
              aoMudar: (p) {
                if (p != null) umPasso(ref, () => c.applySpeedRamp(id, p));
              },
            ),
          ),
        if (temSom && som != null)
          AureaPropertyRow.personalizada(
            rotulo: 'Manter tom',
            chave: 'velocidade-tom',
            filho: AureaToggle(
              valor: som.preservePitch,
              aoMudar: (v) => umPasso(ref, () => c.setClipPreservePitch(id, v)),
            ),
          ),
      ],
    );
  }
}

// ------------------------------------------------------------------------
// OS NOMES DA INTERPOLACAO DE QUADROS (vieram do estudio do tempo antigo).

/// O NOME DE CADA MODO DE INTERPOLACAO NA TELA, no vocabulario que o dono
/// pediu (Nenhuma / Mistura / Fluxo optico / Fluxo optico (IA)). O enum
/// nao muda — ele e gravado por `name` no projeto —, so o rotulo. Mora
/// aqui porque as tres superficies do tempo (esta, a folha Tempo e o
/// cartao do painel de efeitos) precisam dizer a mesma palavra.
String rotuloDaInterpolacao(InterpolacaoDeQuadros modo) => switch (modo) {
  InterpolacaoDeQuadros.nenhuma => 'Nenhuma',
  InterpolacaoDeQuadros.mesclar => 'Mistura',
  InterpolacaoDeQuadros.movimento => 'Fluxo óptico',
  InterpolacaoDeQuadros.ia => 'Fluxo óptico (IA)',
};

/// O SELO DA PREVIA, dito na tela para ninguem procurar no palco um
/// resultado que so sai no arquivo. Nulo = nada a avisar.
String? seloDaInterpolacao(InterpolacaoDeQuadros modo) {
  const previa = kPreviaMisturaQuadros
      ? 'prévia: mistura'
      : 'prévia: quadro mais próximo';
  return switch (modo) {
    InterpolacaoDeQuadros.nenhuma => null,
    InterpolacaoDeQuadros.mesclar =>
      kPreviaMisturaQuadros ? null : '$previa · exportação: mistura',
    InterpolacaoDeQuadros.movimento ||
    InterpolacaoDeQuadros.ia => '$previa · exportação: fluxo óptico',
  };
}

// ------------------------------------------------------------------------
// A COMPENSACAO LEMBRADA (veio da folha de velocidade antiga).

/// O modo de compensacao da ultima vez (Estender fim na primeira).
CompensacaoDaVelocidade compensacaoLembrada(WidgetRef ref) {
  try {
    final i = ref.read(sharedPreferencesProvider).getInt(_chaveDaCompensacao);
    if (i != null && i >= 0 && i < CompensacaoDaVelocidade.values.length) {
      return CompensacaoDaVelocidade.values[i];
    }
  } catch (_) {}
  return CompensacaoDaVelocidade.estenderFim;
}

// ------------------------------------------------------------------------
// A PREVIA DA INTERPOLACAO.

/// A PREVIA JA MISTURA OS QUADROS VIZINHOS? Hoje nao: o palco mostra o
/// quadro mais proximo, e mistura/fluxo optico so existem na exportacao.
/// Quando a previa reduzida (dois quadros do cache + opacidade pela
/// fracao) entrar no palco, esta constante vira `true` e o selo passa a
/// dizer "prévia: mistura" — sem tocar em nenhuma das tres superficies.
const bool kPreviaMisturaQuadros = false;
