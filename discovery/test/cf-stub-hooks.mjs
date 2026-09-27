// Resolve `cloudflare:workers` para um stub mínimo (só nos testes do Node).
export async function resolve(spec, ctx, next) {
  if (spec === "cloudflare:workers") return { url: "data:text/javascript,export class DurableObject { constructor(ctx, env) { this.ctx = ctx; this.env = env; } }", shortCircuit: true };
  if (spec === "cloudflare:email") return { url: "data:text/javascript,export class EmailMessage { constructor(from, to, raw) { this.from = from; this.to = to; this.raw = raw; } }", shortCircuit: true };
  return next(spec, ctx);
}
