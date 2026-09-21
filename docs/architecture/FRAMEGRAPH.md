# FrameGraph

## O que ele resolve

O compositor **não executa passes na ordem em que foram declarados**. Ele declara
o que cada passe lê e escreve, e o grafo resolve:

- **ordem de execução** por dependência, não por declaração;
- **poda**: passe que não contribui para nenhuma saída não roda;
- **aliasing**: recursos com tempos de vida disjuntos dividem a mesma textura;
- **barreiras** de sincronização, no lugar certo.

## Poda

Uma camada com opacidade 0, ou fora da tela, ou coberta por outra opaca tem o
subgrafo inteiro removido antes de custar qualquer coisa. Sem isso, cada camada
invisível ainda pagaria decode + efeitos.

A propagação começa nas saídas e sobe pelas dependências. `culled_count()`
mostra quantos passes foram removidos — se o número é alto, o grafo está fazendo
o trabalho dele.

## Aliasing

Numa cadeia de 8 efeitos sobre 4K (RGBA16F, ~66 MB por frame):

| Sem aliasing | Com aliasing |
| --- | --- |
| 8 intermediários vivos | 2 físicos |
| ~600 MB de pico | ~150 MB de pico |

É literalmente a diferença entre rodar e ser morto pelo sistema num aparelho de
6 GB.

O casamento é por descrição compatível (largura, altura, formato, samples,
mipmaps) e tempo de vida disjunto: o último passe que usa o físico precisa
terminar ANTES de o próximo recurso lógico começar.

### Exceção: recursos persistentes

Um recurso de histórico — para eco, motion blur acumulativo, `RGB time warp` —
**nunca** é aliado. Ele precisa sobreviver ao frame para o efeito temporal poder
ler o frame anterior. É a única exceção, e ela é declarada
(`create_persistent_texture`), não inferida.

## Reaproveitamento entre frames

Os recursos **físicos** sobrevivem ao `reset()`. Realocar 4K a 60 Hz seria o
gargalo dominante do editor. O casamento também é estável: o mesmo recurso
lógico tende a cair no mesmo físico frame após frame.

## Recompilação

`compile()` só refaz o trabalho quando a revisão muda. Durante o playback, 99%
dos frames têm a mesma topologia — o que muda são os PARÂMETROS dos passes, não
os passes.

## Etapas do pipeline

```
Decode → TimeRemap → Transform → Mask → Effects → 3D → Particles
       → Text → Composite → PostProcess → ColorOutput → Present
```

A ordem é informativa; quem manda é a dependência declarada. O painel de
telemetria agrupa o tempo gasto por etapa usando esta classificação.

## Detecção de ciclo

A ordenação topológica é um DFS iterativo com marcação em três estados. Se um
passe depende de outro que já está na pilha de recursão, é ciclo — e o grafo
**devolve erro** em vez de entrar em laço. Um grafo com ciclo é um erro de quem
montou, e é melhor quebrar a compilação do plano do que travar o app.

## Estado atual

A estrutura está completa e testada (ordem, poda, aliasing, persistência,
reaproveitamento, liberação de recursos). Os **passes de composição** ainda não
existem, porque dependem do backend gráfico. O que `render_frame` monta hoje é o
recurso de saída e o esqueleto do grafo.
