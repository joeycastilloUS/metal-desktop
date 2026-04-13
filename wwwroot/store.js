/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */

/**
 * STORE tab — read-only triple store browser.
 * Fetches LOCAL + MESH simultaneously, renders all stores in one unified view.
 * Filter applies across both sources at once.
 * Two-level expand/collapse: unit level (MEMORY, INTEL, etc.) and group level (subjects within).
 */
const StoreController = (() => {
  'use strict';

  let ws = null;
  let localStores = [];
  let meshStores = [];
  let meshError = '';          // visible error for mesh (auth required, etc.)
  let filterText = '';
  let pendingLocal = false;
  let pendingMesh = false;

  // DOM refs
  let refreshBtn = null;
  let statusBadge = null;
  let filterInput = null;
  let sectionsEl = null;
  let collapseAllBtn = null;
  let expandAllBtn = null;

  function init() {
    refreshBtn = document.getElementById('store-refresh');
    statusBadge = document.getElementById('store-status');
    filterInput = document.getElementById('store-filter');
    sectionsEl = document.getElementById('store-sections');
    collapseAllBtn = document.getElementById('store-collapse-all');
    expandAllBtn = document.getElementById('store-expand-all');

    if (!sectionsEl) return;

    if (refreshBtn) refreshBtn.addEventListener('click', refresh);
    if (filterInput) filterInput.addEventListener('input', onFilter);
    if (collapseAllBtn) collapseAllBtn.addEventListener('click', collapseAllUnits);
    if (expandAllBtn) expandAllBtn.addEventListener('click', expandAllUnits);
  }

  function connect(loopWs) {
    ws = loopWs;
  }

  function refresh() {
    if (!ws || ws.readyState !== 1) {
      if (statusBadge) statusBadge.textContent = 'NO WS';
      return;
    }
    if (statusBadge) statusBadge.textContent = 'LOADING';
    if (sectionsEl) sectionsEl.innerHTML = '<div class="store-loading">loading local + mesh...</div>';

    pendingLocal = true;
    pendingMesh = true;
    localStores = [];
    meshStores = [];
    meshError = '';

    ws.send(JSON.stringify({ type: 'store', source: 'local' }));
    ws.send(JSON.stringify({ type: 'store', source: 'mesh' }));
  }

  function onFilter() {
    filterText = (filterInput.value || '').toLowerCase();
    render();
  }

  function handleEvent(msg) {
    if (msg.type !== 'store_result') return false;

    var stores = msg.stores || [];

    if (msg.source === 'mesh') {
      meshStores = stores;
      meshError = msg.error || '';
      pendingMesh = false;
    } else {
      localStores = stores;
      pendingLocal = false;
    }

    updateStatus();
    render();
    return true;
  }

  // --- Expand/Collapse ALL units ---

  function collapseAllUnits() {
    if (!sectionsEl) return;
    sectionsEl.querySelectorAll('.store-section-body').forEach(function (body) {
      body.classList.add('collapsed');
    });
    sectionsEl.querySelectorAll('.store-section-toggle').forEach(function (tog) {
      tog.innerHTML = '&#9654;';
    });
  }

  function expandAllUnits() {
    if (!sectionsEl) return;
    sectionsEl.querySelectorAll('.store-section-body').forEach(function (body) {
      body.classList.remove('collapsed');
    });
    sectionsEl.querySelectorAll('.store-section-toggle').forEach(function (tog) {
      tog.innerHTML = '&#9660;';
    });
  }

  // --- Per-unit: collapse/expand all subject groups ---

  function collapseAllGroups(tbody) {
    tbody.querySelectorAll('.store-group-row').forEach(function (gr) {
      gr.querySelector('td').innerHTML = gr.querySelector('td').innerHTML.replace('&#9660;', '&#9654;').replace('\u25BC', '\u25B6');
      var next = gr.nextElementSibling;
      while (next && !next.classList.contains('store-group-row')) {
        next.style.display = 'none';
        next = next.nextElementSibling;
      }
      gr._groupOpen = false;
    });
  }

  function expandAllGroups(tbody) {
    tbody.querySelectorAll('.store-group-row').forEach(function (gr) {
      gr.querySelector('td').innerHTML = gr.querySelector('td').innerHTML.replace('&#9654;', '&#9660;').replace('\u25B6', '\u25BC');
      var next = gr.nextElementSibling;
      while (next && !next.classList.contains('store-group-row')) {
        next.style.display = '';
        next = next.nextElementSibling;
      }
      gr._groupOpen = true;
    });
  }

  function updateStatus() {
    if (!statusBadge) return;
    var all = getAllStores();
    var totalFacts = 0;
    all.forEach(function (s) { totalFacts += (s.facts || 0); });

    var parts = [];
    if (localStores.length > 0) parts.push(localStores.length + ' local');
    if (meshStores.length > 0) parts.push(meshStores.length + ' mesh');
    if (pendingLocal || pendingMesh) parts.push('loading...');

    statusBadge.textContent = parts.join(' + ') + ' / ' + totalFacts + ' facts';
  }

  function getAllStores() {
    var all = [];
    localStores.forEach(function (s) {
      all.push(Object.assign({}, s, { _source: 'local' }));
    });
    meshStores.forEach(function (s) {
      all.push(Object.assign({}, s, { _source: 'mesh' }));
    });
    return all;
  }

  function render() {
    if (!sectionsEl) return;
    sectionsEl.innerHTML = '';

    var all = getAllStores();

    if (all.length === 0 && !meshError && !pendingLocal && !pendingMesh) {
      sectionsEl.innerHTML = '<div class="store-empty">no stores</div>';
      return;
    }

    // Show mesh error as a visible section
    if (meshError) {
      var errSection = document.createElement('div');
      errSection.className = 'store-section';
      errSection.innerHTML =
        '<div class="store-section-header store-mesh-error">' +
          '<span class="store-source-badge mesh">MESH</span>' +
          '<span class="store-section-name">CATALOG</span>' +
          '<span class="store-mesh-error-text">' + escHtml(meshError) + '</span>' +
        '</div>';
      sectionsEl.appendChild(errSection);
    }

    all.forEach(function (store) {
      var section = document.createElement('div');
      section.className = 'store-section';

      var sourceBadge = store._source === 'mesh'
        ? '<span class="store-source-badge mesh">MESH</span>'
        : '<span class="store-source-badge local">LOCAL</span>';

      // Header with unit-level collapse + per-unit group collapse/expand
      var header = document.createElement('div');
      header.className = 'store-section-header';
      header.innerHTML =
        sourceBadge +
        '<span class="store-section-name">' + escHtml(store.name) + '</span>' +
        '<span class="store-section-stat">' + (store.facts || 0) + ' facts</span>' +
        '<span class="store-section-stat">' + (store.dict || 0) + ' terms</span>' +
        '<span class="store-section-stat">' + formatBytes(store.bytes || 0) + '</span>' +
        '<span class="store-group-btns">' +
          '<button class="store-grp-btn store-grp-collapse" title="collapse all groups">&#9644;</button>' +
          '<button class="store-grp-btn store-grp-expand" title="expand all groups">&#9776;</button>' +
        '</span>' +
        '<span class="store-section-toggle">&#9660;</span>';

      var body = document.createElement('div');
      body.className = 'store-section-body';

      // Unit-level collapse: click the toggle arrow
      var toggleEl = header.querySelector('.store-section-toggle');
      toggleEl.addEventListener('click', function (e) {
        e.stopPropagation();
        body.classList.toggle('collapsed');
        toggleEl.innerHTML = body.classList.contains('collapsed') ? '&#9654;' : '&#9660;';
      });

      // Click header name area also toggles unit
      header.querySelector('.store-section-name').addEventListener('click', function () {
        body.classList.toggle('collapsed');
        toggleEl.innerHTML = body.classList.contains('collapsed') ? '&#9654;' : '&#9660;';
      });

      // Per-unit group buttons
      header.querySelector('.store-grp-collapse').addEventListener('click', function (e) {
        e.stopPropagation();
        var tb = body.querySelector('tbody');
        if (tb) collapseAllGroups(tb);
      });
      header.querySelector('.store-grp-expand').addEventListener('click', function (e) {
        e.stopPropagation();
        var tb = body.querySelector('tbody');
        if (tb) expandAllGroups(tb);
      });

      // Filter triples
      var triples = store.triples || [];
      if (filterText) {
        triples = triples.filter(function (t) {
          return (t.s && t.s.toLowerCase().indexOf(filterText) >= 0) ||
                 (t.p && t.p.toLowerCase().indexOf(filterText) >= 0) ||
                 (t.o && t.o.toLowerCase().indexOf(filterText) >= 0);
        });
      }

      if (triples.length === 0) {
        body.innerHTML = '<div class="store-empty">' +
          (filterText ? 'no matches' : 'empty') + '</div>';
      } else {
        // Group by subject
        var groups = {};
        var groupOrder = [];
        triples.forEach(function (t) {
          var key = t.s || '(empty)';
          if (!groups[key]) {
            groups[key] = [];
            groupOrder.push(key);
          }
          groups[key].push(t);
        });

        var table = document.createElement('table');
        table.className = 'store-table';
        table.innerHTML = '<thead><tr><th>SUBJECT</th><th>PREDICATE</th><th>OBJECT</th></tr></thead>';
        var tbody = document.createElement('tbody');

        groupOrder.forEach(function (subj) {
          var rows = groups[subj];

          var groupRow = document.createElement('tr');
          groupRow.className = 'store-group-row';
          groupRow._groupOpen = true;

          function groupLabel(open) {
            return (open ? '&#9660; ' : '&#9654; ') + escHtml(subj) +
              ' <span style="color:var(--text-dim);font-weight:normal">(' + rows.length + ')</span>';
          }

          groupRow.innerHTML = '<td colspan="3">' + groupLabel(true) + '</td>';
          tbody.appendChild(groupRow);

          groupRow.addEventListener('click', function () {
            groupRow._groupOpen = !groupRow._groupOpen;
            groupRow.querySelector('td').innerHTML = groupLabel(groupRow._groupOpen);
            var next = groupRow.nextElementSibling;
            while (next && !next.classList.contains('store-group-row')) {
              next.style.display = groupRow._groupOpen ? '' : 'none';
              next = next.nextElementSibling;
            }
          });

          rows.forEach(function (t) {
            var tr = document.createElement('tr');
            tr.innerHTML =
              '<td class="col-s" title="' + escAttr(t.s) + '">' + escHtml(t.s) + '</td>' +
              '<td class="col-p" title="' + escAttr(t.p) + '">' + escHtml(t.p) + '</td>' +
              '<td class="col-o" title="' + escAttr(t.o) + '">' + escHtml(t.o) + '</td>';
            tbody.appendChild(tr);
          });
        });

        table.appendChild(tbody);
        body.appendChild(table);
      }

      section.appendChild(header);
      section.appendChild(body);
      sectionsEl.appendChild(section);
    });
  }

  function formatBytes(b) {
    if (b < 1024) return b + ' B';
    if (b < 1024 * 1024) return (b / 1024).toFixed(1) + ' KB';
    return (b / (1024 * 1024)).toFixed(1) + ' MB';
  }

  function escHtml(s) {
    if (!s) return '';
    return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  function escAttr(s) {
    if (!s) return '';
    return s.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  }

  return {
    init: init,
    connect: connect,
    refresh: refresh,
    handleEvent: handleEvent
  };
})();
