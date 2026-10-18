# Verificação das referências 3D — 13/10/2026

Os quatro vídeos de referência e o build 2183 pertencem a outro aplicativo,
conforme esclarecido pelo usuário. Não há projeto editável que permita repetir
seus ajustes exatos. A inspeção REA identifica os artefatos e os frames observados;
ela não prova a implementação interna nem uma falha no AUREA. Os dados brutos
permanecem em `build/reference/beta3-followup-3d-20261013/`.

Foram criadas três cenas próprias no AUREA, sem imagens, código ou shaders
extraídos dos vídeos. Somente `engine/tests/Reference3DRegression.inl` e sua
inclusão em `engine/tests/test_gpu.cpp` mudaram nesta investigação. Nenhuma
alteração de produção foi necessária: os três casos passaram.

| Cena própria | Evidência obtida |
| --- | --- |
| Cubo e pirâmide com textura colorida, rotação animada e motion blur | A textura permanece em cinco frames de cada objeto, diretamente e dentro de uma pré-composição. Os dez pares direto/pré-composição são idênticos pixel a pixel. O blur realmente altera o resultado: 2.641 pixels do cubo e 1.005 da pirâmide diferem por mais de três níveis RGB do frame sem blur. |
| Null 3D girando três filhos com Z local −60, 0 e +70 | Matrizes coincidem com uma rotação Y calculada independentemente, nos ângulos 0°, 35°, 75° e 145°. Os centros renderizados ficam a menos de 0,2 pixel da projeção analítica. |
| Dois planos sobrepostos sob Null animado | O plano vermelho está à frente em 0°/35°; o azul fica à frente em 145°/180°. As oito combinações com e sem blur mantêm essa ordem. |

O teste final `Reference3DGpu` executou **3 casos e 304 verificações, sem falhas
ou skips**, no backend Vulkan do Windows. O primeiro ensaio, mais fraco, também
passou 3/250; seu log foi preservado e não integra uma contagem adicional.
O manifesto registra hashes do executável, da fixture, dos logs e dos 21 PNGs
próprios:
`build/validation/reference-3d-20261013/reference3d-before-manifest.json`.

Executável desse ensaio:
`902da5bd82bae094ccb298efaec8290203a4d9f1b954bcc3f7835934b62e09aa`.
Fixture:
`b6e06b0826be3f0f678f212f757318973d405cf20ef0212bdc7fa16c6cc1f16e`.

Esses resultados demonstram o comportamento das cenas reconstruídas antes da
nova extrusão. Não identificam a causa dos vídeos externos, não comprovam todas
as configurações possíveis de textura/pré-composição e não substituem testes
nativos Android/iOS ou em aparelhos afetados. A execução das novas interfaces de
extrusão será validada separadamente após sua implementação compartilhada.
