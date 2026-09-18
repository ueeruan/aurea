import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import '../../editor/application/registro_de_travadas.dart';

import 'package:aurea/src/core/l10n/app_language.dart';

/// O REGISTRO DE TRAVADAS, para ler no aparelho e me mandar.
///
/// A pergunta que varias tentativas de correcao nao conseguiram
/// responder de longe foi simples: no SEU aparelho, com o SEU projeto, o
/// que exatamente esta demorando? Esta tela responde com o que o proprio
/// aplicativo anotou enquanto travava.
///
/// Sao tres blocos, e a ordem e a ordem de utilidade:
///
///   1. POR MARCA — quanto tempo cada trabalho caro somou desde que o
///      aplicativo abriu. E a resposta direta.
///   2. TRAVADAS — cada quadro que passou de 120 ms e o que foi medido
///      dentro dele. `nada marcado` aqui quer dizer que o tempo saiu de
///      um caminho que ninguem esta cronometrando.
///   3. CONSTROI x DESENHA — se o custo foi em Dart (o mesmo fio que
///      recebe o toque) ou na GPU.
///
/// A linha de cima traz a VERSAO. Sem ela, dois registros iguais de
/// builds diferentes contam historias opostas — foi o que aconteceu.
class TravadasScreen extends StatefulWidget {
  const TravadasScreen({super.key});

  @override
  State<TravadasScreen> createState() => _TravadasScreenState();
}

class _TravadasScreenState extends State<TravadasScreen> {
  static const _mono = TextStyle(fontFamily: 'monospace', fontSize: 11);

  @override
  Widget build(BuildContext context) {
    final travadas = RegistroDeTravadas.travadas;
    final quadros = RegistroDeTravadas.quadros;
    final causas = RegistroDeTravadas.porCausa();
    final titulo = Theme.of(context).textTheme.titleSmall;
    return Scaffold(
      appBar: AppBar(
        title: const AppText('Travadas'),
        actions: [
          IconButton(
            key: const ValueKey('travadas-copiar'),
            tooltip: 'Copiar tudo',
            icon: const Icon(Icons.copy_rounded),
            onPressed: () async {
              await Clipboard.setData(
                ClipboardData(text: RegistroDeTravadas.emTexto()),
              );
              if (!context.mounted) return;
              ScaffoldMessenger.of(context).showSnackBar(
                const SnackBar(content: AppText('Registro copiado')),
              );
            },
          ),
          IconButton(
            key: const ValueKey('travadas-limpar'),
            tooltip: 'Limpar',
            icon: const Icon(Icons.delete_outline_rounded),
            onPressed: () {
              RegistroDeTravadas.limpar();
              setState(() {});
            },
          ),
        ],
      ),
      body: ListView(
        padding: const EdgeInsets.fromLTRB(16, 12, 16, 32),
        children: [
          AppText('Versao e motor 3D em uso', style: titulo),
          const SizedBox(height: 6),
          SelectableText(
            descreverMotor3D(),
            key: const ValueKey('travadas-motor'),
            style: const TextStyle(fontFamily: 'monospace', fontSize: 12),
          ),
          if (causas.isNotEmpty) ...[
            const SizedBox(height: 22),
            AppText('Por marca, do que mais pesou', style: titulo),
            const SizedBox(height: 6),
            for (final c in causas.take(8))
              Padding(
                padding: const EdgeInsets.only(bottom: 4),
                child: AppText(
                  '${c.somaMs.toString().padLeft(6)} ms  '
                  '${c.vezes.toString().padLeft(4)}x  '
                  'pior ${c.piorMs.toString().padLeft(4)} ms   ${c.oQue}',
                  style: _mono,
                ),
              ),
          ],
          const SizedBox(height: 22),
          if (travadas.isEmpty)
            AppText(
              'Nenhum quadro passou de ${RegistroDeTravadas.limiteMs} ms '
              'desde que o aplicativo abriu. Use o projeto que trava e '
              'volte aqui.',
              key: const ValueKey('travadas-vazio'),
            )
          else ...[
            AppTextMoldado(
              '{0} travadas, da mais recente', [travadas.length],
              style: titulo,
            ),
            const SizedBox(height: 6),
            for (final t in travadas)
              Padding(
                padding: const EdgeInsets.only(bottom: 6),
                child: SelectableText(t.linha, style: _mono),
              ),
          ],
          if (quadros.isNotEmpty) ...[
            const SizedBox(height: 22),
            AppText('Constroi x desenha', style: titulo),
            const SizedBox(height: 6),
            for (final q in quadros)
              Padding(
                padding: const EdgeInsets.only(bottom: 4),
                child: SelectableText(q.linha, style: _mono),
              ),
          ],
        ],
      ),
    );
  }
}
