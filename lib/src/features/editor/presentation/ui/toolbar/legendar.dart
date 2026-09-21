import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../settings/application/settings_controller.dart';
import '../../../application/editor_controller.dart';
import '../../../application/transcricao_em_andamento.dart';
import '../../../application/transcription_service.dart';
import '../../../domain/caption.dart';
import '../paineis/comum_de_objetos.dart' show FileiraDePilulas;
import '../paineis/pecas_centrais.dart' show FileiraDeAcoes;

// ===========================================================================
// CRIAR LEGENDAS
// ===========================================================================
//
// A LOGICA veio intacta da folha antiga (`widgets/add_layer_sheet.dart`,
// `showCaptionCreationSheet`): transcrever a primeira midia com som — na
// nuvem (Groq Whisper, pelo servidor do Aurea) ou no aparelho
// (whisper.cpp) — ou criar do SRT colado. So a aparencia e do DS.
//
// A camada nasce num passo so (`addCaptionLayer` e um `_push`), como antes.

/// Legendas: transcricao automatica — na nuvem ou no aparelho — ou SRT
/// colado.
///
/// A transcricao nao trava o editor: roda num [TranscricaoEmAndamento]
/// que sobrevive ao fechamento desta folha. Quem fecha a folha no meio
/// ve a camada de legenda aparecer na timeline quando ficar pronta; quem
/// reabre ve o andamento (ou o erro) de onde parou.
///
/// O audio so sai do aparelho quando a pessoa toca em Transcrever com a
/// nuvem escolhida — nunca sozinho.
Future<void> showCaptionCreationSheet(
  BuildContext context,
  WidgetRef ref,
) async {
  // O controlador e lido AGORA: o `aoTerminar` da transcricao pode rodar
  // depois de a folha fechar, e so precisa dele (nao do `ref`).
  final controller = ref.read(editorControllerProvider.notifier);
  final textController = TextEditingController();
  final job = TranscricaoEmAndamento.instance;
  // Uma transcricao pronta de antes ja virou camada: comeca limpo.
  if (job.estado.value is TranscricaoPronta) job.limpar();
  var mode = CaptionMode.frases;
  var modo = ref.read(settingsControllerProvider).modoDeTranscricao;

  Future<void> transcrever(
    BuildContext folha, {
    ModoDeTranscricao? forcar,
  }) async {
    final media = controller.firstTranscribableMediaPath();
    if (media == null) {
      job.falhar('Adicione um vídeo ao projeto primeiro.');
      return;
    }
    final entrou = await job.rodar(
      () => ref
          .read(transcriptionServiceProvider)
          .transcribeMedia(
            media,
            mode: mode,
            modo: forcar ?? modo,
            onStatus: job.status,
          ),
      aoTerminar: (falas) => controller.addCaptionLayer(falas) == 0
          ? 'Nenhuma fala detectada no áudio.'
          : null,
    );
    // Deu certo e a folha ainda esta aberta: fecha. Se a pessoa a fechou
    // no meio, a camada ja esta na timeline.
    if (entrou && folha.mounted) Navigator.of(folha).pop();
  }

  await mostrarAureaFolha<void>(
    context,
    titulo: 'Legendas',
    grande: true,
    construtor: (folha) => ValueListenableBuilder<EstadoDaTranscricao>(
      valueListenable: job.estado,
      builder: (folha, estado, _) => StatefulBuilder(
        builder: (folha, setFolha) {
          final busy = estado is TranscricaoRodando;
          final falha = estado is TranscricaoFalhou ? estado : null;
          return SingleChildScrollView(
            // O TECLADO DO SRT sobe por cima da folha: o respiro de baixo
            // cresce com ele, senao o botao de criar fica escondido.
            padding: EdgeInsets.fromLTRB(
              AureaDims.margemDoPainel,
              AureaDims.e4,
              AureaDims.margemDoPainel,
              AureaDims.topoDoPainel + MediaQuery.viewInsetsOf(folha).bottom,
            ),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                // COMO O TEXTO E SEGMENTADO NO TEMPO. Durante a
                // transcricao as escolhas ficam apagadas e sem toque: mudar
                // no meio nao valeria para o trabalho que ja esta rodando.
                _Travado(
                  travado: busy,
                  child: FileiraDePilulas<CaptionMode>(
                    opcoes: CaptionMode.values,
                    atual: mode,
                    rotuloDe: captionModeLabel,
                    chave: 'legenda-divisao',
                    chaveDe: (m) => m.name,
                    aoEscolher: (m) => setFolha(() => mode = m),
                  ),
                ),
                // ONDE TRANSCREVER. A escolha fica nos Ajustes tambem;
                // aqui e onde ela importa — e grava la.
                _Travado(
                  travado: busy,
                  child: FileiraDePilulas<ModoDeTranscricao>(
                    opcoes: ModoDeTranscricao.values,
                    atual: modo,
                    rotuloDe: (m) => m.emPalavras,
                    chave: 'transcricao-modo',
                    chaveDe: (m) => m.name,
                    aoEscolher: (m) {
                      setFolha(() => modo = m);
                      ref
                          .read(settingsControllerProvider.notifier)
                          .setModoDeTranscricao(m);
                    },
                  ),
                ),
                const SizedBox(height: AureaDims.e2),
                AppText(modo.explicacao, style: AureaEstilos.propriedade),
                const SizedBox(height: AureaDims.e10),
                _BotaoCheio(
                  chave: 'transcrever',
                  principal: true,
                  onPressed: busy ? null : () => transcrever(folha),
                  child: Row(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      if (busy)
                        Padding(
                          padding: const EdgeInsets.only(
                            right: AureaDims.e10,
                          ),
                          child: CupertinoActivityIndicator(
                            color: AureaCores.sobreAcao,
                          ),
                        )
                      else
                        Padding(
                          padding: const EdgeInsets.only(right: AureaDims.e8),
                          child: Icon(
                            CupertinoIcons.waveform,
                            size: AureaDims.iconeSm + 2,
                            color: AureaCores.sobreAcao,
                          ),
                        ),
                      AppText(
                        busy ? 'Transcrevendo...' : 'Transcrever',
                        style: AureaEstilos.corpo.copyWith(
                          fontSize: 14,
                          fontWeight: FontWeight.w700,
                          color: AureaCores.sobreAcao,
                        ),
                      ),
                    ],
                  ),
                ),
                if (estado is TranscricaoRodando) ...[
                  Padding(
                    padding: const EdgeInsets.only(top: AureaDims.e8),
                    child: AppText(
                      estado.status,
                      style: AureaEstilos.propriedade,
                    ),
                  ),
                  // NAO TRAVA O EDITOR: da para fechar e continuar
                  // mexendo; a legenda entra na timeline quando ficar
                  // pronta.
                  Align(
                    alignment: Alignment.centerLeft,
                    child: CupertinoButton(
                      key: const ValueKey('segundo-plano'),
                      padding: EdgeInsets.zero,
                      onPressed: () => Navigator.of(folha).pop(),
                      child: AppText(
                        'Continuar em segundo plano',
                        style: AureaEstilos.corpo.copyWith(
                          fontSize: 12,
                          color: AureaCores.destaque,
                        ),
                      ),
                    ),
                  ),
                ],
                if (falha != null) ...[
                  Padding(
                    padding: const EdgeInsets.only(top: AureaDims.e8),
                    child: AppText(
                      falha.mensagem,
                      style: AureaEstilos.corpo.copyWith(
                        fontSize: 12,
                        color: AureaCores.perigo,
                      ),
                    ),
                  ),
                  // A SAIDA DO ERRO: tentar de novo quando a rede nao e o
                  // problema; o Whisper do aparelho quando a nuvem nao da.
                  FileiraDeAcoes(
                    acoes: [
                      if (!falha.semInternet)
                        AureaChip(
                          key: const ValueKey('tentar-de-novo'),
                          rotulo: 'Tentar de novo',
                          aoTocar: () => transcrever(folha),
                        ),
                      if (falha.ofereceLocal)
                        AureaChip(
                          key: const ValueKey('usar-local'),
                          rotulo: 'Usar o Whisper do aparelho',
                          ativo: true,
                          aoTocar: () => transcrever(
                            folha,
                            forcar: ModoDeTranscricao.local,
                          ),
                        ),
                    ],
                  ),
                ],
                if (estado is TranscricaoPronta)
                  Padding(
                    padding: const EdgeInsets.only(top: AureaDims.e8),
                    child: AppTextMoldado(
                      'Legendas prontas: {0} falas.',
                      [estado.falas],
                      style: AureaEstilos.corpo.copyWith(
                        fontSize: 12,
                        color: AureaCores.destaque,
                      ),
                    ),
                  ),
                const SizedBox(height: AureaDims.e15),
                AppText('ou cole um SRT:', style: AureaEstilos.propriedade),
                const SizedBox(height: AureaDims.e8),
                CupertinoTextField(
                  key: const ValueKey('legenda-srt-campo'),
                  controller: textController,
                  maxLines: 6,
                  minLines: 3,
                  enabled: !busy,
                  placeholder: translate(
                    folha,
                    '1\n00:00:00,000 --> 00:00:02,000\nSua primeira fala...',
                  ),
                  style: AureaEstilos.corpo,
                  placeholderStyle: AureaEstilos.corpo.copyWith(
                    color: AureaCores.textoSecundario,
                  ),
                  cursorColor: AureaCores.destaque,
                  padding: const EdgeInsets.all(AureaDims.e10 + 2),
                  decoration: BoxDecoration(
                    color: AureaCores.campo,
                    borderRadius: BorderRadius.circular(AureaDims.raioXl),
                  ),
                ),
                const SizedBox(height: AureaDims.e10),
                _BotaoCheio(
                  chave: 'legenda-srt-criar',
                  onPressed: busy
                      ? null
                      : () {
                          controller.addCaptionLayerFromSrt(
                            textController.text,
                          );
                          Navigator.of(folha).pop();
                        },
                  child: AppText(
                    'Criar do SRT colado',
                    style: AureaEstilos.corpo.copyWith(
                      fontSize: 14,
                      fontWeight: FontWeight.w700,
                    ),
                  ),
                ),
              ],
            ),
          );
        },
      ),
    ),
  );
  textController.dispose();
}

/// ESCOLHA QUE NAO VALE AGORA: apagada e sem toque, mas a vista — sumir
/// com ela faria a folha pular de altura no meio da transcricao.
class _Travado extends StatelessWidget {
  const _Travado({required this.travado, required this.child});

  final bool travado;
  final Widget child;

  @override
  Widget build(BuildContext context) => IgnorePointer(
    ignoring: travado,
    child: Opacity(opacity: travado ? .4 : 1, child: child),
  );
}

/// O BOTAO DE LARGURA INTEIRA da folha: preenchido no tom de acao
/// ([principal]) ou no tom de campo. Sem borda, sem ripple.
class _BotaoCheio extends StatelessWidget {
  const _BotaoCheio({
    required this.chave,
    required this.onPressed,
    required this.child,
    this.principal = false,
  });

  final String chave;
  final VoidCallback? onPressed;
  final Widget child;
  final bool principal;

  @override
  Widget build(BuildContext context) => CupertinoButton(
    key: ValueKey(chave),
    color: principal ? AureaCores.acao : AureaCores.campo,
    disabledColor: AureaCores.campoAlto,
    borderRadius: BorderRadius.circular(AureaDims.raioXl),
    onPressed: onPressed,
    child: child,
  );
}
