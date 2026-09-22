# Aurea V2 — O que falta para a release

Lista viva (Fase 8I, §158, §180–190, §195). Marcado só o que foi feito E
conferido; o resto diz por que não está.

## Feito na 8I
- [x] Build de release gerável: `cd android && ./gradlew :app:assembleRelease` (arm64-v8a + armeabi-v7a). Sem `android/key.properties` assina com a chave de debug — a mesma do Aurea oficial instalado.
- [x] R8 + encolher recursos ligados; regras JNI em `android/app/proguard-rules.pro`, conferidas no dex do APK (179/179 nativos e os 2 callbacks do C++).
- [x] `.so` só com os símbolos JNI exportados (`-fvisibility=hidden`).
- [x] APK auditado: 11,01 MB (era 23,09). Sem assets de teste. Prévia de efeito no tamanho do cartão.
- [x] Abertura: 16 pipelines (eram 40); efeito e 3D compilados quando o projeto os usa, fora do playback; cache de pipeline versionado, conferido e à prova de crash do driver.
- [x] Nada de rede fora do toque em "Gerar legendas". Nenhum log por quadro no caminho normal.
- [x] Nenhum botão "em breve" no app; painéis inalcançáveis e permissões sem recurso removidos.

## Bloqueado neste ambiente (não dá para fechar daqui)
- [ ] **iOS**: sem Mac na bancada — sem build, sem Metal, sem Instruments, sem IPA. O motor é o mesmo C++; a camada iOS não foi compilada nesta fase.
- [ ] **Aparelhos Android reais** (§162): nenhum físico na bancada. Falta medir no celular: abertura a frio e a quente (a linha `abertura do motor:` do logcat já traz o número por etapa), primeira compilação de pipeline em Mali/Adreno/PowerVR, tamanho instalado real (`adb shell dumpsys package com.aurea.aurea | grep -i size` ou Ajustes › Apps), temperatura, sessões longas.
- [ ] Rodar o APK de **release** (R8 ligado) num aparelho ao menos uma vez antes de publicar: importar vídeo (usa `openContentFd` pelo JNI), importar imagem (`decodeImage`), exportar, abrir o navegador de efeitos duas vezes (a 2ª deve vir do disco), gerar legenda.
- [ ] Camada de validação Vulkan: não instalada no host nem no Android (baixar pede o dono).

## Decisões do dono antes de publicar
- [ ] **Chave de release**: hoje a "release" sai com a chave de debug da máquina do dono (é o que faz atualizar por cima do Aurea oficial). Para loja, definir a chave de upload/Play App Signing — e guardar o `mapping.txt` de cada build publicado (sem ele, o stack trace de um crash de release fica ofuscado).
- [ ] **APK por ABI ou AAB**: o APK universal leva arm64 + armv7 (11,0 MB). Um aparelho arm64 com APK por ABI baixaria ~7,2 MB. AAB na loja faz isso sozinho; para instalação direta, gerar dois APKs (`-PaureaAbi=arm64-v8a` / `armeabi-v7a`).
- [ ] `versionCode`/`versionName` da publicação (hoje 2102 / 2.0.0-beta1).

## Pequenos, sem risco, ainda abertos
- [ ] Fonte de ícones Cupertino inteira no APK (252 KB crua, 113 KB no APK); o app declara 149 dos 1.257 glifos da fonte. Subconjunto pede `fonttools` (não instalado; sem download nesta fase).
- [ ] Ícone e splash em PNG (≈ 180 KB somando as densidades); WebP sem perda economizaria ~30%. Mexe na identidade da marca — decisão do dono.
- [ ] Laço de status (Choreographer) roda a cada vsync também na Home (8H/8D).
- [ ] Stream AAudio aberto na abertura do motor; poderia abrir no primeiro play (frente de áudio).
- [ ] ~15 wrappers JNI sem chamador em `AureaEngine`/`CommandBatch` (o R8 já tira do dex; o C++ continua na .so).
