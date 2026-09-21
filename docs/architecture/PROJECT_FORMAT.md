# Formato `.aurea`

## Um contêiner de seções, não um blob

```
manifest      — versão, índice de seções, checksum
project       — metadados, ajustes de export e de interface
timeline      — composições e camadas (a seção grande)
assets        — metadados de mídia (NÃO a mídia em si)
animations    — (planejada)
effects       — (planejada)
scene3d       — (planejada)
particles     — (planejada)
fonts         — (planejada)
thumbnails    — (planejada)
```

### Por que separar

Gravar incrementalmente exige saber o que mudou. Com seções independentes, mexer
numa animação reescreve só a seção `animations` e atualiza o índice — o resto do
arquivo não é tocado.

**Estado atual:** a gravação incremental NÃO está implementada, e o motor diz
isso (`incremental_save_implemented()` devolve `false`) em vez de aceitar a
opção e gravar tudo em silêncio. As seções `animations` e `effects` também não
são gravadas separadamente: keyframes e efeitos vão dentro da seção `timeline`,
junto com as camadas. Declarar as seções vazias seria pior do que não as
declarar — um leitor futuro as encontraria vazias e concluiria que o projeto não
tem animação.

## Layout do arquivo

```
[FileHeader]            64 bytes
[SectionHeader × N]     40 bytes cada, contíguos
[bytes da seção 0]
[bytes da seção 1]
...
```

Os cabeçalhos ficam TODOS no início, contíguos. Assim `peek()` lê só o começo do
arquivo para montar o índice completo — a Home lista 200 projetos sem ler 200
vezes dezenas de MB.

### Cabeçalho do arquivo

| Campo | Bytes | Valor |
| --- | --- | --- |
| magic | 4 | `'AURE'` (0x41455255) |
| formatVersion | 2 | 1 |
| minReaderVersion | 2 | 1 |
| sectionCount | 4 | |
| indexOffset | 8 | |
| totalSize | 8 | |
| appVersion | 32 | texto, "2.0.0" |

## Versionamento

A versão vai no manifest **e no cabeçalho de CADA seção**. Assim uma seção de
formato antigo é migrada individualmente, em vez de o arquivo inteiro ser
recusado.

- Arquivo com `minReaderVersion` maior que o do leitor: **recusado** com
  mensagem clara. Abrir e perder silenciosamente o que esta versão não entende
  seria pior.
- Seção com versão antiga e sem caminho de migração: **pulada e reportada**. O
  resto abre.
- Seção corrompida: **pulada e reportada**, quando tolerante.

O `LoadReport` diz exatamente o que foi lido, migrado, pulado e corrompido.
Nunca se abre "quase tudo" em silêncio.

## Integridade

Cada seção carrega um CRC-32. Interpretar lixo como camadas pode produzir
handles inválidos e, a partir daí, acesso a memória errada — por isso o checksum
é verificado **antes** de interpretar.

O comportamento sem `tolerateCorruptSections` é recusar. Com ele (recuperação
pós-crash), abre o que der e reporta.

## Gravação atômica

```
1. grava em <path>.tmp
2. fsync
3. rename sobre <path>
```

Uma queda no meio da escrita deixa o arquivo antigo intacto — nunca um `.aurea`
pela metade, que seria pior do que não ter salvado. Um teste verifica que nenhum
`.tmp` fica para trás.

## Autosave e recuperação

Duas defesas independentes:

| Mecanismo | Cadência | Custo | Conteúdo |
| --- | --- | --- | --- |
| Journal | ~3 s | ~20 KB | log de comandos, append-only |
| Ponto de recuperação | ~60 s | ~40 MB | projeto completo |

Gravar 40 MB a cada 3 segundos travaria o editor. Gravar 20 KB de comandos não.

### Journal

Formato append-only: cada gravação acrescenta um bloco com cabeçalho próprio
(magic `'JRNL'`, versão, contagem, CRC dos comandos). A leitura concatena todos
os blocos.

Uma queda no meio deixa o bloco truncado, e o leitor **para ali** — os blocos
anteriores continuam válidos. É o que faz a recuperação valer a pena: o usuário
recupera a sessão até o instante da queda, não até o último salvamento manual.

Um bloco corrompido é descartado sozinho pelo CRC, sem invalidar os anteriores.

### Na abertura

O motor carrega o último ponto de recuperação e reaplica o journal. O usuário vê
o projeto como estava, não como estava há cinco minutos.

Os comandos recuperados **não entram no histórico de undo**: eles são o estado
anterior, não uma ação do usuário. Colocá-los faria o primeiro "desfazer" apagar
o trabalho recuperado.

## O que o `.aurea` NÃO contém

A mídia. O arquivo guarda caminhos e metadados; o vídeo, as imagens e os modelos
vivem na sandbox do app.

Consequência: o projeto não é portável sozinho. Exportar o trabalho é o caminho
para levar o resultado; "salvar como" leva só a estrutura.
