# AUREA PC — beta teste 0.0.1

Editor nativo para Windows 10/11 x64. Abra `AUREA-PC.exe` na pasta extraída;
as DLLs que acompanham o programa precisam permanecer ao lado do executável.
É necessário um driver de GPU com Vulkan. Esta beta não possui instalador.

O aplicativo usa o motor C++ do AUREA: composições, mídia, texto, formas,
efeitos, keyframes, curvas, histórico de edição, projetos `.aurea` e exportação
de composição em MP4. A interface PC apresenta os espaços Composição e Edição.
As referências externas orientam o arranjo e o comportamento dos painéis;
código, ícones, shaders e assets da Adobe não estão incorporados ao aplicativo.

## Uso

- `Arquivo > Novo`: cria uma composição.
- `Arquivo > Importar` ou arrastar um arquivo: importa imagem, vídeo ou áudio.
- Selecione uma camada na timeline para editar seus valores e efeitos.
- Expanda a camada para exibir seus keyframes. A régua, os clipes e os keyframes
  usam o mesmo sistema de tempo do motor compartilhado.
- `Arquivo > Salvar` grava o projeto; `Arquivo > Abrir` reabre `.aurea`.
- `Arquivo > Exportar` renderiza a composição em MP4.
- `Ctrl+roda` amplia a timeline mantendo o ponto sob o cursor.
- `Shift+roda` desloca a timeline horizontalmente.

Os documentos do usuário continuam nos caminhos escolhidos. Dados de execução
do host ficam em `%LOCALAPPDATA%\AUREA\Desktop`.

## Compilar

Requisitos de desenvolvimento: Windows x64, Visual Studio C++ Build Tools,
SDK Windows, CMake, Ninja, NDK Android com `glslc` e um prefixo FFmpeg nativo.
O `android/local.properties` fornece o caminho do NDK. O script usa apenas
`engine/build/pc-beta001` e reutiliza os objetos existentes.

```powershell
python tools/build_aurea_pc.py --test --package --package-test
```

O prefixo padrão nesta máquina é
`build/astra-ffmpeg/host-dev/ffmpeg-8.1.2-full_build-shared`; pode ser substituído
com `--ffmpeg-prefix`. O pacote fica em `output/aurea-pc-beta-0.0.1` e no ZIP
correspondente. Logs e amostras de teste ficam em `build/validation/pc-beta001`.
O script não apaga builds, projetos ou entregas existentes.

```powershell
.\AUREA-PC.exe --self-test C:\caminho\para\resultados
```

O teste integrado cria suas próprias amostras: verifica transformações de tempo,
keyframes e navegação, imagem PNG com caminho Unicode, importação, save/load,
render Vulkan, vídeo H.264/AAC, decode de vídeo e áudio, exportação real de
composição e renderização 3D. O relatório é `desktop-tests.txt`; código de saída
zero indica aprovação. Isso não substitui os testes interativos da interface.

## Estado da validação

Em 13/10/2026, o executável Release compilou com MSVC x64. O teste integrado
passou **197 verificações, zero falhas**, com render Vulkan real numa NVIDIA
GeForce RTX 3050 e exportação de composição H.264/AAC em MP4 (30/30 quadros).
Incluiu PNG com nome Unicode e preservação exata dos canais, importação de
vídeo e áudio, criação/undo, navegação de keyframes, save/load e render 2D/3D.
O executável portátil também passou as mesmas 197 verificações com `PATH`
restrito a Windows/System32 e à pasta do pacote, sem DLLs do ambiente de build.

O teste visual interativo dos dois espaços de trabalho está **pendente**:
o usuário interrompeu Computer Use; a interação com janelas foi encerrada.
Os testes automatizados em modo `--self-test` não abrem o editor nem comprovam
o posicionamento visual e todos os gestos da interface.

A implementação de Windows é independente da compilação e validação nativa das
interfaces Android e iOS. Os resultados reais desta entrega são registrados no
relatório de validação em `build/validation/pc-beta001`; o funcionamento em outra
GPU ou máquina precisa ser verificado nela.

O FFmpeg de desenvolvimento disponível nesta máquina acompanha licença GPLv3;
as licenças das dependências seguem em `licenses/`. Esta é uma entrega local de
teste. Uma distribuição pública precisa preservar os requisitos das licenças
das bibliotecas efetivamente escolhidas para esse build.
