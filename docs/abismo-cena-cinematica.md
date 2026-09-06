# ABISMO — cena de teste nativa do AUREA

Cena original estilizada de um explorador caindo num abismo de basalto. Sem impacto, violência gráfica ou vídeo usado como fundo. Geometria procedural, personagem com skinning e 17 ossos, poses FK, trajetória, fragmentos, iluminação e quatro câmeras ficam no projeto editável.

## Abrir e editar

Na versão compilada com este código: **Início → Modelos → ABISMO · Cinema 3D**. Cada abertura cria um projeto independente. Não exige download de modelos, imagens ou fontes.

O projeto também é instalado automaticamente na lista de projetos na primeira
abertura desta versão. A instalação grava uma confirmação separada: aberturas
seguintes não sobrescrevem edições e não restauram uma cópia apagada pelo usuário.
O modelo permanece disponível para criar novas cópias. Isso exige instalar um
IPA com o código atualizado; não modifica remotamente o app já instalado no iPhone.

- Selecionar a camada **ABISMO · cena 3D editavel** e reproduzir a timeline.
- **Cameras**: editar as quatro câmeras, lentes e tomadas.
- **Cena 3D → Objetos → Explorador · animar rig → Animar modelo / Rig**: escolher um osso, mover o tempo e ajustar sua pose. O preview acompanha a câmera ativa.
- **Cena 3D → Ambiente → Atmosfera / nevoa**: ajustar densidade, início e cor da névoa.

O arquivo `build/render/abyss/ABISMO-Aurea.json` é um TemplatePack independente, importável pelo comando de abrir modelo JSON. Requer o motor atualizado com ModelAsset3D e ModelMotion3D; o IPA antigo não contém essas mudanças.

## Sequência

| Tempo | Plano |
| --- | --- |
| 0–3,25 s | A borda: abertura com aproximação e início da queda, 28 mm. |
| 3,25–6,5 s | Travelling acompanhando a queda, 38 mm. |
| 6,5–10,5 s | Órbita próxima, personagem girando e câmera inclinando, 30 mm. |
| 10,5–14 s | Zenital: a câmera fica para trás e o personagem desaparece na profundidade, 26 mm. |

Composição 1920 × 804, 24 fps, 14 segundos. Preview MP4 em 1280 × 536, 336 quadros, **sem áudio**. Queda desacelerada para linguagem cinematográfica; não é uma simulação física em tempo real. As poses são avaliadas por tempo absoluto: voltar ou saltar na timeline não muda o resultado.

## Ajustes do motor feitos com este teste

- Roll da câmera passa a afetar o vetor vertical local de render, inclusive na vista zenital.
- Recorte nos planos próximo/distante preserva as partes visíveis de triângulos, UVs e cores interpoladas.
- Colisões nos baldes de profundidade são ordenadas exatamente; a escala do abismo não embaralha superfícies próximas do personagem.
- Névoa exponencial por distância, persistida, editável e aplicada à exportação. Com texturas, é aplicada depois da imagem e preserva a transparência.
- Opacidade de materiais sem iluminação respeitada.
- Geometria de modelos estáticos reutilizada entre quadros.
- Cortes e keyframes de todas as câmeras aparecem na timeline; o painel de rig usa a câmera ativa.

## Validação e limites

Testes cobrem rig, poses, persistência, independência de projetos, cortes, trajetória, recorte geométrico, ordenação, transparência da névoa e controles em larguras de 375 e 430 px. A renderização integral usa o próprio CompositionView/Scene3DPainter do app, sem renderizador externo para a cena.

O visual é uma demonstração 3D estilizada, não fotorrealista. A névoa é perspectiva atmosférica, não luz volumétrica. A ordenação por triângulos continua uma aproximação sem Z-buffer de GPU: interseções complexas entre superfícies ainda podem produzir artefatos. Não inclui IK, simulação de tecido ou física de corpos rígidos. Nenhum IPA foi gerado nesta entrega e não houve validação em iPhone físico.

## Reproduzir o render local

Em PowerShell, com Flutter no PATH:

```powershell
$env:AUREA_ABYSS_RENDER='1'
flutter test test/render_abyss_cinematic_test.dart --no-pub
ffmpeg -framerate 24 -i build/render/abyss/frame-%03d.png -c:v libx264 -crf 17 -pix_fmt yuv420p -movflags +faststart build/render/abyss/ABISMO-Aurea.mp4
```

Para conferir somente alguns quadros, definir `AUREA_ABYSS_FRAMES` com índices separados por vírgulas. O flag de render é opcional; os testes de domínio, controles e pipeline rodam sem ele.
