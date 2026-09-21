/// O PAINEL EFEITOS mora em `efeitos/`: o painel, a pilha (cartoes,
/// Time Remap, Animador de Texto) e o catalogo. Este arquivo so reexporta,
/// para o registro (`registro.dart`) e o painel Cor continuarem com o
/// mesmo import.
library;

export 'efeitos/catalogo_de_efeitos.dart'
    show CatalogoDeEfeitos, abrirCatalogoDeEfeitos;
export 'efeitos/painel_de_efeitos.dart' show PainelEfeitos;
export 'efeitos/pilha_de_efeitos.dart' show PilhaDeEfeitos;
