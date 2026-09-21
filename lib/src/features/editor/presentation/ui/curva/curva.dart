/// O EDITOR DE CURVA E A NAVEGACAO DE KEYFRAMES — um import so para quem
/// abre a curva (timeline, losango dos paineis) ou pula de marca em marca.
library;

export 'editor_de_curva.dart'
    show
        EasingClipboard,
        EditorDeCurva,
        abrirEditorDeCurva,
        chaveDoPreset,
        presetsDoEditorDeCurva;
export 'grafico_da_curva.dart'
    show AlcasParametricas, GraficoDaCurva, ModoDoGrafico;
export 'navegacao_de_keyframes.dart'
    show irParaMarcaDaTrilha, irParaMarcaVizinha, marcaVizinha, setasDaTrilha;
export 'trilha_da_curva.dart'
    show TrilhaDaCurva, relogioCru, relogioDaCamada, trechoEm;
