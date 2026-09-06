import 'dart:async';

import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../editor/application/editor_controller.dart';
import '../../editor/application/media_preview_service.dart';
import '../../editor/application/transcription_service.dart';
import '../../editor/domain/audio_ops.dart';
import '../../editor/domain/caption.dart';
import '../../editor/domain/layer.dart';
import '../domain/autoedit_plan.dart';
import '../domain/autoedit_style.dart';

/// OS PASSOS QUE A TELA MOSTRA.
///
/// A tela do trabalho e honesta: diz o que esta fazendo agora e o que ja
/// terminou, com o numero de cada coisa. "Processando..." sem dizer o que
/// e o tipo de tela que faz o vídeo de dez minutos parecer travado.
enum AutoEditStepId { transcrever, cortar, legendar, zoom, audio }

extension AutoEditStepIdX on AutoEditStepId {
  String get rotulo => switch (this) {
        AutoEditStepId.transcrever => 'Transcrevendo',
        AutoEditStepId.cortar => 'Cortando silencios',
        AutoEditStepId.legendar => 'Legendando',
        AutoEditStepId.zoom => 'Aplicando zooms',
        AutoEditStepId.audio => 'Tratando audio',
      };
}

enum AutoEditStepState { esperando, correndo, feito, pulado }

class AutoEditStep {
  const AutoEditStep(this.id, this.estado, [this.detalhe]);

  final AutoEditStepId id;
  final AutoEditStepState estado;

  /// O numero ao lado do passo: "0:42", "-18 s", "47 falas".
  final String? detalhe;

  AutoEditStep com(AutoEditStepState e, [String? d]) =>
      AutoEditStep(id, e, d ?? detalhe);
}

/// O que a tela do trabalho le a cada mudanca.
class AutoEditRun {
  const AutoEditRun({
    required this.passos,
    this.plano,
    this.erro,
    this.cancelado = false,
  });

  final List<AutoEditStep> passos;

  /// So existe depois da analise. E o que a tela de ajuste manipula.
  final AutoEditPlan? plano;
  final String? erro;
  final bool cancelado;

  bool get terminou =>
      erro != null ||
      cancelado ||
      passos.every((p) =>
          p.estado == AutoEditStepState.feito ||
          p.estado == AutoEditStepState.pulado);
}

/// O AUTOEDIT.
///
/// Nao e motor novo: e uma receita que aciona o que ja existe —
/// transcricao do nivel 7, detector de silencio e audio do nivel 11,
/// keyframes do nucleo — e grava o resultado como PROJETO COMUM. Se o
/// resultado nao abrisse no editor com camadas de verdade, seria caixa
/// preta, e a pessoa nunca aprenderia o editor usando o AutoEdit.
///
/// A analise (lenta, assincrona) e separada da aplicacao (rapida,
/// sincrona) de proposito: a tela de ajuste refaz o plano a cada toque no
/// Ritmo, e reprocessar o video a cada toque seria insuportavel.
class AutoEditRunner {
  AutoEditRunner(this._ref);

  final Ref _ref;
  bool _cancelado = false;

  void cancelar() => _cancelado = true;

  EditorController get _controller =>
      _ref.read(editorControllerProvider.notifier);

