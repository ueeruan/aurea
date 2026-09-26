# Aurea — requisitos da versão 2112

Requisitos de instalação obtidos da configuração do aplicativo. Não representam uma garantia de desempenho em aparelhos físicos.

| Plataforma | Requisito |
| --- | --- |
| Android | Android 8.0 (API 26) ou superior |
| Processador Android | ARMv7 de 32 bits ou ARM64 de 64 bits, usando o APK correspondente ao sistema do aparelho |
| GPU Android | OpenGL ES 3.1; o motor também verifica suporte a render targets de ponto flutuante no driver |
| Vulkan Android | Opcional para instalação; necessário em um driver compatível para o caminho de upscale por GPU. Sem ele, o upscale usa CPU |
| iPhone/iPad | iOS/iPadOS 16.3 ou superior, processador ARM64 e GPU com Metal |
| IPA | A compilação atual produz um IPA sem assinatura; é necessário assiná-lo para instalar |

## Memória e armazenamento

Ainda não foi estabelecido um mínimo de RAM validado em aparelhos físicos. Projetos com muitas camadas, vídeo de alta resolução, upscale e cenas 3D exigem mais memória. O limite de endereço de um processo de 32 bits também restringe projetos grandes.

É necessário espaço livre para os arquivos importados, cache, arquivos temporários e vídeo exportado. O tamanho do APK não representa o espaço total necessário para editar um projeto.

## Limites atuais

- A inferência de upscale no iOS permanece na CPU.
- O modelo de upscale é voltado a anime e ilustração; o tratamento temporal é um filtro conservador, não um modelo neural temporal.
- A fluidez sustentada, a compatibilidade de fontes no Samsung afetado e a aceitação em iPhone físico ainda precisam de validação. Não há um modelo mínimo de aparelho certificado nesta versão.

Fontes: `android/app/build.gradle.kts`, `android/app/src/main/AndroidManifest.xml`, `engine/platform/ios/Aurea.xcodeproj/project.pbxproj`, `engine/platform/ios/app/Info.plist` e a implementação do motor.
