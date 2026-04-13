/* CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved */

/**
 * SVG Tree Layout Engine
 * Two-pass layout: measure (bottom-up) → position (top-down)
 * Renders architecture nodes with Bezier curve edges.
 */
const TreeRenderer = (() => {
  const NODE_W = 140;
  const NODE_H = 28;
  const H_GAP = 20;
  const V_GAP = 40;
  const PAD_X = 20;
  const PAD_Y = 20;

  let svg = null;
  let nodes = [];       // flat list of laid-out nodes
  let edges = [];       // parent-child edges
  let depEdges = [];    // dependency edges
  let selectedId = -1;
  let showDeps = false;
  let treeData = null;
  let onSelect = null;

  function init(svgEl, selectCallback) {
    svg = svgEl;
    onSelect = selectCallback;
  }

  function setTree(data) {
    treeData = data;
    layout();
    render();
  }

  function toggleDeps() {
    showDeps = !showDeps;
    render();
    return showDeps;
  }

  function selectNode(idx) {
    selectedId = idx;
    render();
    if (onSelect && nodes[idx]) onSelect(nodes[idx]);
  }

  function selectNext(dir) {
    if (nodes.length === 0) return;
    let next = selectedId + dir;
    if (next < 0) next = 0;
    if (next >= nodes.length) next = nodes.length - 1;
    selectNode(next);
  }

  function getSelected() {
    return selectedId >= 0 ? nodes[selectedId] : null;
  }

  function getNodes() {
    return nodes;
  }

  // --- Layout pass ---

  function layout() {
    nodes = [];
    edges = [];
    depEdges = [];

    if (!treeData) return;

    // Flatten tree with positions
    measureAndPosition(treeData, PAD_X, PAD_Y, 0);

    // Build dependency edges (for skills with deps in skill.json)
    buildDepEdges(treeData);
  }

  function measureAndPosition(node, x, y, depth) {
    const idx = nodes.length;
    const n = {
      idx,
      name: node.name,
      type: node.type,
      path: node.path,
      claudeMd: node.claudeMd,
      skillJson: node.skillJson,
      x, y, depth,
      children: []
    };
    nodes.push(n);

    if (node.children && node.children.length > 0) {
      let childY = y;
      const childX = x + NODE_W + H_GAP;

      for (const child of node.children) {
        const childIdx = nodes.length;
        n.children.push(childIdx);
        edges.push({ from: idx, to: childIdx });
        const subtreeH = measureAndPosition(child, childX, childY, depth + 1);
        childY += subtreeH + V_GAP;
      }

      // Center parent vertically among children
      const firstChild = nodes[n.children[0]];
      const lastChild = nodes[n.children[n.children.length - 1]];
      n.y = (firstChild.y + lastChild.y) / 2;

      return childY - y - V_GAP;
    }

    return NODE_H;
  }

  function buildDepEdges(node) {
    if (node.skillJson) {
      try {
        const sj = typeof node.skillJson === 'string' ? JSON.parse(node.skillJson) : node.skillJson;
        const deps = sj.dependencies || sj.deps || [];
        for (const dep of deps) {
          const fromNode = nodes.find(n => n.name === node.name && n.type === 'skill');
          const toNode = nodes.find(n => n.name === dep && n.type === 'skill');
          if (fromNode && toNode) {
            depEdges.push({ from: fromNode.idx, to: toNode.idx });
          }
        }
      } catch { /* not valid json */ }
    }
    if (node.children) {
      for (const child of node.children) buildDepEdges(child);
    }
  }

  // --- Render pass ---

  function render() {
    if (!svg) return;
    svg.innerHTML = '';

    // Calculate viewBox
    let maxX = 0, maxY = 0;
    for (const n of nodes) {
      maxX = Math.max(maxX, n.x + NODE_W + PAD_X);
      maxY = Math.max(maxY, n.y + NODE_H + PAD_Y);
    }
    svg.setAttribute('viewBox', `0 0 ${maxX} ${maxY}`);
    svg.style.minWidth = maxX + 'px';
    svg.style.minHeight = maxY + 'px';

    // Draw edges
    const edgeGroup = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    for (const e of edges) {
      edgeGroup.appendChild(makeBezier(nodes[e.from], nodes[e.to], 'tree-edge'));
    }
    svg.appendChild(edgeGroup);

    // Draw dependency edges
    if (showDeps) {
      const depGroup = document.createElementNS('http://www.w3.org/2000/svg', 'g');
      for (const e of depEdges) {
        depGroup.appendChild(makeBezier(nodes[e.from], nodes[e.to], 'dep-edge'));
      }
      svg.appendChild(depGroup);
    }

    // Draw nodes
    for (let i = 0; i < nodes.length; i++) {
      svg.appendChild(makeNode(nodes[i], i === selectedId));
    }
  }

  function makeBezier(from, to, cls) {
    const x1 = from.x + NODE_W;
    const y1 = from.y + NODE_H / 2;
    const x2 = to.x;
    const y2 = to.y + NODE_H / 2;
    const cx = (x1 + x2) / 2;

    const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
    path.setAttribute('d', `M${x1},${y1} C${cx},${y1} ${cx},${y2} ${x2},${y2}`);
    path.setAttribute('class', cls);
    return path;
  }

  function makeNode(n, selected) {
    const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
    g.setAttribute('class', `tree-node node-${n.type}${selected ? ' selected' : ''}`);
    g.setAttribute('transform', `translate(${n.x},${n.y})`);

    const rect = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
    rect.setAttribute('width', NODE_W);
    rect.setAttribute('height', NODE_H);
    g.appendChild(rect);

    const text = document.createElementNS('http://www.w3.org/2000/svg', 'text');
    text.setAttribute('x', 10);
    text.setAttribute('y', NODE_H / 2 + 4);
    // Truncate long names
    const label = n.name.length > 16 ? n.name.slice(0, 15) + '…' : n.name;
    text.textContent = label;
    g.appendChild(text);

    // Type indicator
    const indicator = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
    indicator.setAttribute('cx', NODE_W - 12);
    indicator.setAttribute('cy', NODE_H / 2);
    indicator.setAttribute('r', 3);
    const colors = { root: '#808080', group: '#555', purpose: '#4fc1ff', skill: '#6a9955', intent: '#d4a537' };
    indicator.setAttribute('fill', colors[n.type] || '#555');
    g.appendChild(indicator);

    g.addEventListener('click', () => selectNode(n.idx));

    return g;
  }

  return { init, setTree, toggleDeps, selectNode, selectNext, getSelected, getNodes };
})();
