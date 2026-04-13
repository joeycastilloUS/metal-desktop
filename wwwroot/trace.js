/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */

/**
 * Trace controller — live NDJSON stream from relay/prime trace port.
 * Connects via /ws/trace (metal Desktop proxies TCP→WebSocket).
 * Renders trace events + pulse snapshots with component filtering.
 */
const TraceController = (() => {
  'use strict';

  var ws = null;
  var logEl = null;
  var statusEl = null;
  var targetEl = null;
  var countEl = null;
  var eventCount = 0;
  var activeFilter = 'all';
  var MAX_ENTRIES = 2000;
  var autoScroll = true;

  /* Component colors — match trace.h categories */
  var COMP_COLORS = {
    'net':      '#4fc1ff',
    'route':    '#c586c0',
    'auth':     '#d4a537',
    'store':    '#6a9955',
    'crypt':    '#f44747',
    'relay':    '#4ec9b0',
    'identity': '#dcdcaa',
    'mesh':     '#569cd6',
    'pulse':    '#ce9178'
  };

  function init() {
    logEl = document.getElementById('trace-log');
    statusEl = document.getElementById('trace-status');
    targetEl = document.getElementById('trace-target');
    countEl = document.getElementById('trace-count');

    /* Filter buttons */
    document.querySelectorAll('.trace-filter').forEach(function(btn) {
      btn.addEventListener('click', function() {
        activeFilter = btn.dataset.comp;
        document.querySelectorAll('.trace-filter').forEach(function(b) {
          b.classList.toggle('active', b.dataset.comp === activeFilter);
        });
        applyFilter();
      });
    });

    /* Auto-scroll: disable on manual scroll up, re-enable at bottom */
    if (logEl) {
      logEl.addEventListener('scroll', function() {
        var atBottom = logEl.scrollHeight - logEl.scrollTop - logEl.clientHeight < 40;
        autoScroll = atBottom;
      });
    }

    connect();
  }

  function connect() {
    ws = new WebSocket('ws://' + location.host + '/ws/trace');

    ws.onopen = function() {
      console.log('[trace] connected to desktop proxy');
    };

    ws.onmessage = function(ev) {
      try {
        var msg = JSON.parse(ev.data);
        handleEvent(msg);
      } catch (e) {
        /* raw line — display as-is */
        addRawLine(ev.data);
      }
    };

    ws.onclose = function() {
      console.log('[trace] disconnected, reconnecting...');
      if (statusEl) {
        statusEl.textContent = 'RECONNECTING';
        statusEl.className = 'badge trace-reconnecting';
      }
      setTimeout(connect, 3000);
    };

    ws.onerror = function() {
      console.error('[trace] WebSocket error');
    };
  }

  function handleEvent(msg) {
    if (msg.type === 'trace-status') {
      if (targetEl) targetEl.textContent = msg.target || '--';
      if (statusEl) {
        statusEl.textContent = msg.connected ? 'CONNECTED' : 'DISCONNECTED';
        statusEl.className = 'badge ' + (msg.connected ? 'trace-connected' : 'trace-disconnected');
      }
      return;
    }

    if (msg.type === 'pulse') {
      addPulseEntry(msg);
    } else if (msg.type === 'trace') {
      addTraceEntry(msg);
    } else {
      /* Unknown type — show raw */
      addRawLine(JSON.stringify(msg));
    }
  }

  function addTraceEntry(msg) {
    if (!logEl) return;
    eventCount++;
    if (countEl) countEl.textContent = eventCount;

    var div = document.createElement('div');
    div.className = 'trace-entry';
    div.dataset.comp = msg.c || '';

    var comp = msg.c || '?';
    var color = COMP_COLORS[comp] || '#cccccc';

    var html = '<span class="trace-seq">' + (msg.s || 0) + '</span>';
    html += '<span class="trace-comp" style="color:' + color + '">' + escapeHtml(comp) + '</span>';
    html += '<span class="trace-event">' + escapeHtml(msg.e || '') + '</span>';
    if (msg.d) html += '<span class="trace-detail">' + escapeHtml(msg.d) + '</span>';
    if (msg.f) html += '<span class="trace-source">' + escapeHtml(msg.f) + ':' + (msg.l || 0) + '</span>';

    div.innerHTML = html;

    /* Apply current filter */
    if (activeFilter !== 'all' && comp !== activeFilter) {
      div.style.display = 'none';
    }

    logEl.appendChild(div);
    pruneLog();
    if (autoScroll) logEl.scrollTop = logEl.scrollHeight;
  }

  function addPulseEntry(msg) {
    if (!logEl) return;
    eventCount++;
    if (countEl) countEl.textContent = eventCount;

    var div = document.createElement('div');
    div.className = 'trace-entry trace-pulse';
    div.dataset.comp = 'pulse';

    var color = COMP_COLORS['pulse'];
    var summary = '';
    if (msg.node) summary += 'node=' + msg.node.substring(0, 8) + '.. ';
    if (msg.role) summary += 'role=' + msg.role + ' ';
    if (msg.uptime !== undefined) summary += 'up=' + msg.uptime + 's ';
    if (msg.sessions) summary += 'sess=' + (msg.sessions.active || 0) + ' ';
    if (msg.peers) summary += 'peers=' + (msg.peers.count || 0);

    var html = '<span class="trace-seq">P</span>';
    html += '<span class="trace-comp" style="color:' + color + '">pulse</span>';
    html += '<span class="trace-event">snapshot</span>';
    html += '<span class="trace-detail">' + escapeHtml(summary.trim()) + '</span>';

    div.innerHTML = html;

    if (activeFilter !== 'all' && activeFilter !== 'pulse') {
      div.style.display = 'none';
    }

    logEl.appendChild(div);
    pruneLog();
    if (autoScroll) logEl.scrollTop = logEl.scrollHeight;
  }

  function addRawLine(text) {
    if (!logEl) return;
    var div = document.createElement('div');
    div.className = 'trace-entry trace-raw';
    div.dataset.comp = '';
    div.textContent = text;
    logEl.appendChild(div);
    pruneLog();
    if (autoScroll) logEl.scrollTop = logEl.scrollHeight;
  }

  function pruneLog() {
    if (!logEl) return;
    while (logEl.children.length > MAX_ENTRIES) {
      logEl.removeChild(logEl.firstChild);
    }
  }

  function applyFilter() {
    if (!logEl) return;
    var entries = logEl.querySelectorAll('.trace-entry');
    entries.forEach(function(el) {
      if (activeFilter === 'all') {
        el.style.display = '';
      } else {
        el.style.display = (el.dataset.comp === activeFilter) ? '' : 'none';
      }
    });
  }

  function escapeHtml(text) {
    var div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
  }

  return { init: init };
})();
