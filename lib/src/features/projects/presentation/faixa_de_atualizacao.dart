import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';

import '../../../core/atualizacao/atualizacao_service.dart';
import '../../../core/theme/app_theme.dart';

/// A FAIXA DA VERSAO NOVA, no alto da Inicio.
///
/// Fica ao lado dos avisos ao vivo e segue a mesma ideia: uma linha, um
/// icone, e o que fazer. A diferenca e o botao — aqui ha o que fazer, e
/// o que fazer e baixar o APK novo sem sair do aplicativo.
///
/// QUANDO A VERSAO E OBRIGATORIA o X nao aparece: a faixa nao fecha, e a
/// pessoa atualiza antes de seguir. E para a versao que conserta algo que
/// impede usar — nao para toda versao.
class FaixaDeAtualizacao extends StatefulWidget {
  const FaixaDeAtualizacao({super.key, this.servico});

  /// Para os testes: um servico com a versao ja em maos.
  final AtualizacaoService? servico;

  @override
  State<FaixaDeAtualizacao> createState() => _FaixaDeAtualizacaoState();
}

class _FaixaDeAtualizacaoState extends State<FaixaDeAtualizacao> {
  AtualizacaoService get _s => widget.servico ?? AtualizacaoService.instance;

  @override
  void initState() {
    super.initState();
    if (widget.servico == null) _s.iniciar();
  }

  @override
  void dispose() {
    // A FAIXA E DONA DO RELOGIO, como a dos avisos: o servico e um so para
    // o app inteiro, mas quem pede a busca e esta faixa. Saiu da tela, o
    // relogio para junto.
    if (widget.servico == null) _s.parar();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => ValueListenableBuilder<VersaoPublicada?>(
    valueListenable: _s.oferecida,
    builder: (context, versao, _) {
      if (versao == null) return const SizedBox.shrink();
      return SafeArea(
        bottom: false,
        child: ValueListenableBuilder<FaseDaAtualizacao>(
          valueListenable: _s.fase,
          builder: (context, fase, _) => ValueListenableBuilder<double>(
            valueListenable: _s.progresso,
            builder: (context, progresso, _) => ValueListenableBuilder<String?>(
              valueListenable: _s.erro,
              builder: (context, erro, _) =>
                  _Faixa(
                    versao: versao,
                    fase: fase,
                    progresso: progresso,
                    erro: erro,
                    servico: _s,
                    aoAtualizar: () => _s.baixarEInstalar(),
                    aoAdiar: () => _s.adiar(),
                  ),
            ),
          ),
        ),
      );
    },
  );
}

class _Faixa extends StatelessWidget {
  const _Faixa({
    required this.versao,
    required this.fase,
    required this.progresso,
    required this.erro,
    required this.servico,
    required this.aoAtualizar,
    required this.aoAdiar,
  });

  final VersaoPublicada versao;
  final FaseDaAtualizacao fase;
  final double progresso;
  final String? erro;
  final AtualizacaoService servico;
  final VoidCallback aoAtualizar;
  final VoidCallback aoAdiar;

  /// O QUE A FAIXA DIZ AGORA, em uma linha.
  String get _texto {
    if (erro != null) return erro!;
    return switch (fase) {
      FaseDaAtualizacao.baixando => progresso >= 0
          ? 'Baixando a versao ${versao.nome} · ${(progresso * 100).round()}%'
          : 'Baixando a versao ${versao.nome}…',
      FaseDaAtualizacao.conferindo => 'Conferindo o arquivo…',
      FaseDaAtualizacao.instalando => 'Abra o instalador e toque em Instalar.',
      FaseDaAtualizacao.pronto => 'Instalando…',
      FaseDaAtualizacao.parada =>
        versao.notas.isEmpty
            ? 'A versao ${versao.nome} ja esta disponivel.'
            : 'A versao ${versao.nome}: ${versao.notas}',
    };
  }

  bool get _ocupado =>
      fase == FaseDaAtualizacao.baixando ||
      fase == FaseDaAtualizacao.conferindo ||
      fase == FaseDaAtualizacao.pronto;

  @override
  Widget build(BuildContext context) {
    final cor = erro != null ? const Color(0xFFFF7A7A) : AppColors.lime;
    return Container(
      key: ValueKey('atualizacao-${versao.codigo}'),
      margin: const EdgeInsets.fromLTRB(12, 8, 12, 0),
      padding: const EdgeInsets.fromLTRB(12, 10, 6, 10),
      decoration: BoxDecoration(
        color: cor.withValues(alpha: .14),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          Container(
            width: 22,
            height: 22,
            alignment: Alignment.center,
            decoration: BoxDecoration(color: cor, shape: BoxShape.circle),
            child: const Icon(
              CupertinoIcons.arrow_down_circle_fill,
              size: 14,
              color: Color(0xFF0B0E12),
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: Text(
              _texto,
              key: const ValueKey('atualizacao-texto'),
              style: TextStyle(
                fontSize: 13,
                height: 1.35,
                fontWeight: FontWeight.w600,
                color: AppColors.onDark,
              ),
            ),
          ),
          const SizedBox(width: 8),
          if (!_ocupado)
            GestureDetector(
              key: const ValueKey('atualizacao-baixar'),
              behavior: HitTestBehavior.opaque,
              onTap: aoAtualizar,
              child: Container(
                padding: const EdgeInsets.symmetric(
                  horizontal: 12,
                  vertical: 7,
                ),
                decoration: BoxDecoration(
                  color: cor,
                  borderRadius: BorderRadius.circular(999),
                ),
                child: AppText(
                  erro != null ? 'Tentar de novo' : 'Atualizar',
                  style: const TextStyle(
                    fontSize: 12,
                    fontWeight: FontWeight.w800,
                    color: Color(0xFF0B0E12),
                  ),
                ),
              ),
            )
          else
            const Padding(
              padding: EdgeInsets.symmetric(horizontal: 10),
              child: CupertinoActivityIndicator(radius: 8),
            ),
          // O X TEM 44 PX, como o do aviso. E NAO EXISTE quando a versao
          // e obrigatoria: ali nao ha escolha a respeitar.
          if (!versao.obrigatoria && !_ocupado)
            GestureDetector(
              key: const ValueKey('atualizacao-depois'),
              behavior: HitTestBehavior.opaque,
              onTap: aoAdiar,
              child: SizedBox(
                width: 44,
                height: 44,
                child: Icon(
                  CupertinoIcons.xmark,
                  size: 15,
                  color: AppColors.muted,
                ),
              ),
            ),
        ],
      ),
    );
  }
}
