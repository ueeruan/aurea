package com.aurea.aurea.editor.panels
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.aurea.aurea.captions.CaptionPresetStore
import com.aurea.aurea.captions.CaptionsState
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject

@OptIn(ExperimentalLayoutApi::class)
@Composable
internal fun CaptionPresetBrowser(captions: CaptionsState) {
    val context=LocalContext.current; val library=remember(context){CaptionPresetStore(context)};val scope=rememberCoroutineScope()
    var open by remember { mutableStateOf(false) };var tab by remember { mutableStateOf(0) }
    var name by remember { mutableStateOf("") };var author by remember { mutableStateOf("") };var search by remember { mutableStateOf("") }
    var items by remember { mutableStateOf<List<JSONObject>>(emptyList()) };var message by remember { mutableStateOf<String?>(null) };var busy by remember { mutableStateOf(false) }
    fun load(){scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){when(tab){0->library.local(true,search);4->library.local(false,search);else->library.community(search,tab==2)}}}.onSuccess{items=it;message=null}.onFailure{message=it.message};busy=false}}
    TextButton(onClick={open=!open;if(open)load()}){Text("Presets de legendas")}
    if(!open)return
    FlowRow { listOf("Meus","Comunidade","Populares","Recentes","Baixados").forEachIndexed { i,t -> FilterChip(selected=tab==i,onClick={tab=i;load()},label={Text(t)}) } }
    Row { OutlinedTextField(search,{search=it},label={Text("Buscar presets")},modifier=Modifier.weight(1f));TextButton(onClick={load()},enabled=!busy){Text("Buscar")} }
    if(captions.track!=null){
        OutlinedTextField(name,{name=it.take(80)},label={Text("Nome do seu preset")},modifier=Modifier.fillMaxWidth())
        OutlinedTextField(author,{author=it.take(60)},label={Text("Nome público do autor")},modifier=Modifier.fillMaxWidth())
        Row {
            TextButton(enabled=name.isNotBlank()&&!busy,onClick={val data=captions.capturePreset(name.trim());scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){library.save(data)}}.onSuccess{message="Preset salvo no aparelho";tab=0;load()}.onFailure{message=it.message};busy=false}}){Text("Salvar privado")}
            TextButton(enabled=name.isNotBlank()&&author.isNotBlank()&&!busy,onClick={val data=captions.capturePreset(name.trim());scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){library.publish(library.save(data),author.trim())}}.onSuccess{message="Publicado na comunidade";tab=1;load()}.onFailure{message=it.message};busy=false}}){Text("Publicar")}
        }
    }
    if(busy)LinearProgressIndicator(modifier=Modifier.fillMaxWidth())
    message?.let{Text(it)}
    items.forEach { entry ->
        Column(Modifier.fillMaxWidth().padding(vertical=8.dp)) {
            Text(entry.optString("name"),style=MaterialTheme.typography.titleMedium)
            Text("${entry.optString("author","Neste aparelho")} · v${entry.optInt("version",1)} · ${entry.optInt("likes")} curtidas · ${entry.optInt("downloads")} downloads")
            Row {
                TextButton(enabled=captions.track!=null&&!busy,onClick={scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){library.download(entry)}}.onSuccess{captions.applyPreset(it.getJSONObject("preset").toString());message="Preset aplicado"}.onFailure{message=it.message};busy=false}}){Text("USAR")}
                TextButton(enabled=!busy,onClick={scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){library.download(entry)}}.onSuccess{message="Disponível offline"}.onFailure{message=it.message};busy=false}}){Text("BAIXAR")}
                if(tab in 1..3)TextButton(enabled=!busy,onClick={scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){library.like(entry)}}.onSuccess{message="Curtido"}.onFailure{message=it.message};busy=false}}){Text("CURTIR")}
            }
        }
    }
}
