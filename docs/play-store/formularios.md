# Aurea na Google Play — respostas dos formulários do Play Console

Versão: 0.2.6 (versionCode 2194). Escrito a partir do código em 19/10/2026 (fluxos
de dados levantados arquivo por arquivo). Quando o código mudar, revise.

URLs (funcionam depois do deploy do Worker, ver README.md):
- Política de privacidade: `https://aurea-ai-discovery.aureaapp.workers.dev/privacidade`
- Exclusão de conta: `https://aurea-ai-discovery.aureaapp.workers.dev/excluir-conta`

---

## 1. Segurança dos dados (Data safety)

**O app coleta ou compartilha algum dos tipos de dados exigidos?** Sim.
**Todos os dados coletados são criptografados em trânsito?** Sim (só HTTPS;
`usesCleartextTraffic="false"`).
**Você oferece uma forma de os usuários pedirem a exclusão dos dados?** Sim — no
app (Início → menu → Excluir conta) e pela URL de exclusão acima.

"Compartilhado" no sentido do Play = enviado a terceiros que NÃO agem em nome do
app. Cloudflare (servidor), Resend (e-mail ao desenvolvedor) e 8Scale (geração do
Vídeo com IA) são prestadores de serviço: o que vai para eles conta como
**coletado**, não compartilhado. Os SDKs de anúncio (Unity LevelPlay/ironSource,
Unity Ads, Google Mobile Ads/UMP) contam como **compartilhamento**.

| Categoria → tipo | Coletado | Compartilhado | Opcional? | Finalidades |
|---|---|---|---|---|
| Informações pessoais → **Endereço de e-mail** | Sim | Não | Sim (conta opcional) | Funcionalidade do app, Gerenciamento de conta |
| Informações pessoais → **IDs do usuário** (ID da conta) | Sim | Não | Sim | Funcionalidade do app, Gerenciamento de conta |
| Fotos e vídeos → **Fotos** | Sim | Não | Sim | Funcionalidade do app (foto de perfil; imagem inicial do Vídeo com IA; mídias dentro de projetos publicados na Comunidade) |
| Fotos e vídeos → **Vídeos** | Sim | Não | Sim | Funcionalidade do app (projetos publicados na Comunidade podem conter vídeos) |
| Arquivos e documentos | Sim | Não | Sim | Funcionalidade do app (arquivos de projeto e presets publicados) |
| Atividade no app → **Interações no app** (curtidas, seguidores, bloqueios, denúncias) | Sim | Não | Sim | Funcionalidade do app |
| Atividade no app → **Outros conteúdos gerados pelo usuário** (publicações, comentários, bio, prompts do Vídeo com IA) | Sim | Não | Sim | Funcionalidade do app |
| Informações e desempenho do app → **Registros de falhas** | Sim | Não | **Não** (automático com a conta conectada) | Análise (diagnóstico de falhas) |
| Informações e desempenho do app → **Diagnóstico** (modelo, fabricante, versão do sistema, CPU) | Sim | Sim (SDKs de anúncio) | Não | Análise; Publicidade (SDKs) |
| Local → **Local aproximado** (país pela conexão nos relatórios; SDKs pelo IP) | Sim | Sim (SDKs de anúncio) | Não | Análise; Publicidade ou marketing (SDKs) |
| IDs do dispositivo ou outros IDs (ID de instalação aleatório, App Set ID, ID de publicidade) | Sim | Sim (ID de publicidade com os SDKs) | Não | Funcionalidade do app; Prevenção de fraudes, segurança e conformidade; Publicidade ou marketing |

NÃO coletados: nome real, endereço, telefone, raça, religião, orientação,
informações financeiras, saúde, mensagens, contatos, agenda, histórico de
navegação, áudio (o "Gravar voz" fica no aparelho), localização precisa.

Processamento efêmero: as legendas automáticas, o recorte com IA, o mapa de
profundidade e a ampliação rodam no aparelho; nada disso é enviado.

Antes de enviar, confira o que cada SDK declara (as respostas acima somam o
mínimo conhecido):
- Unity LevelPlay/ironSource e Unity Ads: página "Google Play Data safety" da Unity.
- Google Mobile Ads / UMP: página "Data disclosure" do Google Mobile Ads SDK.

