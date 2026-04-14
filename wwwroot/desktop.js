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
    var verifyDomainBtn = document.getElementById('auth-verify-domain-btn');
    var userInput = document.getElementById('auth-user');
    var totpInput = document.getElementById('auth-totp');
    var errorSpan = document.getElementById('auth-error');
    var qrView = document.getElementById('auth-qr-view');
    var formView = document.getElementById('auth-form');
    var challengeView = document.getElementById('auth-challenge-view');
    var challengeToken = document.getElementById('auth-challenge-token');
    var challengeError = document.getElementById('auth-challenge-error');
    var wkInstructions = document.getElementById('auth-wk-instructions');
    var cfInstructions = document.getElementById('auth-cf-instructions');
    var cfTokenInput = document.getElementById('auth-cf-token');
    var qrTotpInput = document.getElementById('auth-qr-totp');
    var qrErrorSpan = document.getElementById('auth-qr-error');
    var qrCanvas = document.getElementById('auth-qr-canvas');
    var methodRadios = document.getElementsByName('auth-method');
    var resetLink = document.getElementById('auth-reset-totp-link');
    var authMode = 'register'; /* 'register' or 'reset' — controls which verify action to send */

    if (!gate) return;

    cockpit.style.display = 'none';

    /* Method radio toggle */
    for (var i = 0; i < methodRadios.length; i++) {
      methodRadios[i].addEventListener('change', function() {
        if (this.value === 'cloudflare') {
          wkInstructions.classList.add('hidden');
          cfInstructions.classList.remove('hidden');
        } else {
          cfInstructions.classList.add('hidden');
          wkInstructions.classList.remove('hidden');
        }
      });
    }

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

    /* Reset TOTP link */
    if (resetLink) {
      resetLink.addEventListener('click', function(e) {
        e.preventDefault();
        var user = userInput.value.trim();
        if (!user) { errorSpan.textContent = 'enter your username first'; return; }
        errorSpan.textContent = 'requesting reset...';
        authMode = 'reset';
        LoopController.sendRaw({ type: 'relay_reset_totp', user: user });
      });
    }

    /* Verify domain ownership */
    verifyDomainBtn.addEventListener('click', function() {
      var user = userInput.value.trim();
      var method = 'wellknown';
      for (var i = 0; i < methodRadios.length; i++) {
        if (methodRadios[i].checked) { method = methodRadios[i].value; break; }
      }
      var msgType = (authMode === 'reset') ? 'relay_verify_reset' : 'relay_verify_register';
      var payload = { type: msgType, user: user, method: method };
      if (method === 'cloudflare') {
        var token = cfTokenInput.value.trim();
        if (!token) { challengeError.textContent = 'enter Cloudflare API token'; return; }
        payload.cf_token = token;
      }
      challengeError.textContent = 'verifying...';
      verifyDomainBtn.disabled = true;
      LoopController.sendRaw(payload);
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
          verifyBtn.disabled = false;
          if (msg.status === 'ok') {
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
          if (msg.status === 'verify') {
            /* Step 2: show challenge view */
            formView.style.display = 'none';
            challengeView.classList.remove('hidden');
            challengeToken.textContent = msg.challenge || '';
            challengeError.textContent = '';
          } else if (msg.status === 'ok') {
            /* Direct QR (legacy path) */
            formView.style.display = 'none';
            qrView.classList.remove('hidden');
            errorSpan.textContent = '';
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
        else if (msg.type === 'relay_verify_result' || msg.type === 'relay_verify_reset_result') {
          verifyDomainBtn.disabled = false;
          if (msg.status === 'ok') {
            /* Domain verified — show QR */
            challengeView.classList.add('hidden');
            qrView.classList.remove('hidden');
            challengeError.textContent = '';
            if (msg.qr_data && msg.qr_size) {
              renderQR(qrCanvas, msg.qr_data, msg.qr_size);
            }
            qrTotpInput.focus();
          } else {
            challengeError.textContent = 'verification failed: ' + (msg.message || msg.status);
          }
        }
        else if (msg.type === 'relay_reset_result') {
          if (msg.status === 'verify') {
            /* Show challenge view for reset */
            formView.style.display = 'none';
            challengeView.classList.remove('hidden');
            challengeToken.textContent = msg.challenge || '';
            challengeError.textContent = '';
            errorSpan.textContent = '';
          } else {
            errorSpan.textContent = 'reset failed: ' + (msg.message || msg.status);
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
