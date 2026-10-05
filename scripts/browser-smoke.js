const { chromium } = require('playwright');

const fatalPattern = /shader compile error|program link error|webgl error|framebuffer ['"][^'"]+['"] incomplete|abort\(|runtimeerror|pageerror/i;

function inspectPage(page, label, onConsole) {
  const messages = [];

  page.on('console', message => {
    const text = message.text();
    const line = `[${label}:${message.type()}] ${text}`;
    messages.push(line);
    console.log(line);
    onConsole?.(text, line);
  });

  page.on('pageerror', error => {
    const line = `[${label}:pageerror] ${error.message}`;
    messages.push(line);
    console.error(line);
  });

  return messages;
}

function timeout(ms, message) {
  return new Promise((_, reject) => setTimeout(() => reject(new Error(message)), ms));
}

async function closeWithTimeout(browser) {
  if (!browser) return;
  await Promise.race([
    browser.close().catch(() => {}),
    new Promise(resolve => setTimeout(resolve, 3000))
  ]);
}

async function assertHttp(response, label) {
  if (!response || !response.ok()) {
    throw new Error(`${label} HTTP load failed: ${response ? response.status() : 'no response'}`);
  }
}

async function testShell(browser) {
  const desktop = await browser.newPage({ viewport: { width: 640, height: 360 } });
  await desktop.route('**/index.js', route => route.abort());
  const desktopMessages = inspectPage(desktop, 'desktop-shell');

  await assertHttp(
    await desktop.goto('http://127.0.0.1:8000', { waitUntil: 'domcontentloaded', timeout: 10000 }),
    'Desktop shell'
  );

  const desktopState = await desktop.evaluate(() => {
    const canvas = document.querySelector('#canvas');
    const touch = document.querySelector('#touch-controls');
    return {
      canvas: !!canvas,
      touchEnabled: document.documentElement.classList.contains('touch-enabled'),
      touchHidden: !touch || getComputedStyle(touch).display === 'none'
    };
  });

  if (!desktopState.canvas || desktopState.touchEnabled || !desktopState.touchHidden) {
    throw new Error(`Desktop shell state is invalid: ${JSON.stringify(desktopState)}`);
  }
  await desktop.close();

  const mobileContext = await browser.newContext({
    viewport: { width: 844, height: 390 },
    hasTouch: true,
    isMobile: true
  });
  const mobile = await mobileContext.newPage();
  await mobile.route('**/index.js', route => route.abort());
  const mobileMessages = inspectPage(mobile, 'mobile-shell');

  await assertHttp(
    await mobile.goto('http://127.0.0.1:8000/?touch=1', { waitUntil: 'domcontentloaded', timeout: 10000 }),
    'Mobile shell'
  );

  const beforeRuntime = await mobile.evaluate(() => ({
    touchEnabled: document.documentElement.classList.contains('touch-enabled'),
    menuVisible: getComputedStyle(document.querySelector('#mobile-title-menu')).display !== 'none',
    freeSkateDisabled: document.querySelector('[data-menu-action="9"]').disabled
  }));

  if (!beforeRuntime.touchEnabled || !beforeRuntime.menuVisible || !beforeRuntime.freeSkateDisabled) {
    throw new Error(`Mobile shell pre-runtime state is invalid: ${JSON.stringify(beforeRuntime)}`);
  }

  await mobile.evaluate(() => {
    window.__smokeInputCalls = [];
    window.__smokeMobileMode = null;
    Module._mobile_input = (action, down) => window.__smokeInputCalls.push([action, down]);
    Module._web_set_mobile = enabled => { window.__smokeMobileMode = enabled; };
    Module._web_resize = () => {};
    Module.onRuntimeInitialized();
  });

  const readyState = await mobile.evaluate(() => ({
    mobileMode: window.__smokeMobileMode,
    freeSkateEnabled: !document.querySelector('[data-menu-action="9"]').disabled
  }));

  if (readyState.mobileMode !== 1 || !readyState.freeSkateEnabled) {
    throw new Error(`Mobile shell runtime bridge did not initialize: ${JSON.stringify(readyState)}`);
  }

  await mobile.evaluate(() => {
    document.querySelector('[data-menu-action="9"]').dispatchEvent(new PointerEvent('pointerdown', {
      bubbles: true,
      cancelable: true,
      pointerId: 7,
      pointerType: 'touch',
      isPrimary: true
    }));
  });

  const afterStart = await mobile.evaluate(() => ({
    menuHidden: getComputedStyle(document.querySelector('#mobile-title-menu')).display === 'none',
    dpadVisible: getComputedStyle(document.querySelector('#dpad')).display !== 'none',
    actionsVisible: getComputedStyle(document.querySelector('#actions')).display !== 'none',
    calls: window.__smokeInputCalls
  }));

  const started = afterStart.calls.some(call => call[0] === 9 && call[1] === 1) &&
    afterStart.calls.some(call => call[0] === 9 && call[1] === 0);

  if (!afterStart.menuHidden || !afterStart.dpadVisible || !afterStart.actionsVisible || !started) {
    throw new Error(`Mobile shell start flow failed: ${JSON.stringify(afterStart)}`);
  }

  const shellFatal = [...desktopMessages, ...mobileMessages].filter(line => fatalPattern.test(line));
  await mobileContext.close();

  if (shellFatal.length) {
    throw new Error('Browser shell reported fatal errors:\n' + shellFatal.join('\n'));
  }
}

async function testRuntime(browser) {
  const runtime = await browser.newPage({ viewport: { width: 480, height: 270 } });

  let rendererReady;
  const ready = new Promise(resolve => { rendererReady = resolve; });
  const messages = inspectPage(runtime, 'runtime', text => {
    if (text.includes('web renderer targets ready')) rendererReady();
  });

  await assertHttp(
    await runtime.goto('http://127.0.0.1:8000', { waitUntil: 'domcontentloaded', timeout: 10000 }),
    'Runtime'
  );

  await Promise.race([
    ready,
    timeout(15000, 'Web renderer did not reach target initialization within 15 seconds')
  ]);

  const fatal = messages.filter(line => fatalPattern.test(line));
  if (fatal.length) {
    throw new Error('Browser runtime reported fatal rendering errors:\n' + fatal.join('\n'));
  }
}

async function run() {
  let browser;
  try {
    browser = await chromium.launch({
      headless: true,
      args: ['--use-gl=swiftshader', '--enable-webgl', '--ignore-gpu-blocklist']
    });

    await testShell(browser);
    await testRuntime(browser);
  } finally {
    await closeWithTimeout(browser);
  }
}

const watchdog = setTimeout(() => {
  console.error('Browser smoke test exceeded 45 seconds');
  process.exit(1);
}, 45000);
watchdog.unref();

run().then(() => {
  clearTimeout(watchdog);
  process.exit(0);
}).catch(error => {
  clearTimeout(watchdog);
  console.error(error.stack || error);
  process.exit(1);
});
