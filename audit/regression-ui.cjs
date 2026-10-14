// Execute the embedded UI functions against delayed HTTP boundaries; no device is contacted.
const fs = require('fs');
const vm = require('vm');
const assert = require('assert').strict;
const source = fs.readFileSync(require('path').join(__dirname, '../src/webui.h'), 'utf8');
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
