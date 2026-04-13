/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */

/**
 * metal Desktop — main controller.
 * Tab switching, keyboard dispatch, architecture WebSocket.
 * Tabs: DISCOVER (default) | TRY | ARCHITECTURE | TRACE
 */
(function () {
  'use strict';

  let activeTab = 'loop';

  // --- Auth Gate ---
  // Block metal Desktop until authenticated. Waits for loop WS to be ready.
  var authGate = document.getElementById('auth-gate');
  var authAuthenticated = false;

  function initAuthGate() {
    var gate = document.getElementById('auth-gate');
    var cockpit = document.getElementById('cockpit');
    var loginBtn = document.getElementById('auth-login-btn');
    var registerBtn = document.getElementById('auth-register-btn');
    var verifyBtn = document.getElementById('auth-qr-verify-btn');
    var userInput = document.getElementById('auth-user');
    var totpInput = document.getElementById('auth-totp');
    var errorSpan = document.getElementById('auth-error');
    var qrView = document.getElementById('auth-qr-view');
    var formView = document.getElementById('auth-form');
    var qrTotpInput = document.getElementById('auth-qr-totp');
    var qrErrorSpan = document.getElementById('auth-qr-error');
    var qrCanvas = document.getElementById('auth-qr-canvas');

    if (!gate) return;

    cockpit.style.display = 'none';

    loginBtn.addEventListener('click', function() {
      var user = userInput.value.trim();
      var totp = totpInput.value.trim();
      if (!user || !totp) { errorSpan.textContent = 'enter user and TOTP'; return; }
      errorSpan.textContent = 'authenticating...';
      loginBtn.disabled = true;
      LoopController.sendRaw({ type: 'relay_auth', user: user, totp: totp });
    });

    totpInput.addEventListener('keydown', function(e) {
      if (e.key === 'Enter') loginBtn.click();
    });

    registerBtn.addEventListener('click', function() {
      var user = userInput.value.trim();
      if (!user) { errorSpan.textContent = 'enter a username first'; return; }
      errorSpan.textContent = 'registering...';
      registerBtn.disabled = true;
      LoopController.sendRaw({ type: 'relay_register', user: user });
    });

    verifyBtn.addEventListener('click', function() {
      var user = userInput.value.trim();
      var totp = qrTotpInput.value.trim();
      if (!totp) { qrErrorSpan.textContent = 'enter the TOTP code'; return; }
      qrErrorSpan.textContent = 'verifying...';
      verifyBtn.disabled = true;
      LoopController.sendRaw({ type: 'relay_auth', user: user, totp: totp });
    });

    qrTotpInput.addEventListener('keydown', function(e) {
      if (e.key === 'Enter') verifyBtn.click();
    });

    window.AuthGate = {
      handleEvent: function(msg) {
        if (msg.type === 'relay_auth_result') {
          loginBtn.disabled = false;
          document.getElementById('auth-qr-verify-btn').disabled = false;
          if (msg.status === 'ok') {
            // Authenticated — dismiss gate
            authAuthenticated = true;
            gate.classList.add('hidden');
            cockpit.style.display = '';
            var li = document.getElementById('loop-input');
            if (li) li.focus();
          } else if (msg.status === 'not_found') {
            errorSpan.textContent = 'user not found — register below';
          } else {
            errorSpan.textContent = 'rejected — wrong code';
            qrErrorSpan.textContent = 'wrong code — try again';
          }
        }
        else if (msg.type === 'relay_register_result') {
          registerBtn.disabled = false;
          if (msg.status === 'ok') {
            // Show QR view
            formView.style.display = 'none';
            qrView.classList.remove('hidden');
            errorSpan.textContent = '';
            // Render QR on canvas
            if (msg.qr_data && msg.qr_size) {
              renderQR(qrCanvas, msg.qr_data, msg.qr_size);
            }
            qrTotpInput.focus();
          } else if (msg.status === 'exists') {
            errorSpan.textContent = 'user already exists — login instead';
          } else {
            errorSpan.textContent = 'registration failed: ' + (msg.message || msg.status);
          }
        }
      }
    };

    userInput.focus();
  }

  function renderQR(canvas, b64Data, size) {
    // Decode base64 → module array (1=dark, 0=light)
    var raw = atob(b64Data);
    var modules = new Uint8Array(raw.length);
    for (var i = 0; i < raw.length; i++) modules[i] = raw.charCodeAt(i);

    var px = 6;       // pixels per module
    var quiet = 4;    // quiet zone in modules
    var total = size + quiet * 2;
    canvas.width = total * px;
    canvas.height = total * px;

    var ctx = canvas.getContext('2d');
    // White background
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    // Dark modules
    ctx.fillStyle = '#000000';
    for (var y = 0; y < size; y++) {
      for (var x = 0; x < size; x++) {
        if (modules[y * size + x]) {
          ctx.fillRect((x + quiet) * px, (y + quiet) * px, px, px);
        }
      }
    }
  }

  // --- Initialize ---

  initAuthGate();
  LoopController.init();
  StoreController.init();
  setupTabs();

  // --- Tab switching ---

  function setupTabs() {
    document.querySelectorAll('#tabs .tab').forEach(btn => {
      btn.addEventListener('click', () => switchTab(btn.dataset.tab));
    });
  }

  function switchTab(tab) {
    activeTab = tab;

    document.querySelectorAll('#tabs .tab').forEach(btn => {
      btn.classList.toggle('active', btn.dataset.tab === tab);
    });

    document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
    var target = document.getElementById(tab + '-view');
    if (target) target.classList.add('active');

    if (tab === 'loop') {
      var input = document.getElementById('loop-input');
      if (input) input.focus();
    } else if (tab === 'store') {
      StoreController.refresh();
    }
  }

  // --- Keyboard ---

  document.addEventListener('keydown', (e) => {
    var active = document.activeElement;
    if (active && (active.tagName === 'INPUT' || active.tagName === 'TEXTAREA')) {
      if (e.key === 'Escape') {
        active.blur();
        e.preventDefault();
      }
      return;
    }

    if (e.key === '1') { e.preventDefault(); switchTab('loop'); return; }
    if (e.key === '2') { e.preventDefault(); switchTab('store'); return; }
  });

})();
