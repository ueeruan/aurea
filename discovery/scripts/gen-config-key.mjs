// Gera o par de chaves ECDSA P-256 que assina /api/app-config (appconfig.js).
//
//   node scripts/gen-config-key.mjs <arquivo-da-chave-privada>
//
// Grava a chave PRIVADA (PKCS8, base64, uma linha) no arquivo pedido — ele é o
// valor do secret AUREA_CONFIG_SIGNING_KEY e NUNCA vai para o git — e imprime
// só a PÚBLICA (SPKI, base64), que vai embutida nos apps. Recusa sobrescrever
// um arquivo que já existe (trocar a chave exige um app novo com a pública nova).
import { webcrypto } from "node:crypto";
import { existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, resolve } from "node:path";

const destino = process.argv[2];
if (!destino) {
  console.error("uso: node scripts/gen-config-key.mjs <arquivo-da-chave-privada>");
  process.exit(2);
}
const caminho = resolve(destino);
if (existsSync(caminho)) {
  console.error("ja existe: " + caminho + " (apague a mao se quer mesmo trocar a chave)");
  process.exit(1);
}
const par = await webcrypto.subtle.generateKey({ name: "ECDSA", namedCurve: "P-256" }, true, ["sign", "verify"]);
const privada = Buffer.from(await webcrypto.subtle.exportKey("pkcs8", par.privateKey)).toString("base64");
const publica = Buffer.from(await webcrypto.subtle.exportKey("spki", par.publicKey)).toString("base64");
mkdirSync(dirname(caminho), { recursive: true });
writeFileSync(caminho, privada + "\n", { mode: 0o600 });
console.log("chave privada gravada em: " + caminho);
console.log("chave publica (SPKI base64, embutir nos apps):");
console.log(publica);
