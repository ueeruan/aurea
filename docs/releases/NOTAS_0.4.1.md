# Aurea Beta 0.4.1 — 0.4.0 Fase 2 (build 2205) · 28/10/2026

Android (AAB, Google Play) e iPhone (IPA). Mesmas mudanças nas duas plataformas.

## Interface nova (mockup "interface 03", de Hanszx)
- **Tema AMOLED** (preto puro, azul iOS) como padrão; os outros temas continuam em Ajustes.
- **Barra de reprodução** com os ícones do mockup: ↶ ↷ · ⏮ ▶ ⏭ · marcador, copiar e painel.
  O ▶ fica sem círculo e, tocando, o ⏸ gira pelas cores do arco-íris.
- **Dock flutuante**: pílula com Forma, Mídia, Áudio, Texto e Elemento e o "+" redondo que abre a
  grade de 8; o resto da tela escurece.
- **Gavetas em cartão** flutuante, com botões em duas colunas e ícones em azul.
- **Inspetor em folha**: cantos arredondados, puxador, e a ferramenta entra deslizando.
- **Palco**: grade, "olho" (esconde contorno e alças) e zoom "100%" flutuando no canto.
- **Mover e transformar**: a roda de rotação com trilha tracejada e ponteiro azul.
- **Cor e preenchimento**: 7 cores rápidas.
- **Timeline**: o tempo numa ficha azul e um contorno fino na cor de cada clipe.
- O painel da camada mostra só as ferramentas que ela tem, sem buracos.

## Novidades
- **Aurora em Elementos**: camada de ajuste 3D com o efeito Aurora já aplicado.
- **Entrar com o Google** (Firebase Auth, projeto `aurea-8e5f1`): o mesmo e-mail abre a mesma
  conta Aurea; e-mail novo cria a conta. Servidor: `POST /api/auth/firebase` (Worker `f93429da`).
- **Conta obrigatória** de novo, como até a 0.1.4, com o selo "8 mil+ membros".
- **AureaHyper pago**: o servidor está em modo "entitled"; só as contas liberadas usam.

## Correções
- **Remapear tempo**: a curva é a da coluna esquerda (o botão próprio na linha saiu).

## Validação
- Servidor: 83 testes (inclui a conferência do token do Firebase); publicado e conferido no ar.
- Motor: teste novo da Aurora (camada de ajuste 3D com o efeito, um desfazer).
- Android: cada tela conferida no emulador (x86_64), menos o que exige login (Remapear tempo e o
  botão do Google em uso real).
- iPhone: o IPA do GitHub Actions é a primeira compilação das mudanças em Swift.
- **Não testado em aparelho**: o login com o Google (Android e iPhone).

## Pendências
- Play: copiar a SHA-1 da "assinatura de apps" (Play Console › Integridade do app) para o app
  `com.aurea.editor` no Firebase; sem ela, o Google recusa o login no app baixado da Play.
- O selo mostra "8 mil+"; o servidor conta 3.537 contas.
