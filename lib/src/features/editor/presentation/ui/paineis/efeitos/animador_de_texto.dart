import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../../core/ds/ds.dart';
import '../../../../application/editor_controller.dart';
import '../../../../application/playback_controller.dart';
import '../../../../domain/animador_de_texto.dart';
import '../../../../domain/keyframe.dart';
import '../../../../domain/layer.dart';
import '../../../../domain/text_animator.dart';
import '../pecas_centrais.dart';

/// O ANIMADOR DE TEXTO COMO UM EFEITO DA PILHA.
///
/// Ele nao mora em `layer.effects` (o motor dele anima por unidade de
/// texto — letra, palavra, linha —, nao e um passe de pixel), mas para
/// quem usa e a mesma coisa: um [AureaEffectCard] com olho, menu e
/// [AureaPropertyRow]s, o MESMO losango ([KeyframeState]) em toda linha
/// animavel e o Range Selector (Start, End, Offset, Unidade, Forma,
/// Ease High/Low) — o seletor de faixa do After Effects.
///
/// Os numeros da faixa vivem em fracao (0..1) no motor e em porcentagem
/// na tela; a conversao acontece nas duas pontas, e so aqui.
class CartaoDoAnimadorDeTexto extends ConsumerWidget {
  const CartaoDoAnimadorDeTexto({
    super.key,
    required this.layerId,
    required this.animador,
    required this.camada,
    required this.t,
    required this.playback,
    required this.aberto,
    required this.aoMudarAberto,
  });

  final String layerId;
  final TextAnimator animador;

  /// A camada GRAVADA: e dela que o losango tira as marcas.
  final Layer camada;
  final Duration t;
  final PlaybackController playback;
  final bool aberto;
  final ValueChanged<bool> aoMudarAberto;

  String get _chave => 'animador-${animador.id}';

  Future<void> _menu(BuildContext botao, WidgetRef ref) async {
    final c = ref.read(editorControllerProvider.notifier);
    final escolha = await mostrarAureaMenu<String>(
      botao,
      titulo: nomeDoAnimadorDeTexto,
      itens: const [
        AureaMenuItem(
          valor: 'resetar',
          rotulo: 'Resetar',
          icone: CupertinoIcons.arrow_counterclockwise,
          chave: 'animador-resetar',
        ),
        AureaMenuItem(
          valor: 'apagar',
          rotulo: 'Apagar',
          icone: CupertinoIcons.trash,
          destrutivo: true,
          chave: 'animador-apagar',
        ),
      ],
    );
    switch (escolha) {
      case 'resetar':
        umPasso(
          ref,
          () => c.aplicarPresetNoAnimador(
            layerId,
            animador.id,
            presetsDoAnimador.firstWhere(
              (p) => p.id == presetPadraoDoAnimador,
            ),
          ),
        );
      case 'apagar':
        umPasso(ref, () => c.removeTextAnimator(layerId, animador.id));
    }
  }

  @override
  Widget build(BuildContext context, WidgetRef ref) {
    final faixa = faixaDoAnimador(animador);
    final c = ref.read(editorControllerProvider.notifier);
    return AureaEffectCard(
      chave: _chave,
      nome: nomeDoAnimadorDeTexto,
      ligado: animador.enabled,
      aberto: aberto,
      aoMudarAberto: aoMudarAberto,
      aoAlternarLigado: () =>
          umPasso(ref, () => c.toggleTextAnimator(layerId, animador.id)),
      aoMenu: (botao) => _menu(botao, ref),
      filhos: faixa == null
          ? const [AureaAvisoDoPainel(texto: 'Este animador não tem faixa.')]
          : _linhas(ref, faixa),
    );
  }

