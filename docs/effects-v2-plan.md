# Motor de efeitos V2 - execução

Pedido: refazer os efeitos com referência no After Effects, adicionar guia curto
no app e entregar IPA antes de iniciar APK. Não executar builds simultâneos.

1. Auditoria: catálogo de 43 efeitos; aproximações incorretas em Posterize,
   Levels/Gamma, Curves, Vibrance; controles de Deep Glow não consumidos.
2. Implementação: backend por pixel, contratos explícitos de parâmetros,
   alfa premultiplicado, kernels determinísticos e caminhos de compatibilidade.
3. Guia offline: primeiros passos, animação, efeitos, exportação e referência
   de todo o catálogo; acesso em Sobre e no editor de efeitos.
4. Verificação: testes numéricos e de renderização, regressão e documentação
   exata da cobertura. Igualdade integral com AE não será afirmada sem pares
   de referência equivalentes e comparação mensurada.
5. Release sequencial: publicar código autorizado no repositório privado,
   compilar/verificar/entregar IPA; só então compilar/verificar/entregar APK.

Preservar alterações locais anteriores e formatos de projetos. Não copiar
código/binários de plugins Adobe/Cycore nem incluir recursos não licenciados.
AE 26.0x67 está instalado; verificar referência por script sem tocar projetos
do usuário. Builds continuam separados dos testes de referência.
