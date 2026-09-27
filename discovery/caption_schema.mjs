const fail = message => { throw new Error(message); };
const object = value => value && typeof value === 'object' && !Array.isArray(value);
export const MAX_PRESET_BYTES = 128 * 1024;
// A declarative preset only: never HTML, JavaScript, expressions, URLs or files.
export function validatePreset(input) {
  if (!object(input) || new TextEncoder().encode(JSON.stringify(input)).length > MAX_PRESET_BYTES) fail('preset_size');
  const allowed = new Set(['schema','minAppVersion','name','width','height','caption','text','effects','animation','transform']);
  if (Object.keys(input).some(k => !allowed.has(k)) || input.schema !== 1 || input.minAppVersion !== 2113) fail('preset_schema');
  if (typeof input.name !== 'string' || !input.name.trim() || input.name.length > 80 || /[\x00-\x1f<>]/.test(input.name)) fail('preset_name');
  for (const key of ['width','height']) if (!Number.isInteger(input[key]) || input[key] < 1 || input[key] > 16384) fail('preset_dimensions');
  if (!Array.isArray(input.transform) || input.transform.length !== 10 || input.transform.some(n => !Number.isFinite(n) || Math.abs(n) > 10000)) fail('preset_transform');
  let nodes = 0;
  function walk(value, depth = 0) {
    if (++nodes > 16000 || depth > 14) fail('preset_complexity');
    if (typeof value === 'number' && (!Number.isFinite(value) || Math.abs(value) > 1e7)) fail('preset_number');
    if (typeof value === 'string' && (value.length > 1024 || /(?:https?:|file:|javascript:|data:|<script|<svg)/i.test(value))) fail('preset_string');
    if (Array.isArray(value)) { if (value.length > 2048) fail('preset_array'); value.forEach(v => walk(v,depth+1)); }
    else if (object(value)) for (const [key, child] of Object.entries(value)) {
      if (['__proto__','constructor','prototype','expression','expressionEnabled','fontPath','pathLayer','sourcePath','script','code'].includes(key)) fail('preset_executable_or_file');
      if (key === 'ref' && child !== 0) fail('preset_external_reference');
      // Native ParamSource: constant=0, keyframes=1; expressions stay forbidden.
      if (key === 'src' && child !== 0 && child !== 1) fail('preset_external_reference');
      walk(child,depth+1);
    }
  }
  for (const kind of ['caption','text','effects','animation']) {
    if (typeof input[kind] !== 'string' || (!input[kind] && ['caption','text'].includes(kind))) fail('preset_component');
    if (!input[kind]) continue;
    const data = JSON.parse(input[kind]);
    if (!object(data) || data.aurea_preset !== 1 || data.kind !== kind) fail('preset_kind');
    walk(data);
    if (kind === 'caption') {
      if (!object(data.caption)) fail('caption_options');
      const c = data.caption;
      for (const [key,min,max] of [['mode',0,1],['style',0,5],['maxWords',1,20],['maxChars',4,80],['maxLines',1,4]])
        if (c[key] !== undefined && (!Number.isInteger(c[key]) || c[key] < min || c[key] > max)) fail('caption_range');
    }
    if (kind === 'effects' && (!Array.isArray(data.effects) || data.effects.length > 16)) fail('effects_limit');
    if (kind === 'text' && (!object(data.style) || (data.animators?.length ?? 0) > 16)) fail('text_style');
  }
  return structuredClone(input);
}
export function previewSvg(name) {
  const safe = name.replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&apos;'}[c]));
  return `<svg xmlns="http://www.w3.org/2000/svg" width="480" height="270"><rect width="480" height="270" rx="24" fill="#161820"/><text x="240" y="145" text-anchor="middle" fill="#ffe45c" font-family="sans-serif" font-size="28">${safe}</text></svg>`;
}
