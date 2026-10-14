// Run against the real embedded HTML with mocked device endpoints; no TV is contacted.
// PLAYWRIGHT_MODULE_PATH may point to a Playwright installation outside this repository.
const fs = require('fs');
const path = require('path');
const http = require('http');
const assert = require('assert').strict;
const { chromium } = require(process.env.PLAYWRIGHT_MODULE_PATH || 'playwright');
const source = fs.readFileSync(path.join(__dirname, '../src/webui.h'), 'utf8');
(async () => {
    const browser = await chromium.launch({headless: true, executablePath: process.env.BROWSER_EXECUTABLE});
    let fixtureServer;
    try {
        const page = await browser.newPage();
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        let previewFailures=0,previewRequests=0,saveRequests=0;
        const server = http.createServer((request, response) => {
            const endpoint = new URL(request.url, 'http://localhost').pathname;
            if (endpoint === '/' || endpoint === '/update') {
                const name=endpoint === '/' ? 'index_html' : 'ota_html';
                let body=Buffer.from(source.match(new RegExp('const char '+name+'\\[\\] PROGMEM = R"rawliteral\\(([\\s\\S]*?)\\)rawliteral";'))[1]);
                if (process.env.WEB_ASSET_HEADER) {
                    const header=fs.readFileSync(process.env.WEB_ASSET_HEADER,'utf8');
                    const array=header.match(new RegExp(name+'_gzip\\[\\] PROGMEM = \\{([\\s\\S]*?)\\};'))[1];
                    body=Buffer.from(Array.from(array.matchAll(/0x([0-9a-f]{2})/g),match=>parseInt(match[1],16)));
                    response.setHeader('Content-Encoding','gzip');
                }
                response.setHeader('Content-Type','text/html');
                response.setHeader('Content-Length',body.length);
                return response.end(body);
            }
            if(endpoint === '/dashboard/live') {
                previewRequests++;
                if(previewFailures>0) {
                    previewFailures--;response.writeHead(503,{'Content-Type':'text/plain'});
                    return response.end('Temporary failure');
                }
            }
            if(endpoint === '/dashboard/save') saveRequests++;
            const fixtures = {
                '/auth/status': {authenticated: false, canRevealPassword: true, deviceName: 'Test TV', hostName: 'test-tv'},
                '/app.json': {deviceName: 'LivingRoomClockWithALongNameHere', hostName: 'livingroomclockwithalongnamehere', brt: 70},
                '/dashboard.json': {config: {}, data: {}, meta: {}},
                '/feeds.json': {config: {}, status: {}, meta: {}},
                '/version.json': {version: 'test'},
                '/space.json': {total: 1024, free: 512}
            };
            response.setHeader('Content-Type','application/json');
            response.end(JSON.stringify(fixtures[endpoint] || {}));
        });
        await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
        fixtureServer=server;
        const origin=`http://127.0.0.1:${server.address().port}`;
        const mainResponse=await page.goto(origin+'/');
        if(process.env.WEB_ASSET_HEADER) assert.equal(mainResponse.headers()['content-encoding'],'gzip');
        await page.waitForFunction(() => document.getElementById('authHint').textContent.includes('password'));
        assert(await page.locator('#appRoot').evaluate(element => element.inert));
        await page.setViewportSize({width:375,height:320});
        for (const id of ['authPassword','authRevealButton']) {
            await page.locator('#'+id).scrollIntoViewIfNeeded();
            const box=await page.locator('#'+id).boundingBox();
            assert(box.y>=0 && box.y+box.height<=320, `Login control ${id} is clipped in landscape`);
        }
        await page.setViewportSize({width:1280,height:900});
        await page.locator('#authPassword').focus();
        await page.keyboard.press('Shift+Tab');
        assert.equal(await page.evaluate(() => document.activeElement.id), 'authRevealButton');
        await page.keyboard.press('Tab');
        assert.equal(await page.evaluate(() => document.activeElement.id), 'authPassword');
        await page.evaluate(async () => {hideAuthOverlay(); await loadAllState();});
        assert.equal(await page.evaluate(() => document.activeElement.dataset.tab), 'display');
        assert.equal(await page.locator('#appRoot').evaluate(element => element.inert), false);
        previewFailures=1;
        await page.evaluate(()=>{
            const control=document.getElementById('brightness');control.value='80';
            control.dispatchEvent(new Event('input',{bubbles:true}));
        });
        await page.waitForFunction(()=>document.getElementById('syncState').textContent==='Preview failed');
        await page.locator('#saveDisplayButton').click();
        await page.waitForFunction(()=>document.getElementById('syncState').textContent==='Saved');
        assert.equal(previewRequests,2);assert.equal(saveRequests,1,'Save retries a failed preview in the actual browser');
        for (const theme of ['light','dark']) {
        await page.evaluate(nextTheme => setUiTheme(nextTheme), theme);
        for (const width of [320, 375, 1280]) {
            await page.setViewportSize({width, height: 900});
            for (const tab of ['display', 'feeds', 'homeassistant', 'widgets', 'network', 'system']) {
                await page.evaluate(tabId => switchTab(tabId), tab);
                const sizes = await page.evaluate(() => ({scroll: document.documentElement.scrollWidth, client: document.documentElement.clientWidth}));
                assert(sizes.scroll <= sizes.client, `${tab} overflows at ${width}px: ${sizes.scroll}`);
            }
        }
        }
        await page.evaluate(() => switchTab('display'));
        await page.keyboard.press('Tab');
        await page.locator('#pageClock').focus();
        const outline = await page.locator('#pageClock + span').evaluate(element => getComputedStyle(element).outlineStyle);
        assert.equal(outline, 'solid', 'Page checkbox exposes a keyboard focus indicator');
        if (process.env.AUDIT_SCREENSHOT_DIR) {
            fs.mkdirSync(process.env.AUDIT_SCREENSHOT_DIR, {recursive:true});
            for (const width of [375,1280]) {
                await page.setViewportSize({width,height:900});
                await page.screenshot({path:path.join(process.env.AUDIT_SCREENSHOT_DIR, `dashboard-${width}.png`),fullPage:true});
            }
        }
        const otaResponse=await page.goto(origin+'/update');
        if(process.env.WEB_ASSET_HEADER) assert.equal(otaResponse.headers()['content-encoding'],'gzip');
        assert.equal(await page.locator('input[type="file"]').count(),1);
        for (const width of [320,375,1280]) {
            await page.setViewportSize({width,height:900});
            const sizes=await page.evaluate(()=>({scroll:document.documentElement.scrollWidth,client:document.documentElement.clientWidth}));
            assert(sizes.scroll<=sizes.client,`OTA page overflows at ${width}px`);
        }
        assert.deepEqual(errors, [], 'Embedded UI has no browser errors');
        console.log('Browser UI checks passed: login focus, inert background, chip focus, preview/save recovery, all six tabs and OTA at 320/375/1280px');
    } finally {
        await browser.close();
        if(fixtureServer) await new Promise(resolve=>fixtureServer.close(resolve));
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
