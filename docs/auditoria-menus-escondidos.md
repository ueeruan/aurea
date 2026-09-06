# Auditoria de menus escondidos

Correção 10.1.1, item 2. A regra: **nenhuma função pode existir apenas dentro
de um menu escondido**. Toque longo continua valendo como atalho, mas sempre
com um caminho visível que faz a mesma coisa.

Levantamento de 2026-09-03. Zero gavetas (`Drawer`) no aplicativo.

---

## 1 · Três pontinhos

### 1.1 Editor de curvas — `curve_panel.dart`

É o caso que o documento nomeia. Quatro comandos, todos escondidos.

| Item | Destino |
|---|---|
| Copiar curva | **botão visível** no rodapé do editor |
| Colar curva | **botão visível**, esmaecido quando não há curva copiada |
| Aplicar curva a todos os keyframes | **botão visível** |
| Ativar overshoot | **interruptor visível**, mostrando ligado/desligado sem abrir nada |

### 1.2 Cartão de efeito — `effects_panel.dart`

| Item | Destino |
|---|---|
| Mover para cima / para baixo | **setas visíveis** no cartão; a ordem dos efeitos é o resultado, e ela precisa ser manipulável à vista |
| Ligar / Desligar | **interruptor visível** no cartão (já há espaço na linha do título) |
| Remover | **botão visível** no cartão — já existe (`onRemove`), sai do menu |
| Resetar parâmetros | **botão visível** na profundidade Avançado |
| Salvar como preset | **botão visível** na seção Presets |

### 1.3 Editar pontos — `points_panel.dart`

| Item | Destino |
|---|---|
| Cravar / tirar keyframe dos pontos aqui | **já existe visível**: é o diamante do trilho esquerdo. Remover do menu |
| Adicionar ponto no cursor | **já existe visível**: o modo `add` do trackpad. Remover do menu |
| Canto / suave | **botão visível** na fileira do painel — é o comando mais usado ao editar um caminho |
| Apagar ponto | **botão visível** na fileira do painel |
| Abrir / fechar o caminho | **interruptor visível** na fileira do painel |

### 1.4 "Mais" do menu da camada — `layer_menu.dart`

Este não é um menu de conveniência: é onde moram editores inteiros. O
documento diz que cada tipo de camada mostra só as seções que fazem sentido
para ele — então **cada item aqui é uma seção do tipo, não um item de menu**.

| Item | Tipo de camada | Destino |
|---|---|---|
| ~~Módulo Grade~~ | Nulo | ✅ **feito**: virou a seção **Clonar**, visível na grade |
| Editar texto · Fonte | Texto | seção **Editar texto** do tipo Texto |
| Editar legendas | Legenda | seção **Editar legendas** do tipo Legenda |
| Partículas | Partículas | seção **Partículas** do tipo Partículas |
| Elemento 3D · Cena 3D · Estúdio 3D · Câmeras e cortes | 3D | seções do tipo Cena 3D |
| Texto em caminho | Texto | dentro da seção Editar texto |
| Precompor · Desagrupar · Tempo da precomp | Grupo | **cabeçalho** — são comandos estruturais, e o documento tira comandos estruturais da grade |
| Excluir e fechar · Fechar buracos | qualquer | **cabeçalho**, junto de dividir/duplicar/excluir |

### 1.5 `⋯` do trilho esquerdo — `panel_chrome.dart`

O quarto item do trilho (`← ◆ ⌇ ⋯`). O documento descreve o trilho com **três**
itens: voltar, diamante e curva. **Destino: remover o `⋯`**; o que ele abre em
cada painel vira controle visível do próprio painel.

### 1.6 `⋯` do cabeçalho da camada — `editor_screen.dart`

Abre o menu da camada (a grade de sete seções). **Não é menu escondido**: é o
caminho principal para a grade, e a grade é toda visível. Fica.

---

## 2 · Toques longos

Doze no aplicativo. Todos precisam de um caminho visível equivalente.

| Onde | O que faz | Caminho visível equivalente |
|---|---|---|
| Barra do clipe (`am_timeline`) | congelar quadro | ✅ ícone de congelar na fileira de ícones pequenos |
| Barra da camada | abre o menu da camada | ✅ o toque simples já abre |
| Junção entre clipes | trocar transição | **falta**: a junção precisa de um alvo visível ao ser tocada |
| Barra em modo compacto | menu | ✅ mesmo do toque simples |
| Marcas da régua (`editor_screen`) | menu das marcas | **falta**: botão visível de marcas no transporte |
| Cartão de projeto (`projects_tab`, 4×) | renomear, duplicar, excluir | **falta**: os cartões precisam de um `⋯` visível **por cartão** — aqui um menu por item é o padrão da plataforma e não esconde função do editor |

Os cinco atalhos de poder do documento 10.1 (`◆`, `⌇`, valor, junção, dois
dedos na barra) continuam valendo — **como atalho**, com o caminho visível ao
lado.

---

## Contagem para o teste final

| O que contar | Hoje | Obrigatório |
|---|---|---|
| Funções que só existem dentro de três pontinhos | 4 menus (curva, efeito, pontos, Mais) | zero |
| Gavetas | 0 | 0 |
| Toques longos sem caminho visível | 3 | zero |
