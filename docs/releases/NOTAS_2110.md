# Aurea beta — build 2110

## Novidades

- Graph Editor junto à timeline, alças maiores e famílias reais Bounce, Elastic e 4 Steps.
- Upscale IA 2x/4x na exportação para anime/ilustração, com cancelamento.
- Cena 3D com câmeras, luzes, nulos e materiais por objeto. A câmera de navegação não altera a exportação.
- Glitchify ampliado: pixelização, escala RGB, blocos, rasgos horizontais, flicker e ordenação local de pixels.
- Home reorganizada e opções de apoio voluntário.

## Correções

- Autosave de projetos novos/vazios e salvamento das alterações antes de fechar.
- Exportação Android: corrigido o deslocamento duplicado dos pacotes do encoder; erros preservam detalhes da plataforma.
- Time Remap agora edita a curva usada no playback/exportação. Curvas antigas são migradas com dados de recuperação; freeze, reverso e presets de velocidade corrigidos.
- Preview reutiliza uploads do mesmo frame e recupera falhas de alocação. TimeWarpRGB reaproveita frames vizinhos e evita seeks desnecessários em VFR; a exportação que parava foi retestada até o fim.
- Texto recorre à fonte padrão para caracteres ausentes na fonte escolhida.
- Texto 3D novo começa estático; campos numéricos preservam a digitação. Luzes e materiais animados afetam a renderização.
- Nulos 3D expõem os eixos XYZ; animações de profundidade afetam os filhos e persistem após reabrir o projeto.
- Busca VFR no iOS preserva timestamps exatos; testes nativos de CPU e IOSurface passaram no simulador.

## Testes realizados

- **721 testes do core e 182 GLES no emulador**, sem falhas, antes dos últimos ajustes de curvas/fontes. Testes focados posteriores: 3 de easing no host, 1 no GLES e 58 de texto/Text3D/GPU aprovados. **103 testes Android JVM** aprovados.
- Exportações reais H.264 1080p horizontal/vertical com AAC e HEVC 720p; comparação independente de pacotes/vídeo/áudio. Proxy VFR: 63 frames, 960×540.
- Reteste no app do projeto com texto e TimeWarpRGB: 63/63 frames em H.264 720p, 21 fps, 3 s; arquivo completo decodificado sem erro, mantendo o efeito e sem ampliar o timeout.
- Upscale pelo app: 63 frames, 320×180 H.264, decodificados sem erro. O clipe não tinha áudio; áudio e cancelamento foram testados separadamente no pipeline compartilhado.
- Cena criada pela UI Android: Texto 3D parentado ao Nulo, keyframes Z=0/1000, reabertura após encerrar o processo e exportação 720p de 300 frames. O vídeo decodificado mantém a animação de profundidade.
- Simulador iOS: 25 cenários de abertura/renderização, exportação H.264 com AAC e verificações de decode/busca em vídeos regulares, VFR e com B-frames passaram. Isso não substitui testes em iPhone físico.

## Pendências e limites

- **O rebuild P0–P10 não está concluído.** Faltam cobertura em Android/iPhone físicos, celulares fracos e sessões longas. Pacotes finais e QA das interfaces serão registrados no manifesto de entrega.
- Upscale usa CPU e modelo de anime/ilustração: pode ser lento e oscilar entre frames. Sem inferência GPU, modelo temporal ou geral para fotografias.
- Tracking de câmera/objetos, estabilização, optical flow, motion blur, partículas e 3D/PBR completo ainda exigem aceitação mais ampla com cenas reais nas duas plataformas.
- Vídeo enorme ao trocar qualidade não foi reproduzido; não está declarado resolvido. Fontes ainda precisam de reteste no Samsung afetado. A falha interna Unity/WebView após exportar permanece sem correção comprovada.
- O teste nativo do iOS reproduziu o travamento ao mover o vídeo pela seta; correção ainda em investigação. Seis dos oito testes de gestos passaram. O teste de efeitos parou no nome acessível do botão, corrigido no código e aguardando reteste de adicionar/fechar/desfazer.

Evidências: [checkpoint técnico](../architecture/AUREA_REBUILD_CHECKPOINT.md).
