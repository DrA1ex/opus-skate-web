const { chromium } = require('playwright');

const fatalPattern = /shader compile error|program link error|webgl error|abort\(|runtimeerror|pageerror/i;

async function inspectPage(page, label) {
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

(async () => {
  const browser = await chromium.launch({
    headless: true,
    args: ['--use-gl=swiftshader', '--enable-webgl', '--ignore-gpu-blocklist']
  });

  const desktop = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  const desktopMessages = await inspectPage(desktop, 'desktop');
  let response = await desktop.goto('http://127.0.0.1:8000', { waitUntil: 'load', timeout: 30000 });

  if (!response || !response.ok()) {
    throw new Error(`Desktop HTTP load failed: ${response ? response.status() : 'no response'}`);
  }

  await desktop.waitForTimeout(4000);
  const desktopState = await desktop.evaluate(() => {
    const canvas = document.querySelector('#canvas');
    if (!canvas) return { canvas: false };

    const gl = canvas.getContext('webgl2');
    return {
      canvas: true,
      width: canvas.width,
      height: canvas.height,
      clientWidth: canvas.clientWidth,
      clientHeight: canvas.clientHeight,
      webgl2: !!gl,
      glError: gl ? gl.getError() : null,
      touchControlsHidden: getComputedStyle(document.querySelector('#touch-controls')).display === 'none'
    };
  });

  console.log('desktop state:', JSON.stringify(desktopState));
  if (!desktopState.canvas || !desktopState.webgl2 || desktopState.width === 0 || desktopState.height === 0) {
    throw new Error('Desktop WebGL2 canvas was not initialized correctly');
  }
  if (!desktopState.touchControlsHidden) {
    throw new Error('Touch controls are visible in desktop mode');
  }

  await desktop.screenshot({ path: 'runtime-smoke.png', fullPage: true });

  const mobileContext = await browser.newContext({
    viewport: { width: 844, height: 390 },
    hasTouch: true,
    isMobile: true
  });
  const mobile = await mobileContext.newPage();
  const mobileMessages = await inspectPage(mobile, 'mobile');
  response = await mobile.goto('http://127.0.0.1:8000/?touch=1', { waitUntil: 'load', timeout: 30000 });

  if (!response || !response.ok()) {
    throw new Error(`Mobile HTTP load failed: ${response ? response.status() : 'no response'}`);
  }

  await mobile.waitForTimeout(4000);
  const mobileState = await mobile.evaluate(() => {
    const controls = document.querySelector('#touch-controls');
    const dpad = document.querySelector('#dpad');
    const actions = [...document.querySelectorAll('[data-action]')].map(button => button.dataset.action);
    return {
      touchEnabled: document.documentElement.classList.contains('touch-enabled'),
      controlsVisible: controls && getComputedStyle(controls).display !== 'none',
      dpadVisible: dpad && dpad.getBoundingClientRect().width > 0,
      actions,
      bridge: typeof Module._mobile_input === 'function'
    };
  });

  console.log('mobile state:', JSON.stringify(mobileState));
  const expectedActions = ['flip', 'grind', 'ollie', 'grab', 'manual'];
  if (!mobileState.touchEnabled || !mobileState.controlsVisible || !mobileState.dpadVisible || !mobileState.bridge) {
    throw new Error('Mobile controls did not initialize correctly');
  }
  for (const action of expectedActions) {
    if (!mobileState.actions.includes(action)) throw new Error(`Missing mobile action button: ${action}`);
  }

  await mobile.evaluate(() => {
    const originalMobileInput = Module._mobile_input;
    window.__mobileInputCalls = [];
    Module._mobile_input = (action, down) => {
      window.__mobileInputCalls.push([action, down]);
      return originalMobileInput(action, down);
    };
  });

  const ollie = mobile.locator('[data-action="ollie"]');
  const ollieBox = await ollie.boundingBox();
  if (!ollieBox) throw new Error('OLLIE button has no layout box');
  const ollieX = ollieBox.x + ollieBox.width / 2;
  const ollieY = ollieBox.y + ollieBox.height / 2;
  await mobile.dispatchEvent('[data-action="ollie"]', 'pointerdown', {
    pointerId: 11, pointerType: 'touch', clientX: ollieX, clientY: ollieY, isPrimary: false
  });
  await mobile.dispatchEvent('[data-action="ollie"]', 'pointerup', {
    pointerId: 11, pointerType: 'touch', clientX: ollieX, clientY: ollieY, isPrimary: false
  });

  const dpad = mobile.locator('#dpad');
  const dpadBox = await dpad.boundingBox();
  if (!dpadBox) throw new Error('D-pad has no layout box');
  const dpadX = dpadBox.x + dpadBox.width * .15;
  const dpadY = dpadBox.y + dpadBox.height * .15;
  await mobile.dispatchEvent('#dpad', 'pointerdown', {
    pointerId: 22, pointerType: 'touch', clientX: dpadX, clientY: dpadY, isPrimary: true
  });
  await mobile.dispatchEvent('#dpad', 'pointerup', {
    pointerId: 22, pointerType: 'touch', clientX: dpadX, clientY: dpadY, isPrimary: true
  });
  await mobile.waitForTimeout(100);

  const inputCalls = await mobile.evaluate(() => window.__mobileInputCalls);
  console.log('mobile input calls:', JSON.stringify(inputCalls));
  for (const expected of [[4, 1], [4, 0], [0, 1], [2, 1], [0, 0], [2, 0]]) {
    if (!inputCalls.some(call => call[0] === expected[0] && call[1] === expected[1])) {
      throw new Error(`Missing mobile input transition: ${expected[0]},${expected[1]}`);
    }
  }

  await mobile.screenshot({ path: 'runtime-smoke-mobile.png', fullPage: true });

  const fatal = [...desktopMessages, ...mobileMessages].filter(line => fatalPattern.test(line));
  await mobileContext.close();
  await browser.close();

  if (fatal.length) {
    throw new Error('Browser runtime reported fatal rendering errors:\n' + fatal.join('\n'));
  }
})();
