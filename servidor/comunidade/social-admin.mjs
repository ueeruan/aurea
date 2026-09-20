// Private admin client. Credentials come from stdin, never command arguments.
// node social-admin.mjs < private-request.json
import {readFileSync, writeFileSync, existsSync, mkdirSync} from 'node:fs';
import {dirname, resolve} from 'node:path';
const input=JSON.parse(readFileSync(0,'utf8').replace(/^\uFEFF/,''));
const base='https://mural-do-aurea.aureaapp.workers.dev';
const code=input.codeFile?readFileSync(input.codeFile,'utf8').trim():input.code;
if(!/^[0-9a-f]{48}$/.test(code??''))throw Error('Provide a valid private credential via stdin.');
async function request(path,method='GET',body){
 const res=await fetch(base+path,{method,headers:{authorization:`Bearer ${code}`,'content-type':'application/json'},body:body==null?undefined:JSON.stringify(body),signal:AbortSignal.timeout(30000)});
 const data=await res.json();if(!res.ok)throw Error(`${res.status}: ${data.erro??'Request failed'}`);return data;
}
if(input.action==='bootstrap'){
 const file=resolve(input.output);
 if(existsSync(file))throw Error('Private output already exists; refusing to overwrite.');
 const result=await request('/social/admin/bootstrap','POST',{});
 if(result.codigoOficial){mkdirSync(dirname(file),{recursive:true});writeFileSync(file,result.codigoOficial+'\n',{flag:'wx',mode:0o600});}
 console.log(JSON.stringify({creator:result.criador,official:result.aurea,credentialSaved:!!result.codigoOficial}));
}else if(input.action==='migrate'){
 for(const [path,extra]of[['migrate',{}],['migrate-posts',{}],['migrate-posts',{respostas:true}]]){
  let cursor=null,total=0;do{const r=await request('/social/admin/'+path,'POST',{...extra,cursor});total+=r.importadas;cursor=r.cursor;}while(cursor);
  console.log(JSON.stringify({collection:path,...extra,total}));
 }
}else if(input.action==='notice'){
 console.log(await request('/social/admin/notice','POST',{texto:input.text}));
}else if(input.action==='verify'){
 console.log(await request('/social/admin/verify/'+encodeURIComponent(input.userId),'POST',{verificado:input.verified===true}));
}else if(input.action==='avatar'){
 const res=await fetch(base+'/midia',{method:'POST',headers:{authorization:`Bearer ${code}`,'content-type':'image/png'},body:readFileSync(input.file),signal:AbortSignal.timeout(30000)});
 const uploaded=await res.json();if(!res.ok)throw Error(uploaded.erro??'Upload failed');
 console.log(await request('/social/me','PATCH',{avatar:uploaded.url}));
}else if(input.action==='post'){
 const result=await request('/post','POST',{texto:input.text,...(input.image?{imagem:input.image,tipoDeMidia:'imagem'}:{})});
 console.log(JSON.stringify({id:result.post?.id,author:result.post?.autor}));
}else if(input.action==='check'){
 const me=await request('/social/me');
 const feed=await request('/social/feed');
 const official=await request('/social/profiles?q=aurea');
 console.log(JSON.stringify({me,posts:feed.posts.length,official:official.perfis}));
}else throw Error('Unknown action.');
