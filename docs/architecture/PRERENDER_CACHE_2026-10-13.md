# Cache PreRender privado — 13/10/2026

Implementação independente em `PreRenderCache.hpp/.cpp`, sem modificar o
motor, o compositor, o renderer offline ou os sistemas de exportação atuais.
O trabalho de geração, apresentação e gestos Android/iOS está na integração
do motor e das interfaces; os testes abaixo verificam o cache isolado.

## Contrato

`begin(cacheRoot, Key, frames, direction, Limits)` devolve um token. A chave
contém sessão do projeto, composição, revisão completa do modelo, dimensões,
FPS racional e versão de render. `write(token, frame, RGBA16F, cancel)` aceita
todos os frames sequencialmente: `0..N−1` para +1 ou `N−1..0` para −1. Os arquivos
usam o índice lógico do frame, permitindo leitura em qualquer sentido.
`finish(token)` anuncia conclusão somente depois de N gravações bem sucedidas
e da publicação atômica do manifesto final com CRC.

Cada frame mantém os bits RGBA16F lineares premultiplicados, antes do output,
zoom e checkerboard. Alfa e HDR não passam por MP4, conversão sRGB8 ou codec da
plataforma. A compressão por blocos de 64 KiB reutiliza `zlib_compress` do motor;
o decoder `stbi_zlib_decode_buffer` já existe na importação PSD. Cabeçalho,
blocos, frame e manifesto são verificados por tamanho, identidade e CRC.

`request` e `take` usam mutex com tentativa imediata, sem I/O ou espera pelo
worker. Há no máximo dois pedidos/frames, incluindo a leitura em andamento;
`take` transfere a propriedade do buffer ao chamador. `NotFound` significa
pendente e `Busy` solicita nova tentativa em outro tick. Uma requisição nova
pode descartar um frame pronto antigo; não cresce uma fila por todo o vídeo.

`invalidate` invalida a geração/token atomicamente, sem mutex, join ou I/O.
Uma corrida com `begin` não ressuscita a sessão antiga. O reader elimina apenas
a pasta única criada por esta instância, depois de terminarem as leases de I/O.
O produtor deve retornar antes da destruição do objeto.

## Limites e limpeza

O teto de disco é no máximo 8 GiB, com reserva mínima de 16 MiB e padrão de
256 MiB livres. A consulta de espaço ocorre antes do manifesto e de cada
bloco; falhas retornam `StorageFull`/`IoError` e não marcam um cache parcial
como completo. Nenhuma lista de pixels ou índice proporcional à duração é
mantida na RAM. O padrão de RAM é 16 MiB e a leitura se adapta a um ou dois
frames, conforme o espaço disponível. Os stats incluem buffers residentes e
uma reserva conservadora constante do compressor/decoder, sem incluir o span
de entrada do chamador, buffers já transferidos ou memória GPU do renderer.

Nova admissão retorna `Busy` até leases e limpeza anteriores terminarem. Se a
remoção falhar, a pasta fica retida, ocorre retry assíncrono e a admissão retorna
`IoError`; não acumula uma nova cota de 8 GiB por tentativa. O diagnóstico é uma
cópia própria. O chamador deve aplicar um prazo finito à admissão.

As pastas ficam em `cacheRoot/pre-render/owned-<16 hex>-<16 hex>`. O cache não
apaga o root, irmãos, projetos ou mídias. A admissão no worker examina órfãos:
exige nome exato, marker `AURPRO01`, versão 1, tamanho e CRC válidos, arquivos
planos e ausência de symlinks/junctions. Uma lease nativa por sessão protege
instâncias vivas: `CreateFileW` exclusivo no Windows e `flock(LOCK_EX|LOCK_NB)`
no POSIX. O sistema libera a lease após encerramento do processo.

