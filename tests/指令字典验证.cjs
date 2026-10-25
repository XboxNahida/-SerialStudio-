// 仅检查数据和纯补全逻辑，不执行任何模板命令。
const fs=require('fs'),vm=require('vm'),assert=require('assert'),path=require('path');
const root=path.join(__dirname,'..');
const entries=JSON.parse(fs.readFileSync(path.join(root,'data/terminal_commands.json'),'utf8'));
assert(entries.length>=500,'字典需要补齐调试、部署、设置、应用和操作任务');
for(const e of entries.filter(e=>e.interaction==='shell')) {
    assert(e.location && e.usage && Array.isArray(e.parameters) && Array.isArray(e.options),e.id+' 缺少字典说明');
    for(const p of e.parameters)assert(Number.isInteger(p.index) && p.label && p.hint && Array.isArray(p.choices),e.id+' 参数描述');
    for(const o of e.options)assert(o.value && o.description,e.id+' 选项缺少含义');
    for(const a of e.advanced || [])assert(a.title && a.command && a.description,e.id+' 高级说明');
}
for(const s of ['powershell','cmd'])assert(entries.some(e=>e.system===s && e.example==='adb tcpip 5555' && e.location.includes('电脑')),s+' 主机ADB');
assert(entries.some(e=>e.system==='android' && e.example.includes('service.adb.tcp.port') && e.requires.includes('root')));
assert(entries.some(e=>e.system==='linux' && e.example.startsWith('gdb ')));
assert(entries.some(e=>e.system==='android' && e.example.startsWith('am broadcast')));
const ctx=vm.createContext({});
vm.runInContext(fs.readFileSync(path.join(root,'qml/components/CommandGuide.js'),'utf8').replace('.pragma library',''),ctx);
const candidates=(text,cursor=text.length)=>ctx.complete(text,cursor,entries.filter(e=>e.system==='linux'));
assert(candidates('').length>0,'空输入应有入门推荐');
const flag=candidates('ls -').find(c=>c.insert==='-l');assert(flag,'选项含义候选');
assert.strictEqual(flag.result,'ls -l');
const preserved=candidates('ls - /tmp',4).find(c=>c.insert==='-l');assert.strictEqual(preserved.result,'ls -l /tmp');
const middle=candidates('ls -wrong /tmp',4).find(c=>c.insert==='-l');assert.strictEqual(middle.result,'ls -l /tmp');assert.strictEqual(middle.cursor,5,'补全后光标在新词末尾');
const quoted=candidates('cp "a b" ');assert(!quoted.some(c=>c.kind==='整条示例'),'参数阶段不覆盖已填文件');
const item={example:'demo --mode fast',syntax:'demo --mode <模式>',parameters:[{index:2,label:'运行模式',hint:'fast 快速；safe 保守',choices:[{value:'safe',description:'保守模式'}]}]};
const values=ctx.complete('demo --mode ',12,[item]);assert(values.some(c=>c.result==='demo --mode safe'));
const parts=ctx.parts(item);assert.strictEqual(parts[2].label,'运行模式');
assert(ctx.complete('dmesg | ',8,entries.filter(e=>e.system==='linux')).some(c=>c.insert==='grep'));
assert(ctx.complete('echo "x| y" ',12,[]).every(c=>c.kind!=='管道指令'),'引号内竖线不是管道');
assert(candidates('gdb -ex ').some(c=>c.result==='gdb -ex run'),'高级模板的参数也可补全');
const host=entries.filter(e=>e.system==='powershell');
assert(ctx.complete('adb -s SERIAL ',14,host).some(c=>c.insert==='tcpip'),'显式目标的高级ADB语法');
console.log('指令字典数据、执行位置、参数替换、复杂语法补全：通过，共 '+entries.length+' 条');
for(const system of ['powershell','cmd']) {
    const wsl=entries.filter(e=>e.system===system && e.category==='WSL/虚拟机');
    assert(ctx.complete('wsl --',6,wsl).some(c=>c.insert==='--shutdown'));
    assert(ctx.complete('wsl --list ',11,wsl).some(c=>c.insert==='--verbose'));
    assert(ctx.complete('wsl --distribution ',19,wsl).some(c=>c.insert==='Ubuntu'));
    assert(ctx.complete('wsl --set-version Ubuntu ',25,wsl).some(c=>c.insert==='2'));
    const example=wsl.find(e=>e.id===system+'-wsl-import');
    assert(ctx.parts(example).length>=5,'导入指令应逐项填写名称、目录和归档');
}
// 隔离模拟文件系统，确认旧维护工具不会覆盖手工整理的 WSL 参数说明。
let maintained='';
vm.runInNewContext(fs.readFileSync(path.join(root,'tools/维护指令字典.cjs'),'utf8'),{
    __dirname:path.join(root,'tools'),console:{log(){}},
    require(name){return name==='fs'?{readFileSync(){return JSON.stringify(entries)},writeFileSync(file,data){maintained=data}}:require(name)}
});
const wslEntries=list=>list.filter(e=>e.category==='WSL/虚拟机');
assert.deepStrictEqual(wslEntries(JSON.parse(maintained)),wslEntries(entries),'维护工具必须保留WSL手工元数据');
