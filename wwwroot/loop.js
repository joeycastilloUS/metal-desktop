/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */

/**
 * metal Desktop — Loop controller, three-column layout.
 * Col 1: THINKING — routed expert selection
 * Col 2: LEARNING — 8 skill tiles (LANGUAGE→UNDERSTAND→READ→LEARN→REASON→ACQUIRE→MEMORY→CATALOG)
 * Col 3: DATA ACCESS — unified DAPs: LOCAL, CATALOG, WEB, AI
 * Status bar: sense + daily cost
 */
const LoopController = (() => {
  'use strict';

  let config = null;
  let pillarConfig = null;
  let expertise = {};
  let providers = [];
  let pinnedProviders = [];
  let ws = null;
  let inputEl = null;
  let sendBtn = null;
  let hints = {};

  // DOM refs — THINKING (col 1)
  let driverCol = null;
  let searchBody = null;
  let searchBadge = null;
  let traceOn = true;

  // DOM refs — columns
  let midCol = null;   // col 2: LEARNING
  let rightCol = null; // col 3: DATA ACCESS
  let routeMode = 'local'; // 'local' or 'mesh'

  // DOM refs — intelligence skill tiles (8 skills)
  let languageBody = null;
  let languageBadge = null;
  let understandBody = null;
  let understandBadge = null;
  let readBody = null;
  let readBadge = null;
  let learnBody = null;
  let learnBadge = null;
  let reasonBody = null;
  let reasonBadge = null;
  let acquireBody = null;
  let acquireBadge = null;
  let memoryBody = null;
  let memoryBadge = null;
  let catalogBody = null;
  let catalogBadge = null;
  let providerBodies = {};   // name → body element
  let providerPanels = {};   // name → panel element
  let providerCosts = {};    // name → cost element
  let providerTraces = {};   // name → tron trace line element
  let providerRequests = {}; // name → request body string
  let providerFooters = {};  // name → footer element

  // DOM refs — status
  let senseDisplay = null;
  let dailyDisplay = null;

  // Hint editor
  let hintEditor = null;
  let hintText = null;
  let hintProviderName = null;
  let editingProvider = null;

  // Global hint
  let globalHint = '';
  let globalHintBar = null;
  let globalHintTextEl = null;
  let globalHintClearBtn = null;

  // Cost tracking per provider
  let queryCost = 0.0;
  let queryTokensIn = 0;
  let queryTokensOut = 0;
  let sessionCosts = {};    // name → { last: 0, total: 0, actual: false, tokIn: 0, tokOut: 0, totalTokIn: 0, totalTokOut: 0 }
  let disabledProviders = {};  // name → true/false

  let selectedEngine = '';  // '' = Auto

  // Stock ticker state
  let tickerEl = null;
  let tickerTrack = null;
  let tickerAnimId = null;
  let tickerOffset = 0;
  let tickerSpeed = 0.5;  // px per frame

  // Collapse state — all start collapsed
  let expandedProviders = {};  // name → true if expanded
  let allExpanded = false;
  let currentSort = 'default';  // 'default' | 'cost' | 'time' | 'rank'
  let providerElapsed = {};    // name → ms elapsed
  let providerModelMap = {};   // name → model (long name like claude-opus-4-6)

  // Query progress tracking
  let queryPending = {};   // provider → true while waiting
  let queryDone = 0;
  let queryTotal = 0;
  let senseHtml = '';  // pillar info HTML (stable part of sense display)
  let goStartTime = 0;     // performance.now() when query starts
  let lastGoElapsed = 0;   // ms from backend
  let lastGoResponded = 0; // how many providers responded
  let lastGoAttempted = 0; // how many providers attempted

  // Judge progress tracking
  let judgeTotal = 0;
  let judgeCompleted = 0;
  let judgePending = {};  // provider → true while waiting
  let judgeScores = {};   // ranked provider → { totalRank, votes }
  let judgeCosts = {};     // voter provider → cost
  let judgeLeaderboardEl = null;  // live leaderboard DOM element
  let lastJudgeElapsed = 0;  // ms from backend
  let lastJudgeVotes = 0;    // how many judges voted

  // Input history
  let history = [];
  let historyIdx = -1;
  let historyDraft = '';

  async function init() {
    inputEl = document.getElementById('loop-input');
    sendBtn = document.getElementById('loop-send');
    driverCol = document.getElementById('loop-driver');
    searchBody = document.getElementById('search-body');
    searchBadge = document.getElementById('search-badge');
    midCol = document.getElementById('loop-mid');
    rightCol = document.getElementById('loop-right');
    senseDisplay = document.getElementById('sense-display');
    dailyDisplay = document.getElementById('daily-display');
    hintEditor = document.getElementById('hint-editor');
    hintText = document.getElementById('hint-text');
    hintProviderName = document.getElementById('hint-provider-name');
    globalHintBar = document.getElementById('global-hint-bar');
    globalHintTextEl = document.getElementById('global-hint-text');
    globalHintClearBtn = document.getElementById('global-hint-clear');
    if (globalHintClearBtn) {
      globalHintClearBtn.addEventListener('click', function() {
        setGlobalHint('');
      });
    }

    // Load config from store
    try {
      var resp = await fetch('/api/loop-config');
      config = await resp.json();
      providers = (config.providers || []).map(function(p) { return p.name; });
      // Build name→model lookup (long name is primary display)
      (config.providers || []).forEach(function(p) {
        if (p.model) providerModelMap[p.name] = p.model;
      });
    } catch (e) {
      console.error('[loop] Failed to load config:', e);
      providers = [];
    }

    // Load pillar config
    try {
      var resp2 = await fetch('/api/pillar-config');
      pillarConfig = await resp2.json();
      expertise = buildExpertise(pillarConfig);
    } catch (e) {
      console.error('[loop] Failed to load pillar config:', e);
    }

    // Load pinned providers
    try {
      var resp3 = await fetch('/api/pinned');
      pinnedProviders = await resp3.json();
    } catch (e) {
      pinnedProviders = [];
    }

    // Reorder: pinned providers first
    if (pinnedProviders.length > 0) {
      var pinSet = {};
      pinnedProviders.forEach(function(n) { pinSet[n.toLowerCase()] = true; });
      var pinned = providers.filter(function(n) { return pinSet[n.toLowerCase()]; });
      var rest = providers.filter(function(n) { return !pinSet[n.toLowerCase()]; });
      providers = pinned.concat(rest);
    }

    buildProviderPanels();
    buildEngineSelector();

    if (sendBtn) sendBtn.addEventListener('click', send);
    if (inputEl) {
      inputEl.addEventListener('keydown', function(e) {
        if (e.key === 'Enter' && !e.shiftKey) {
          e.preventDefault();
          send();
        } else if (e.key === 'ArrowUp') {
          e.preventDefault();
          if (history.length === 0) return;
          if (historyIdx === -1) {
            historyDraft = inputEl.value;
            historyIdx = history.length - 1;
          } else if (historyIdx > 0) {
            historyIdx--;
          }
          inputEl.value = history[historyIdx];
        } else if (e.key === 'ArrowDown') {
          e.preventDefault();
          if (historyIdx === -1) return;
          if (historyIdx < history.length - 1) {
            historyIdx++;
            inputEl.value = history[historyIdx];
          } else {
            historyIdx = -1;
            inputEl.value = historyDraft;
          }
        }
      });
    }

    // Command menu
    buildCommandMenu();

    // Hint editor buttons
    var saveBtn = document.getElementById('hint-save');
    var clearBtn = document.getElementById('hint-clear');
    var cancelBtn = document.getElementById('hint-cancel');
    if (saveBtn) saveBtn.addEventListener('click', saveHint);
    if (clearBtn) clearBtn.addEventListener('click', clearHint);
    if (cancelBtn) cancelBtn.addEventListener('click', closeHintEditor);

    connect();
  }

  function buildExpertise(pc) {
    var map = {};
    if (!pc || !pc.pillars) return map;
    pc.pillars.forEach(function(pillar) {
      if (!pillar.rank) return;
      pillar.rank.forEach(function(provId, idx) {
        var provLower = provId.toLowerCase();
        providers.forEach(function(name) {
          var nameLower = name.toLowerCase();
          var parts = provLower.split('-');
          var match = true;
          for (var j = 0; j < parts.length; j++) {
            if (nameLower.indexOf(parts[j]) === -1) { match = false; break; }
          }
          if (match) {
            if (!map[name]) map[name] = [];
            if (idx === 0) map[name].unshift(pillar.id);
            else map[name].push(pillar.id);
          }
        });
      });
    });
    return map;
  }

  var slashCommands = [
    { cmd: '/judge',      desc: 'All providers rank each other\'s responses — who answered best?' },

    { cmd: '/summarize',  desc: 'Compare responses — consensus, differences, outliers' },
    { cmd: '/reconsider', desc: 'Top 3 judge winners re-examine and challenge their answers' },
    { cmd: '/hint',       desc: 'Set a system prompt for all providers (e.g. /hint be concise)' },
    { cmd: '/new',        desc: 'Show recently learned knowledge from the store' }
  ];
  var cmdMenuEl = null;
  var cmdSelected = -1;

  function buildCommandMenu() {
    cmdMenuEl = document.getElementById('cmd-menu');
    if (!inputEl || !cmdMenuEl) return;

    inputEl.addEventListener('input', function() {
      var val = inputEl.value;
      if (val.indexOf('/') === 0) {
        showCommandMenu(val);
      } else {
        hideCommandMenu();
      }
    });

    /* Override keydown for arrow nav + tab/enter in menu */
    inputEl.addEventListener('keydown', function(e) {
      if (!cmdMenuEl || cmdMenuEl.classList.contains('hidden')) return;
      var items = cmdMenuEl.querySelectorAll('.cmd-item');
      if (items.length === 0) return;

      if (e.key === 'ArrowDown') {
        e.preventDefault();
        e.stopImmediatePropagation();
        cmdSelected = Math.min(cmdSelected + 1, items.length - 1);
        highlightCmd(items);
      } else if (e.key === 'ArrowUp') {
        e.preventDefault();
        e.stopImmediatePropagation();
        cmdSelected = Math.max(cmdSelected - 1, 0);
        highlightCmd(items);
      } else if (e.key === 'Tab') {
        e.preventDefault();
        if (cmdSelected >= 0 && cmdSelected < items.length) {
          selectCmd(items[cmdSelected]);
        } else if (items.length > 0) {
          selectCmd(items[0]);
        }
      } else if (e.key === 'Escape') {
        hideCommandMenu();
      } else if (e.key === 'Enter' && cmdSelected >= 0) {
        /* If a command is highlighted, fill it and send */
        e.preventDefault();
        e.stopImmediatePropagation();
        selectCmd(items[cmdSelected]);
        send();
      }
    }, true); /* capture phase so it runs before the existing keydown */
  }

  function showCommandMenu(filter) {
    if (!cmdMenuEl) return;
    var f = filter.toLowerCase();
    var matches = slashCommands.filter(function(c) {
      return c.cmd.indexOf(f) === 0 || f === '/';
    });
    if (matches.length === 0) {
      hideCommandMenu();
      return;
    }
    cmdMenuEl.innerHTML = '';
    cmdSelected = -1;
    matches.forEach(function(c) {
      var div = document.createElement('div');
      div.className = 'cmd-item';
      div.innerHTML = '<span class="cmd-name">' + escapeHtml(c.cmd) + '</span>' +
        '<span class="cmd-desc">' + escapeHtml(c.desc) + '</span>';
      div.addEventListener('click', function() {
        selectCmd(div);
        inputEl.focus();
      });
      div.setAttribute('data-cmd', c.cmd);
      cmdMenuEl.appendChild(div);
    });
    cmdMenuEl.classList.remove('hidden');
  }

  function hideCommandMenu() {
    if (cmdMenuEl) cmdMenuEl.classList.add('hidden');
    cmdSelected = -1;
  }

  function highlightCmd(items) {
    items.forEach(function(el, i) {
      el.classList.toggle('selected', i === cmdSelected);
    });
  }

  function selectCmd(el) {
    var cmd = el.getAttribute('data-cmd') || '';
    inputEl.value = cmd;
    hideCommandMenu();
  }

  function buildEngineSelector() {
    var sel = document.getElementById('engine-select');
    if (!sel) return;
    sel.innerHTML = '';
    var opt0 = document.createElement('option');
    opt0.value = '';
    opt0.textContent = 'Auto';
    sel.appendChild(opt0);
    providers.forEach(function(name) {
      var opt = document.createElement('option');
      opt.value = name;
      opt.textContent = providerModelMap[name] || name;
      sel.appendChild(opt);
    });
    sel.value = selectedEngine;
    sel.addEventListener('change', function() {
      selectedEngine = sel.value;
      if (ws && ws.readyState === 1) {
        ws.send(JSON.stringify({ type: 'set_engine', engine: selectedEngine }));
      }
    });
  }

  function buildProviderPanels() {
    if (!midCol || !rightCol) return;
    midCol.innerHTML = '';
    rightCol.innerHTML = '';
    providerBodies = {};
    providerPanels = {};
    providerTraces = {};

    // Col 2 heading: LEARNING
    var midHeading = document.createElement('div');
    midHeading.className = 'col-heading';
    midHeading.textContent = 'LEARNING';
    midCol.appendChild(midHeading);

    // Col 3 heading: DATA ACCESS + master toggle + route mode
    var rightHeading = document.createElement('div');
    rightHeading.className = 'col-heading';
    rightHeading.innerHTML = '<span>DATA ACCESS</span>' +
      '<button class="ai-master-toggle" id="ai-master-toggle" title="enable/disable all AI calls">ON</button>' +
      '<button class="route-toggle" id="route-toggle" title="LOCAL = direct / MESH = relay">LOCAL</button>' +
      '<button class="expand-all-btn" id="expand-all-btn" title="expand/collapse all">&#9660;</button>';
    rightCol.appendChild(rightHeading);
    var masterToggle = document.getElementById('ai-master-toggle');
    masterToggle.addEventListener('click', function() {
      toggleAllProviders();
    });

    // Expand/collapse all
    var expandAllBtn = document.getElementById('expand-all-btn');
    if (expandAllBtn) {
      expandAllBtn.addEventListener('click', function() {
        allExpanded = !allExpanded;
        expandAllBtn.innerHTML = allExpanded ? '&#9650;' : '&#9660;';
        providers.forEach(function(name) {
          expandedProviders[name] = allExpanded;
          applyCollapseState(name);
        });
      });
    }

    // Sort bar
    var sortBar = document.createElement('div');
    sortBar.className = 'dap-sort-bar';
    sortBar.innerHTML =
      '<span class="sort-label">SORT</span>' +
      '<button class="sort-btn active" data-sort="default">DEFAULT</button>' +
      '<button class="sort-btn" data-sort="cost">COST</button>' +
      '<button class="sort-btn" data-sort="time">TIME</button>' +
      '<button class="sort-btn" data-sort="rank">RANK</button>';
    rightCol.appendChild(sortBar);
    sortBar.querySelectorAll('.sort-btn').forEach(function(btn) {
      btn.addEventListener('click', function() {
        sortBar.querySelectorAll('.sort-btn').forEach(function(b) { b.classList.remove('active'); });
        btn.classList.add('active');
        currentSort = btn.getAttribute('data-sort');
        sortProviderPanels();
      });
    });

    // Route toggle: LOCAL ↔ MESH
    var routeBtn = document.getElementById('route-toggle');
    if (routeBtn) {
      routeBtn.addEventListener('click', function() {
        routeMode = routeMode === 'local' ? 'mesh' : 'local';
        routeBtn.textContent = routeMode.toUpperCase();
        routeBtn.classList.toggle('mesh-mode', routeMode === 'mesh');
        if (ws && ws.readyState === 1) {
          ws.send(JSON.stringify({ type: 'set_route', mode: routeMode }));
        }
      });
    }

    // Stock ticker — bottom bar
    tickerEl = document.getElementById('bottom-ticker');
    tickerTrack = document.getElementById('bottom-ticker-track');
    startTicker();

    // Col 2: Intelligence purpose trace — 8 skill tiles in pipeline order
    var skillTiles = [
      { id: 'language',   label: 'LANGUAGE',   headCls: 'language-tile-head',   labelCls: 'language-tile-label' },
      { id: 'understand', label: 'UNDERSTAND', headCls: 'understand-tile-head', labelCls: 'understand-tile-label' },
      { id: 'read',       label: 'READ',       headCls: 'read-tile-head',       labelCls: 'read-tile-label' },
      { id: 'learn',      label: 'LEARN',      headCls: 'learn-tile-head',      labelCls: 'learn-tile-label' },
      { id: 'reason',     label: 'REASON',     headCls: 'reason-tile-head',     labelCls: 'reason-tile-label' },
      { id: 'acquire',    label: 'ACQUIRE',    headCls: 'acquire-tile-head',    labelCls: 'acquire-tile-label' },
      { id: 'memory',     label: 'MEMORY',     headCls: 'memory-tile-head',     labelCls: 'memory-tile-label' },
      { id: 'catalog',    label: 'CATALOG',    headCls: 'catalog-tile-head',    labelCls: 'catalog-tile-label' }
    ];

    skillTiles.forEach(function(t) {
      var panel = document.createElement('div');
      panel.className = 'provider-panel ' + t.id + '-tile';
      panel.innerHTML =
        '<div class="provider-head skill-tile-head ' + t.headCls + '">' +
          '<span class="prov-name ' + t.labelCls + '">' + t.label + '</span>' +
          '<span class="track-badge" id="' + t.id + '-badge"></span>' +
        '</div>';
      var body = document.createElement('div');
      body.className = 'skill-tile-body';
      body.id = t.id + '-body';
      body.innerHTML = '<span class="track-empty">waiting...</span>';
      panel.appendChild(body);
      midCol.appendChild(panel);
    });

    languageBody = document.getElementById('language-body');
    languageBadge = document.getElementById('language-badge');
    understandBody = document.getElementById('understand-body');
    understandBadge = document.getElementById('understand-badge');
    readBody = document.getElementById('read-body');
    readBadge = document.getElementById('read-badge');
    learnBody = document.getElementById('learn-body');
    learnBadge = document.getElementById('learn-badge');
    reasonBody = document.getElementById('reason-body');
    reasonBadge = document.getElementById('reason-badge');
    acquireBody = document.getElementById('acquire-body');
    acquireBadge = document.getElementById('acquire-badge');
    memoryBody = document.getElementById('memory-body');
    memoryBadge = document.getElementById('memory-badge');
    catalogBody = document.getElementById('catalog-body');
    catalogBadge = document.getElementById('catalog-badge');

    // Col 3: DATA ACCESS — grouped by type with section dividers
    var pinSet = {};
    pinnedProviders.forEach(function(n) { pinSet[n.toLowerCase()] = true; });

    // Section: LOCAL — reason result (already in Col 2, reference only)
    addDapDivider(rightCol, 'LOCAL', 'reason (memory + intel stores)');

    // Section: CATALOG — from catalog concern routing
    addDapDivider(rightCol, 'CATALOG', 'concern-routed knowledge sources');

    // Section: WEB — acquire scouts (future)
    addDapDivider(rightCol, 'WEB', 'search scouts');

    // Section: AI — all LLM providers
    addDapDivider(rightCol, 'AI', providers.length + ' providers');

    providers.forEach(function(name) {
      if (!sessionCosts[name]) sessionCosts[name] = { last: 0, total: 0, actual: false, tokIn: 0, tokOut: 0, totalTokIn: 0, totalTokOut: 0 };
      if (disabledProviders[name] === undefined) disabledProviders[name] = false;

      var panel = document.createElement('div');
      panel.className = 'provider-panel' + (disabledProviders[name] ? ' disabled' : '');
      panel.id = 'panel-' + name;

      var model = '';
      if (config && config.providers) {
        for (var i = 0; i < config.providers.length; i++) {
          if (config.providers[i].name === name) {
            model = config.providers[i].model || '';
            break;
          }
        }
      }

      var head = document.createElement('div');
      head.className = 'provider-head';
      var isPinned = pinSet[name.toLowerCase()] || false;
      head.innerHTML =
        '<button class="prov-toggle ' + (disabledProviders[name] ? 'disabled' : 'enabled') + '" data-prov="' + name + '" title="toggle on/off"></button>' +
        '<span class="prov-status-dot" id="status-' + name + '"></span>' +
        '<span class="prov-chevron" id="chev-' + name + '">&#9654;</span>' +
        '<span class="prov-name" data-p="' + name + '">' + escapeHtml(model || name) + '</span>' +
        (isPinned ? '<span class="pinned-badge" title="pinned — always queried">PIN</span>' : '') +
        '<span class="hint-support" id="hintsup-' + name + '" title=""></span>' +
        '<span class="sense-rank"></span>' +
        '<span class="prov-elapsed" id="elapsed-' + name + '"></span>' +
        '<span class="cost-group">' +
          '<span class="prov-session" id="session-' + name + '"></span>' +
          '<span class="prov-1k" id="proj-' + name + '"></span>' +
          '<span class="prov-tokens" id="tokens-' + name + '"></span>' +
          '<span class="prov-tier" id="tier-' + name + '"></span>' +
          '<span class="hint-dot" title="edit hint"></span>' +
        '</span>';

      // Toggle click (enable/disable)
      var toggleBtn = head.querySelector('.prov-toggle');
      toggleBtn.addEventListener('click', function(e) {
        e.stopPropagation();
        toggleProvider(name);
      });

      // Hint dot click → hint editor
      var hintDot = head.querySelector('.hint-dot');
      if (hintDot) {
        hintDot.addEventListener('click', function(e) {
          e.stopPropagation();
          openHintEditor(name);
        });
      }

      // Header click → expand/collapse
      head.addEventListener('click', function(e) {
        if (e.target.classList.contains('prov-toggle') || e.target.classList.contains('hint-dot')) return;
        expandedProviders[name] = !expandedProviders[name];
        applyCollapseState(name);
      });

      var traceLine = document.createElement('div');
      traceLine.className = 'prov-trace-line hidden';
      traceLine.id = 'trace-' + name;

      var body = document.createElement('div');
      body.className = 'provider-body';
      body.id = 'out-' + name;

      var footer = document.createElement('div');
      footer.className = 'provider-footer hidden';
      footer.id = 'footer-' + name;

      panel.appendChild(head);
      panel.appendChild(traceLine);
      panel.appendChild(body);
      panel.appendChild(footer);
      providerTraces[name] = traceLine;
      providerFooters[name] = footer;

      // Start collapsed
      expandedProviders[name] = false;
      panel.classList.add('collapsed');

      // All providers go to col 3
      rightCol.appendChild(panel);

      providerBodies[name] = body;
      providerPanels[name] = panel;
    });
  }

  function toggleProvider(name) {
    disabledProviders[name] = !disabledProviders[name];
    var panel = providerPanels[name];
    if (!panel) return;
    panel.classList.toggle('disabled', disabledProviders[name]);
    var btn = panel.querySelector('.prov-toggle');
    if (btn) {
      btn.classList.toggle('enabled', !disabledProviders[name]);
      btn.classList.toggle('disabled', disabledProviders[name]);
    }
    // Tell backend
    if (ws && ws.readyState === 1) {
      ws.send(JSON.stringify({ type: 'set_disabled', provider: name, disabled: disabledProviders[name] }));
    }
    updateMasterToggle();
  }

  function toggleAllProviders() {
    // If any are enabled, disable all. If all disabled, enable all.
    var anyEnabled = providers.some(function(n) { return !disabledProviders[n]; });
    var newState = anyEnabled; // true = disable all
    providers.forEach(function(name) {
      disabledProviders[name] = newState;
      var panel = providerPanels[name];
      if (!panel) return;
      panel.classList.toggle('disabled', newState);
      var btn = panel.querySelector('.prov-toggle');
      if (btn) {
        btn.classList.toggle('enabled', !newState);
        btn.classList.toggle('disabled', newState);
      }
      if (ws && ws.readyState === 1) {
        ws.send(JSON.stringify({ type: 'set_disabled', provider: name, disabled: newState }));
      }
    });
    updateMasterToggle();
  }

  function updateMasterToggle() {
    var btn = document.getElementById('ai-master-toggle');
    if (!btn) return;
    var anyEnabled = providers.some(function(n) { return !disabledProviders[n]; });
    btn.textContent = anyEnabled ? 'ON' : 'OFF';
    btn.classList.toggle('master-off', !anyEnabled);
  }

  // --- WebSocket ---

  function connect() {
    ws = new WebSocket('ws://' + location.host + '/ws/loop');

    ws.onopen = function() {
      console.log('[loop] connected');
      ws.send(JSON.stringify({ type: 'get_hints' }));
      StoreController.connect(ws);
    };

    ws.onmessage = function(ev) {
      try {
        handleEvent(JSON.parse(ev.data));
      } catch (e) {
        console.error('[loop] bad message:', e);
      }
    };

    ws.onclose = function() {
      console.log('[loop] disconnected, reconnecting...');
      setTimeout(connect, 3000);
    };

    ws.onerror = function() {
      console.error('[loop] WebSocket error');
    };
  }

  // --- Send ---

  function send() {
    if (!inputEl || !inputEl.value.trim()) return;
    var text = inputEl.value.trim();
    inputEl.value = '';
    hideCommandMenu();
    history.push(text);
    historyIdx = -1;
    historyDraft = '';

    // /hint or /prompt command — set global prompt for all providers
    if (text.indexOf('/hint ') === 0 || text.indexOf('/prompt ') === 0) {
      var hint = text.substring(text.indexOf(' ') + 1).trim();
      setGlobalHint(hint);
      return;
    }
    if (text === '/hint' || text === '/prompt') {
      setGlobalHint('');
      return;
    }

    // /judge or /rank — peer review
    if (text === '/judge' || text === '/rank') {
      if (ws && ws.readyState === 1) {
        ws.send(JSON.stringify({ type: 'judge' }));
      }
      return;
    }

    // /summarize — compare responses: consensus, differences, outliers
    if (text === '/summarize' || text === '/summary') {
      if (ws && ws.readyState === 1) ws.send(JSON.stringify({ type: 'summarize', engine: selectedEngine }));
      return;
    }

    // /reconsider — reverse-think from top 3 judge winners
    if (text === '/reconsider') {
      if (ws && ws.readyState === 1) {
        ws.send(JSON.stringify({ type: 'reconsider' }));
      }
      return;
    }

    // /new — show recently learned knowledge
    if (text === '/new') {
      if (ws && ws.readyState === 1) {
        searchBody.innerHTML = '<span class="thinking">querying store...</span>';
        ws.send(JSON.stringify({ type: 'new' }));
      }
      return;
    }

    queryCost = 0.0;
    queryTokensIn = 0;
    queryTokensOut = 0;
    queryPending = {};
    queryDone = 0;
    queryTotal = 0;
    senseHtml = '';
    goStartTime = performance.now();
    lastGoElapsed = 0;
    lastGoResponded = 0;
    lastGoAttempted = 0;

    // Clear tron traces
    clearProviderTraces();

    // Clear all 8 intelligence skill tiles
    if (languageBody) languageBody.innerHTML = '<span class="thinking">classifying...</span>';
    if (languageBadge) { languageBadge.textContent = ''; languageBadge.className = 'track-badge'; }
    if (understandBody) understandBody.innerHTML = '<span class="thinking">parsing...</span>';
    if (understandBadge) { understandBadge.textContent = ''; understandBadge.className = 'track-badge'; }
    if (readBody) readBody.innerHTML = '<span class="thinking">detecting...</span>';
    if (readBadge) { readBadge.textContent = ''; readBadge.className = 'track-badge'; }
    if (learnBody) learnBody.innerHTML = '<span class="thinking">curating...</span>';
    if (learnBadge) { learnBadge.textContent = ''; learnBadge.className = 'track-badge'; }
    if (reasonBody) reasonBody.innerHTML = '<span class="thinking">reasoning...</span>';
    if (reasonBadge) { reasonBadge.textContent = ''; reasonBadge.className = 'track-badge'; }
    if (acquireBody) acquireBody.innerHTML = '<span class="thinking">routing...</span>';
    if (acquireBadge) { acquireBadge.textContent = ''; acquireBadge.className = 'track-badge'; }
    if (memoryBody) memoryBody.innerHTML = '<span class="thinking">querying...</span>';
    if (memoryBadge) { memoryBadge.textContent = ''; memoryBadge.className = 'track-badge'; }
    if (catalogBody) catalogBody.innerHTML = '<span class="thinking">searching...</span>';
    if (catalogBadge) { catalogBadge.textContent = ''; catalogBadge.className = 'track-badge'; }
    if (searchBody) searchBody.innerHTML = '<span class="thinking">routing to expert...</span>';
    if (searchBadge) { searchBadge.textContent = ''; searchBadge.className = 'track-badge'; }

    // Clear right panels
    providers.forEach(function(p) {
      if (providerBodies[p]) providerBodies[p].innerHTML = '';
      if (providerPanels[p]) providerPanels[p].classList.remove('sensed', 'has-content', 'skipped');
      var costEl = document.getElementById('cost-' + p);
      if (costEl) costEl.textContent = '';
      var rankEl = providerPanels[p] ? providerPanels[p].querySelector('.sense-rank') : null;
      if (rankEl) rankEl.textContent = '';
    });

    // Clear status
    if (senseDisplay) senseDisplay.innerHTML = '';

    if (ws && ws.readyState === 1) {
      ws.send(JSON.stringify({ type: 'task', content: text }));
    }
  }

  // --- Event handler ---

  function handleEvent(msg) {
    // Route store events to StoreController
    if (msg.type === 'store_result') {
      StoreController.handleEvent(msg);
      return;
    }

    // Route auth events to AuthGate
    if (msg.type === 'relay_auth_result' || msg.type === 'relay_register_result') {
      if (window.AuthGate) window.AuthGate.handleEvent(msg);
      return;
    }

    switch (msg.type) {
      case 'phase':
        // Status bar update
        if (senseDisplay) {
          senseDisplay.innerHTML = '<span style="color:var(--text-dim)">' + escapeHtml(msg.status || '') + '</span>';
        }
        break;

      case 'sense':
        showSense(msg);
        break;

      case 'thinking':
        var body = providerBodies[msg.provider];
        var thinkPanel = providerPanels[msg.provider];
        if (body) body.innerHTML = '<span class="thinking">thinking...</span>';
        if (thinkPanel) thinkPanel.classList.add('has-content');
        setProviderTrace(msg.provider, 'SEND', 'requesting...', 'normal');
        setProviderStatus(msg.provider, 'thinking');
        queryPending[msg.provider] = true;
        queryTotal++;
        updateQueryStatus();
        break;

      case 'text':
        showProviderText(msg);
        var tMs = msg.elapsed_ms || 0;
        providerElapsed[msg.provider] = tMs;
        // Inline elapsed + tokens in header
        var elapsedEl = document.getElementById('elapsed-' + msg.provider);
        if (elapsedEl) elapsedEl.textContent = fmtTime(tMs);
        var tokensEl = document.getElementById('tokens-' + msg.provider);
        if (tokensEl) tokensEl.textContent = fmtK(msg.tokens_in || 0) + '/' + fmtK(msg.tokens_out || 0);
        setProviderStatus(msg.provider, 'done');
        // Hide trace line — footer has all the info now
        if (providerTraces[msg.provider]) providerTraces[msg.provider].classList.add('hidden');
        delete queryPending[msg.provider];
        queryDone++;
        updateQueryStatus();
        break;

      case 'language':
        showLanguage(msg);
        break;

      case 'understand':
        showUnderstand(msg);
        break;

      case 'reason':
        showReason(msg);
        break;

      case 'local':
        showMemory(msg);
        break;

      case 'catalog':
        showCatalog(msg);
        showAcquire(msg);
        break;

      case 'search_step':
        showSearchStep(msg);
        updateProviderTronTrace(msg);
        break;

      case 'search':
        showSearch(msg);
        // Accumulate guided AI thread tokens into query totals
        if (msg.tokens_in || msg.tokens_out) {
          queryTokensIn += (msg.tokens_in || 0);
          queryTokensOut += (msg.tokens_out || 0);
        }
        if (msg.cost) queryCost += msg.cost;
        break;

      case 'daily_cost':
        showDailyCost(msg);
        break;

      case 'done':
        lastGoElapsed = msg.elapsed_ms || Math.round(performance.now() - goStartTime);
        lastGoResponded = msg.responded || queryDone;
        lastGoAttempted = queryTotal;
        if (queryCost > 0) {
          sessionGoCost += queryCost;
        }
        rebuildStatusBar();
        // Show completed in sense display
        if (senseDisplay) {
          senseDisplay.innerHTML = senseHtml +
            ' <span style="color:var(--accent-green)">DONE ' + fmtTime(lastGoElapsed) +
            ' (' + lastGoResponded + '/' + lastGoAttempted + ')</span>';
        }
        queryPending = {};
        break;

      case 'skipped':
        var skipPanel = providerPanels[msg.provider];
        if (skipPanel) skipPanel.classList.add('skipped');
        setProviderTrace(msg.provider, 'SKIP', msg.reason || 'not sensed', 'dim');
        setProviderStatus(msg.provider, 'skipped');
        delete queryPending[msg.provider];
        break;

      case 'timeout':
        var toBody = providerBodies[msg.provider];
        if (toBody) {
          toBody.innerHTML = '<div class="error" style="color:var(--accent-gold)">TIMEOUT (60s)</div>';
          var toPanel = providerPanels[msg.provider];
          if (toPanel) toPanel.classList.add('has-content');
        }
        setProviderTrace(msg.provider, 'TIMEOUT', '60s limit exceeded', 'error');
        setProviderStatus(msg.provider, 'timeout');
        // Don't count timeouts — remove from pending but don't increment done
        if (queryPending[msg.provider]) {
          delete queryPending[msg.provider];
          queryTotal--;
          updateQueryStatus();
        }
        break;

      case 'error':
        var errBody = providerBodies[msg.provider];
        if (errBody) {
          errBody.innerHTML = '<div class="error">' + escapeHtml(msg.content || 'error') + '</div>';
          var errPanel = providerPanels[msg.provider];
          if (errPanel) errPanel.classList.add('has-content');
        }
        setProviderTrace(msg.provider, 'ERROR', msg.content || 'error', 'error');
        setProviderStatus(msg.provider, 'error');
        if (queryPending[msg.provider]) {
          delete queryPending[msg.provider];
          queryDone++;
          updateQueryStatus();
        }
        break;

      case 'hints':
        hints = msg.hints || {};
        if (msg.global_hint !== undefined) {
          globalHint = msg.global_hint || '';
          updateGlobalHintBar();
        }
        updateHintDots();
        break;

      case 'hint_set':
        hints[msg.provider] = msg.hint;
        updateHintDots();
        updateHintSupport();
        break;

      case 'global_hint_set':
        globalHint = msg.hint || '';
        updateGlobalHintBar();
        break;

      case 'judge_start':
        // Clear old rank badges + reset accumulators
        judgeTotal = 0;
        judgeCompleted = 0;
        judgePending = {};
        judgeScores = {};
        judgeCosts = {};
        judgeLeaderboardEl = null;
        lastJudgeElapsed = 0;
        lastJudgeVotes = 0;
        clearProviderTraces();
        providers.forEach(function(p) {
          var badge = document.getElementById('rank-' + p);
          if (badge) badge.textContent = '';
        });
        if (senseDisplay) senseDisplay.innerHTML = '<span style="color:var(--accent-gold)">JUDGING — collecting responses...</span>';
        if (searchBody) searchBody.innerHTML = '<span class="thinking">judging responses...</span>';
        break;

      case 'judge_step':
        showJudgeStep(msg.detail || '');
        // Count total judges from the "sending judge prompt" step
        var sendMatch = (msg.detail || '').match(/sending judge prompt to (\d+)/);
        if (sendMatch) {
          judgeTotal = parseInt(sendMatch[1]) || 0;
          judgeCompleted = 0;
        }
        if (senseDisplay) {
          senseDisplay.innerHTML = '<span style="color:var(--accent-gold)">JUDGING — ' + escapeHtml(msg.detail || '') + '</span>';
        }
        break;

      case 'judge_thinking':
        showJudgeStep('waiting for ' + (msg.provider || '') + '...');
        judgePending[msg.provider] = true;
        if (msg.provider && providerBodies[msg.provider]) {
          providerBodies[msg.provider].innerHTML = '<span class="thinking">judging...</span>';
        }
        setProviderTrace(msg.provider, 'JUDGE', 'evaluating', 'normal');
        setProviderStatus(msg.provider, 'thinking');
        updateJudgeStatus();
        break;

      case 'judge_error':
        showJudgeStep('error: ' + (msg.provider || '') + ' — ' + (msg.detail || ''));
        judgeCompleted++;
        delete judgePending[msg.provider];
        setProviderTrace(msg.provider, 'ERROR', msg.detail || 'judge failed', 'error');
        setProviderStatus(msg.provider, 'error');
        updateJudgeStatus();
        break;

      case 'judge_vote':
        showJudgeVote(msg);
        judgeCompleted++;
        delete judgePending[msg.provider];
        // Update provider tile with judge response
        if (msg.provider && providerBodies[msg.provider]) {
          var rawSnip = (msg.raw || '').substring(0, 200);
          providerBodies[msg.provider].innerHTML = '';
          var span = document.createElement('span');
          span.textContent = rawSnip;
          providerBodies[msg.provider].appendChild(span);
        }
        // Elapsed + tokens on tile
        if (msg.provider) {
          var elEl = document.getElementById('elapsed-' + msg.provider);
          if (elEl) elEl.textContent = fmtTime(msg.elapsed_ms || 0);
          var tokEl = document.getElementById('tokens-' + msg.provider);
          if (tokEl) tokEl.textContent = fmtK(msg.tokens_in || 0) + '/' + fmtK(msg.tokens_out || 0);
          setProviderStatus(msg.provider, 'done');
          setProviderTrace(msg.provider, 'DONE', '$' + formatCost(msg.cost || 0) + ' ' + fmtTime(msg.elapsed_ms || 0), 'done');
        }
        // Accumulate judge tokens into query totals
        queryTokensIn += (msg.tokens_in || 0);
        queryTokensOut += (msg.tokens_out || 0);
        updateJudgeStatus();
        break;

      case 'judge_result':
        lastJudgeElapsed = msg.elapsed_ms || 0;
        lastJudgeVotes = msg.judges || 0;
        showJudgeResult(msg);
        if (senseDisplay) senseDisplay.innerHTML = '<span style="color:var(--accent-green)">RANKED ' +
          fmtTime(lastJudgeElapsed) + ' — ' + (msg.final_ranks || []).length + ' providers, ' +
          lastJudgeVotes + ' votes</span>';
        break;

      case 'judge_cost':
        showJudgeCost(msg);
        break;

      case 'judge_done':
        break;

      case 'summarize_start':
        if (searchBody) searchBody.innerHTML = '<span class="thinking">comparing responses...</span>';
        if (searchBadge) { searchBadge.textContent = 'SUMMARIZE'; searchBadge.className = 'track-badge active-gold'; }
        break;

      case 'summarize_step':
        showSummarizeStep(msg.detail || '');
        break;

      case 'summarize_prompt':
        showInspectLink('SUMMARIZE', 'PROMPT SENT', msg.content || '', msg.length || 0, 'Summarize Prompt');
        break;

      case 'summarize_done':
        showSummarizeDone(msg);
        break;

      case 'new_result':
        showNewResult(msg);
        break;
    }
  }

  function showNewResult(msg) {
    if (!searchBody) return;
    var items = msg.items || [];
    if (items.length === 0) {
      searchBody.innerHTML = '<span class="track-empty">no learned knowledge yet — run queries first</span>';
      return;
    }
    var html = '<div class="new-results">';
    html += '<div class="new-header">RECENTLY LEARNED (' + items.length + ')</div>';
    for (var i = 0; i < items.length; i++) {
      var it = items[i];
      var q = escapeHtml(it.question || '').substring(0, 80);
      if ((it.question || '').length > 80) q += '...';
      html += '<div class="new-item">';
      html += '<span class="new-q">' + q + '</span>';
      html += '<span class="new-meta">';
      html += '<span class="new-provider">' + escapeHtml(it.provider || '?') + '</span>';
      html += ' <span class="new-triples">+' + it.triples + ' triples</span>';
      if (it.learned_at) html += ' <span class="new-time">' + escapeHtml(it.learned_at) + '</span>';
      html += '</span>';
      html += '</div>';
    }
    html += '</div>';
    searchBody.innerHTML = html;
  }

  // --- Display functions ---

  function showSense(msg) {
    if (!senseDisplay) return;
    senseHtml = '';
    if (msg.pillar) senseHtml += '<span class="sense-pillar">' + escapeHtml(msg.pillar) + '</span>';
    if (msg.stage1) senseHtml += ' <span class="sense-stage1">[' + escapeHtml(msg.stage1) + ']</span>';
    if (msg.related && msg.related.length > 0) {
      senseHtml += ' <span class="sense-related">' + escapeHtml(msg.related.join(', ')) + '</span>';
    }
    senseDisplay.innerHTML = senseHtml;

    // Mark sensed providers + reorder
    markSensedProviders(msg.providers || []);
  }

  function markSensedProviders(sensedList) {
    // Clear all
    providers.forEach(function(name) {
      if (providerPanels[name]) {
        providerPanels[name].classList.remove('sensed');
        var rankEl = providerPanels[name].querySelector('.sense-rank');
        if (rankEl) rankEl.textContent = '';
      }
    });

    if (sensedList.length === 0) return;

    // Mark sensed
    providers.forEach(function(name) {
      var nameLower = name.toLowerCase();
      for (var i = 0; i < sensedList.length; i++) {
        var tag = sensedList[i].toLowerCase();
        var parts = tag.split('-');
        var match = true;
        for (var j = 0; j < parts.length; j++) {
          if (nameLower.indexOf(parts[j]) === -1) { match = false; break; }
        }
        if (match && providerPanels[name]) {
          providerPanels[name].classList.add('sensed');
          var rankEl = providerPanels[name].querySelector('.sense-rank');
          if (rankEl) rankEl.textContent = '';
          break;
        }
      }
    });

    // Reorder within col 3 (rightCol): sensed providers float up, pinned always first
    if (!rightCol) return;
    var panels = Array.from(rightCol.querySelectorAll('.provider-panel'));
    var pinSet2 = {};
    pinnedProviders.forEach(function(n) { pinSet2[n.toLowerCase()] = true; });
    panels.sort(function(a, b) {
      var aName = (a.querySelector('.prov-name') || {}).getAttribute('data-p') || '';
      var bName = (b.querySelector('.prov-name') || {}).getAttribute('data-p') || '';
      var aP = pinSet2[aName.toLowerCase()] ? 0 : 1;
      var bP = pinSet2[bName.toLowerCase()] ? 0 : 1;
      if (aP !== bP) return aP - bP;
      var aS = a.classList.contains('sensed') ? 0 : 1;
      var bS = b.classList.contains('sensed') ? 0 : 1;
      if (aS !== bS) return aS - bS;
      if (aS === 0) {
        var aR = parseInt(((a.querySelector('.sense-rank') || {}).textContent || '#99').replace('#',''));
        var bR = parseInt(((b.querySelector('.sense-rank') || {}).textContent || '#99').replace('#',''));
        return aR - bR;
      }
      return 0;
    });
    panels.forEach(function(p) { rightCol.appendChild(p); });
  }

  function showProviderText(msg) {
    var name = msg.provider;
    var body = providerBodies[name];
    var panel = providerPanels[name];
    if (body) {
      body.innerHTML = '';
      var span = document.createElement('span');
      span.textContent = msg.content || '';
      body.appendChild(span);
    }
    if (panel) panel.classList.add('has-content');

    /* Store request body for inspection */
    if (msg.request) providerRequests[name] = msg.request;

    /* Build footer: links left, stats right aligned with header */
    var footer = providerFooters[name];
    if (footer && msg.cost > 0) {
      var tIn = msg.tokens_in || 0;
      var tOut = msg.tokens_out || 0;
      var tMs = msg.elapsed_ms || 0;
      var proj1k = msg.cost * 1000;
      footer.innerHTML =
        '<span class="footer-links">' +
          '<span class="footer-link" id="footer-req-' + name + '">REQUEST</span>' +
          '<span class="footer-link" id="footer-raw-' + name + '">RAW</span>' +
          '<span class="footer-link" id="footer-resp-' + name + '">RESPONSE</span>' +
        '</span>' +
        '<span class="footer-stats">' +
          '<span class="footer-elapsed">' + fmtTime(tMs) + '</span>' +
          '<span class="footer-cost">$' + formatCost(msg.cost) + '</span>' +
          '<span class="footer-proj">$' + formatCost(proj1k) + '/1k</span>' +
          '<span class="footer-tokens">' + fmtK(tIn) + '/' + fmtK(tOut) + '</span>' +
        '</span>';
      footer.classList.remove('hidden');

      var reqLink = document.getElementById('footer-req-' + name);
      var rawLink = document.getElementById('footer-raw-' + name);
      var respLink = document.getElementById('footer-resp-' + name);
      if (reqLink) {
        (function(n) {
          reqLink.addEventListener('click', function(e) {
            e.stopPropagation();
            var reqText = providerRequests[n] || '(no request body captured)';
            try { reqText = JSON.stringify(JSON.parse(reqText), null, 2); } catch(ex) {}
            openResponseViewer(n, reqText, (providerModelMap[n] || n) + ' — REQUEST');
          });
        })(name);
      }
      if (rawLink) {
        (function(n, content) {
          rawLink.addEventListener('click', function(e) {
            e.stopPropagation();
            openResponseViewer(n, content, (providerModelMap[n] || n) + ' — RAW');
          });
        })(name, msg.raw || msg.content || '');
      }
      if (respLink) {
        (function(n, content) {
          respLink.addEventListener('click', function(e) {
            e.stopPropagation();
            openResponseViewer(n, content, (providerModelMap[n] || n) + ' — RESPONSE');
          });
        })(name, msg.content || '');
      }
    }

    // Cost + token tracking
    if (msg.cost !== undefined && msg.cost > 0) {
      if (!sessionCosts[name]) sessionCosts[name] = { last: 0, total: 0, actual: false, tokIn: 0, tokOut: 0, totalTokIn: 0, totalTokOut: 0, queries: 0 };
      sessionCosts[name].prev = sessionCosts[name].last;
      sessionCosts[name].last = msg.cost;
      sessionCosts[name].total += msg.cost;
      sessionCosts[name].queries = (sessionCosts[name].queries || 0) + 1;
      sessionCosts[name].actual = (msg.tokens_in > 0 || msg.tokens_out > 0);
      var tIn = msg.tokens_in || 0;
      var tOut = msg.tokens_out || 0;
      sessionCosts[name].tokIn = tIn;
      sessionCosts[name].tokOut = tOut;
      sessionCosts[name].totalTokIn = (sessionCosts[name].totalTokIn || 0) + tIn;
      sessionCosts[name].totalTokOut = (sessionCosts[name].totalTokOut || 0) + tOut;

      var proj1k = msg.cost * 1000;

      // Header: session total
      var sessionEl = document.getElementById('session-' + name);
      if (sessionEl) {
        var q = sessionCosts[name].queries;
        sessionEl.textContent = '$' + formatCost(sessionCosts[name].total);
        sessionEl.title = 'session: $' + formatCost(sessionCosts[name].total) + ' / ' + q + ' queries\n' +
          'tokens: ' + fmtK(sessionCosts[name].totalTokIn) + '↑ ' + fmtK(sessionCosts[name].totalTokOut) + '↓';
      }

      // Header: /1k projection
      var projEl = document.getElementById('proj-' + name);
      if (projEl) {
        var proj1kTok = (tIn + tOut) * 1000;
        projEl.innerHTML = '$' + formatCost(proj1k) + '/1k';
        projEl.title = 'at this rate:\n' +
          '1K queries = $' + formatCost(proj1k) + ' / ' + fmtK(proj1kTok) + ' tokens\n' +
          '$1 buys ~' + Math.round(1 / msg.cost) + ' queries\n' +
          '$100 budget = ~' + fmtK(Math.round(100 / msg.cost)) + ' queries';
      }

      queryCost += msg.cost;
      queryTokensIn += (msg.tokens_in || 0);
      queryTokensOut += (msg.tokens_out || 0);

      // Tier will be recalculated after all providers respond
      updateCostTiers();
      updateTicker();
    }
  }

  // Dynamic cost tier: percentile-based — bottom 1/3 = $, mid 1/3 = $$, top 1/3 = $$$
  function updateCostTiers() {
    // Collect all active costs and sort ascending
    var entries = [];
    providers.forEach(function(name) {
      var sc = sessionCosts[name];
      if (sc && sc.last > 0) entries.push({ name: name, cost: sc.last });
    });
    if (entries.length === 0) return;

    entries.sort(function(a, b) { return a.cost - b.cost; });
    var n = entries.length;

    // Assign tier by position in sorted list
    var tierMap = {};
    entries.forEach(function(e, idx) {
      var pct = n < 3 ? 0.5 : idx / (n - 1); // 0 = cheapest, 1 = most expensive
      if (pct <= 0.33) tierMap[e.name] = { tier: 1, cls: 'tier-1', label: '$' };
      else if (pct <= 0.66) tierMap[e.name] = { tier: 2, cls: 'tier-2', label: '$$' };
      else tierMap[e.name] = { tier: 3, cls: 'tier-3', label: '$$$' };
    });

    providers.forEach(function(name) {
      var tierEl = document.getElementById('tier-' + name);
      if (!tierEl) return;
      var t = tierMap[name];
      if (!t) { tierEl.innerHTML = ''; return; }
      tierEl.innerHTML = '<span class="' + t.cls + '">' + t.label + '</span>';
      tierEl.title = t.tier === 1 ? 'cheapest third' :
                     t.tier === 2 ? 'mid-range' :
                                    'most expensive third';
    });
  }

  // Role → color map for atom trace
  var roleColors = {
    'entity': '#4fc1ff',
    'action': '#ff8c00',
    'property': '#6a9955',
    'structure': '#c586c0',
    'signal': '#d4a537',
    'reference': '#ce9178',
    'unknown': '#808080'
  };

  // DAP section divider for Col 3
  let dapEnabled = { 'LOCAL': true, 'CATALOG': true, 'WEB': true, 'AI': true };

  function addDapDivider(container, label, detail) {
    var div = document.createElement('div');
    div.className = 'dap-divider';
    div.id = 'dap-section-' + label;
    div.innerHTML = '<span class="dap-label">' + escapeHtml(label) + '</span>' +
      (detail ? ' <span class="dap-detail">' + escapeHtml(detail) + '</span>' : '') +
      '<button class="dap-toggle on" id="dap-toggle-' + label + '">ON</button>';
    container.appendChild(div);

    var toggleBtn = div.querySelector('.dap-toggle');
    toggleBtn.addEventListener('click', function(e) {
      e.stopPropagation();
      dapEnabled[label] = !dapEnabled[label];
      toggleBtn.textContent = dapEnabled[label] ? 'ON' : 'OFF';
      toggleBtn.classList.toggle('on', dapEnabled[label]);
      toggleBtn.classList.toggle('off', !dapEnabled[label]);
      applyDapSectionState(label);
    });
  }

  function applyDapSectionState(label) {
    // For AI section — toggle all provider panels
    if (label === 'AI') {
      providers.forEach(function(name) {
        var panel = providerPanels[name];
        if (panel) panel.style.display = dapEnabled['AI'] ? '' : 'none';
      });
    }
    // For other sections — future: toggle LOCAL/CATALOG/WEB panels
    // Currently these are divider-only, so just dim them
    var divider = document.getElementById('dap-section-' + label);
    if (divider) divider.classList.toggle('dap-disabled', !dapEnabled[label]);
  }

  function applyCollapseState(name) {
    var panel = providerPanels[name];
    if (!panel) return;
    var expanded = expandedProviders[name];
    panel.classList.toggle('collapsed', !expanded);
    var chev = document.getElementById('chev-' + name);
    if (chev) chev.innerHTML = expanded ? '&#9660;' : '&#9654;';
  }

  function setProviderStatus(name, status) {
    // status: 'idle' | 'thinking' | 'done' | 'error' | 'timeout' | 'skipped'
    var dot = document.getElementById('status-' + name);
    if (!dot) return;
    dot.className = 'prov-status-dot status-' + status;
  }

  function sortProviderPanels() {
    if (!rightCol) return;
    var panels = Array.from(rightCol.querySelectorAll('.provider-panel'));
    if (panels.length === 0) return;

    var pinSet2 = {};
    pinnedProviders.forEach(function(n) { pinSet2[n.toLowerCase()] = true; });

    panels.sort(function(a, b) {
      var aName = (a.querySelector('.prov-name') || {}).getAttribute('data-p') || '';
      var bName = (b.querySelector('.prov-name') || {}).getAttribute('data-p') || '';

      // Pinned always first
      var aP = pinSet2[aName.toLowerCase()] ? 0 : 1;
      var bP = pinSet2[bName.toLowerCase()] ? 0 : 1;
      if (aP !== bP) return aP - bP;

      if (currentSort === 'cost') {
        var aCost = (sessionCosts[aName] || {}).last || 0;
        var bCost = (sessionCosts[bName] || {}).last || 0;
        return aCost - bCost; // cheapest first
      } else if (currentSort === 'time') {
        var aTime = providerElapsed[aName] || 999999;
        var bTime = providerElapsed[bName] || 999999;
        return aTime - bTime; // fastest first
      } else if (currentSort === 'rank') {
        var aRank = getJudgePosition(aName);
        var bRank = getJudgePosition(bName);
        return aRank - bRank; // best rank first
      }
      return 0; // default — preserve order
    });
    panels.forEach(function(p) { rightCol.appendChild(p); });
  }

  function getJudgePosition(name) {
    var badge = (providerPanels[name] || document.createElement('div')).querySelector('.rank-badge');
    if (badge && badge.textContent) {
      var pos = parseInt(badge.textContent.replace('#', ''));
      if (!isNaN(pos)) return pos;
    }
    return 999;
  }

  function showLanguage(msg) {
    if (!languageBody) return;
    languageBody.innerHTML = '';
    var gate = msg.vocab_gate || 'pass';
    var atoms = msg.atoms || [];
    var atomCount = msg.atom_count || atoms.length;
    var understandUs = msg.understand_us || 0;
    var vocabUs = msg.vocab_us || 0;
    var langEntryCount = msg.lang_entry_count || 0;
    var langWnCount = msg.lang_wn_count || 0;

    if (gate === 'blocked') {
      languageBody.innerHTML = '<span class="track-blocked">BLOCKED — vocabulary gate rejected input</span>';
      if (languageBadge) { languageBadge.textContent = 'BLOCKED'; languageBadge.className = 'track-badge active-purple'; }
    } else {
      // Header line: PASS + timing + word counts
      var timeStr = '';
      if (understandUs > 0 || vocabUs > 0) {
        timeStr = ' <span class="lang-timing">' + fmtUs(understandUs) + ' parse';
        if (vocabUs > 0) timeStr += ' + ' + fmtUs(vocabUs) + ' vocab';
        timeStr += '</span>';
      }
      var html = '<span class="lang-gate-pass">PASS</span>' +
        ' <span class="lang-count">' + atomCount + ' atoms</span>' + timeStr;

      // Word count line
      if (langEntryCount > 0 || langWnCount > 0) {
        html += '<div style="font-size:9px;color:var(--text-dim)">';
        if (langEntryCount > 0) html += fmtK(langEntryCount) + ' words';
        if (langWnCount > 0) html += ', ' + fmtK(langWnCount) + ' WordNet';
        html += '</div>';
      }

      // Atom trace — each word colored by role
      if (atoms.length > 0) {
        html += '<div class="lang-atoms">';
        atoms.forEach(function(a) {
          var color = roleColors[a.r] || '#808080';
          var fn = a.f && a.f !== 'none' ? a.f : '';
          var title = a.r + (fn ? ':' + fn : '');
          html += '<span class="lang-atom" style="color:' + color + '" title="' + escapeHtml(title) + '">' +
            escapeHtml(a.w) +
            '<span class="lang-atom-role">' + escapeHtml(a.r.charAt(0).toUpperCase()) + '</span>' +
            '</span>';
        });
        html += '</div>';
      }

      languageBody.innerHTML = html;
      if (languageBadge) { languageBadge.textContent = 'PASS'; languageBadge.className = 'track-badge active-orange'; }
    }
    // READ tile — not yet in pipeline, show idle
    if (readBody) readBody.innerHTML = '<span class="track-empty">idle</span>';
    // LEARN tile — not yet in pipeline, show idle
    if (learnBody) learnBody.innerHTML = '<span class="track-empty">idle</span>';
  }

  function fmtUs(us) {
    if (!us || us <= 0) return '0\u00B5s';
    if (us < 1000) return us + '\u00B5s';
    if (us < 1000000) return (us / 1000).toFixed(1) + 'ms';
    return (us / 1000000).toFixed(2) + 's';
  }

  function showUnderstand(msg) {
    if (!understandBody) return;
    understandBody.innerHTML = '';
    var mode = msg.mode || '';
    var intent = msg.intent || '';
    var entities = msg.entities || [];
    var entityCount = msg.entity_count || entities.length;
    var actionCount = msg.action_count || 0;
    var constraintCount = msg.constraint_count || 0;
    var atomCount = msg.atom_count || 0;
    if (!mode && entities.length === 0) {
      understandBody.innerHTML = '<span class="track-empty">no parse</span>';
      return;
    }
    var usTime = msg.us || 0;
    var html = '<span style="color:#e0a0ff;font-weight:600;font-size:10px;letter-spacing:0.5px">' +
      escapeHtml(mode.toUpperCase()) + '</span>';
    if (intent) html += ' \u2192 <span style="color:var(--text-dim);font-size:10px">' + escapeHtml(intent) + '</span>';
    // Counts trace
    var parts = [];
    if (entityCount > 0) parts.push(entityCount + ' entities');
    if (constraintCount > 0) parts.push(constraintCount + ' constraints');
    if (actionCount > 0) parts.push(actionCount + ' actions');
    if (parts.length > 0) html += ' <span style="color:var(--text-dim);font-size:9px">' + parts.join(', ') + '</span>';
    if (usTime > 0) html += ' <span class="lang-timing">' + fmtUs(usTime) + '</span>';
    if (entities.length > 0) {
      html += '<div style="margin-top:2px;font-size:11px">';
      entities.forEach(function(e) {
        html += '<span style="display:inline-block;background:var(--bg-lighter);padding:1px 5px;border-radius:2px;margin:1px 2px;color:var(--text)">' + escapeHtml(e) + '</span>';
      });
      html += '</div>';
    }
    understandBody.innerHTML = html;
    if (understandBadge) {
      understandBadge.textContent = mode;
      understandBadge.className = 'track-badge active-lavender';
    }
  }

  function showAcquire(msg) {
    if (!acquireBody) return;
    var concerns = msg.concerns || [];
    if (concerns.length === 0) {
      acquireBody.innerHTML = '<span class="track-empty">no sources routed</span>';
      return;
    }
    var totalSources = 0;
    concerns.forEach(function(c) { totalSources += (c.sources || []).length; });
    acquireBody.innerHTML = '<span style="color:#569cd6;font-weight:600;font-size:10px">' +
      concerns.length + ' concerns → ' + totalSources + ' sources</span>';
    if (acquireBadge) {
      acquireBadge.textContent = totalSources + ' routed';
      acquireBadge.className = 'track-badge active-steel';
    }
  }

  function showReason(msg) {
    if (!reasonBody) return;
    reasonBody.innerHTML = '';
    var answer = msg.answer || '';
    var factsUsed = msg.facts_used || 0;
    var confidence = msg.confidence || 0;
    var vocabGate = msg.vocab_gate || 'pass';
    var level = msg.level || 0;
    var clauses = msg.clauses || 0;
    var scanned = msg.scanned || 0;

    if (vocabGate === 'blocked') {
      reasonBody.innerHTML = '<span class="track-blocked">vocabulary gate blocked</span>';
    } else if (!answer || factsUsed === 0) {
      reasonBody.innerHTML = '<span class="track-empty">no reasoning result</span>';
    } else {
      var reasonUs = msg.us || 0;
      // Level label
      var levelLabels = { 1: 'scan', 2: 'set-ops', 3: 'constrained', 4: 'chain' };
      var levelStr = level > 0 ? 'L' + level + ':' + (levelLabels[level] || '') : '';
      var html = '';
      if (levelStr) html += '<span style="color:var(--accent-blue);font-size:10px;font-weight:700">' + levelStr + '</span> ';
      html += '<span style="color:var(--accent-blue);font-size:10px;font-weight:600">';
      if (clauses > 0) html += clauses + ' clauses, ';
      if (scanned > 0) html += scanned + ' scanned, ';
      html += factsUsed + ' matched, ' + (confidence * 100).toFixed(0) + '%</span>';
      if (reasonUs > 0) html += ' <span class="lang-timing">' + fmtUs(reasonUs) + '</span>';
      html += '<div style="margin-top:2px">' + escapeHtml(answer) + '</div>';
      reasonBody.innerHTML = html;
    }
    if (reasonBadge) {
      if (factsUsed > 0) {
        var badgeText = level > 0 ? 'L' + level : (confidence > 0.5 ? 'MATCH' : 'WEAK');
        reasonBadge.textContent = badgeText;
        reasonBadge.className = 'track-badge ' + (confidence > 0.5 ? 'active-blue' : 'active-gold');
      } else {
        reasonBadge.textContent = '';
        reasonBadge.className = 'track-badge';
      }
    }
  }

  function showMemory(msg) {
    if (!memoryBody) return;
    memoryBody.innerHTML = '';

    var answer = msg.answer || '';
    var factsUsed = msg.facts_used || 0;
    var vocabGate = msg.vocab_gate || 'pass';
    var memFacts = msg.mem_facts || 0;
    var intelFacts = msg.intel_facts || 0;

    // Filter auto-learn noise — treat as empty
    var isNoise = !answer || factsUsed === 0 ||
      answer.indexOf("don't have knowledge") !== -1 ||
      answer.indexOf("Let me search") !== -1;

    if (vocabGate === 'blocked') {
      memoryBody.innerHTML = '<span class="track-blocked">vocabulary gate blocked</span>';
    } else if (isNoise) {
      // Still show store stats even when no answer
      var storeHtml = '<span class="track-empty">no local knowledge</span>';
      if (memFacts > 0 || intelFacts > 0) {
        storeHtml = '<span style="color:var(--text-dim);font-size:10px">';
        if (memFacts > 0) storeHtml += 'Memory: ' + memFacts + ' facts';
        if (intelFacts > 0) storeHtml += (memFacts > 0 ? ', ' : '') + 'Intel: ' + intelFacts + ' facts';
        storeHtml += '</span>';
      }
      memoryBody.innerHTML = storeHtml;
    } else {
      var html = '';
      if (memFacts > 0 || intelFacts > 0) {
        html += '<span style="color:var(--text-dim);font-size:9px">';
        if (memFacts > 0) html += 'Memory: ' + memFacts;
        if (intelFacts > 0) html += (memFacts > 0 ? ' · ' : '') + 'Intel: ' + intelFacts;
        html += '</span><br>';
      }
      html += escapeHtml(answer);
      memoryBody.innerHTML = html;
    }

    if (memoryBadge) {
      if (factsUsed > 0) {
        memoryBadge.textContent = factsUsed + ' facts';
        memoryBadge.className = 'track-badge active-gold';
      } else {
        memoryBadge.textContent = '';
        memoryBadge.className = 'track-badge';
      }
    }
  }

  function showCatalog(msg) {
    if (!catalogBody) return;
    catalogBody.innerHTML = '';

    var concerns = msg.concerns || [];
    if (concerns.length === 0) {
      catalogBody.innerHTML = '<span class="track-empty">no matching sources</span>';
      return;
    }

    var totalSources = 0;
    var html = '';
    concerns.forEach(function(concern) {
      var sources = concern.sources || [];
      totalSources += sources.length;
      html += '<div style="margin-bottom:4px"><span class="catalog-concern">' + escapeHtml(concern.id) + '</span>';
      if (sources.length > 0) {
        sources.forEach(function(src) {
          html += ' <span class="catalog-source">' + escapeHtml(src.name || src.id);
          if (src.triples > 0) {
            html += ' <span class="catalog-triples">' + src.triples.toLocaleString() + '</span>';
          }
          html += '</span>';
        });
      }
      html += '</div>';
    });
    catalogBody.innerHTML = html;

    if (catalogBadge) {
      if (totalSources > 0) {
        catalogBadge.textContent = totalSources + ' sources';
        catalogBadge.className = 'track-badge active-green';
      } else {
        catalogBadge.textContent = '';
        catalogBadge.className = 'track-badge';
      }
    }
  }

  function showSearchStep(msg) {
    if (!searchBody) return;
    // First step clears the placeholder
    if (searchBody.querySelector('.thinking')) {
      searchBody.innerHTML = '';
    }
    var line = document.createElement('div');
    line.className = 'search-step' + (msg.indent ? ' search-step-indent' : '');
    var stepClass = msg.step === 'error' ? 'search-step-error' :
                    msg.step === 'done' ? 'search-step-done' : 'search-step-normal';
    if (msg.indent) {
      line.innerHTML = '<span class="search-step-tree">├─</span> ' +
                       '<span class="search-step-detail">' + escapeHtml(msg.detail || '') + '</span>';
    } else {
      line.innerHTML = '<span class="search-step-label ' + stepClass + '">' + escapeHtml(msg.step || '') + '</span> ' +
                       '<span class="search-step-detail">' + escapeHtml(msg.detail || '') + '</span>';
    }
    searchBody.appendChild(line);
    searchBody.scrollTop = searchBody.scrollHeight;
  }

  // Per-engine trace line — shows current status under each provider header
  function setProviderTrace(name, step, detail, level) {
    var el = providerTraces[name];
    if (!el) return;
    var cls = level === 'error' ? 'search-step-error' :
              level === 'done' ? 'search-step-done' :
              level === 'dim' ? 'search-step-detail' : 'search-step-normal';
    el.innerHTML = '<span class="search-step-label ' + cls + '">' + escapeHtml(step) + '</span> ' +
      '<span class="search-step-detail">' + escapeHtml(detail).substring(0, 100) + '</span>';
    el.classList.remove('hidden');
  }

  function clearProviderTraces() {
    Object.keys(providerTraces).forEach(function(name) {
      providerTraces[name].innerHTML = '';
      providerTraces[name].classList.add('hidden');
      setProviderStatus(name, 'idle');
      var elEl = document.getElementById('elapsed-' + name);
      if (elEl) elEl.textContent = '';
      var tokEl = document.getElementById('tokens-' + name);
      if (tokEl) tokEl.textContent = '';
    });
    providerElapsed = {};
  }

  // Update TRON expert trace — mirror search_step on the selected expert engine
  var tronExpert = null;
  function updateProviderTronTrace(msg) {
    var step = msg.step || '';
    var detail = msg.detail || '';
    if (step === 'select' && detail.indexOf('expert:') === 0) {
      var match = detail.match(/expert:\s*(\S+)/);
      if (match) {
        tronExpert = match[1];
        providers.forEach(function(name) {
          if (name.toLowerCase() === tronExpert.toLowerCase()) tronExpert = name;
        });
      }
    }
    if (tronExpert) {
      setProviderTrace(tronExpert, step.toUpperCase(), detail, step === 'error' ? 'error' : step === 'done' ? 'done' : 'normal');
    }
  }

  function showSearch(msg) {
    if (!searchBody) return;

    var content = msg.content || '';
    if (msg.error) {
      var errDiv = document.createElement('div');
      errDiv.className = 'search-step';
      errDiv.innerHTML = '<span class="search-step-error">error</span> ' + escapeHtml(msg.error);
      searchBody.appendChild(errDiv);
      return;
    }

    if (!content) {
      var emptyDiv = document.createElement('div');
      emptyDiv.className = 'search-step';
      emptyDiv.innerHTML = '<span class="search-step-done">done</span> no results';
      searchBody.appendChild(emptyDiv);
      return;
    }

    // Result — snippet + clickable link to full modal
    var sep = document.createElement('div');
    sep.className = 'search-separator';
    sep.textContent = '── RESULT ──';
    searchBody.appendChild(sep);

    var lines = content.split('\n');
    var snippet = lines.slice(0, 3).join('\n');
    var result = document.createElement('pre');
    result.className = 'search-result';
    result.style.cssText = 'white-space:pre-wrap;font-size:12px;color:var(--text-main);margin:4px 0;';
    result.textContent = snippet;
    searchBody.appendChild(result);

    if (lines.length > 3) {
      var more = document.createElement('div');
      more.className = 'search-step';
      var link = document.createElement('span');
      link.className = 'winner-link';
      link.style.cursor = 'pointer';
      var provName = msg.provider || '';
      var modalTitle = provName ? (providerModelMap[provName] || provName) + ' — GUIDED AI' : 'GUIDED AI RESULT';
      link.textContent = 'read more... (' + lines.length + ' lines)';
      (function(c, t) {
        link.addEventListener('click', function() {
          openResponseViewer(provName, c, t);
        });
      })(content, modalTitle);
      more.appendChild(link);
      searchBody.appendChild(more);
    }

    // Citations
    var citations = msg.citations || [];
    if (citations.length > 0) {
      var citSep = document.createElement('div');
      citSep.className = 'search-separator';
      citSep.textContent = '── SOURCES ──';
      searchBody.appendChild(citSep);

      citations.forEach(function(url, idx) {
        var cite = document.createElement('div');
        cite.className = 'search-citation';
        cite.innerHTML = '<span class="cite-num">[' + (idx + 1) + ']</span> ' + escapeHtml(url);
        searchBody.appendChild(cite);
      });
    }

    if (searchBadge && msg.provider) {
      var label = msg.provider;
      if (msg.pillar && msg.pillar !== 'general') label += ' · ' + msg.pillar;
      searchBadge.textContent = label;
      searchBadge.className = 'track-badge active-blue';
    }
  }

  // --- Judge / Rank ---

  function updateQueryStatus() {
    if (!senseDisplay) return;
    var pending = Object.keys(queryPending);
    if (pending.length === 0 && queryDone > 0) {
      senseDisplay.innerHTML = senseHtml +
        ' <span style="color:var(--accent-green)">DONE (' + queryDone + '/' + queryTotal + ')</span>';
    } else if (pending.length > 0) {
      var names = pending.map(function(n) { return n.toUpperCase(); }).join(', ');
      senseDisplay.innerHTML = senseHtml +
        ' <span style="color:var(--text-dim)">(' + queryDone + '/' + queryTotal + ') waiting: ' + escapeHtml(names) + '</span>';
    }
  }

  function updateJudgeStatus() {
    if (!senseDisplay) return;
    var pending = Object.keys(judgePending);
    var progress = judgeTotal > 0 ? ' (' + judgeCompleted + '/' + judgeTotal + ')' : '';
    if (pending.length === 0) {
      senseDisplay.innerHTML = '<span style="color:var(--accent-gold)">JUDGING' + progress + '</span> <span style="color:var(--accent-green)">all voted</span>';
    } else {
      var names = pending.map(function(n) { return n.toUpperCase(); }).join(', ');
      senseDisplay.innerHTML = '<span style="color:var(--accent-gold)">JUDGING' + progress + '</span> <span style="color:var(--text-dim)">waiting: ' + escapeHtml(names) + '</span>';
    }
  }

  function showJudgeStep(detail) {
    if (!searchBody) return;
    if (searchBody.querySelector('.thinking')) searchBody.innerHTML = '';
    var line = document.createElement('div');
    line.className = 'search-step';
    line.innerHTML = '<span class="search-step-label judge-step-label">JUDGE</span> ' +
      '<span class="search-step-detail">' + escapeHtml(detail) + '</span>';
    searchBody.appendChild(line);
    searchBody.scrollTop = searchBody.scrollHeight;
  }

  function showJudgeVote(msg) {
    if (!searchBody) return;
    var rankings = msg.rankings || [];
    var line = document.createElement('div');
    line.className = 'search-step';
    var voteMs = msg.elapsed_ms || 0;
    line.innerHTML = '<span class="search-step-label judge-step-label">VOTE</span> ' +
      '<span class="search-step-detail">' + escapeHtml(msg.provider) +
      ' <span style="color:var(--text-dim)">' + fmtTime(voteMs) + '</span> ' +
      ': ' + rankings.map(function(r, i) { return (i+1) + '.' + r; }).join(' ') +
      ' — $' + formatCost(msg.cost || 0) + '</span>';
    searchBody.appendChild(line);
    // Show raw reasoning as indent
    if (msg.raw) {
      var rawLine = document.createElement('div');
      rawLine.className = 'search-step search-step-indent';
      rawLine.innerHTML = '<span class="search-step-tree">├─</span> ' +
        '<span class="search-step-detail judge-raw">' + escapeHtml(msg.raw) + '</span>';
      searchBody.appendChild(rawLine);
    }

    // Accumulate scores for live leaderboard
    judgeCosts[msg.provider] = msg.cost || 0;
    rankings.forEach(function(name, idx) {
      if (!judgeScores[name]) judgeScores[name] = { totalRank: 0, votes: 0 };
      judgeScores[name].totalRank += (idx + 1);
      judgeScores[name].votes++;
    });

    // Render live leaderboard
    renderLiveLeaderboard();
    searchBody.scrollTop = searchBody.scrollHeight;
  }

  function renderLiveLeaderboard() {
    if (!searchBody) return;
    // Remove old leaderboard if exists
    if (judgeLeaderboardEl) judgeLeaderboardEl.remove();

    var names = Object.keys(judgeScores);
    if (names.length === 0) return;
    var total = names.length;

    // Sort by avg rank ascending (lower = better)
    names.sort(function(a, b) {
      var avgA = judgeScores[a].totalRank / judgeScores[a].votes;
      var avgB = judgeScores[b].totalRank / judgeScores[b].votes;
      return avgA - avgB;
    });

    var voteCount = names.length > 0 ? judgeScores[names[0]].votes : 0;

    judgeLeaderboardEl = document.createElement('div');
    judgeLeaderboardEl.className = 'judge-live-board';
    judgeLeaderboardEl.innerHTML = '<div class="search-separator">── LIVE (' + voteCount + ' votes) ──</div>';

    names.forEach(function(name, idx) {
      var s = judgeScores[name];
      var avgRank = s.totalRank / s.votes;
      var score = Math.round(((total - avgRank) / (total - 1)) * 100);
      if (isNaN(score)) score = 0;
      var pos = idx + 1;
      var medalClass = pos === 1 ? 'rank-medal-gold' : pos === 2 ? 'rank-medal-silver' : pos === 3 ? 'rank-medal-bronze' : 'rank-medal-dim';
      var row = document.createElement('div');
      row.className = 'judge-rank-line';
      var dispName = providerModelMap[name] || name;
      row.innerHTML = '<span class="' + medalClass + '">#' + pos + '</span> ' +
        '<span class="judge-rank-name">' + escapeHtml(dispName) + '</span> ' +
        '<span class="judge-rank-score">' + score + ' pts</span> ' +
        '<span class="judge-rank-voters">(' + s.votes + ' votes)</span>';
      judgeLeaderboardEl.appendChild(row);
    });

    searchBody.appendChild(judgeLeaderboardEl);
  }

  function showJudgeResult(msg) {
    var ranks = msg.final_ranks || [];
    var total = ranks.length || 1;

    // Remove live leaderboard
    if (judgeLeaderboardEl) { judgeLeaderboardEl.remove(); judgeLeaderboardEl = null; }

    if (searchBody) {
      var sep = document.createElement('div');
      sep.className = 'search-separator';
      sep.textContent = '── FINAL RANKINGS ──';
      searchBody.appendChild(sep);

      ranks.forEach(function(r) {
        var medalIcon = r.medal === 'gold' ? '#1' : r.medal === 'silver' ? '#2' : r.medal === 'bronze' ? '#3' : '#' + r.position;
        var medalClass = r.medal !== 'none' ? 'rank-medal-' + r.medal : 'rank-medal-dim';
        var score = Math.round(((total - r.avg_rank) / (total - 1)) * 100);
        if (isNaN(score)) score = 0;
        var line = document.createElement('div');
        line.className = 'judge-rank-line';
        var dispName = providerModelMap[r.provider] || r.provider;
        line.innerHTML = '<span class="' + medalClass + '">' + medalIcon + '</span> ' +
          '<span class="judge-rank-name winner-link" data-winner="' + escapeHtml(r.provider) + '">' + escapeHtml(dispName) + '</span> ' +
          '<span class="judge-rank-score">' + score + ' pts</span> ' +
          '<span class="judge-rank-voters">(' + r.votes + ' votes)</span>';
        line.querySelector('.winner-link').addEventListener('click', function() {
          openResponseViewer(r.provider);
        });
        searchBody.appendChild(line);
      });

      searchBody.scrollTop = searchBody.scrollHeight;
    }
    if (searchBadge) {
      searchBadge.textContent = 'RANKED';
      searchBadge.className = 'track-badge active-gold';
    }

    // Put rank badges on provider tiles
    ranks.forEach(function(r) {
      var panel = providerPanels[r.provider];
      if (!panel) return;
      var badge = panel.querySelector('.rank-badge');
      if (!badge) {
        badge = document.createElement('span');
        badge.className = 'rank-badge';
        badge.id = 'rank-' + r.provider;
        var nameEl = panel.querySelector('.prov-name');
        if (nameEl) nameEl.parentNode.insertBefore(badge, nameEl.nextSibling);
      }
      var medalClass = r.medal !== 'none' ? 'rank-medal-' + r.medal : 'rank-medal-dim';
      badge.className = 'rank-badge ' + medalClass;
      var tileScore = Math.round(((total - r.avg_rank) / (total - 1)) * 100);
      if (isNaN(tileScore)) tileScore = 0;
      badge.textContent = '#' + r.position;
      badge.title = tileScore + ' pts (' + r.votes + ' votes)';
    });
  }

  function showInspectLink(label, title, content, length, modalTitle) {
    if (!searchBody || !content) return;
    var snippet = content.substring(0, 80).replace(/\n/g, ' ');
    if (content.length > 80) snippet += '...';
    var line = document.createElement('div');
    line.className = 'search-step';
    var labelColor = label === 'SUMMARIZE' ? 'var(--accent-cyan,#0ff)' : 'var(--accent-gold)';
    line.innerHTML = '<span class="search-step-label" style="color:' + labelColor + '">' + escapeHtml(label) + '</span> ' +
      '<span class="search-step-detail">' + escapeHtml(title) + ' (' + length + ' chars): ' +
      '<span class="winner-link inspect-link">' + escapeHtml(snippet) + '</span></span>';
    line.querySelector('.inspect-link').addEventListener('click', function() {
      openResponseViewer('', content, modalTitle);
    });
    searchBody.appendChild(line);
    searchBody.scrollTop = searchBody.scrollHeight;
  }

  function showSummarizeStep(detail) {
    if (!searchBody) return;
    if (searchBody.querySelector('.thinking')) searchBody.innerHTML = '';
    var line = document.createElement('div');
    line.className = 'search-step';
    line.innerHTML = '<span class="search-step-label" style="color:var(--accent-cyan,#0ff)">SUMMARIZE</span> ' +
      '<span class="search-step-detail">' + escapeHtml(detail) + '</span>';
    searchBody.appendChild(line);
    searchBody.scrollTop = searchBody.scrollHeight;
  }

  function showSummarizeDone(msg) {
    var content = msg.content || '';
    if (!content) {
      if (searchBadge) { searchBadge.textContent = 'THINKING'; searchBadge.className = 'track-badge'; }
      return;
    }

    if (searchBody) {
      /* Response inspect link in THINKING trace */
      showInspectLink('SUMMARIZE', 'RESPONSE', content, content.length, 'Summary Response');

      var sep = document.createElement('div');
      sep.className = 'search-separator';
      sep.textContent = '── COMPARATIVE SUMMARY ──';
      searchBody.appendChild(sep);

      /* Show first 8 lines as preview */
      var lines = content.split('\n');
      var preview = lines.slice(0, 8).join('\n');
      var pre = document.createElement('pre');
      pre.className = 'advise-preview';
      pre.style.cssText = 'white-space:pre-wrap;font-size:12px;color:var(--text-main);margin:4px 0;';
      pre.textContent = preview;
      searchBody.appendChild(pre);

      if (lines.length > 8) {
        var more = document.createElement('div');
        more.className = 'search-step';
        var link = document.createElement('span');
        link.className = 'winner-link';
        link.style.cursor = 'pointer';
        link.textContent = 'read more... (' + lines.length + ' lines)';
        link.addEventListener('click', function() {
          openResponseViewer(msg.provider, content, 'COMPARATIVE SUMMARY');
        });
        more.appendChild(link);
        searchBody.appendChild(more);
      }

      if (msg.cost) {
        var costLine = document.createElement('div');
        costLine.className = 'search-step';
        var dispName = providerModelMap[msg.provider] || msg.provider;
        costLine.innerHTML = '<span class="search-step-label" style="color:var(--text-dim)">$' + formatCost(msg.cost) + '</span> ' +
          '<span class="search-step-detail">' + escapeHtml(dispName) + ' — ' +
          fmtK(msg.tokens_in || 0) + '/' + fmtK(msg.tokens_out || 0) + ' tok — ' + fmtTime(msg.elapsed_ms || 0) + '</span>';
        searchBody.appendChild(costLine);
      }

      searchBody.scrollTop = searchBody.scrollHeight;
    }

    queryCost += (msg.cost || 0);
    queryTokensIn += (msg.tokens_in || 0);
    queryTokensOut += (msg.tokens_out || 0);
    rebuildStatusBar();

    if (searchBadge) {
      searchBadge.textContent = 'SUMMARIZED';
      searchBadge.className = 'track-badge active-green';
    }
  }

  // Session cost accumulators for status bar
  var sessionGoCost = 0;
  var sessionJudgeCost = 0;
  var lastDailyTotal = 0;
  var lastDailyQueries = 0;
  var lastDailyTokensIn = 0;
  var lastDailyTokensOut = 0;

  function showJudgeCost(msg) {
    sessionJudgeCost += (msg.last || 0);
    rebuildStatusBar();
  }

  function showDailyCost(msg) {
    lastDailyTotal = msg.total || 0;
    lastDailyQueries = msg.queries || 0;
    lastDailyTokensIn = msg.tokens_in || 0;
    lastDailyTokensOut = msg.tokens_out || 0;
    rebuildStatusBar();
  }

  function rebuildStatusBar() {
    if (!dailyDisplay) return;
    var parts = [];

    // LAST — this query: cost + tokens in/out
    var lastCost = sessionGoCost + sessionJudgeCost;
    var lastIn = queryTokensIn;
    var lastOut = queryTokensOut;
    if (lastCost > 0 || lastIn > 0) {
      parts.push('<span class="stat-label">LAST</span> <span class="stat-cost">$' +
        formatCost(lastCost) + '</span> <span class="stat-count">' +
        fmtK(lastIn) + '/' + fmtK(lastOut) + '</span>');
    }

    // TOTAL — daily: cost + tokens in/out
    if (lastDailyTotal > 0 || lastDailyTokensIn > 0) {
      parts.push('<span class="stat-label stat-label-today">TOTAL</span> <span class="stat-cost-today">$' +
        formatCost(lastDailyTotal) + '</span> <span class="stat-count">' +
        fmtK(lastDailyTokensIn) + '/' + fmtK(lastDailyTokensOut) + '</span>');
    }

    dailyDisplay.innerHTML = parts.join(' <span class="stat-sep">\u2502</span> ');
  }

  // --- Global hint ---

  function setGlobalHint(hint) {
    globalHint = hint;
    if (ws && ws.readyState === 1) {
      ws.send(JSON.stringify({ type: 'set_global_hint', hint: hint }));
    }
    updateGlobalHintBar();
  }

  // Engines that fully support system prompts
  var hintFullSupport = ['claude-opus','claude-sonnet','gpt','codex','gemini','grok','deepseek','perplexity','mistral'];
  // Engines with limited/no system prompt support (reasoning models)
  var hintLimited = ['o3','deepseek-r1'];

  function updateGlobalHintBar() {
    if (!globalHintBar || !globalHintTextEl) return;
    if (globalHint && globalHint.length > 0) {
      globalHintTextEl.textContent = globalHint;
      globalHintBar.classList.remove('hidden');
    } else {
      globalHintBar.classList.add('hidden');
    }
    updateHintSupport();
  }

  function updateHintSupport() {
    var hasHint = (globalHint && globalHint.length > 0);
    providers.forEach(function(name) {
      var el = document.getElementById('hintsup-' + name);
      if (!el) return;
      // Also check per-provider hints
      var provHint = hints[name] && hints[name].length > 0;
      var active = hasHint || provHint;
      if (!active) {
        el.textContent = '';
        el.className = 'hint-support';
        el.title = '';
        return;
      }
      if (hintLimited.indexOf(name) !== -1) {
        el.textContent = '\u25CB'; // hollow circle — hint may be ignored
        el.className = 'hint-support hint-limited';
        el.title = 'hint active (reasoning model — may ignore)';
      } else {
        el.textContent = '\u25CF'; // filled circle — hint active
        el.className = 'hint-support hint-full';
        el.title = 'hint active';
      }
    });
  }

  // --- Response viewer ---

  function openResponseViewer(name, overrideContent, overrideTitle) {
    var body = providerBodies[name];
    var text = overrideContent || (body ? body.textContent || '' : '');
    var viewer = document.getElementById('response-viewer');
    var nameEl = document.getElementById('response-viewer-name');
    var textEl = document.getElementById('response-viewer-text');
    if (nameEl) nameEl.textContent = overrideTitle || providerModelMap[name] || name;
    if (textEl) textEl.textContent = text;
    if (viewer) viewer.classList.remove('hidden');

    var copyBtn = document.getElementById('response-copy');
    var closeBtn = document.getElementById('response-close');
    if (copyBtn) {
      copyBtn.onclick = function() {
        navigator.clipboard.writeText(text).then(function() {
          copyBtn.textContent = 'COPIED!';
          setTimeout(function() { copyBtn.textContent = 'COPY'; }, 1500);
        });
      };
    }
    function closeViewer() {
      viewer.classList.add('hidden');
      document.removeEventListener('keydown', escHandler);
    }
    function escHandler(e) {
      if (e.key === 'Escape') { closeViewer(); }
    }
    if (closeBtn) { closeBtn.onclick = closeViewer; }
    document.addEventListener('keydown', escHandler);
  }

  // --- Hint editor ---

  function openHintEditor(provider) {
    editingProvider = provider;
    if (hintProviderName) hintProviderName.textContent = provider.toUpperCase();
    if (hintText) hintText.value = hints[provider] || '';
    if (hintEditor) hintEditor.classList.remove('hidden');
    if (hintText) hintText.focus();
  }

  function closeHintEditor() {
    editingProvider = null;
    if (hintEditor) hintEditor.classList.add('hidden');
  }

  function saveHint() {
    if (!editingProvider || !hintText) return;
    var hint = hintText.value;
    hints[editingProvider] = hint;
    if (ws && ws.readyState === 1) {
      ws.send(JSON.stringify({ type: 'set_hint', provider: editingProvider, hint: hint }));
    }
    closeHintEditor();
  }

  function clearHint() {
    if (hintText) hintText.value = '';
  }

  function updateHintDots() {
    document.querySelectorAll('.provider-head').forEach(function(el) {
      var nameEl = el.querySelector('.prov-name');
      if (!nameEl) return;
      var p = (nameEl.getAttribute('data-p') || '').toLowerCase();
      var dot = el.querySelector('.hint-dot');
      if (dot) {
        var hasHint = false;
        Object.keys(hints).forEach(function(k) {
          if (k.toLowerCase() === p && hints[k] && hints[k].length > 0) hasHint = true;
        });
        dot.classList.toggle('active', hasHint);
      }
    });
  }

  // --- Util ---

  function formatCost(cost) {
    if (cost < 0.01) return cost.toFixed(4);
    if (cost < 1) return cost.toFixed(2);
    return cost.toFixed(2);
  }

  function fmtK(n) {
    if (n >= 1000000) return (n / 1000000).toFixed(1) + 'M';
    if (n >= 1000) return (n / 1000).toFixed(1) + 'K';
    return '' + n;
  }

  function fmtTime(ms) {
    if (!ms || ms <= 0) return '';
    if (ms < 1000) return ms + 'ms';
    var s = ms / 1000;
    if (s < 60) return s.toFixed(1) + 's';
    var m = Math.floor(s / 60);
    var rem = Math.round(s % 60);
    return m + 'm' + (rem > 0 ? rem + 's' : '');
  }

  function escapeHtml(text) {
    var div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
  }

  function toggleTrace() {
    // Trace always on — no toggle
  }

  // --- Stock ticker ---

  function updateTicker() {
    if (!tickerTrack) return;
    var items = [];
    providers.forEach(function(name) {
      var sc = sessionCosts[name];
      if (!sc || sc.last <= 0) return;
      var dir = 0; // 0=flat, 1=up, -1=down
      if (sc.prev !== undefined && sc.prev > 0) {
        if (sc.last > sc.prev) dir = 1;
        else if (sc.last < sc.prev) dir = -1;
      }
      var per1k = sc.last * 1000;
      var elapsed = providerElapsed[name] || 0;
      items.push({ name: name, cost: sc.last, per1k: per1k, total: sc.total, dir: dir, queries: sc.queries || 0, elapsed: elapsed });
    });
    if (items.length === 0) {
      tickerTrack.innerHTML = '<span class="ticker-empty">awaiting data...</span>';
      return;
    }
    // Build ticker items — duplicate for seamless loop
    var html = '';
    function renderItems() {
      var s = '';
      for (var i = 0; i < items.length; i++) {
        var it = items[i];
        var arrow = it.dir > 0 ? '<span class="tick-up">&#9650;</span>' :
                    it.dir < 0 ? '<span class="tick-down">&#9660;</span>' :
                    '<span class="tick-flat">&#9654;</span>';
        s += '<span class="ticker-item">' +
          '<span class="tick-name" data-p="' + escapeHtml(it.name) + '">' + escapeHtml(providerModelMap[it.name] || it.name) + '</span>' +
          arrow +
          '<span class="tick-cost' + (it.dir > 0 ? ' cost-up' : it.dir < 0 ? ' cost-down' : '') + '">$' + formatCost(it.cost) + '</span>' +
          '<span class="tick-per1k">$' + formatCost(it.per1k) + '/1K</span>' +
          (it.elapsed > 0 ? '<span class="tick-elapsed">' + fmtTime(it.elapsed) + '</span>' : '') +
          '<span class="tick-total">(' + it.queries + 'q $' + formatCost(it.total) + ')</span>' +
          '</span>';
      }
      return s;
    }
    // Triple the items for seamless wrap
    html = renderItems() + renderItems() + renderItems();
    tickerTrack.innerHTML = html;
    // Reset offset so it doesn't jump
    tickerOffset = 0;
  }

  function startTicker() {
    if (tickerAnimId) return;
    tickerLoop();
  }

  function tickerLoop() {
    if (!tickerTrack) { tickerAnimId = null; return; }
    tickerOffset += tickerSpeed;
    // Width of one set of items = total / 3
    var totalW = tickerTrack.scrollWidth;
    var setW = totalW / 3;
    if (setW > 0 && tickerOffset >= setW) {
      tickerOffset -= setW;
    }
    tickerTrack.style.transform = 'translateX(-' + tickerOffset + 'px)';
    tickerAnimId = requestAnimationFrame(tickerLoop);
  }

  function sendRaw(obj) {
    if (ws && ws.readyState === 1) {
      ws.send(JSON.stringify(obj));
    }
  }

  return { init: init, toggleTron: toggleTrace, sendRaw: sendRaw };
})();
