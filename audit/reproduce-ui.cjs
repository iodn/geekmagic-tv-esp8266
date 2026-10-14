// Source-extracted UI diagnostics; run with Node.js. No device is contacted.
const fs = require('fs');
const vm = require('vm');
const source = fs.readFileSync(require('path').join(__dirname, '../src/webui.h'), 'utf8');
const scripts = [...source.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)];
for (const [index, match] of scripts.entries()) new vm.Script(match[1], {filename:`embedded-script-${index}`});
console.log(`JavaScript syntax: ${scripts.length} embedded scripts parsed`);
function extract(name) {
 const start = source.search(new RegExp(`^(?:async )?function ${name}\\(`, 'm'));
 const brace = source.indexOf('{', start); let depth=1, end=brace+1;
 while (depth) { depth += (source[end]==='{') - (source[end]==='}'); end++; }
 return source.slice(start,end);
}
(async () => {
 let resolveSave;
 const context = vm.createContext({
  hydrating:false, dirty:true, liveTimer:0, livePromise:Promise.resolve(),
  document:{getElementById:() => ({value:'clock'})}, deviceIdentity:{defaultDeviceName:'clock'},
  window:{clearTimeout(){}}, console,
  buildLivePayload:() => ({settings:{brightness:50}}),
  postJson:(url,payload) => { console.log(`Saving payload brightness=${payload.settings.brightness}`); return new Promise(resolve=>{resolveSave=resolve;}); },
  setSyncState(){},updateActionButtons(){},setDeviceIdentity(){},buildHostnameFromDeviceName(){return 'clock';}
 });
 vm.runInContext(extract('flushLiveSync')+'\n'+extract('commitState'),context);
 const saving = vm.runInContext('commitState()',context);
 await new Promise(resolve=>setImmediate(resolve));
 vm.runInContext('dirty=true',context);
 console.log('User makes another edit while save is pending: dirty=true');
 resolveSave({ok:true});await saving;
 console.log(`After earlier save completes: dirty=${vm.runInContext('dirty',context)} (new edit is marked saved)`);
})();
