# Timeline

## A timeline não é a UI da timeline

Esta é a estrutura de dados. Ela não sabe quantos pixels tem a régua, não sabe o
que é um dedo arrastando, não conhece zoom nem scroll.

É essa separação que permite o MESMO objeto alimentar o preview e o export. Se a
timeline morasse na UI, o export teria que reconstruí-la — e as duas versões
divergiriam no primeiro caso especial.

## Modelo

```
Timeline
  ├── Composition (a raiz, e quantas aninhadas houver)
  │     ├── formato: width, height, fps, duration, background
  │     ├── order[]        ← ordem vertical, do FUNDO para a FRENTE
  │     ├── layers         ← tabela de slots com geração
  │     ├── câmera ativa
  │     └── ajustes: sombras, ambiente, pós-processo, motion blur
  └── relógio (TimelineClock)
```

### Uma camada, todos os tipos

`Layer` é uma struct só — vídeo, texto, forma, null, câmera, luz, modelo 3D,
partículas, composição aninhada. O que não se aplica fica no valor padrão.

Por que não herança: a timeline precisa ordenar, mover, agrupar e duplicar
QUALQUER camada junto com as outras. Com hierarquia de classes, "mover 8 camadas
de tipos diferentes" viraria 8 caminhos de código, e cada tipo novo obrigaria a
revisitar todos.

### Ordem vertical é dado

A ordem vive num vetor explícito (`order_`), não é derivada do `zOrder` nem da
ordem de criação. Arrastar uma camada para cima de outra é uma operação O(1) que
não invalida handle nenhum.

`zOrder` e `drawIndex` são **derivados** — a UI mostra "camada 3 de 7", o
renderer desenha na ordem resolvida.

### Fim de camada é exclusivo

`start` inclusive, `end` EXCLUSIVO. Se fosse inclusivo, dois cortes adjacentes
mostrariam um frame repetido na junção — o tipo de erro que só aparece no export.

### Composições aninhadas

Não existe tipo separado para "pre-comp": é a MESMA estrutura, referenciada por
uma camada de tipo `Composition`. A recursão fica natural e o renderer não
precisa de caso especial.

Duas proteções: profundidade máxima de 8 níveis (cada nível custa um render
target e uma recursão), e checagem de ciclo antes de permitir aninhar.

## Handles, não ponteiros

A UI roda em outra thread e segura referências durante um gesto. A timeline pode
apagar essa camada no meio do gesto. Um handle carrega a **geração**, então
resolver um handle velho devolve `nullptr` de forma determinística — a UI trata
como "a camada sumiu", não como crash.

A tabela de slots nunca fragmenta: os slots livres ficam numa free-list
intrusiva, então inserir e remover não realocam o vetor.

## Relógio

Dois relógios, nunca misturados:

| Relógio | Uso |
| --- | --- |
| `FrameIndex` | posição discreta na grade. O que a timeline guarda, o que o cache usa como chave, o que o export percorre |
| `TickNs` | nanossegundos. O que o master clock de áudio produz |

São tipos distintos, não dois `int64` — misturá-los é a origem clássica de
drift A/V, e o compilador ajuda a não misturar.

### Quem manda no tempo

- **Durante o playback, o ÁUDIO.** O renderer pergunta "que instante é agora?" e
  desenha o frame correspondente. Se o vídeo travar, o áudio continua e o vídeo
  pula — o contrário produz estalo.
- **Parado, o playhead.** A UI posiciona, e o relógio fica travado. Sem isso, o
  clock de áudio devolveria a posição antiga e o playhead voltaria sozinho
  enquanto o dedo arrasta.

`seek()` é a intenção do usuário (trava o relógio). `set_playhead()` é o relógio
informando a posição (não trava).

### Conversão taxa ↔ tempo

`tick_at()` calcula o instante de um frame e depois **confere** que o instante
caiu dentro do frame certo, corrigindo um nanossegundo se preciso.

A verificação parece redundante e não é: a 29,97 fps, `f * 1e9 / fps` em ponto
flutuante pode cair um nanossegundo ANTES do início real do frame, e
`frame_at()` devolveria `f - 1`. O erro é de um frame, aparece só em algumas
taxas, e o sintoma é um frame repetido no meio de um vídeo longo.

## Operações

- **Split** no playhead: a segunda metade recebe `offset` ajustado, senão
  repetiria o começo do vídeo — o erro clássico de corte.
- **Parenting**: checagem de ciclo ao definir. Sobe a cadeia de pais do candidato
  e recusa se encontrar a própria camada; sem isso a avaliação de transform entra
  em laço infinito.
- **Ímã**: `next_snap_point` / `prev_snap_point` acham bordas de camada e
  keyframes. É o que faz alinhar corte com corte sem precisar de zoom alto.
- **Duplicar**: cópia profunda (tracks, efeitos, máscaras, texto), posicionada
  logo acima do original na ordem vertical.

## Invariante verificada

`order().size() == layers().count()` — a ordem de desenho contém exatamente as
camadas vivas. Se essa asserção dispara, alguma operação inseriu ou removeu de
um lado só, e o resultado seria uma camada que existe mas nunca é desenhada.

Essa invariante existe porque o bug aconteceu: `duplicate_layer` chamava
`move_to` sem antes ter inserido o id na lista, e a cópia ficava fora da ordem.
A camada aparecia na contagem e não aparecia na tela.
