(function () {
    var destination = new Folder('C:/Users/Ruan/Documents/Aureabeta/output/juan-ffx-audit'); destination.create();
    var source = 'C:/Users/Ruan/Downloads/coisas do juan-20260926T043605Z-1-001/coisas do juan/';
    var names = ['juan Text Bounce 2.ffx','juan turb time 6.ffx','juan TEXT ANIMATION 01.ffx','Juan Text Animation 5.ffx','juan Text Animation2.ffx','juan text animation fast 1.ffx','juan text animation jump bounce.ffx','juan text animation word jump.ffx','juan Text Animation.ffx'];
    function quote(s) { return '"'+String(s).replace(/\\/g,'\\\\').replace(/"/g,'\\"').replace(/\r/g,'\\r').replace(/\n/g,'\\n').replace(/\t/g,'\\t')+'"'; }
    function json(v) {
        if(v===null || v===undefined) return 'null';
        if(typeof v==='string') return quote(v);
        if(typeof v==='number') return isFinite(v)?String(v):'null';
        if(typeof v==='boolean') return String(v);
        var r=[],i;
        if(v instanceof Array) {for(i=0;i<v.length;i++)r.push(json(v[i]));return '['+r.join(',')+']';}
        for(i in v)if(v.hasOwnProperty(i))r.push(quote(i)+':'+json(v[i]));return '{'+r.join(',')+'}';
    }
    function value(v) {
        if(v instanceof TextDocument) return {text:v.text,font:v.font,fontSize:v.fontSize,fillColor:v.fillColor,tracking:v.tracking};
        if(v instanceof Shape) return {vertices:v.vertices,inTangents:v.inTangents,outTangents:v.outTangents,closed:v.closed};
        if(typeof v==='number'||typeof v==='string'||typeof v==='boolean'||v instanceof Array)return v;
        return String(v);
    }
    function ease(e) {var a=[];for(var i=0;i<e.length;i++)a.push({speed:e[i].speed,influence:e[i].influence});return a;}
    function walk(p) {
        var o={name:p.name,match:p.matchName,index:p.propertyIndex};
        if(p.propertyType!==PropertyType.PROPERTY) {
            o.children=[];for(var i=1;i<=p.numProperties;i++)o.children.push(walk(p.property(i)));return o;
        }
        try{o.value=value(p.value);}catch(e){o.valueError=String(e);}
        try{if(p.canSetExpression){o.expression=p.expression;o.expressionEnabled=p.expressionEnabled;o.expressionError=p.expressionError;}}catch(e){}
        o.keys=[];
        for(var k=1;k<=p.numKeys;k++) {
            var key={time:p.keyTime(k),value:value(p.keyValue(k)),inType:String(p.keyInInterpolationType(k)),outType:String(p.keyOutInterpolationType(k))};
            try{key.inEase=ease(p.keyInTemporalEase(k));key.outEase=ease(p.keyOutTemporalEase(k));}catch(e){}
            try{key.inSpatial=p.keyInSpatialTangent(k);key.outSpatial=p.keyOutSpatialTangent(k);}catch(e){}
            o.keys.push(key);
        }
        return o;
    }
    var oldSecurity=app.preferences.getPrefAsLong('Main Pref Section','Pref_SCRIPTING_FILE_NETWORK_SECURITY');
    app.preferences.savePrefAsLong('Main Pref Section','Pref_SCRIPTING_FILE_NETWORK_SECURITY',1);
    app.beginSuppressDialogs();
    try {
        app.newProject();
        for(var n=0;n<names.length;n++) {
            var result={file:names[n],fps:60};
            try {
                var comp=app.project.items.addComp(names[n],1920,1080,1,10,60);
                var layer=comp.layers.addText('AUREA JUAN TEST');
                var td=layer.property('ADBE Text Properties').property('ADBE Text Document').value;td.fontSize=100;td.font='ArialMT';layer.property('ADBE Text Properties').property('ADBE Text Document').setValue(td);
                comp.time=0;layer.selected=true;layer.applyPreset(new File(source+names[n]));
                result.properties=[];for(var j=1;j<=layer.numProperties;j++)result.properties.push(walk(layer.property(j)));
                result.inPoint=layer.inPoint;result.outPoint=layer.outPoint;result.threeD=layer.threeDLayer;
            }catch(e){result.error=String(e);}
            var f=new File(destination.fsName+'/'+names[n]+'.json');f.encoding='UTF-8';f.open('w');f.write(json(result));f.close();
        }
        app.project.save(new File(destination.fsName+'/Juan-reference.aep'));
    } catch(e) {
        var err=new File(destination.fsName+'/error.txt');err.open('w');err.write(String(e));err.close();
    } finally {
        app.endSuppressDialogs(false);
        app.preferences.savePrefAsLong('Main Pref Section','Pref_SCRIPTING_FILE_NETWORK_SECURITY',oldSecurity);
    }
})();
