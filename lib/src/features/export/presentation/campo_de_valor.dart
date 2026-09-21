import 'package:aurea/src/core/l10n/app_language.dart';
import 'package:flutter/cupertino.dart';

import '../../../core/ui/am_colors.dart';

/// AS DUAS EXPRESSOES VIVEM FORA DAS FUNCOES porque um campo formata a
/// cada `build`, e sao seis campos na tela: recompilar a expressao a
/// cada quadro e trabalho que ninguem ve e todo mundo paga.
final _soZeros = RegExp(r'^-0(?:[.,]0*)?$');
final _espacos = RegExp(r'\s+');

/// O NUMERO EM PT-BR: virgula no decimal, casas FIXAS.
///
/// Fixas, e nao "as casas que forem precisas", porque os campos ficam
/// lado a lado numa fila (`x` `y` `z`) e com casas variaveis `540,05` e
/// `540` colocariam a virgula em colunas diferentes — a fila deixaria
/// de se ler de relance, que e a unica coisa que ela faz. A referencia
/// medida usa uma casa na posicao e no tamanho (`540,0`) e duas na
/// inclinacao (`0,00`), e por isso [casas] e parametro e nao constante.
String numeroPtBr(double v, {int casas = 1}) {
  // VALOR QUEBRADO VIRA ZERO NA TELA. Um NaN que chega aqui e bug de
  // quem calculou; escrever a palavra "NaN" no campo so repassa o susto
  // ao usuario e ainda estoura a largura da caixa.
  final n = v.isFinite ? v : 0.0;
  // CASAS FORA DA FAIXA SAO PRESAS, e nao recusadas: `toStringAsFixed`
  // lanca fora de 0..20, e derrubar o painel inteiro por causa de um
  // parametro mal passado e pior que mostrar o numero com outra
  // precisao.
  final c = casas < 0 ? 0 : (casas > 6 ? 6 : casas);
  var texto = n.toStringAsFixed(c);
  // O ZERO NEGATIVO EXISTE em ponto flutuante, e -0,04 arredondado para
  // uma casa vira "-0,0": um sinal de menos que nao quer dizer nada e
  // que faz o campo piscar entre "0,0" e "-0,0" durante um arrasto.
  if (_soZeros.hasMatch(texto)) texto = texto.substring(1);
  return texto.replaceAll('.', ',');
}

/// A VOLTA: le tanto virgula quanto ponto.
///
/// Os dois, porque quem escolhe a tecla decimal NAO E O APP: o teclado
/// numerico do sistema mostra ponto em aparelho configurado em ingles e
/// virgula em pt-BR, e o mesmo projeto anda entre os dois. Recusar um
/// deles faria o campo parecer quebrado em metade dos aparelhos.
///
/// Devolve NULO quando o texto nao e numero — nulo, e nao zero. Zero e
/// um valor legitimo: engolir "abc" como zero apagaria em silencio o
/// que ja estava no parametro.
double? numeroDePtBr(String texto) {
  final limpo = texto
      .replaceAll(_espacos, '')
      // O SUFIXO VOLTA JUNTO quando o campo ja vem preenchido ('45°'):
      // e o proprio texto que este arquivo escreveu, entao ele tem de
      // saber ler o que escreveu.
      .replaceAll('°', '')
      .replaceAll('%', '')
      .replaceAll(',', '.');
  if (limpo.isEmpty) return null;
  final v = double.tryParse(limpo);
  // INFINITO E NAN NAO PASSAM: `double.tryParse('Infinity')` aceita de
  // bom grado, e um valor desses entra no projeto, e salvo, e depois
  // nao ha edicao que o traga de volta.
  if (v == null || !v.isFinite) return null;
  return v;
}

