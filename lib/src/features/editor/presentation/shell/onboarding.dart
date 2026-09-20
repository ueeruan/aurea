import 'package:flutter/cupertino.dart';
import 'package:flutter_riverpod/flutter_riverpod.dart';

import '../../../../core/storage/prefs.dart';
import '../am/am_colors.dart';
import 'package:aurea/src/core/l10n/app_language.dart';

/// AS QUATRO DICAS DE PRIMEIRO USO (Fase 6): o que o editor precisa que a
/// pessoa saiba, e nada mais. Aparecem uma vez, no alto do preview, e
/// voltam por ⚙ Projeto › "Ver as dicas de novo".
const dicasDoEditor = [
  'Toque no objeto na tela para selecionar. Arraste para mover; a bolinha de baixo redimensiona e a de cima gira.',
  'Com o objeto selecionado, as ações e as categorias dele aparecem embaixo.',
  'Toque no + (na barra de transporte) para adicionar mídia, texto ou forma.',
  'O ◆ na barra de transporte crava um keyframe no instante do cabeçote.',
];

/// Lembrado por aparelho.
class OnboardingPrefs {
  static const kVistas = 'editor.dicasVistas';

  static bool vistas(WidgetRef ref) {
    try {
      return ref.read(sharedPreferencesProvider).getBool(kVistas) ?? false;
    } catch (_) {
      return false;
    }
  }

  static Future<void> marcar(WidgetRef ref, bool vistas) async {
    try {
      await ref.read(sharedPreferencesProvider).setBool(kVistas, vistas);
    } catch (_) {}
  }
}

/// O cartao de dicas: uma por vez, Proxima e Entendi.
class OnboardingCoach extends StatefulWidget {
  const OnboardingCoach({super.key, required this.onFechar});

  final VoidCallback onFechar;

  @override
  State<OnboardingCoach> createState() => _OnboardingCoachState();
}

class _OnboardingCoachState extends State<OnboardingCoach> {
  int _i = 0;

  @override
  Widget build(BuildContext context) {
    final ultima = _i == dicasDoEditor.length - 1;
    return Container(
      key: const ValueKey('editor-dica'),
      padding: const EdgeInsets.fromLTRB(14, 10, 10, 8),
      decoration: BoxDecoration(
        color: AmColors.panel.withValues(alpha: .94),
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: AmColors.hairline),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        mainAxisSize: MainAxisSize.min,
        children: [
          // O TEXTO NAO PODE ROUBAR O TOQUE.
          //
          // Um paragrafo ABSORVE o ponteiro (o Flutter precisa disso para
          // selecao de texto). Como o cartao fica por cima do alto do
          // palco, enquanto ele estava na tela nao dava para pegar nada
          // ali — inclusive a alca de girar, que mora no canto de cima da
          // selecao. O relato "a rotacao nao gira" tinha mais esta causa.
          // Ignorando o ponteiro no texto, o cartao continua legivel e o
          // palco continua tocavel; so os dois botoes respondem.
          // O TEXTO CEDE ANTES DE ESTOURAR.
          //
          // O cartao agora vive na folha de baixo, cuja altura e dada
          // pelo layout — e uma dica de tres linhas num aparelho baixo
          // estourava a coluna. Flexivel com rolagem, ele encolhe em vez
          // de vazar, e a dica continua inteira.
          Flexible(
            child: IgnorePointer(
              child: SingleChildScrollView(
                child: AppText(
                  dicasDoEditor[_i],
                  style: const TextStyle(
                    fontSize: 12.5,
                    height: 1.35,
                    color: AmColors.text,
                  ),
                ),
              ),
            ),
          ),
          const SizedBox(height: 4),
          Row(
            children: [
              IgnorePointer(
                child: AppText(
                  '${_i + 1}/${dicasDoEditor.length}',
                  style: const TextStyle(fontSize: 11, color: AmColors.muted),
                ),
              ),
              const Spacer(),
              if (!ultima)
                CupertinoButton(
                  key: const ValueKey('editor-dica-proxima'),
                  padding: const EdgeInsets.symmetric(horizontal: 10),
                  minimumSize: const Size(0, 30),
                  onPressed: () => setState(() => _i++),
                  child: const AppText('Próxima',
                    style: TextStyle(fontSize: 13, color: AmColors.text),
                  ),
                ),
              CupertinoButton(
                key: const ValueKey('editor-dica-entendi'),
                padding: const EdgeInsets.symmetric(horizontal: 10),
                minimumSize: const Size(0, 30),
                onPressed: widget.onFechar,
                child: AppText('Entendi',
                  style: TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w700,
                    color: AmColors.action,
                  ),
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

/// A LINHA DE DICA quando nada esta selecionado.
///
/// O painel some sem selecao (a timeline fica com o espaco), mas quem
/// abre o app pela primeira vez precisa saber por onde comecar. Uma
/// linha so, que some no primeiro toque em qualquer objeto.
class DicaDoPalco extends StatelessWidget {
  const DicaDoPalco({super.key});

  @override
  Widget build(BuildContext context) => ColoredBox(
    color: AmColors.panel,
    child: Center(
      child: Padding(
        padding: EdgeInsets.symmetric(horizontal: 16),
        child: AppText('Toque num objeto na tela para editar.',
          key: ValueKey('dica-palco'),
          maxLines: 1,
          overflow: TextOverflow.ellipsis,
          style: TextStyle(fontSize: 12.5, color: AmColors.muted),
        ),
      ),
    ),
  );
}