  /// FASE 1 — ANALISAR. Decodifica, transcreve e mede. Nada e alterado no
  /// projeto aqui: cancelar a qualquer momento nao deixa nada pela metade.
  Future<AutoEditRun> analisar({
    required String videoPath,
    required AutoEditStyle estilo,
    required void Function(AutoEditRun) onProgresso,
  }) async {
    _cancelado = false;
    var passos = [
      for (final id in AutoEditStepId.values)
        AutoEditStep(id, AutoEditStepState.esperando),
    ];

    void publica() => onProgresso(AutoEditRun(passos: passos, plano: null));

    AutoEditRun cancelado() =>
        AutoEditRun(passos: passos, cancelado: true);

    void marca(AutoEditStepId id, AutoEditStepState e, [String? d]) {
      passos = [
        for (final p in passos)
          if (p.id == id) p.com(e, d) else p,
      ];
      publica();
    }

    // ------------------------------------------------ transcrever
    marca(AutoEditStepId.transcrever, AutoEditStepState.correndo);
    var falas = <Cue>[];
    if (estilo.legendar || estilo.zoom != AutoEditZoom.nenhum) {
      try {
        falas = await _ref.read(transcriptionServiceProvider).transcribeMedia(
              videoPath,
              mode: estilo.captionMode,
            );
      } catch (e) {
        return AutoEditRun(passos: passos, erro: 'Nao consegui transcrever: $e');
      }
      if (_cancelado) return cancelado();
    }
    marca(
      AutoEditStepId.transcrever,
      falas.isEmpty ? AutoEditStepState.pulado : AutoEditStepState.feito,
      falas.isEmpty ? 'sem fala' : '${falas.length} falas',
    );

    // --------------------------------------------------- silencios
    marca(AutoEditStepId.cortar, AutoEditStepState.correndo);
    await MediaPreviewService.instance.ensureWaveform(videoPath);
    if (_cancelado) return cancelado();
    final picos = MediaPreviewService.instance.peaksOf(videoPath);
    final silencios = picos == null || picos.isEmpty
        ? const <(Duration, Duration)>[]
        : detectSilence(picos);

    final plano = planejar(
      estilo: estilo,
      silencios: silencios,
      falas: falas,
    );
    marca(
      AutoEditStepId.cortar,
      plano.cortes.isEmpty ? AutoEditStepState.pulado : AutoEditStepState.feito,
      plano.cortes.isEmpty
          ? 'nada a cortar'
          : '-${plano.economia.inSeconds} s',
    );
    marca(
      AutoEditStepId.legendar,
      plano.falas.isEmpty ? AutoEditStepState.pulado : AutoEditStepState.feito,
      plano.falas.isEmpty ? 'sem fala' : '${plano.falas.length} falas',
    );
    marca(
      AutoEditStepId.zoom,
      plano.zoom.isEmpty ? AutoEditStepState.pulado : AutoEditStepState.feito,
      plano.zoom.isEmpty ? 'sem zoom' : '${plano.zoom.length ~/ 2} zooms',
    );
    marca(
      AutoEditStepId.audio,
      estilo.normalizar || estilo.melhorarVoz
          ? AutoEditStepState.feito
          : AutoEditStepState.pulado,
    );

    return AutoEditRun(passos: passos, plano: plano);
  }

  /// FASE 2 — APLICAR. Rapido, sincrono, e cada passo e UM undo.
  ///
  /// A camada de video ja tem de estar no projeto: quem importa e o
  /// controller de sempre, pelo caminho de sempre.
  void aplicar(AutoEditPlan plano, String layerId) {
    final controller = _controller;

    // Os cortes primeiro: mexem no tempo, e tudo o que vem depois se
    // apoia no tempo ja encurtado.
    if (plano.cortes.isNotEmpty) {
      controller.runAsOneUndo(
        () => controller.cutRangesOf(layerId, plano.cortes),
      );
    }

    if (plano.falas.isNotEmpty) {
      controller.runAsOneUndo(() => controller.addCaptionLayer(plano.falas));
    }

    if (plano.zoom.isNotEmpty) {
      controller.runAsOneUndo(
        () => controller.setScaleKeyframes(layerId, plano.zoom),
      );
    }

    final estilo = plano.estilo;
    if (estilo.normalizar || estilo.melhorarVoz || estilo.ducking) {
      controller.runAsOneUndo(() {
        if (estilo.normalizar) controller.normalizeAudio(layerId);
        if (estilo.melhorarVoz) {
          controller.updateAudioSpec(
            layerId,
            (a) => a.copyWith(
              processing: a.processing.copyWith(voice: 1),
            ),
          );
        }
        // O ducking so tem sentido com outra trilha: a musica desce
        // quando a voz entra. Sem musica no projeto, nao ha o que baixar.
        if (estilo.ducking) {
          final projeto = _ref.read(editorControllerProvider);
          for (final l in projeto.layers) {
            if (l is AudioLayer && l.id != layerId) {
              controller.updateAudioSpec(
                l.id,
                (a) => a.copyWith(duckAgainstId: layerId),
              );
            }
          }
        }
      });
    }
  }
}

final autoEditRunnerProvider = Provider<AutoEditRunner>(AutoEditRunner.new);
