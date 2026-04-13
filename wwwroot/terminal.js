/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */

/**
 * Terminal controller — xterm.js init, WebSocket attach, resize handling.
 */
const TerminalController = (() => {
  let term = null;
  let ws = null;
  let fitAddon = null;
  let statusEl = null;
  let reconnectTimer = null;
  let currentCmd = null;
  let currentCwd = null;
  let dataHandlerAttached = false;

  function init(container, statusElement) {
    statusEl = statusElement;

    term = new Terminal({
      cursorBlink: true,
      fontSize: 13,
      fontFamily: "'Cascadia Code', 'Consolas', 'Courier New', monospace",
      theme: {
        background: '#1e1e1e',
        foreground: '#cccccc',
        cursor: '#cccccc',
        selectionBackground: '#264f78',
        black: '#1e1e1e',
        red: '#f44747',
        green: '#6a9955',
        yellow: '#d4a537',
        blue: '#4fc1ff',
        magenta: '#c586c0',
        cyan: '#4ec9b0',
        white: '#cccccc',
        brightBlack: '#808080',
        brightRed: '#f44747',
        brightGreen: '#6a9955',
        brightYellow: '#d4a537',
        brightBlue: '#4fc1ff',
        brightMagenta: '#c586c0',
        brightCyan: '#4ec9b0',
        brightWhite: '#ffffff'
      },
      allowProposedApi: true
    });

    fitAddon = new FitAddon.FitAddon();
    term.loadAddon(fitAddon);
    term.open(container);
    fitAddon.fit();

    // Register input handler once
    term.onData(data => {
      if (ws && ws.readyState === 1) {
        ws.send(new TextEncoder().encode(data));
      }
    });

    // Resize observer — notify server when terminal size changes
    const ro = new ResizeObserver(() => {
      if (fitAddon) {
        fitAddon.fit();
        sendResize();
      }
    });
    ro.observe(container);

    connect();
  }

  function connect(cmd, cwd) {
    if (ws && ws.readyState <= 1) {
      ws.close();
    }

    currentCmd = cmd || null;
    currentCwd = cwd || null;

    // Pass initial size so server creates ConPTY at the right dimensions
    const params = new URLSearchParams();
    if (cmd) {
      params.set('cmd', 'cmd.exe');
      params.set('args', '/k ' + cmd);
    }
    if (cwd) {
      params.set('cwd', cwd);
    }
    if (term) {
      params.set('cols', term.cols);
      params.set('rows', term.rows);
    }

    const url = `ws://${location.host}/ws/terminal${params.toString() ? '?' + params : ''}`;
    setStatus('CONNECTING');

    ws = new WebSocket(url);
    ws.binaryType = 'arraybuffer';

    let msgCount = 0;
    let byteCount = 0;

    ws.onopen = () => {
      setStatus('CONNECTED');
    };

    ws.onmessage = (ev) => {
      msgCount++;
      const dataType = typeof ev.data;
      const isAB = ev.data instanceof ArrayBuffer;
      const isBlob = ev.data instanceof Blob;
      const size = ev.data.byteLength || ev.data.length || ev.data.size || 0;
      byteCount += size;
      setStatus(`${msgCount}msg ${byteCount}B`);

      if (isAB) {
        term.write(new Uint8Array(ev.data));
      } else if (isBlob) {
        ev.data.arrayBuffer().then(buf => term.write(new Uint8Array(buf)));
      } else if (dataType === 'string') {
        term.write(ev.data);
      }
    };

    ws.onclose = () => {
      setStatus('DISCONNECTED');
      scheduleReconnect();
    };

    ws.onerror = () => {
      setStatus('ERROR');
    };
  }

  function sendResize() {
    if (!ws || ws.readyState !== 1 || !term) return;
    ws.send(JSON.stringify({
      type: 'resize',
      cols: term.cols,
      rows: term.rows
    }));
  }

  function relaunch(cmd, cwd) {
    term.clear();
    connect(cmd, cwd);
  }

  function focus() {
    if (term) term.focus();
  }

  function blur() {
    if (term) term.blur();
  }

  function scheduleReconnect() {
    if (reconnectTimer) return;
    reconnectTimer = setTimeout(() => {
      reconnectTimer = null;
      connect(currentCmd, currentCwd);
    }, 3000);
  }

  function setStatus(text) {
    if (statusEl) statusEl.textContent = text;
  }

  function getTerminal() {
    return term;
  }

  return { init, connect, relaunch, focus, blur, getTerminal };
})();