  List<Widget> _linhas(WidgetRef ref, RangeSelector faixa) {
    final c = ref.read(editorControllerProvider.notifier);
    final local = camada.localTime(t);
    final gravada = camada is TextLayer
        ? (camada as TextLayer).animators
              .where((a) => a.id == animador.id)
              .firstOrNull
        : null;
    final faixaGravada = gravada == null ? null : faixaDoAnimador(gravada);

    ({KeyframeState estado, VoidCallback? anterior, VoidCallback? proximo})
    losango(AnimatedDouble? trilha, VoidCallback alternar) => losangoNoInstante(
      marcasUs: marcasDe(trilha),
      agoraUs: local.inMicroseconds,
      inicio: camada.startTime,
      playback: playback,
      aoAlternar: alternar,
    );

    // UMA LINHA DO SELETOR (Start, End, Offset, Ease High, Ease Low).
    Widget linhaDaFaixa(
      String chave,
      String rotulo,
      AnimatedDouble trilhaVisivel,
      AnimatedDouble? trilhaGravada, {
      required double min,
      required double max,
      bool porcento = true,
    }) {
      final bruto = trilhaVisivel.valueAt(local);
      final kf = losango(
        trilhaGravada,
        () => c.toggleSelectorParamKeyframe(
          layerId,
          animador.id,
          faixa.id,
          chave,
          playback.time.value,
        ),
      );
      return AureaPropertyRow(
        rotulo: rotulo,
        chave: '$_chave-$chave',
        valor: porcento ? fracaoParaPorcento(bruto) : bruto,
        min: min,
        max: max,
        casas: 0,
        unidade: porcento ? '%' : '',
        keyframe: kf.estado,
        aoAnterior: kf.anterior,
        aoProximo: kf.proximo,
        aoComecarGesto: c.beginGesture,
        aoTerminarGesto: c.endGesture,
        aoMudar: aCadaPasso(
          (v) => c.editSelectorParam(
            layerId,
            animador.id,
            faixa.id,
            chave,
            playback.time.value,
            porcento ? porcentoParaFracao(v.clamp(min, max)) : v.clamp(min, max),
          ),
        ),
      );
    }

    // UMA PROPRIEDADE ANIMAVEL do animador (posicao, escala, giro...).
    // Aparece sempre, mesmo antes de existir: o valor mostrado e o
    // neutro, e a primeira mexida a cria.
    Widget linhaDaPropriedade(TextAnimProp tipo) {
      final trilha = propriedadeDoAnimador(animador, tipo)?.value;
      final trilhaGravada = gravada == null
          ? null
          : propriedadeDoAnimador(gravada, tipo)?.value;
      final (min, max) = faixaDaPropriedade(tipo);
      final kf = losango(
        trilhaGravada,
        () => c.toggleAnimadorPropKeyframe(
          layerId,
          animador.id,
          tipo,
          playback.time.value,
        ),
      );
      return AureaPropertyRow(
        rotulo: textAnimPropLabel(tipo),
        chave: '$_chave-prop-${tipo.name}',
        valor: trilha?.valueAt(local) ?? neutroDaPropriedade(tipo),
        min: min,
        max: max,
        casas: 0,
        unidade: unidadeDaPropriedade(tipo),
        keyframe: kf.estado,
        aoAnterior: kf.anterior,
        aoProximo: kf.proximo,
        aoResetar: () => umPasso(
          ref,
          () => c.editAnimadorProp(
            layerId,
            animador.id,
            tipo,
            playback.time.value,
            neutroDaPropriedade(tipo),
          ),
        ),
        aoComecarGesto: c.beginGesture,
        aoTerminarGesto: c.endGesture,
        aoMudar: aCadaPasso(
          (v) => c.editAnimadorProp(
            layerId,
            animador.id,
            tipo,
            playback.time.value,
            v.clamp(min, max),
          ),
        ),
      );
    }

    final indiceDoPreset = presetsDoAnimador.indexWhere(
      (p) => p.nome == animador.name,
    );
    return [
      AureaPropertyRow.personalizada(
        rotulo: 'Preset',
        chave: '$_chave-preset',
        filho: AureaDropdown<int>(
          valor: indiceDoPreset,
          opcoes: [for (var i = 0; i < presetsDoAnimador.length; i++) i],
          rotuloDe: (i) => i < 0 ? 'Personalizado' : presetsDoAnimador[i].nome,
          titulo: 'Preset',
          aoMudar: (i) => umPasso(
            ref,
            () => c.aplicarPresetNoAnimador(
              layerId,
              animador.id,
              presetsDoAnimador[i],
            ),
          ),
        ),
      ),
      AureaSection(
        titulo: 'Range Selector',
        chave: '$_chave-faixa',
        filhos: [
          linhaDaFaixa(
            'start',
            'Start',
            faixa.start,
            faixaGravada?.start,
            min: 0,
            max: 100,
          ),
          linhaDaFaixa(
            'end',
            'End',
            faixa.end,
            faixaGravada?.end,
            min: 0,
            max: 100,
          ),
          // O OFFSET E O QUE FAZ A ANIMACAO ANDAR: de -100 a 100 a janela
          // atravessa a frase inteira, unidade por unidade.
          linhaDaFaixa(
            'offset',
            'Offset',
            faixa.offset,
            faixaGravada?.offset,
            min: -100,
            max: 100,
          ),
          AureaPropertyRow.personalizada(
            rotulo: 'Unidade',
            chave: '$_chave-unidade',
            filho: AureaDropdown<SelectorBasedOn>(
              valor: faixa.basedOn,
              opcoes: unidadesDoAnimador,
              rotuloDe: rotuloDaUnidade,
              titulo: 'Unidade',
              aoMudar: (u) => umPasso(
                ref,
                () => c.setAnimadorUnidade(layerId, animador.id, u),
              ),
            ),
          ),
          AureaPropertyRow.personalizada(
            rotulo: 'Forma',
            chave: '$_chave-forma',
            filho: AureaDropdown<SelectorShape>(
              valor: faixa.shape,
              opcoes: formasDoAnimador,
              rotuloDe: rotuloDaForma,
              titulo: 'Forma',
              aoMudar: (s) => umPasso(
                ref,
                () => c.setRangeSelectorShape(
                  layerId,
                  animador.id,
                  faixa.id,
                  s,
                ),
              ),
            ),
          ),
        ],
      ),
      AureaSection(
        titulo: 'Easing',
        chave: '$_chave-easing',
        inicialmenteAberta: false,
        filhos: [
          linhaDaFaixa(
            'easeHigh',
            'Ease High',
            faixa.easeHigh,
            faixaGravada?.easeHigh,
            min: -100,
            max: 100,
            porcento: false,
          ),
          linhaDaFaixa(
            'easeLow',
            'Ease Low',
            faixa.easeLow,
            faixaGravada?.easeLow,
            min: -100,
            max: 100,
            porcento: false,
          ),
        ],
      ),
      AureaSection(
        titulo: 'Propriedades',
        chave: '$_chave-propriedades',
        inicialmenteAberta: false,
        filhos: [for (final p in propriedadesDoAnimador) linhaDaPropriedade(p)],
      ),
    ];
  }
}