Uma lease de namespace não bloqueante serializa admissão, quota e remoção entre
instâncias. A quota de até 8 GiB inclui os bytes restantes das outras sessões e
diretórios desconhecidos; erro de limpeza não apaga essa cobrança. A remoção usa
somente `filesystem::remove` em filhos planos, mantendo `owner.bin` até a remoção
dos payloads. Falha de compartilhamento preserva a identidade para um retry.
Arquivos sem marker válido são preservados. A contabilização de disco ocorre no
worker e varre os arquivos existentes; `request`, `take`, `matches` e `stats`
continuam sem I/O.

## Validação e integração pendente

O ensaio final Windows executou **8 casos / 1.764 verificações, sem falhas**:

Esse é o estado histórico anterior à proteção de órfãos. A reprodução da lacuna
adicionou dois casos e executou 10/1.789 com duas falhas: o órfão permanecia no
disco e duas instâncias conseguiam ultrapassar juntas a quota. Após a correção,
o ensaio independente executou **11 casos / 1.806 verificações, sem falhas**,
incluindo três instâncias vivas, lease mantida/solta, marker inválido, junction
NTFS real, quota cumulativa e preservação de marker após sharing violation.
São os mesmos casos anteriores acrescidos de três; reprises não se somam.
Na suíte compartilhada, uma execução revelou uma asserção instável do fixture:
`request()` pode devolver `Busy` ao disputar o `try_lock`, como prevê o contrato.
O teste agora repete somente `Busy`, com prazo de cinco segundos, e continua
exigindo `NotFound`/`OutOfRange` ao adquirir a consulta. Sem mudança de produção,
o rerun integrado passou **11 casos / 1.805 verificações**; a diferença de uma
verificação vem do heap probe desativado no executável compartilhado.

- 307 frames em cada sentido, sem o teto de 300; leituras em pontos iniciais,
  intermediários e finais mantêm o índice, alfa 0/0,5/1 e valores HDR bit a bit.
- Corrupção de cabeçalho, payload e manifesto produz erro e não devolve pixels.
- Edição/revisão, token antigo e cancelamento invalidam o acesso; arquivos fora
  da pasta própria permanecem preservados.
- Cota de disco, reserva livre, cache parcial e orçamento de RAM têm erros claros.
- Leitura 540p com dados também pouco compressíveis mantém a fila/buffers limitados.
- Uma lease Windows que impede a remoção produz `IoError`, bloqueia nova admissão
  e permite recuperação após soltar o arquivo.

Em 960×540, um frame RGBA16F tem 4.147.200 bytes. O pico contabilizado de
cache/workspace foi 8.441.856 bytes. Instrumentação do heap nativo mediu crescimento
máximo de 12.574.116 bytes, incluindo os buffers de entrada/saída do chamador.
Os 614 frames do teste de ordem usam dimensões pequenas; essa contagem não
comprova um projeto completo de 614 frames em 540p.

Runner, logs, hashes e manifesto:
`build/validation/prerender-cache-20261013/`. Os dois ensaios anteriores 7/1.752
são repetições preservadas e não se somam aos oito casos finais.

Inclusões de build realizadas pelo integrador, mantendo validação nativa móvel
e do Engine como etapas separadas:

```cmake
# engine/CMakeLists.txt, AUREA_CORE_SOURCES:
src/render/PreRenderCache.cpp
# engine/tests/CMakeLists.txt, add_executable(aurea_tests):
test_prerender_cache.cpp
```

O runner isolado compila o cache real, o compressor real e o mesmo STB vendorizado,
sem Ninja e sem novas dependências. Ainda não demonstra integração Engine,
gestos, reprodução, exportação, compilação Android/iOS ou execução nativa móvel.
O source POSIX também passou `-fsyntax-only` com NDK 28, aarch64 Android 26 e
exceções desativadas; isso comprova compilação da branch `flock`, não execução
em Android ou iOS. O fixture de órfão simula uma sessão sem lease viva; o teste
de lease utiliza handles reais no mesmo processo, sem induzir crash de aparelho.
