# Discovery — por que o endereço muda sem APK novo

O túnel `trycloudflare` ganha um nome novo a cada sessão do Colab. Se esse
endereço estivesse compilado, cada reinício pediria um APK e um IPA novos.

Não está. O que o app conhece é **um endereço fixo**:

```
https://aurea-ai-discovery.aureaapp.workers.dev/server
```

Ele responde o documento abaixo. É de lá que sai o endereço do momento:

```json
{
  "online": true,
  "endpoint": "https://xxxxx.trycloudflare.com",
  "model": "MiniMax-H3",
  "gpu": "NVIDIA A100-SXM4-80GB",
  "capabilities": ["text_to_video", "image_to_video"],
  "updatedAt": 1750000000
}
```

## O caminho de uma conexão

```
Aurea
  → GET discovery fixo          (sempre o mesmo endereço)
  → recebe online + endpoint
  → GET {endpoint}/system_stats
  → 200 + JSON com "system"
  → "Aurea AI • Online"
```

**Online só depois do `/system_stats`.** O documento pode estar velho — o Colab
caiu e a última batida ficou. Um 200 de portal de wifi não passa: o corpo
precisa ter `system`.

A `BASE_URL` compilada é só a **muda de arranque**: entra quando o discovery
ainda não publicou nada, para o app não ficar sem nenhum endereço. Nunca tem
prioridade sobre o discovery.

## O Colab precisa publicar

O que falta no START: depois de validar `/system_stats`, contar ao mundo o
endereço de agora.

```python
from publicar_discovery import publicar
publicar("https://xxxxx.trycloudflare.com", gpu="NVIDIA A100-SXM4-80GB")
```

A função espera o DNS resolver, espera o `/system_stats` passar, e só então
publica. Ao desligar, `marcar_offline()` faz o app mostrar "Offline" na hora, em
vez de esperar a batida vencer.

## A credencial que falta

**Uma só:**

| nome | onde vive | para quê |
|---|---|---|
| `AUREA_DISCOVERY_SECRET` | Colab (env) **e** Worker (`wrangler secret put`) | publicar o endereço |

Ela **não** entra no APK nem no IPA. O app não tem segredo nenhum: ele só lê o
documento, que é público.

O Worker do repositório (`discovery/worker.js`) já usa essa variável. Para
publicar:

```bash
cd discovery
npx wrangler kv namespace create AUREA_KV
npx wrangler secret put AUREA_DISCOVERY_SECRET
npx wrangler deploy
```

O nome do Worker e o da conta precisam continuar `aurea-ai-discovery` e
`aureaapp`: o endereço `https://aurea-ai-discovery.aureaapp.workers.dev/server`
é a única coisa compilada no app que não muda.

## O que NUNCA é publicado

- o segredo (`AUREA_DISCOVERY_SECRET`);
- token administrativo ou de cliente;
- caminho do Colab, IP, porta do ComfyUI.

O documento tem só o que a tela mostra: se está no ar, onde, qual GPU, o nome do
modelo e o que ele sabe fazer.