## 2. Anúncios

- **O app contém anúncios?** Sim (intersticial depois de exportar, no máximo 1 a
  cada 30 min; com recompensa antes do Vídeo com IA; sem banners).
- **ID de publicidade:** Sim. Finalidades: Publicidade ou marketing; Análise;
  Prevenção de fraudes, segurança e conformidade. (Declarado no manifesto:
  `com.google.android.gms.permission.AD_ID`.)

## 3. Acesso ao app (App access)

"Todo ou parte do app é restrito": **Parte** — a Comunidade e o Vídeo com IA
pedem login (a conta é gratuita e criada no próprio app).

Instruções para a revisão (preencha a conta de teste; crie-a você mesmo, não use a
sua conta pessoal e não deixe a senha no repositório):
1. Abra o app, aceite a tela de privacidade e termos.
2. Início → menu (⋮) → Entrar → e-mail `[CONTA DE TESTE]` / senha `[SENHA]`.
3. A Comunidade é a aba "Comunidade" na barra de baixo da tela inicial. O Vídeo
   com IA fica no editor (abra um projeto → Adicionar → Mídia → Vídeo com IA) e
   mostra um anúncio com recompensa antes de gerar.

## 4. Classificação do conteúdo (IARC)

- Categoria: Utilidades/Produtividade/Comunicação/Outro → **Todos os outros tipos de app**.
- Violência, sexo, linguagem, drogas, apostas: **Não** (o app não contém; é uma
  ferramenta de edição).
- **Os usuários podem interagir ou trocar conteúdo?** Sim (Comunidade: publicações
  públicas e comentários).
- **Compartilha a localização do usuário com outros usuários?** Não.
- **Permite compras de produtos digitais?** Não.
- **Conteúdo gerado por IA?** Sim (Vídeo com IA).

## 5. Público-alvo e conteúdo

- Faixas etárias: **16–17 e 18+** (recomendado). Não marque menores de 13: o app tem
  Comunidade pública, anúncios e IA generativa (evita a política de Famílias).
- "O app pode atrair crianças?" **Não**.

## 6. Declarações de permissão

**Fotos e vídeos (READ_MEDIA_IMAGES / READ_MEDIA_VIDEO):**
> O Aurea é um editor de vídeo e motion graphics. A galeria do editor (Adicionar →
> Mídia) mostra fotos e vídeos do aparelho para a pessoa montar a timeline,
> escolher vários itens de uma vez e ver a duração dos vídeos antes de importar —
> é o uso principal do app. A permissão é pedida só quando a galeria abre; o
> seletor do sistema continua disponível sem permissão.

**Câmera:** usada só na Câmera Real (rastreamento de posição com ARCore, opcional),
pedida quando a pessoa liga o recurso. Nenhuma imagem é enviada.

**Microfone:** usado só no "Gravar voz" (narração), pedido quando a pessoa toca em
Gravar. O áudio fica no projeto, no aparelho.

Sem permissões de localização, contatos, SMS, chamadas, acessibilidade, instalação
de pacotes, alarmes exatos, sobreposição ou serviço em primeiro plano.

## 7. Outras declarações de "Conteúdo do app"

- App de notícias: Não. · Apps de saúde: Não. · Governo: Não.
- Recursos financeiros: Nenhum. (Na build da Play não há doação por PIX/PayPal.)
- **IA generativa:** Sim — Vídeo com IA (texto/imagem para vídeo, fornecedor
  8Scale). Denúncia dentro do app: no resultado ("Denunciar vídeo"); a geração passa
  pelo filtro do fornecedor.
- **Conteúdo gerado pelo usuário:** Comunidade com publicações, comentários e
  perfis; presets de legenda públicos. No app: denunciar post, comentário, perfil e
  preset; bloquear perfis; conteúdo denunciado por 3 pessoas sai do feed até a
  revisão; o admin remove qualquer conteúdo; as denúncias chegam por e-mail ao
  desenvolvedor. Termos aceitos na primeira abertura (uso legal, responsabilidade
  pelo conteúdo).
- **Exclusão de conta:** no app e pela URL de exclusão.
