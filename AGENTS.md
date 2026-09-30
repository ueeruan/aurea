# Android e iOS

Toda mudança funcional ou de interface no AUREA deve ser implementada nas duas
plataformas. Regra solicitada pelo usuário em 29/09/2026.

- Coloque regras de edição no motor C++ compartilhado; as interfaces controlam o motor.
- Atualize Android/Kotlin e iOS/Swift no mesmo trabalho, inclusive textos e acessibilidade.
- Registre separadamente implementação, compilação e validação em aparelho. Um teste
  do motor ou uma auditoria estática não comprova execução nativa das duas interfaces.
- Use referências externas para especificar comportamento. Não incorpore código,
  shaders ou assets extraídos de outro aplicativo no produto.
- Guarde APKs extraídos e resultados brutos de inspeção somente em `build/reference/`.
