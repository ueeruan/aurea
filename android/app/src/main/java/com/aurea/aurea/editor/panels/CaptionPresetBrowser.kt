package com.aurea.aurea.editor.panels
import com.aurea.aurea.R
import androidx.compose.ui.res.stringResource
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
    val savedMsg=stringResource(R.string.edt_cp_saved);val publishedMsg=stringResource(R.string.edt_cp_published);val appliedMsg=stringResource(R.string.edt_cp_applied)
    val offlineMsg=stringResource(R.string.edt_cp_offline);val likedMsg=stringResource(R.string.edt_cp_liked);val onDevice=stringResource(R.string.edt_cp_on_device)
    fun load(){scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){when(tab){0->library.local(true,search);4->library.local(false,search);else->library.community(search,tab==2)}}}.onSuccess{items=it;message=null}.onFailure{message=it.message};busy=false}}
    TextButton(onClick={open=!open;if(open)load()}){Text(stringResource(R.string.edt_cp_title))}
    if(!open)return
    FlowRow { listOf(R.string.edt_cp_tab_mine,R.string.home_community_title,R.string.edt_cp_tab_popular,R.string.effect_recentes,R.string.home_downloaded).forEachIndexed { i,t -> FilterChip(selected=tab==i,onClick={tab=i;load()},label={Text(stringResource(t))}) } }
    Row { OutlinedTextField(search,{search=it},label={Text(stringResource(R.string.home_search_presets))},modifier=Modifier.weight(1f));TextButton(onClick={load()},enabled=!busy){Text(stringResource(R.string.common_search))} }
    if(captions.track!=null){
        OutlinedTextField(name,{name=it.take(80)},label={Text(stringResource(R.string.edt_cp_name))},modifier=Modifier.fillMaxWidth())
        OutlinedTextField(author,{author=it.take(60)},label={Text(stringResource(R.string.edt_cp_author))},modifier=Modifier.fillMaxWidth())
        Row {
            TextButton(enabled=name.isNotBlank()&&!busy,onClick={val data=captions.capturePreset(name.trim());scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){library.save(data)}}.onSuccess{message=savedMsg;tab=0;load()}.onFailure{message=it.message};busy=false}}){Text(stringResource(R.string.edt_cp_save_private))}
            TextButton(enabled=name.isNotBlank()&&author.isNotBlank()&&!busy,onClick={val data=captions.capturePreset(name.trim());scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){library.publish(library.save(data),author.trim())}}.onSuccess{message=publishedMsg;tab=1;load()}.onFailure{message=it.message};busy=false}}){Text(stringResource(R.string.edt_cp_publish))}
        }
    }
    if(busy)LinearProgressIndicator(modifier=Modifier.fillMaxWidth())
    message?.let{Text(it)}
    items.forEach { entry ->
        Column(Modifier.fillMaxWidth().padding(vertical=8.dp)) {
            Text(entry.optString("name"),style=MaterialTheme.typography.titleMedium)
            Text(stringResource(R.string.edt_cp_meta,entry.optString("author",onDevice),entry.optInt("version",1),entry.optInt("likes"),entry.optInt("downloads")))
            Row {
                TextButton(enabled=captions.track!=null&&!busy,onClick={scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){library.download(entry)}}.onSuccess{captions.applyPreset(it.getJSONObject("preset").toString());message=appliedMsg}.onFailure{message=it.message};busy=false}}){Text(stringResource(R.string.edt_cp_use))}
                TextButton(enabled=!busy,onClick={scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){library.download(entry)}}.onSuccess{message=offlineMsg}.onFailure{message=it.message};busy=false}}){Text(stringResource(R.string.edt_cp_download))}
                if(tab in 1..3)TextButton(enabled=!busy,onClick={scope.launch{busy=true;runCatching{withContext(Dispatchers.IO){library.like(entry)}}.onSuccess{message=likedMsg}.onFailure{message=it.message};busy=false}}){Text(stringResource(R.string.edt_cp_like))}
            }
        }
    }
}
