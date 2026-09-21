import 'package:flutter/cupertino.dart';
import 'package:flutter/services.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../../core/ds/ds.dart';
import '../../../../../core/l10n/app_language.dart';
import '../../../../../core/ui/snack.dart';
import '../../../application/editor_controller.dart';
import '../paineis/comum_de_objetos.dart' show FileiraDePilulas;
import '../paineis/pecas_centrais.dart' show respiroDoPainel;

/// DECUPAR — o clipe inteiro cortado onde a cena muda, sozinho.
///
/// O detector e o mesmo scdet que o Premiere usa por baixo do "Scene Edit
/// Detection" (FFmpeg, numa versao encolhida do video). A folha oferece a
/// SENSIBILIDADE em palavras, e duas saidas: cortar de verdade (um
/// desfazer so — o controlador ja agrupa os cortes) ou virar marcas na
/// regua para revisar antes.
Future<void> showDecuparSheet(
  BuildContext context,
  WidgetRef ref,
  String layerId,
) => mostrarAureaFolha<void>(
  context,
  titulo: 'Decupar',
  construtor: (folha) => _Decupar(ref: ref, layerId: layerId),
);

/// A sensibilidade em palavras, e o limiar do scdet que cada uma usa.
enum _Sensibilidade {
  sensivel,
  normal,
  secos;

  String get emPalavras => switch (this) {
    _Sensibilidade.sensivel => 'Sensível',
    _Sensibilidade.normal => 'Normal',
    _Sensibilidade.secos => 'Só cortes secos',
  };

  String get explicacao => switch (this) {
    _Sensibilidade.sensivel =>
      'Pega até transições suaves. Pode cortar demais.',
    _Sensibilidade.normal => 'O padrão: cortes de câmera comuns.',
    _Sensibilidade.secos => 'Só mudanças bruscas de cena.',
  };

  double get limiar => switch (this) {
    _Sensibilidade.sensivel => 0.22,
    _Sensibilidade.normal => 0.35,
    _Sensibilidade.secos => 0.55,
  };
}

class _Decupar extends StatefulWidget {
  const _Decupar({required this.ref, required this.layerId});

  final WidgetRef ref;
  final String layerId;

  @override
  State<_Decupar> createState() => _DecuparState();
}

class _DecuparState extends State<_Decupar> {
  _Sensibilidade _sens = _Sensibilidade.normal;
  bool _rodando = false;

  EditorController get _c => widget.ref.read(editorControllerProvider.notifier);

  Future<void> _rodar({required bool cortar}) async {
    setState(() => _rodando = true);
    final n = cortar
        ? await _c.decuparCamada(widget.layerId, sensibilidade: _sens.limiar)
        : await _c.cortesDeCenaViramMarcas(
            widget.layerId,
            sensibilidade: _sens.limiar,
          );
    // Fechada no meio da leitura: a pessoa desistiu, nada a dizer.
    if (!mounted) return;
    setState(() => _rodando = false);
    if (n == null) {
      AureaSnack.show(context, 'Não consegui ler esse vídeo para decupar.');
      return;
    }
    // O aviso sai ANTES de fechar: depois do pop este contexto esta de
    // saida. O mensageiro e o mesmo do editor.
    AureaSnack.show(
      context,
      n == 0
          ? 'Nenhuma mudança de cena nesse trecho. Tente Sensível.'
          : cortar
          ? '$n corte${n == 1 ? '' : 's'} de cena feitos'
          : '$n marca${n == 1 ? '' : 's'} de cena na régua',
      // Marca na regua nao mexe no projeto: nao ha o que desfazer.
      actionLabel: n == 0 || !cortar ? null : 'Desfazer',
      onAction: _c.undo,
      duration: const Duration(seconds: 5),
    );
    Navigator.of(context).maybePop();
  }

  @override
  Widget build(BuildContext context) => SingleChildScrollView(
    padding: respiroDoPainel,
    child: Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        const AureaAvisoDoPainel(
          texto:
              'Acha onde a cena muda e corta o clipe em todos os pontos — '
              'o mesmo detector do Scene Edit Detection.',
        ),
        AureaPropertyRow.personalizada(
          rotulo: 'Sensibilidade',
          chave: 'decupar-sensibilidade',
          // As chaves das pilulas sao `decupar-<nome>`, as mesmas da
          // folha antiga.
          filho: FileiraDePilulas<_Sensibilidade>(
            chave: 'decupar',
            chaveDe: (s) => s.name,
            opcoes: _Sensibilidade.values,
            atual: _sens,
            rotuloDe: (s) => s.emPalavras,
            aoEscolher: (s) => setState(() => _sens = s),
          ),
        ),
        AureaAvisoDoPainel(texto: _sens.explicacao),
        const SizedBox(height: AureaDims.e8),
        if (_rodando)
          // LENDO: o lugar dos botoes vira o aviso, na mesma altura — a
          // folha nao pula e nao da para disparar duas leituras.
          Container(
            key: const ValueKey('decupar-rodando'),
            constraints: const BoxConstraints(minHeight: 44),
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: AureaCores.campo,
              borderRadius: BorderRadius.circular(AureaDims.raioXl),
            ),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const CupertinoActivityIndicator(radius: 8),
                const SizedBox(width: AureaDims.e8),
                AppText('Lendo o vídeo...', style: AureaEstilos.corpo),
              ],
            ),
          )
        else ...[
          CupertinoButton(
            key: const ValueKey('decupar-cortar'),
            color: AureaCores.acao,
            padding: const EdgeInsets.symmetric(vertical: 12),
            onPressed: () {
              HapticFeedback.lightImpact();
              _rodar(cortar: true);
            },
            child: AppText(
              'Decupar agora',
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w700,
                color: AureaCores.sobreAcao,
              ),
            ),
          ),
          const SizedBox(height: AureaDims.e8),
          CupertinoButton(
            key: const ValueKey('decupar-marcas'),
            color: AureaCores.campo,
            padding: const EdgeInsets.symmetric(vertical: 12),
            onPressed: () => _rodar(cortar: false),
            child: AppText(
              'Só marcar na régua (revisar antes)',
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w600,
                color: AureaCores.texto,
              ),
            ),
          ),
        ],
      ],
    ),
  );
}
