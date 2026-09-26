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

async function dispatchPointer(page, selector, type, init) {
  await page.evaluate(({ selector, type, init }) => {
    const element = document.querySelector(selector);
    if (!element) throw new Error(`Missing pointer target: ${selector}`);

    element.dispatchEvent(new PointerEvent(type, {
      bubbles: true,
      cancelable: true,
      ...init
    }));
  }, { selector, type, init });
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
  await desktop.keyboard.press('Enter');
  await desktop.waitForTimeout(1200);
  await desktop.screenshot({ path: 'runtime-smoke-gameplay.png', fullPage: true });

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

  const mobileTitleState = await mobile.evaluate(() => {
    const menu = document.querySelector('#mobile-title-menu');
    const dpad = document.querySelector('#dpad');
    const actions = document.querySelector('#actions');
    const freeSkate = document.querySelector('[data-menu-action="9"]');
    return {
      menuVisible: menu && getComputedStyle(menu).display !== 'none',
      dpadHidden: dpad && getComputedStyle(dpad).display === 'none',
      actionsHidden: actions && getComputedStyle(actions).display === 'none',
      setter: typeof Module._web_set_mobile === 'function',
      freeSkatePresent: !!freeSkate,
      freeSkateEnabled: !!freeSkate && !freeSkate.disabled
    };
  });
  console.log('mobile title state:', JSON.stringify(mobileTitleState));
  if (!mobileTitleState.menuVisible || !mobileTitleState.dpadHidden ||
      !mobileTitleState.actionsHidden || !mobileTitleState.setter ||
      !mobileTitleState.freeSkatePresent || !mobileTitleState.freeSkateEnabled) {
    throw new Error('Mobile title menu did not initialize correctly');
  }

  await dispatchPointer(mobile, '[data-menu-action="9"]', 'pointerdown', {
    pointerId: 7, pointerType: 'touch', isPrimary: true
  });
  await mobile.waitForTimeout(250);

  const afterStartState = await mobile.evaluate(() => ({
    menuHidden: getComputedStyle(document.querySelector('#mobile-title-menu')).display === 'none',
    dpadVisible: getComputedStyle(document.querySelector('#dpad')).display !== 'none',
    actionsVisible: getComputedStyle(document.querySelector('#actions')).display !== 'none'
  }));
  console.log('mobile after start:', JSON.stringify(afterStartState));
  if (!afterStartState.menuHidden || !afterStartState.dpadVisible || !afterStartState.actionsVisible) {
    throw new Error('Mobile gameplay controls did not replace the title menu');
  }

  const mobileState = await mobile.evaluate(() => {
    const controls = document.querySelector('#touch-controls');
    const dpad = document.querySelector('#dpad');
    const joystickKnob = document.querySelector('#dpad-knob');
    const actions = [...document.querySelectorAll('[data-action]')].map(button => button.dataset.action);
    const canvas = document.querySelector('#canvas');
    const stage = document.querySelector('#stage');
    const rects = [...document.querySelectorAll('#dpad .control-button, #actions .control-button')]
      .map(element => {
        const rect = element.getBoundingClientRect();
        return { id: element.id, left: rect.left, top: rect.top, right: rect.right, bottom: rect.bottom };
      });
    return {
      touchEnabled: document.documentElement.classList.contains('touch-enabled'),
      controlsVisible: controls && getComputedStyle(controls).display !== 'none',
      dpadVisible: dpad && dpad.getBoundingClientRect().width > 0,
      joystickKnob: !!joystickKnob,
      actions,
      bridge: typeof Module._mobile_input === 'function',
      viewport: { width: innerWidth, height: innerHeight },
      stage: { width: stage.clientWidth, height: stage.clientHeight },
      canvas: { width: canvas.width, height: canvas.height, clientWidth: canvas.clientWidth, clientHeight: canvas.clientHeight },
      rects
    };
  });

  console.log('mobile state:', JSON.stringify(mobileState));
  const expectedActions = ['flip', 'grind', 'ollie', 'grab', 'manual'];
  if (!mobileState.touchEnabled || !mobileState.controlsVisible || !mobileState.dpadVisible ||
      !mobileState.bridge || !mobileState.joystickKnob) {
    throw new Error('Mobile controls did not initialize correctly');
  }
  for (const action of expectedActions) {
    if (!mobileState.actions.includes(action)) throw new Error(`Missing mobile action button: ${action}`);
  }

  function assertControlsInside(state, label) {
    const tolerance = 2;
    if (Math.abs(state.stage.width - state.viewport.width) > tolerance ||
        Math.abs(state.stage.height - state.viewport.height) > tolerance) {
      throw new Error(`${label}: stage does not match viewport: ${JSON.stringify(state)}`);
    }
    if (Math.abs(state.canvas.clientWidth - state.viewport.width) > tolerance ||
        Math.abs(state.canvas.clientHeight - state.viewport.height) > tolerance) {
      throw new Error(`${label}: canvas CSS size does not match viewport: ${JSON.stringify(state.canvas)}`);
    }
    for (const rect of state.rects) {
      if (rect.left < -tolerance || rect.top < -tolerance ||
          rect.right > state.viewport.width + tolerance ||
          rect.bottom > state.viewport.height + tolerance) {
        throw new Error(`${label}: control ${rect.id} is outside viewport: ${JSON.stringify(rect)}`);
      }
    }
  }

  assertControlsInside(mobileState, 'landscape');

  await mobile.setViewportSize({ width: 390, height: 844 });
  await mobile.evaluate(() => window.dispatchEvent(new Event('orientationchange')));
  await mobile.waitForTimeout(500);
  const portraitState = await mobile.evaluate(() => {
    const canvas = document.querySelector('#canvas');
    const stage = document.querySelector('#stage');
    const rects = [...document.querySelectorAll('#dpad .control-button, #actions .control-button')]
      .map(element => {
        const rect = element.getBoundingClientRect();
        return { id: element.id, left: rect.left, top: rect.top, right: rect.right, bottom: rect.bottom };
      });
    return {
      viewport: { width: innerWidth, height: innerHeight },
      stage: { width: stage.clientWidth, height: stage.clientHeight },
      canvas: { width: canvas.width, height: canvas.height, clientWidth: canvas.clientWidth, clientHeight: canvas.clientHeight },
      rects
    };
  });
  console.log('portrait state:', JSON.stringify(portraitState));
  assertControlsInside(portraitState, 'portrait');
  if (portraitState.canvas.height <= portraitState.canvas.width) {
    throw new Error('Canvas backing resolution did not rotate to portrait');
  }

  await mobile.setViewportSize({ width: 844, height: 390 });
  await mobile.evaluate(() => window.dispatchEvent(new Event('orientationchange')));
  await mobile.waitForTimeout(500);
  const landscapeAgainState = await mobile.evaluate(() => {
    const canvas = document.querySelector('#canvas');
    const stage = document.querySelector('#stage');
    const rects = [...document.querySelectorAll('#dpad .control-button, #actions .control-button')]
      .map(element => {
        const rect = element.getBoundingClientRect();
        return { id: element.id, left: rect.left, top: rect.top, right: rect.right, bottom: rect.bottom };
      });
    return {
      viewport: { width: innerWidth, height: innerHeight },
      stage: { width: stage.clientWidth, height: stage.clientHeight },
      canvas: { width: canvas.width, height: canvas.height, clientWidth: canvas.clientWidth, clientHeight: canvas.clientHeight },
      rects
    };
  });
  console.log('landscape-again state:', JSON.stringify(landscapeAgainState));
  assertControlsInside(landscapeAgainState, 'landscape-again');
  if (landscapeAgainState.canvas.width <= landscapeAgainState.canvas.height) {
    throw new Error('Canvas backing resolution did not rotate back to landscape');
  }

  await mobile.evaluate(() => {
    const originalMobileInput = Module._mobile_input;
    window.__mobileInputCalls = [];
    Module._mobile_input = (action, down) => {
      window.__mobileInputCalls.push([action, down]);
      return originalMobileInput(action, down);
    };
  });

  const ollieBox = await mobile.evaluate(() => {
    const rect = document.querySelector('[data-action="ollie"]').getBoundingClientRect();
    return { x: rect.x, y: rect.y, width: rect.width, height: rect.height };
  });
  if (!ollieBox || !ollieBox.width || !ollieBox.height) throw new Error('OLLIE button has no layout box');
  const ollieX = ollieBox.x + ollieBox.width / 2;
  const ollieY = ollieBox.y + ollieBox.height / 2;
  await mobile.dispatchEvent('[data-action="ollie"]', 'pointerdown', {
    pointerId: 11, pointerType: 'touch', clientX: ollieX, clientY: ollieY, isPrimary: false
  });
  await mobile.dispatchEvent('[data-action="ollie"]', 'pointerup', {
    pointerId: 11, pointerType: 'touch', clientX: ollieX, clientY: ollieY, isPrimary: false
  });

  const dpadBox = await mobile.evaluate(() => {
    const rect = document.querySelector('#dpad').getBoundingClientRect();
    return { x: rect.x, y: rect.y, width: rect.width, height: rect.height };
  });
  if (!dpadBox || !dpadBox.width || !dpadBox.height) throw new Error('D-pad has no layout box');
  const dpadX = dpadBox.x + dpadBox.width * .15;
  const dpadY = dpadBox.y + dpadBox.height * .15;
  await dispatchPointer(mobile, '#dpad', 'pointerdown', {
    pointerId: 22, pointerType: 'touch', clientX: dpadX, clientY: dpadY, isPrimary: true
  });
  await dispatchPointer(mobile, '#dpad', 'pointerup', {
    pointerId: 22, pointerType: 'touch', clientX: dpadX, clientY: dpadY, isPrimary: true
  });
  await mobile.waitForTimeout(100);

  await dispatchPointer(mobile, '#dpad', 'pointerdown', {
    pointerId: 23, pointerType: 'touch',
    clientX: dpadBox.x + dpadBox.width * .78,
    clientY: dpadBox.y + dpadBox.height * .50,
    isPrimary: true
  });
  const joystickActive = await mobile.evaluate(() => ({
    active: document.querySelector('#dpad').classList.contains('joystick-active'),
    x: getComputedStyle(document.querySelector('#dpad')).getPropertyValue('--joy-x').trim()
  }));
  await dispatchPointer(mobile, '#dpad', 'pointerup', {
    pointerId: 23, pointerType: 'touch',
    clientX: dpadBox.x + dpadBox.width * .78,
    clientY: dpadBox.y + dpadBox.height * .50,
    isPrimary: true
  });
  const joystickReleased = await mobile.evaluate(() => ({
    active: document.querySelector('#dpad').classList.contains('joystick-active'),
    x: getComputedStyle(document.querySelector('#dpad')).getPropertyValue('--joy-x').trim()
  }));
  console.log('joystick visual state:', JSON.stringify({ joystickActive, joystickReleased }));
  if (!joystickActive.active || !joystickActive.x || joystickActive.x === '0px' ||
      joystickReleased.active || joystickReleased.x !== '0px') {
    throw new Error('Joystick visual feedback did not move and reset correctly');
  }

  const inputCalls = await mobile.evaluate(() => window.__mobileInputCalls);
  console.log('mobile input calls:', JSON.stringify(inputCalls));
  for (const expected of [[4, 1], [4, 0], [0, 1], [2, 1], [0, 0], [2, 0]]) {
    if (!inputCalls.some(call => call[0] === expected[0] && call[1] === expected[1])) {
      throw new Error(`Missing mobile input transition: ${expected[0]},${expected[1]}`);
    }
  }

  await dispatchPointer(mobile, '#dpad', 'pointerdown', {
    pointerId: 31, pointerType: 'touch', clientX: dpadX, clientY: dpadY, isPrimary: true
  });
  await mobile.dispatchEvent('body', 'pointerout', {
    pointerId: 31, pointerType: 'touch', clientX: -1, clientY: -1, relatedTarget: null
  });
  await dispatchPointer(mobile, '#dpad', 'pointerdown', {
    pointerId: 32, pointerType: 'touch', clientX: dpadX, clientY: dpadY, isPrimary: true
  });
  await dispatchPointer(mobile, '#dpad', 'pointerup', {
    pointerId: 32, pointerType: 'touch', clientX: dpadX, clientY: dpadY, isPrimary: true
  });
  const recovered = await mobile.evaluate(() => ({
    active: [...document.querySelectorAll('#dpad .active')].map(element => element.id)
  }));
  console.log('stale dpad recovery:', JSON.stringify(recovered));
  if (recovered.active.length) throw new Error('D-pad remained stuck after pointer loss recovery');

  try {
    await mobile.screenshot({
      path: 'runtime-smoke-mobile.png',
      fullPage: false,
      timeout: 5000
    });
  } catch (error) {
    console.warn('mobile screenshot skipped:', error.message);
  }

  const fatal = [...desktopMessages, ...mobileMessages].filter(line => fatalPattern.test(line));
  await mobileContext.close();
  await browser.close();

  if (fatal.length) {
    throw new Error('Browser runtime reported fatal rendering errors:\n' + fatal.join('\n'));
  }
})();