/// A CAIXA DE VALOR do topo do painel: le o numero e deixa digitar o
/// numero exato.
///
/// ELA E O UNICO JEITO DE CRAVAR UM VALOR. As superficies do painel
/// (almofada, dial, fita) sao todas RELATIVAS de proposito: dizem
/// "quanto andou", nunca "onde parou", e e isso que as faz servir tanto
/// para 0..1 quanto para 0..4000 sem limite inventado. O preco e que
/// nenhuma delas chega em exatamente 540,0 com o dedo — este campo
/// existe para pagar esse preco.
///
/// O NUMERO E SUBLINHADO porque sublinhado e o unico aviso de que a
/// caixa se toca. Sem ele o campo parece uma etiqueta, e a referencia
/// medida usa exatamente esta marca (`docs/painel-de-transformacao-
/// alight.md`).
///
/// O ROTULO FICA FORA DA CAIXA, embaixo. Dentro, ele roubaria largura
/// do numero, que e o que se le de longe; embaixo, ele ocupa espaco que
/// nao disputa com nada e sai da atencao sozinho.
class CampoDeValor extends StatelessWidget {
  const CampoDeValor({
    super.key,
    required this.rotulo,
    required this.valor,
    this.casas = 1,
    this.sufixo = '',
    this.aoDigitar,
    this.largura = 61,
    this.nome,
    this.cor,
    this.aoSelecionar,
    this.aoSegurar,
  });

  final Color? cor;
  final VoidCallback? aoSelecionar;

  /// O TOQUE LONGO NO NUMERO, quando ele faz outra coisa alem de digitar.
  ///
  /// Duas coisas moram aqui: a EXPRESSAO (Pro) e o ANIMADOR automatico.
  /// No painel antigo as duas pendiam do nome da propriedade numa linha
  /// de parametro; a linha saiu, e com ela as duas portas — o campo e
  /// onde elas voltam.
  final VoidCallback? aoSegurar;

  /// A ALTURA DA CAIXA e o RAIO, medidos na referencia. Publicos porque
  /// quem monta a fila precisa reservar a linha sem adivinhar.
  static const altura = 24.0;
  static const raio = 8.0;

  /// O texto minusculo embaixo da caixa: 'x', 'Largura', 'X Skew'.
  final String rotulo;

  final double valor;

  /// Casas decimais: uma na posicao e no tamanho, duas na inclinacao.
  final int casas;

  /// O SUFIXO E DA LEITURA, E NAO DO NUMERO: '°' no angulo, '%' na
  /// escala. Ele entra no texto da caixa, onde diz de que unidade se
  /// esta falando, e fica de fora do campo de digitar, onde so
  /// atrapalharia — ver [_abrirDialogo].
  final String sufixo;

  /// NULO DEIXA O CAMPO SO DE LEITURA. Ha valores que o painel mostra e
  /// nao manda — o angulo enquanto o dial esta com o dedo em cima, um
  /// parametro que so o motor calcula. Passar um callback vazio
  /// diria "toque aqui" e nao faria nada, que e pior do que nao
  /// convidar.
  final void Function(double)? aoDigitar;

  /// 61 px, da referencia: cabe `540,0` sem apertar e tres campos cabem
  /// na largura do miolo.
  final double largura;

  /// O NOME PARA QUEM NAO VE A TELA, quando ele nao pode ser o [rotulo].
  ///
  /// Na linha de parametro o rotulo vai vazio de proposito — o nome ja
  /// esta no chip a esquerda, e repeti-lo custaria altura em cada linha.
  /// So que o leitor de tela nao ve o chip da esquerda: sem este campo
  /// ele anunciaria "Valor de" sem dizer valor de que, e o teclado
  /// abriria com o titulo em branco. O nome do parametro e a unica pista
  /// de o que se esta editando nesses dois lugares.
  final String? nome;

  bool get _digitavel => aoDigitar != null;

  String get _texto => '${numeroPtBr(valor, casas: casas)}$sufixo';

