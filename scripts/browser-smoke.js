const { chromium } = require('playwright');

(async () => {
  const browser = await chromium.launch({
    headless: true,
    args: ['--use-gl=swiftshader', '--enable-webgl', '--ignore-gpu-blocklist']
  });

  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  const messages = [];

  page.on('console', message => {
    const line = `[${message.type()}] ${message.text()}`;
    messages.push(line);
    console.log(line);
  });

  page.on('pageerror', error => {
    const line = `[pageerror] ${error.message}`;
    messages.push(line);
    console.error(line);
  });

  const response = await page.goto('http://127.0.0.1:8000', {
    waitUntil: 'load',
    timeout: 30000
  });

  if (!response || !response.ok()) {
    throw new Error(`HTTP load failed: ${response ? response.status() : 'no response'}`);
  }

  await page.waitForTimeout(5000);

  const state = await page.evaluate(() => {
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
      glError: gl ? gl.getError() : null
    };
  });

  console.log('browser state:', JSON.stringify(state));

  if (!state.canvas || !state.webgl2 || state.width === 0 || state.height === 0) {
    throw new Error('WebGL2 canvas was not initialized correctly');
  }

  const fatal = messages.filter(line =>
    /shader compile error|program link error|webgl error|abort\(|runtimeerror|pageerror/i.test(line)
  );

  await page.screenshot({ path: 'runtime-smoke.png', fullPage: true });
  await browser.close();

  if (fatal.length) {
    throw new Error('Browser runtime reported fatal rendering errors:\n' + fatal.join('\n'));
  }
})();
