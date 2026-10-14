// Exercise actual embedded preview scheduling and save recovery after HTTP failures.
const fs=require('fs'),vm=require('vm'),assert=require('assert').strict;
const source=fs.readFileSync(require('path').join(__dirname,'../src/webui.h'),'utf8');
function extract(name){
 const start=source.search(new RegExp(`^(?:async )?function ${name}\\(`,'m'));
 assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;
 while(depth){depth+=(source[end]==='{')-(source[end]==='}');end++;}
 return source.slice(start,end);
}
const tick=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 const unhandled=[];const onUnhandled=error=>unhandled.push(error);
 process.on('unhandledRejection',onUnhandled);
 try {
  for(const feed of [false,true]){
   const calls=[],states=[],timers=[];let fail=true;
   const context=vm.createContext({
    authenticated:true,hydrating:false,dirty:false,feedDirty:false,editGeneration:0,feedEditGeneration:0,
    liveTimer:0,feedLiveTimer:0,livePromise:Promise.resolve(),feedPromise:Promise.resolve(),operationQueue:Promise.resolve(),
    window:{clearTimeout(){},setTimeout(callback){timers.push(callback);return 1;}},
    console:{error(){}},setSyncState(label){states.push(label);},updateActionButtons(){},
    getTimezoneFromUI:()=>0,buildLivePayload:()=>({}),buildFeedPayload:()=>({}),buildWidgetSnapshot:()=>({}),
    request:async()=>({ok:true,json:async()=>({})}),setDeviceIdentity(){},loadFeedStateOnly:async()=>{},
    postJson:async(url)=>{calls.push(url);if(fail)throw new Error('Connection lost');return {ok:true};},
    postFeedConfig:async(url)=>{calls.push(url);if(fail)throw new Error('Connection lost');return {ok:true};}
   });
   vm.runInContext(['queueOperation','scheduleLiveSync','scheduleFeedPreview','doLiveSync','doFeedPreview','flushLiveSync','flushFeedPreview','commitState','commitFeedConfig'].map(extract).join('\n'),context);
   vm.runInContext(feed?'scheduleFeedPreview()':'scheduleLiveSync()',context);
   timers.shift()();await tick();await tick();
   assert.equal(states.at(-1),feed?'Feed preview failed':'Preview failed');
   assert.equal(feed?context.feedDirty:context.dirty,true,'Failed preview preserves the draft');
   assert.equal(calls.length,1);
   fail=false;
   await vm.runInContext(feed?'commitFeedConfig()':'commitState()',context);
   assert.deepEqual(calls,feed?['/feeds/live','/feeds/live','/feeds/save']:['/dashboard/live','/dashboard/live','/dashboard/save']);
   assert.equal(feed?context.feedDirty:context.dirty,false,'Save recovers without requiring another edit');
   assert.equal(states.at(-1),'Saved');
   // A persistent outage gets one retry per explicit flush, never an endless loop.
   fail=true;vm.runInContext(feed?'scheduleFeedPreview()':'scheduleLiveSync()',context);
   timers.shift()();await tick();await tick();
   const previousCalls=calls.length;
   await vm.runInContext(feed?'commitFeedConfig()':'commitState()',context);
   assert.equal(calls.length,previousCalls+1);
   assert.equal(feed?context.feedDirty:context.dirty,true);
   let reject;
   const delayed=async()=>new Promise((resolve,fail)=>{reject=fail;});
   if(feed)context.postFeedConfig=delayed;else context.postJson=delayed;
   vm.runInContext(feed?'scheduleFeedPreview()':'scheduleLiveSync()',context);
   timers.shift()();await tick();
   vm.runInContext(feed?'scheduleFeedPreview()':'scheduleLiveSync()',context);
   reject(new Error('Old preview failed'));await tick();await tick();
   assert.equal(states.at(-1),feed?'Feeds pending':'Pending','An old failure does not overwrite newer feedback');
  }
  await tick();assert.deepEqual(unhandled,[],'Background previews handle rejected promises');
  console.log('UI error recovery passed: visible preview errors, retained edits, save retry, bounded outage attempts');
 } finally {process.off('unhandledRejection',onUnhandled);}
})().catch(error=>{console.error(error);process.exitCode=1;});
