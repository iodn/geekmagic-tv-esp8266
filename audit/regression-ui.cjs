// Execute the embedded UI functions against delayed HTTP boundaries; no device is contacted.
const fs = require('fs');
const vm = require('vm');
const assert = require('assert').strict;
const source=require('./read-webui.cjs')();
for (const script of source.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)) new vm.Script(script[1]);
function extract(name) {
    const start = source.search(new RegExp(`^(?:async )?function ${name}\\(`, 'm'));
    assert(start >= 0, name);
    const brace = source.indexOf('{', start); let depth = 1, end = brace + 1;
    while (depth) { depth += (source[end] === '{') - (source[end] === '}'); end++; }
    return source.slice(start, end);
}
const tick = () => new Promise(resolve => setImmediate(resolve));
(async () => {
    let release, payload, brightness = 50;
    const context = vm.createContext({
        hydrating: false, dirty: true, editGeneration: 1, feedDirty: true, feedEditGeneration: 1,
        liveTimer: 0, feedLiveTimer: 0, livePromise: Promise.resolve(), feedPromise: Promise.resolve(), operationQueue: Promise.resolve(),
        widgetBaseline: {focus: {remainingSeconds: 1500, running: true}, event: {remainingSeconds: 3600}},
        buildWidgetSnapshot: () => ({focus: {remainingSeconds: 1500, running: true}, event: {remainingSeconds: 3600}}),
        buildLivePayload: () => ({settings: {brightness}}), buildFeedPayload: () => ({weather: {label: String(brightness)}}),
        postJson: async (url, body) => {payload = body; return new Promise(resolve => {release = resolve;});},
        request: async () => ({ok: true, json: async () => ({hostName: 'active-name', rebootRequired: true})}),
        confirm: () => true, window: {clearTimeout() {}, setTimeout(resolve) {resolve();}}, console,
        setSyncState() {}, updateActionButtons() {}, setDeviceIdentity() {},
        loadFeedStateOnly() {throw new Error('New edits must not be overwritten by hydration');},
        loadAllState() {throw new Error('New edits must not be overwritten by hydration');}
    });
    vm.runInContext(['queueOperation','buildWidgetPayload','flushLiveSync','flushFeedPreview','commitState','commitFeedConfig','discardState','discardFeedConfig','postFeedConfig','waitQueuedResponse','resetDashboardDefaults','resetFeedDefaults'].map(extract).join('\n'), context);
    assert.equal(vm.runInContext('JSON.stringify(buildWidgetPayload())', context), '{}', 'Unrelated settings omit timer fields');
    assert.equal(vm.runInContext('JSON.stringify(buildWidgetPayload({focus:{remainingSeconds:1500,running:false},event:{remainingSeconds:3600}}))', context), '{"focus":{"running":false}}', 'Pause omits stored remainder');
    let saving = vm.runInContext('commitState()', context); await tick();
    assert.equal(payload.settings.brightness, 50);
    brightness = 70; vm.runInContext('editGeneration++; dirty=true', context);
    release({ok: true}); await saving;
    assert.equal(context.dirty, true, 'A newer edit stays unsaved');
    saving = vm.runInContext('commitState()', context); await tick(); release({ok: true}); await saving;
    assert.equal(context.dirty, false, 'Saved generation is acknowledged');
    saving = vm.runInContext('commitFeedConfig()', context); await tick();
    vm.runInContext('feedEditGeneration++; feedDirty=true', context);release({ok: true});await saving;
    assert.equal(context.feedDirty, true, 'A newer feed edit stays unsaved');
    context.request=async () => new Promise(resolve => {release=resolve;});
    saving=vm.runInContext('resetDashboardDefaults()',context);await tick();
    vm.runInContext('editGeneration++; dirty=true',context);release({ok:true});await saving;
    assert.equal(context.dirty,true,'Reset preserves edits made while pending');
    saving=vm.runInContext('resetFeedDefaults()',context);await tick();
    vm.runInContext('feedEditGeneration++; feedDirty=true',context);release({ok:true});await saving;
    assert.equal(context.feedDirty,true,'Feed reset preserves edits made while pending');
    let appliedDisplay=0,appliedFeeds=0;
    const elements=new Map();
    const hydrationContext=vm.createContext({
        authenticated:true,authCanRevealPassword:false,hydrating:false,
        dirty:false,feedDirty:false,editGeneration:0,feedEditGeneration:0,
        liveTimer:11,feedLiveTimer:12,window:{clearTimeout(){},setTimeout(){return 13;}},
        setSyncState(){},updateActionButtons(){},console,formatBytes:()=>'',
        applyAppState(){appliedDisplay++;},applyDashboardState(){appliedDisplay++;},applyFeedState(){appliedFeeds++;},
        document:{getElementById(id){if(!elements.has(id))elements.set(id,{});return elements.get(id);}},
        request:async()=>new Promise(resolve=>pendingLoads.push(resolve))
    });
    let pendingLoads=[];
    vm.runInContext(['loadAllState','loadFeedStateOnly','scheduleLiveSync','scheduleFeedPreview'].map(extract).join('\n'),hydrationContext);
    let loading=vm.runInContext('loadAllState()',hydrationContext);await tick();
    vm.runInContext('scheduleLiveSync();scheduleFeedPreview()',hydrationContext);
    assert.equal(hydrationContext.editGeneration,1,'Edits during refresh are tracked');
    assert.equal(hydrationContext.feedEditGeneration,1);
    pendingLoads.splice(0).forEach(resolve=>resolve({ok:true,json:async()=>({meta:{hasDraft:false}})}));
    await loading;assert.equal(appliedDisplay,0);assert.equal(appliedFeeds,0);
    assert.equal(hydrationContext.dirty,true);assert.equal(hydrationContext.feedDirty,true);
    loading=vm.runInContext('loadFeedStateOnly()',hydrationContext);await tick();
    pendingLoads.splice(0).forEach(resolve=>resolve({ok:true,json:async()=>({})}));
    await loading;assert.equal(appliedFeeds,0,'Feed refresh preserves an existing local draft');
    vm.runInContext('dirty=false;feedDirty=false',hydrationContext);
    loading=vm.runInContext('loadAllState()',hydrationContext);await tick();
    pendingLoads.splice(0).forEach(resolve=>resolve({ok:true,json:async()=>({})}));
    await loading;assert.equal(appliedDisplay,2);assert.equal(appliedFeeds,1,'Clean controls hydrate normally');
    loading=vm.runInContext('loadAllState()',hydrationContext);await tick();
    pendingLoads.splice(0).forEach(resolve=>resolve({status:202,ok:true,json:async()=>{throw new Error('Pending payload must not hydrate controls');}}));
    await loading;assert.equal(appliedDisplay,2);assert.equal(appliedFeeds,1);
    loading=vm.runInContext('loadFeedStateOnly()',hydrationContext);await tick();
    pendingLoads.splice(0).forEach(resolve=>resolve({status:202,ok:true,json:async()=>{throw new Error('Pending payload must not hydrate controls');}}));
    await loading;assert.equal(appliedFeeds,1);
    let focusIndex=0,prevented=0;
    const controls=[{offsetParent:{}},{offsetParent:{}},{offsetParent:null}];
    controls.forEach((control,index)=>control.focus=()=>{focusIndex=index;});
    const authContext=vm.createContext({authenticated:false,
        document:{activeElement:controls[0],getElementById(){return {querySelectorAll(){return controls;}};}}
    });
    vm.runInContext(extract('trapAuthFocus'),authContext);
    authContext.event={key:'Tab',shiftKey:true,preventDefault(){prevented++;}};
    vm.runInContext('trapAuthFocus(event)',authContext);assert.equal(focusIndex,1);assert.equal(prevented,1);
    authContext.document.activeElement=controls[1];authContext.event.shiftKey=false;
    vm.runInContext('trapAuthFocus(event)',authContext);assert.equal(focusIndex,0);assert.equal(prevented,2);
    authContext.authenticated=true;vm.runInContext('trapAuthFocus(event)',authContext);assert.equal(prevented,2);
    const calls=[];let releaseFirst;
    context.first = () => {calls.push('preview'); return new Promise(resolve => {releaseFirst=resolve;});};
    context.second = () => {calls.push('discard');};
    vm.runInContext('queueOperation(first); queueOperation(second)',context);await tick();assert.deepEqual(calls,['preview']);
    releaseFirst();await tick();assert.deepEqual(calls,['preview','discard']);
    let polls=0;
    context.request=async () => (++polls===1 ? {status:202} : {status:200,ok:true});
    const result=await vm.runInContext('waitQueuedResponse({status:202},"/feeds/job")',context);
    assert.equal(result.status,200);assert.equal(polls,2);
    console.log('UI regressions passed: sparse timer payloads, edit generations, request ordering, queued polling');
})().catch(error => {console.error(error);process.exitCode=1;});
