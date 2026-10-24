.pragma library

// 保留原始引号和转义，不把语法示意中的占位符当作可执行参数。
function tokens(value) {
    var result=[],token="",quote="",escape=false
    for(var i=0;i<value.length;i++) {
        var c=value[i]
        if(escape){token+=c;escape=false;continue}
        if(c==="\\" || c==="`"){token+=c;escape=true;continue}
        if(quote){token+=c;if(c===quote)quote="";continue}
        if(c==="'" || c==='"'){quote=c;token+=c;continue}
        if(/\s/.test(c)){if(token){result.push(token);token=""}}
        else token+=c
    }
    if(token)result.push(token)
    return result
}
function parts(entry) {
    var example=String(entry.example || ""), syntax=String(entry.syntax || "")
    var values=tokens(example)
    if(!values.length || entry.interaction==="editor-key")return []
    // 脚本/管道作为完整表达式编辑，避免拆分破坏 Shell 语义。
    if(/[|;&<>\n]/.test(example))return [{label:"完整表达式",value:example,hint:"| 管道；> 覆盖输出；>> 追加；2>&1 合并错误；&& 成功后继续；; 分隔命令。请保留引号、括号及变量。",editable:true,choices:[]}]
    var placeholders=syntax.match(/\[[^\]]+\]|<[^>]+>/g) || [], position=0
    placeholders=placeholders.filter(function(p){return !/^\[-/.test(p) && !/^\[选项/.test(p)})
    var positional={"cp":["源文件 / 目录","目标文件 / 目录"],"mv":["源文件 / 目录","目标文件 / 目录"],"copy":["源文件","目标文件"],"move":["源文件","目标文件"],
        "Copy-Item":["源路径","目标路径"],"Move-Item":["源路径","目标路径"],"cd":["目标目录"],"mkdir":["新目录"],"touch":["新文件"],"cat":["文件路径"],"rm":["删除目标"],
        "ls":["目录路径"],"grep":["搜索关键词 / 表达式","文件路径"],"find":["搜索目录","条件及值"],"ping":["目标IP / 域名"],"ssh":["用户@主机"],
        "curl":["网址及参数"],"wget":["下载网址"],"chmod":["权限数字 / 模式","目标路径"],"chown":["用户:组","目标路径"],"kill":["进程PID"],
        "nano":["编辑的文件"],"vim":["编辑的文件"],"vi":["编辑的文件"],"notepad":["编辑的文件"],"ip":["对象 addr / link / route","操作及参数"],
        "systemctl":["操作 status / start / stop","服务名"],"pm":["操作 list / install / uninstall","包名或路径"],"am":["操作 start / force-stop","组件名或包名"]}
    var hints={"ls":"-a 显示隐藏项；-l 详细列表；-h 易读大小。","cp":"复制文件；-r 递归复制目录；目标已存在时注意覆盖。",
        "mv":"移动或重命名；核对源与目标路径。","rm":"删除；-r 递归，-f 强制。请先核对路径。",
        "grep":"-n 显示行号；-i 忽略大小写；-r 搜索目录。","ping":"目标可填 IP 或域名；Linux 用 -c 次数，Windows 用 -n 次数。",
        "ssh":"用户@主机；-p 指定端口；-i 指定私钥。","ip":"addr 地址；link 网卡；route 路由。",
        "chmod":"权限如 644、755；权限不足需要管理员。","curl":"-I 仅响应头；-o 保存文件；-X 指定方法。"}
    return values.map(function(value,index) {
        var explicit=(entry.parameters || []).find(function(p){return p.index===index})
        if(explicit)return {label:explicit.label,value:value,editable:index>0,hint:explicit.hint,choices:explicit.choices || []}
        var option=/^[-/]/.test(value) && (value[0]==="-" || /^\/[a-zA-Z?]$/.test(value))
        var label=index===0?"工具 / 指令":option?"选项":placeholders[position] || (positional[values[0]] || [])[position] || "参数 "+index
        if(index>0 && !option)position++
        return {label:label.replace(/^[\[<]|[\]>]$/g,""),value:value,editable:index>0,
            hint:index===0?"固定指令名称":option?(hints[values[0]] || "选项含义以目标系统帮助为准；可留空移除。"):
                "填入实际值；含空格请保留引号。可留空移除可选参数。"}
    })
}

// 只在当前词范围插入，保留前后文字；不解释或执行任何 Shell 表达式。
function wordSpans(text) {
    var out=[],start=-1,quote="",escaped=false
    for(var i=0;i<text.length;i++) {
        var c=text[i]
        if(start<0 && !/\s/.test(c))start=i
        if(escaped){escaped=false;continue}
        if((c==="\\" || c==="`") && quote!=="'"){escaped=true;continue}
        if(quote){if(c===quote)quote="";continue}
        if(c==="'" || c==='"'){quote=c;continue}
        if(/\s/.test(c) && start>=0){out.push({start:start,end:i,value:text.slice(start,i)});start=-1}
    }
    if(start>=0)out.push({start:start,end:text.length,value:text.slice(start)})
    return out
}
function segmentStart(text,cursor) {
    var quote="",escaped=false,start=0
    for(var i=0;i<cursor;i++) {
        var c=text[i]
        if(escaped){escaped=false;continue}
        if((c==="\\" || c==="`") && quote!=="'"){escaped=true;continue}
        if(quote){if(c===quote)quote="";continue}
        if(c==="'" || c==='"'){quote=c;continue}
        if(c==='|' || c===';' || c==='&')start=i+1
    }
    return start
}
function complete(text,cursor,entries) {
    cursor=Math.max(0,Math.min(text.length,cursor))
    var segment=segmentStart(text,cursor),local=text.slice(segment),words=wordSpans(local)
    var at=words.findIndex(function(w){return cursor-segment>=w.start && cursor-segment<=w.end})
    var index=at>=0?at:words.filter(function(w){return w.end<cursor-segment}).length
    var start=at>=0?segment+words[at].start:cursor,end=at>=0?segment+words[at].end:cursor
    var prefix=text.slice(start,cursor).toLowerCase(),command=words.length?words[0].value.toLowerCase():""
    var out=[],seen={}
    function put(value,title,hint,kind,entry,whole) {
        if(!value || /[\u0000-\u001f]/.test(value))return
        var result=whole?value:text.slice(0,start)+value+text.slice(end)
        if(result===text || seen[result])return
        seen[result]=true
        out.push({insert:value,title:title,syntax:hint,description:hint,kind:kind,entry:entry,
            example:value,result:result,cursor:whole?result.length:start+value.length})
    }
    var shell=[]
    entries.filter(function(e){return e.interaction!=="editor-key" && !e.hex}).forEach(function(e) {
        shell.push(e)
        ;(e.advanced || []).forEach(function(a) {
            shell.push(Object.assign({},e,{example:a.command,title:a.title,description:a.description,syntax:a.command,parameters:[],advanced:[]}))
        })
    })
    if(index===0) {
        shell.forEach(function(e) {
            var ex=String(e.example || e.text || ""),first=tokens(ex)[0] || ""
            if(!prefix || ex.toLowerCase().indexOf(prefix)===0 || String(e.title || "").toLowerCase().indexOf(prefix)>=0) {
                if(segment>0)put(first,e.title,e.syntax || e.description,"管道指令",e,false)
                else put(ex,e.title,e.syntax || e.description,"整条示例",e,true)
            }
        })
    } else {
        var matching=shell.filter(function(e){
            var ts=tokens(String(e.example || e.text || ""))
            return (ts[0] || "").toLowerCase()===command && (command!=="toybox" || index<2 || ts[1]===words[1].value)
        })
        matching.forEach(function(e) {
            var es=tokens(String(e.example || e.text || ""))
            var before=words.slice(0,index).map(function(w){return w.value})
            // 只有前置词匹配的条目才提供位置参数，避免 am start 混入 am broadcast。
            var same=before.every(function(v,i){return i===0 || es[i]===v})
            if(same) {
                var param=(e.parameters || []).find(function(p){return p.index===index})
                if(param)(param.choices || []).forEach(function(c) {
                    if(c.value.toLowerCase().indexOf(prefix)===0)put(c.value,param.label,c.description || param.hint,"参数值",e,false)
                })
                // 子命令/示例参数同时可发现；保持其它参数原样。
                if(es[index] && es[index].toLowerCase().indexOf(prefix)===0)
                    put(es[index],param?param.label:e.title,param?param.hint:e.syntax,"参数示例",e,false)
            }
            if(prefix[0]==='-' || prefix[0]==='/' || !prefix)(e.options || []).forEach(function(o) {
                if(o.value.toLowerCase().indexOf(prefix)===0)put(o.value,"选项",o.description,"可选参数",e,false)
            })
        })
    }
    return out
}
