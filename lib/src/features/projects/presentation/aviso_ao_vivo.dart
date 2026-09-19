import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';
import 'package:url_launcher/url_launcher.dart';
import 'package:aurea/src/core/theme/aurea_colors.dart';

import '../../../core/avisos/avisos_service.dart';
import '../../../core/theme/app_theme.dart';

/// AS FAIXAS DOS AVISOS AO VIVO, no alto da Início.
///
/// Uma linha por recado, um "!" e o texto que veio do servidor. Sem
/// cartão, sem bolha: é um recado, e um recado se lê e se fecha. O X
/// esconde só aquele aviso — os outros ficam, e o próximo, com outro id,
/// aparece de novo.
///
/// Um aviso marcado como POPUP também abre uma janela na primeira vez
/// que o app abre; a faixa continua aqui depois de fechá-la.
class AvisoAoVivo extends StatefulWidget {
  const AvisoAoVivo({super.key, this.servico});

  /// Para os testes: um serviço com os avisos já em mãos.
  final AvisosService? servico;

  @override
  State<AvisoAoVivo> createState() => _AvisoAoVivoState();
}

class _AvisoAoVivoState extends State<AvisoAoVivo> {
  AvisosService get _s => widget.servico ?? AvisosService.instance;
  bool _mostrandoJanela = false;

  @override
  void initState() {
    super.initState();
    if (widget.servico == null) _s.iniciar();
    _s.emJanela.addListener(_talvezAbrirJanela);
    WidgetsBinding.instance.addPostFrameCallback((_) => _talvezAbrirJanela());
  }

  @override
  void dispose() {
    // A FAIXA E DONA DO RELOGIO. O servico e um so para o app inteiro,
    // mas quem pede a busca de dez em dez minutos e esta faixa — quando
    // ela sai da tela (ou o teste termina), o relogio para junto.
    _s.emJanela.removeListener(_talvezAbrirJanela);
    if (widget.servico == null) _s.parar();
    super.dispose();
  }

  /// A JANELA DO AVISO, uma vez por recado.
  void _talvezAbrirJanela() {
    final aviso = _s.emJanela.value;
    if (aviso == null || _mostrandoJanela || !mounted) return;
    _mostrandoJanela = true;
    showCupertinoDialog<void>(
      context: context,
      builder: (dialogo) => CupertinoAlertDialog(
        key: ValueKey('aviso-janela-${aviso.id}'),
        title: const AppText('Aviso'),
        content: Padding(
          padding: const EdgeInsets.only(top: 8),
          child: Text(aviso.texto),
        ),
        actions: [
          if (aviso.link != null)
            CupertinoDialogAction(
              key: const ValueKey('aviso-janela-abrir'),
              isDefaultAction: true,
              onPressed: () {
                launchUrl(
                  Uri.parse(aviso.link!),
                  mode: LaunchMode.externalApplication,
                );
                Navigator.of(dialogo).pop();
              },
              child: const AppText('Abrir'),
            ),
          CupertinoDialogAction(
            key: const ValueKey('aviso-janela-fechar'),
            onPressed: () => Navigator.of(dialogo).pop(),
            child: const AppText('Agora não'),
          ),
        ],
      ),
    ).then((_) {
      _mostrandoJanela = false;
      _s.fecharJanela();
    });
  }

  @override
  Widget build(BuildContext context) => ValueListenableBuilder<List<Aviso>>(
    valueListenable: _s.todos,
    builder: (context, avisos, _) {
      if (avisos.isEmpty) return const SizedBox.shrink();
      return SafeArea(
        bottom: false,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            for (final aviso in avisos) _Faixa(aviso: aviso, servico: _s),
          ],
        ),
      );
    },
  );
}

class _Faixa extends StatelessWidget {
  const _Faixa({required this.aviso, required this.servico});

  final Aviso aviso;
  final AvisosService servico;

  @override
  Widget build(BuildContext context) {
    final cor = switch (aviso.nivel) {
      NivelDoAviso.info => AppColors.lime,
      NivelDoAviso.atencao => const Color(0xFFFFC978),
      NivelDoAviso.problema => const Color(0xFFFF7A7A),
    };
    return Container(
      key: ValueKey('aviso-${aviso.id}'),
      margin: const EdgeInsets.fromLTRB(12, 8, 12, 0),
      padding: const EdgeInsets.fromLTRB(12, 10, 6, 10),
      decoration: BoxDecoration(
        color: cor.withValues(alpha: .14),
        borderRadius: BorderRadius.circular(12),
      ),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Container(
            width: 22,
            height: 22,
            alignment: Alignment.center,
            decoration: BoxDecoration(color: cor, shape: BoxShape.circle),
            child: const AppText(
              '!',
              style: TextStyle(
                fontSize: 14,
                fontWeight: FontWeight.w900,
                color: AureaColors.onAccent,
              ),
            ),
          ),
          const SizedBox(width: 10),
          Expanded(
            child: GestureDetector(
              behavior: HitTestBehavior.opaque,
              onTap: aviso.link == null
                  ? null
                  : () => launchUrl(
                      Uri.parse(aviso.link!),
                      mode: LaunchMode.externalApplication,
                    ),
              child: Padding(
                padding: const EdgeInsets.only(top: 2),
                child: Text(
                  aviso.link == null
                      ? aviso.texto
                      : '${aviso.texto}  Saiba mais ›',
                  style: TextStyle(
                    fontSize: 13,
                    height: 1.35,
                    fontWeight: FontWeight.w600,
                    color: AppColors.onDark,
                  ),
                ),
              ),
            ),
          ),
          // O X TEM 44 PX: um recado que só some acertando um
          // alvo de 16 px vira um recado que não some.
          GestureDetector(
            key: ValueKey('aviso-fechar-${aviso.id}'),
            behavior: HitTestBehavior.opaque,
            onTap: () => servico.dispensar(aviso.id),
            child: SizedBox(
              width: 40,
              height: 28,
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