  /// COM NOME NENHUM SOBRA "Valor", e nao "Valor de ": um rotulo com o
  /// rabo cortado soa a defeito para quem ouve, e nunca a campo sem
  /// nome.
  String get _nome => nome ?? rotulo;

  String get _etiqueta => _nome.isEmpty ? 'Valor' : 'Valor de $_nome';

  @override
  Widget build(BuildContext context) {
    // UM SO GESTO, DECLARADO DUAS VEZES. `excludeSemantics` apaga a
    // arvore de baixo inteira, e junto com ela a acao de toque que o
    // `GestureDetector` publicaria: sem repetir o mesmo callback no
    // `Semantics`, o campo se anuncia como botao, o leitor de tela
    // oferece "toque duas vezes para ativar" e o toque duplo nao faz
    // nada. E o mesmo defeito do sublinhado sem callback — prometer o
    // que nao se cumpre —, so que invisivel para quem revisa olhando.
    final tocar =
        aoSelecionar ?? (_digitavel ? () => _abrirDialogo(context) : null);
    return Semantics(
      container: true,
      excludeSemantics: true,
      button: _digitavel,
      label: _etiqueta,
      value: _texto,
      onTap: tocar,
      child: GestureDetector(
        behavior: HitTestBehavior.opaque,
        onTap: tocar,
        // O SEGURAR EXPLICITO GANHA DO IMPLICITO: quem passa [aoSegurar]
        // esta dizendo o que o dedo longo faz ali (abrir a expressao,
        // oferecer o animador). Sem ele, o segurar continua sendo o
        // atalho para digitar, que e o que o campo Z do mover usa.
        onLongPress: aoSegurar ??
            (aoSelecionar != null && _digitavel
                ? () => _abrirDialogo(context)
                : null),
        child: SizedBox(
          width: largura,
          child: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              Container(
                height: altura,
                alignment: Alignment.center,
                padding: const EdgeInsets.symmetric(horizontal: 4),
                decoration: BoxDecoration(
                  // #242436, e nao o #242430 da capsula do tempo: a
                  // referencia foi medida caixa a caixa e as duas telas
                  // deram tons diferentes (ver [AmColors.campo]).
                  color: AmColors.campo,
                  borderRadius: BorderRadius.circular(raio),
                ),
                // O NUMERO ENCOLHE ANTES DE VAZAR: a escala nao tem teto
                // (4000,0 e um valor legitimo) e a caixa tem largura
                // fixa. Encolher mantem o valor legivel; cortar com
                // "..." mentiria sobre o numero.
                child: FittedBox(
                  fit: BoxFit.scaleDown,
                  child: AppText(
                    _texto,
                    maxLines: 1,
                    softWrap: false,
                    style: TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                      color: cor ?? AmColors.accent,
                      // SEM CALLBACK, SEM SUBLINHADO: o sublinhado e a
                      // promessa de que da para digitar, e prometer o
                      // que nao se cumpre e o unico jeito de o campo
                      // mentir.
                      decoration: _digitavel
                          ? TextDecoration.underline
                          : TextDecoration.none,
                      decorationColor: cor ?? AmColors.accent,
                      fontFeatures: const [FontFeature.tabularFigures()],
                    ),
                  ),
                ),
              ),
              // ROTULO VAZIO NAO DESENHA NADA. Numa linha de parametro o
              // nome ja esta no chip a esquerda, e repetir embaixo da
              // caixa custaria 12 px de altura por linha.
              if (rotulo.isNotEmpty) ...[
                const SizedBox(height: 3),
                AppText(
                  rotulo,
                  maxLines: 1,
                  overflow: TextOverflow.ellipsis,
                  textAlign: TextAlign.center,
                  style: const TextStyle(fontSize: 9, color: AmColors.muted),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }

  Future<void> _abrirDialogo(BuildContext context) async {
    final avisar = aoDigitar;
    if (avisar == null) return;
    final novo = await showCupertinoDialog<double>(
      context: context,
      builder: (c) => _DialogoDeValor(
        titulo: _etiqueta,
        // O SUFIXO NAO ENTRA NO CAMPO DE DIGITAR: quem digita quer
        // escrever o numero, e ter de desviar do '°' com o cursor e
        // trabalho que o campo pode poupar.
        inicial: numeroPtBr(valor, casas: casas),
      ),
    );
    if (novo == null) return;
    // O CONTEXTO PODE TER MORRIDO enquanto o dialogo estava aberto (a
    // camada foi apagada, o painel trocou de modo). Avisar depois disso
    // seria mandar um valor para um dono que ja nao existe.
    if (!context.mounted) return;
    avisar(novo);
  }
}

/// O DIALOGO DONO DO PROPRIO CONTROLADOR: com estado porque o
/// `TextEditingController` so pode ser descartado quando o dialogo sai
/// da arvore, e nao a cada `build` do campo.
class _DialogoDeValor extends StatefulWidget {
  const _DialogoDeValor({required this.titulo, required this.inicial});

  final String titulo;
  final String inicial;

  @override
  State<_DialogoDeValor> createState() => _DialogoDeValorState();
}

class _DialogoDeValorState extends State<_DialogoDeValor> {
  /// O TEXTO JA NASCE SELECIONADO. Quem abre o campo quase sempre quer
  /// TROCAR o numero, nao emendar nele: sem a selecao, o primeiro gesto
  /// e apagar cinco caracteres um a um.
  late final TextEditingController _campo =
      TextEditingController(text: widget.inicial)
        ..selection = TextSelection(
          baseOffset: 0,
          extentOffset: widget.inicial.length,
        );

  @override
  void dispose() {
    _campo.dispose();
    super.dispose();
  }

  /// TEXTO ILEGIVEL FECHA SEM MUDAR NADA. [numeroDePtBr] devolve nulo, o
  /// nulo sai por aqui e quem chamou nao avisa ninguem — o valor de
  /// antes continua de pe, que e o que a pessoa esperaria de um campo
  /// que ela nao conseguiu preencher.
  void _confirmar() => Navigator.of(context).pop(numeroDePtBr(_campo.text));

  @override
  Widget build(BuildContext context) => CupertinoAlertDialog(
    title: AppText(widget.titulo),
    content: Padding(
      padding: const EdgeInsets.only(top: 12),
      child: CupertinoTextField(
        key: const ValueKey('campo-de-valor-entrada'),
        controller: _campo,
        autofocus: true,
        textAlign: TextAlign.center,
        // TECLADO COM SINAL E DECIMAL: metade dos parametros aceita
        // negativo (posicao, inclinacao) e todos aceitam fracao. Um
        // teclado so de inteiros positivos tornaria o campo inutil
        // justamente nos casos em que a fita nao chega.
        keyboardType: const TextInputType.numberWithOptions(
          signed: true,
          decimal: true,
        ),
        onSubmitted: (_) => _confirmar(),
        cursorColor: AmColors.accent,
        style: const TextStyle(
          fontSize: 17,
          color: AmColors.text,
          fontFeatures: [FontFeature.tabularFigures()],
        ),
        // CORES PROPRIAS, e nao as do Cupertino: o dialogo herda o
        // claro/escuro do sistema, e um campo branco no meio do editor
        // preto seria a unica coisa clara da tela.
        decoration: BoxDecoration(
          color: AmColors.chip,
          borderRadius: BorderRadius.circular(8),
        ),
      ),
    ),
    actions: [
      CupertinoDialogAction(
        key: const ValueKey('campo-de-valor-cancelar'),
        onPressed: () => Navigator.of(context).pop(),
        child: const AppText('Cancelar'),
      ),
      CupertinoDialogAction(
        key: const ValueKey('campo-de-valor-ok'),
        isDefaultAction: true,
        onPressed: _confirmar,
        child: const AppText('OK'),
      ),
    ],
  );
}
