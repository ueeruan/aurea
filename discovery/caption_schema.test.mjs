import test from 'node:test';
import assert from 'node:assert/strict';
import {validatePreset, previewSvg} from './caption_schema.mjs';
const part = (kind, fields) => JSON.stringify({aurea_preset:1, kind, ...fields});
function fixture() {
  return {schema:1,minAppVersion:2113,name:'Legenda segura',width:1080,height:1920,
    caption:part('caption',{caption:{mode:0,style:0,maxWords:5,maxChars:32,maxLines:2}}),
    text:part('text',{style:{size:70,color:[1,1,1,1]},animators:[]}),
    effects:'',animation:'',transform:[.5,.8,0,1,1,1,0,0,0,1]};
}
test('accepts a private declarative preset without modifying its input',()=>{
  const p=fixture(), result=validatePreset(p); assert.deepEqual(result,p); assert.notEqual(result,p);
});
test('accepts effect keyframes but refuses expression sources and asset references',()=>{
  const p=fixture();
  p.effects=part('effects',{effects:[{key:'aurea.light.glow',params:[{src:1,ref:0}]}]});
  assert.doesNotThrow(()=>validatePreset(p));
  p.effects=p.effects.replace('"src":1','"src":2');assert.throws(()=>validatePreset(p));
  p.effects=part('effects',{effects:[{params:[{src:0,ref:123}]}]});assert.throws(()=>validatePreset(p));
});
test('refuses executable and local-file fields at arbitrary depths',()=>{
  for(const key of ['expression','fontPath','sourcePath','script','code','__proto__']) {
    const p=fixture();p.text=part('text',{style:JSON.parse(`{"${key}":"payload"}`)});
    assert.throws(()=>validatePreset(p),key);
  }
});
test('refuses unsupported versions, malformed components and oversized envelopes',()=>{
  for(const mutate of [p=>p.minAppVersion++,p=>p.schema=2,p=>p.text='{',p=>p.text=part('effects',{}),
    p=>p.width=0,p=>p.height=1.5,p=>p.transform[0]=Infinity,p=>p.name='x'.repeat(200000),
    p=>p.text=part('text',{style:{font:'https://example.test/payload'}})]) {
    const p=fixture();mutate(p);assert.throws(()=>validatePreset(p));
  }
});
test('escapes all text embedded in preview SVG',()=>{
  const svg=previewSvg('<script>&"\'');
  assert.ok(!svg.includes('<script>'));assert.ok(svg.includes('&lt;script&gt;&amp;&quot;&apos;'));
});
