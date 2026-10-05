const { chromium } = require('playwright');

const fatalPattern = /shader compile error|program link error|webgl error|abort\(|runtimeerror|pageerror/i;

function inspectPage(page, label) {
  const messages = [];
  page.on('console', message => {
    const line = `[${label}:${message.type()}] ${message.text()}`;
    messages.push(line);
    console.log(line);
  });
  page.on('pageerror', error => {
    const line = `[${label}:pageerror] ${error.message}`;
    messages.push(line);
    console.error(line);
  });
  return messages;
}

async function closeWithTimeout(browser) {
  if (!browser) return;
  await Promise.race([
    browser.close().catch(() => {}),
    new Promise(resolve => setTimeout(resolve, 3000))
  ]);
}

async function run() {
  let browser;
  try {
    browser = await chromium.launch({
      headless: true,
      args: ['--use-gl=swiftshader', '--enable-webgl', '--ignore-gpu-blocklist']
    });

    const desktop = await browser.newPage({ viewport: { width: 1280, height: 720 } });
    desktop.setDefaultTimeout(10000);
    desktop.setDefaultNavigationTimeout(15000);
    const desktopMessages = inspectPage(desktop, 'desktop');

    const response = await desktop.goto('http://127.0.0.1:8000', { waitUntil: 'domcontentloaded' });
    if (!response || !response.ok()) {
      throw new Error(`Desktop HTTP load failed: ${response ? response.status() : 'no response'}`);
    }

    await desktop.waitForFunction(() => {
      const canvas = document.querySelector('#canvas');
      return !!canvas && canvas.width > 0 && canvas.height > 0 &&
        !!canvas.getContext('webgl2') &&
        typeof Module !== 'undefined' &&
        typeof Module._web_set_mobile === 'function';
    }, null, { timeout: 20000 });

    const desktopState = await desktop.evaluate(() => {
      const canvas = document.querySelector('#canvas');
      const gl = canvas.getContext('webgl2');
      const touch = document.querySelector('#touch-controls');
      return {
        width: canvas.width,
        height: canvas.height,
        webgl2: !!gl,
        glError: gl ? gl.getError() : null,
        touchControlsHidden: !touch || getComputedStyle(touch).display === 'none'
      };
    });
    console.log('desktop state:', JSON.stringify(desktopState));
    if (!desktopState.webgl2 || !desktopState.touchControlsHidden) {
      throw new Error('Desktop runtime did not initialize correctly');
    }

    try {
      await desktop.screenshot({ path: 'runtime-smoke.png', fullPage: false, timeout: 3000 });
    } catch (error) {
      console.warn('desktop screenshot skipped:', error.message);
    }

    const mobileContext = await browser.newContext({
      viewport: { width: 844, height: 390 },
      hasTouch: true,
      isMobile: true
    });
    const mobile = await mobileContext.newPage();
    mobile.setDefaultTimeout(10000);
    mobile.setDefaultNavigationTimeout(15000);
    const mobileMessages = inspectPage(mobile, 'mobile');

    const mobileResponse = await mobile.goto('http://127.0.0.1:8000/?touch=1', { waitUntil: 'domcontentloaded' });
    if (!mobileResponse || !mobileResponse.ok()) {
      throw new Error(`Mobile HTTP load failed: ${mobileResponse ? mobileResponse.status() : 'no response'}`);
    }

    await mobile.waitForFunction(() => {
      const menu = document.querySelector('#mobile-title-menu');
      return typeof Module !== 'undefined' &&
        typeof Module._mobile_input === 'function' &&
        typeof Module._web_set_mobile === 'function' &&
        !!menu && getComputedStyle(menu).display !== 'none';
    }, null, { timeout: 20000 });

    const titleState = await mobile.evaluate(() => ({
      menuVisible: getComputedStyle(document.querySelector('#mobile-title-menu')).display !== 'none',
      freeSkateEnabled: !!document.querySelector('[data-menu-action="9"]:not(:disabled)')
    }));
    console.log('mobile title state:', JSON.stringify(titleState));
    if (!titleState.menuVisible || !titleState.freeSkateEnabled) {
      throw new Error('Mobile title menu did not initialize correctly');
    }

    await mobile.evaluate(() => {
      const button = document.querySelector('[data-menu-action="9"]');
      button.dispatchEvent(new PointerEvent('pointerdown', {
        bubbles: true, cancelable: true, pointerId: 7, pointerType: 'touch', isPrimary: true
      }));
    });

    await mobile.waitForFunction(() => {
      const menu = document.querySelector('#mobile-title-menu');
      const dpad = document.querySelector('#dpad');
      const actions = document.querySelector('#actions');
      return menu && dpad && actions &&
        getComputedStyle(menu).display === 'none' &&
        getComputedStyle(dpad).display !== 'none' &&
        getComputedStyle(actions).display !== 'none';
    }, null, { timeout: 10000 });

    const fatal = [...desktopMessages, ...mobileMessages].filter(line => fatalPattern.test(line));
    await mobileContext.close();
    if (fatal.length) {
      throw new Error('Browser runtime reported fatal rendering errors:\n' + fatal.join('\n'));
    }
  } finally {
    await closeWithTimeout(browser);
  }
}

Promise.race([
  run(),
  new Promise((_, reject) =>
    setTimeout(() => reject(new Error('Browser smoke test exceeded 60 seconds')), 60000))
]).catch(error => {
  console.error(error.stack || error);
  process.exitCode = 1;
});
