// Run against the real embedded HTML with mocked device endpoints; no TV is contacted.
// PLAYWRIGHT_MODULE_PATH may point to a Playwright installation outside this repository.
const fs = require('fs');
const path = require('path');
const assert = require('assert').strict;
const { chromium } = require(process.env.PLAYWRIGHT_MODULE_PATH || 'playwright');
const source = fs.readFileSync(path.join(__dirname, '../src/webui.h'), 'utf8');
const html = source.match(/const char index_html\[\] PROGMEM = R"rawliteral\(([\s\S]*?)\)rawliteral";/)[1];
(async () => {
    const browser = await chromium.launch({headless: true, executablePath: process.env.BROWSER_EXECUTABLE});
    try {
        const page = await browser.newPage();
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.route('http://device.test/**', async route => {
            const endpoint = new URL(route.request().url()).pathname;
            if (endpoint === '/') return route.fulfill({contentType: 'text/html', body: html});
            const fixtures = {
                '/auth/status': {authenticated: false, canRevealPassword: true, deviceName: 'Test TV', hostName: 'test-tv'},
                '/app.json': {deviceName: 'LivingRoomClockWithALongNameHere', hostName: 'livingroomclockwithalongnamehere', brt: 70},
                '/dashboard.json': {config: {}, data: {}, meta: {}},
                '/feeds.json': {config: {}, status: {}, meta: {}},
                '/version.json': {version: 'test'},
                '/space.json': {total: 1024, free: 512}
            };
            await route.fulfill({contentType: 'application/json', body: JSON.stringify(fixtures[endpoint] || {})});
        });
        await page.goto('http://device.test/');
        await page.waitForFunction(() => document.getElementById('authHint').textContent.includes('password'));
        assert(await page.locator('#appRoot').evaluate(element => element.inert));
        await page.locator('#authPassword').focus();
        await page.keyboard.press('Shift+Tab');
        assert.equal(await page.evaluate(() => document.activeElement.id), 'authRevealButton');
        await page.keyboard.press('Tab');
        assert.equal(await page.evaluate(() => document.activeElement.id), 'authPassword');
        await page.evaluate(async () => {hideAuthOverlay(); await loadAllState();});
        assert.equal(await page.evaluate(() => document.activeElement.dataset.tab), 'display');
        assert.equal(await page.locator('#appRoot').evaluate(element => element.inert), false);
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
        assert.deepEqual(errors, [], 'Embedded UI has no browser errors');
        console.log('Browser UI checks passed: login focus, inert background, chip focus, all six tabs at 320/375/1280px');
    } finally {
        await browser.close();
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
