// Run only against an isolated local wrangler dev instance; never writes to production.
import assert from 'node:assert/strict';
const base='http://127.0.0.1:8794';
const ip=`192.0.2.${Math.floor(Math.random()*250)+1}`;
async function call(path,{token,body,method='GET',status=200}={}) {
  const r=await fetch(base+path,{method,headers:{'content-type':'application/json','CF-Connecting-IP':ip,...(token?{authorization:`Bearer ${token}`}:{})},body:body===undefined?undefined:JSON.stringify(body)});
  const data=await r.json(); assert.equal(r.status,status,JSON.stringify(data));return data;
}
const part=(kind,data)=>JSON.stringify({aurea_preset:1,kind,...data});
const preset={schema:1,minAppVersion:2113,name:`Teste local ${crypto.randomUUID()}`,width:1080,height:1920,
caption:part('caption',{caption:{style:0,mode:0,maxWords:5}}),text:part('text',{style:{size:64}}),effects:'',animation:'',transform:[.5,.8,0,1,1,1,0,0,0,1]};
const {token}=await call('/api/captions/session',{method:'POST',status:201});
await call('/api/captions/presets',{method:'POST',body:{preset,author:'Teste'},status:401});
const published=await call('/api/captions/presets',{method:'POST',token,body:{preset,author:'Teste'},status:201});
assert.equal(published.version,1);
const path=`/api/captions/presets/${published.id}`;
for(let i=0;i<2;i++) await call(path+'/like',{method:'POST',token});
for(let i=0;i<2;i++) {
  const data=await call(path+'/download',{method:'POST',token});assert.deepEqual(data.preset,preset);
}
const list=await call('/api/captions/presets?q='+encodeURIComponent(preset.name));
assert.equal(list.items.length,1);assert.equal(list.items[0].likes,1);assert.equal(list.items[0].downloads,1);
const v2=await call('/api/captions/presets',{method:'POST',token,body:{id:published.id,preset,author:'Teste'},status:201});assert.equal(v2.version,2);
assert.equal(v2.likes,1);assert.equal(v2.downloads,1);
const other=await call('/api/captions/session',{method:'POST',status:201});
await call('/api/captions/presets',{method:'POST',token:other.token,body:{id:published.id,preset,author:'Outro'},status:403});
const bad={...preset,text:part('text',{style:{expression:'bad'}})};
await call('/api/captions/presets',{method:'POST',token,body:{preset:bad,author:'Teste'},status:400});
const image=await fetch(base+path+'/preview');assert.equal(image.status,200);assert.equal(image.headers.get('content-type'),'image/svg+xml');
const discovery=await call('/server');assert.equal(typeof discovery.online,'boolean');
console.log('PASS: local session, authorization, publish, version, search, download, deduplicated counts, ownership, validation, preview and existing discovery.');
