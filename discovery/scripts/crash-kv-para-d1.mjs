// Copia os relatórios de crash que ainda estão no KV (`crash:*`, até 30 dias)
// para um .sql que entra na tabela crash_reports (migração 0014).
//
//   npx wrangler kv key list --namespace-id <id> --remote --prefix "crash:" > chaves.json
//   node scripts/crash-kv-para-d1.mjs chaves.json ../build/backups/crash-kv.sql
//   npx wrangler d1 execute aurea-contas --remote --file ../build/backups/crash-kv.sql
//
// Só LÊ o KV (`wrangler kv bulk get`, 100 por vez). Os relatórios têm e-mails:
// o .sql fica em build/ (fora do git) e não sai da máquina. INSERT OR IGNORE:
// rodar de novo, ou depois que o Worker novo já gravou, não duplica nada.
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { gzipSync } from "node:zlib";
import { tmpdir } from "node:os";
import { join } from "node:path";

const NAMESPACE = "df8d88aef8db4746935268a0d4371233";
const [entrada, saida] = process.argv.slice(2);
if (!entrada || !saida) {
  console.error("uso: node scripts/crash-kv-para-d1.mjs <chaves.json> <saida.sql>");
  process.exit(2);
}

const chaves = JSON.parse(readFileSync(entrada, "utf8")).filter((k) => /^crash:[0-9]{13}-[0-9a-f]{16}$/.test(k.name));
const tmp = mkdtempSync(join(tmpdir(), "crash-kv-"));
const sql = (v) => (v === null || v === undefined ? "NULL" : `'${String(v).replace(/'/g, "''")}'`);
const linhas = [];
let pulados = 0;
try {
  for (let i = 0; i < chaves.length; i += 100) {
    const lote = chaves.slice(i, i + 100);
    const arquivo = join(tmp, "lote.json");
    writeFileSync(arquivo, JSON.stringify(lote.map((k) => k.name)));
    const bruto = execFileSync("npx", ["wrangler", "kv", "bulk", "get", arquivo, "--namespace-id", NAMESPACE, "--remote"],
      { encoding: "utf8", maxBuffer: 256 * 1024 * 1024, shell: process.platform === "win32", stdio: ["ignore", "pipe", "ignore"] });
    const valores = JSON.parse(bruto.slice(bruto.indexOf("{")));
    for (const k of lote) {
      const texto = valores[k.name];
      if (typeof texto !== "string") { pulados++; continue; }
      let r;
      try { r = JSON.parse(texto); } catch { pulados++; continue; }
      if (!r?.id || !r.installId || !r.reportId || !Number.isFinite(r.recebido)) { pulados++; continue; }
      const { email_enviado, email_via, email_erro, ...registro } = r;
      const dedupe = createHash("sha256").update(`${r.installId}|${r.reportId}`).digest("hex");
      const expira = Number.isFinite(k.expiration) ? k.expiration * 1000 : r.recebido + 30 * 86400_000;
      const gz = gzipSync(JSON.stringify(registro)).toString("hex");
      linhas.push(
        "INSERT OR IGNORE INTO crash_reports (id, dedupe, received_at, expires_at, platform, reason, app_build, device, " +
        `emailed, email_via, email_error, report_gz) VALUES (${sql(r.id)}, ${sql(dedupe)}, ${r.recebido}, ${expira}, ` +
        `${sql(r.platform ?? "")}, ${sql(r.reason ?? "")}, ${sql(r.appBuild ?? "")}, ` +
        `${sql(`${r.manufacturer ?? ""} ${r.deviceModel ?? ""}`.slice(0, 60))}, ${email_enviado ? 1 : 0}, ` +
        `${sql(email_via ?? null)}, ${sql(email_erro ?? null)}, X'${gz}');`);
    }
    process.stderr.write(`\r${Math.min(i + 100, chaves.length)}/${chaves.length}`);
  }
} finally {
  rmSync(tmp, { recursive: true, force: true });
}
writeFileSync(saida, linhas.join("\n") + "\n");
console.error(`\n${linhas.length} relatórios em ${saida} (${pulados} pulados)`);
