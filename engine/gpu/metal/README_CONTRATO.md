# Backend Metal — contrato para a SESSÃO A2 (camada iOS)

1. **Device — o caminho escolhido é `set_device`.** `new aurea::mtl::Backend()`; se o device
   não for o padrão, `set_device(id<MTLDevice>)` ANTES de `initialize(config)`. Canônico:
   `GPUBackend* b = aurea::mtl::create_backend(device);` (device `nullptr` = padrão do sistema).
   `create_backend` NÃO inicializa: quem chama faz `initialize`, igual ao Vulkan.
2. **Slots.** O índice Metal é o binding do Vulkan (tabela em `MetalBackend.hpp`, `namespace slot`):
   texturas 0..11 (e samplers 0..11), uniform 12, imagens de armazenamento 13/14, storage 15/16,
   push constants no **buffer 30**. Um slot não amarrado recebe o recurso de reserva.
3. **Shaders.** O blob é MSL (texto ou `.metallib`), não SPIR-V: ver `msl_glue.md`. Entrada
   `vs_main`/`fs_main`/`cs_main`; blob de compute PRECISA do cabeçalho com o tamanho do grupo.
4. **Cache de pipeline.** `MTLBinaryArchive` (iOS 14+/macOS 11+) guardado por `pipelineCacheTag`
   no cabeçalho `AUREAMTL` + FNV-1a. Abaixo do iOS 14 o backend reporta `Load::None` — cache
   vazio honesto, nunca mentira.
5. **Vídeo externo (YCbCr).** `ExternalTexture::rgb = true` e `sampler` inválido: o formato
   `MTLPixelFormat*YpCbCr*` do buffer já converte na amostra (matriz BT.601 do formato). Devolver
   `rgb = false` faria o shader aplicar a matriz duas vezes. Consequência: o cache de pipeline do
   motor usa um pipeline único por formato de vídeo (o `immutableSampler0` é ignorado). A matriz
   do arquivo (BT.709/BT.2020) não é honrada pelo sampler — o caminho exato para isso é pedir
   `kCVPixelFormatType_32BGRA` ao VideoToolbox (o backend importa BGRA direto).
6. **Integração no CMake:** uma linha em `engine/CMakeLists.txt`, depois do bloco `aurea_vulkan`:
   `add_subdirectory(gpu/metal)` — a guarda de plataforma está dentro do arquivo.
7. **Não verificado aqui:** este host é Windows, sem Xcode — nenhum `.mm` foi compilado. Verificado:
   `CommandList` 24/24, `GPUBackend` 42/42 (nome, contagem e tipos de parâmetros) e
   `MetalBackend.hpp` compilando em C++23 no host (`/Zs`, MSVC).
