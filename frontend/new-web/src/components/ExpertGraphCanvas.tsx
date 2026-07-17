import { useCallback, useEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import type { Exponent, ExpertEdge, ExpertGraph, ExpertRouteRule } from '../types';
import type { WorkflowEdgeStatus, WorkflowNodeStatus, WorkflowTaskState } from '../workflowState';
import { normalizeExpertName, workflowEdgeKey } from '../workflowState';
import {
  getExpert,
  getExpertGraph,
  getExpertGraphPositions,
  saveExpertGraphPositions,
  setExpertRoutes,
  updateExpert,
  setExpertTools,
} from '../api/api';
import { ExpertEditModal } from './ExpertEditModal';
import type { CanvasSize, LayoutRect } from '../graphLayout';
import { ADD_EXPERT_SIZE, correctControlIfOutside, defaultAddExpertPosition, findSafeNodePosition } from '../graphLayout';

interface Props {
  theme: 'dark' | 'light';
  workflowState?: WorkflowTaskState;
}

interface NodePosition { x: number; y: number; }

interface MousedownInfo { nodeId: string; clientX: number; clientY: number; time: number; zoom: number; panX: number; panY: number; }

interface WiringState {
  sourceNodeId: string;
  sourceX: number;
  sourceY: number;
  currentScreenX: number;
  currentScreenY: number;
}

interface EdgeEditorState {
  edgeId: string;
  source: string;
  target: string;
  conditionType: string;
  conditionValue: string;
  priority: number;
  isNew: boolean;
  anchorX: number; // screen position for panel
  anchorY: number;
}

// ── Constants ──
const NODE_W = 180; const NODE_H = 60; const EXPANDED_W = 300;
const VNODE_W = 90; const VNODE_H = 36;
const PORT_RADIUS = 7; const PORT_HIT = 14;
const VERTICAL_GAP = 120;
const DRAG_THRESHOLD = 5;
const CREATE_MODE = '<<CREATE>>';

const CONDITION_COLORS: Record<string, string> = {
  tag_exists: '#6393eb',
  tag_value_match: '#22c55e',
  plan_state: '#f59e0b',
  default: '#6b7280',
  on_fail: '#ef4444',
};
const RISK_COLORS: Record<string, string> = { safe: '#22c55e', medium: '#f59e0b', dangerous: '#ef4444' };
const WORKFLOW_COLORS: Record<Exclude<WorkflowNodeStatus, 'idle'>, string> = {
  pending: '#f59e0b', running: '#3b82f6', completed: '#22c55e', failed: '#ef4444',
};

// ── Helpers ──
function getNodeSize(nodeId: string, isExpanded: boolean) {
  if (nodeId.startsWith('_')) return { w: VNODE_W, h: VNODE_H };
  return { w: isExpanded ? EXPANDED_W : NODE_W, h: NODE_H };
}

function screenToGraph(sx: number, sy: number, zoom: number, pan: { x: number; y: number }) {
  return { x: (sx - pan.x) / zoom, y: (sy - pan.y) / zoom };
}
function graphToScreen(gx: number, gy: number, zoom: number, pan: { x: number; y: number }) {
  return { x: gx * zoom + pan.x, y: gy * zoom + pan.y };
}

function edgeLabel(type: string, value: string): string {
  switch (type) {
    case 'tag_exists': return `输出 <${value || '?'}>`;
    case 'tag_value_match': return `匹配 ${value || '?'}`;
    case 'plan_state': return `计划: ${value || '?'}`;
    case 'default': return '兜底路由';
    case 'on_fail': return '失败兜底';
    default: return value || type;
  }
}

// ── Routes ↔ Edges conversion ──
interface ExpertRoutes { rules: ExpertRouteRule[]; on_fail: string; }
type RoutesMap = Record<string, ExpertRoutes>;

function routesMapFromGraph(graph: ExpertGraph): RoutesMap {
  const map: RoutesMap = {};
  for (const n of graph.nodes) {
    map[n.id] = { rules: [], on_fail: n.on_fail || '' };
  }
  // Fill from edges (on_fail edges are stored on the node, not as separate edges)
  for (const e of graph.edges || []) {
    if (e.condition_type === 'on_fail') {
      if (map[e.source]) map[e.source].on_fail = e.target;
      continue;
    }
    if (!map[e.source]) map[e.source] = { rules: [], on_fail: '' };
    map[e.source].rules.push({
      type: e.condition_type,
      value: e.condition_value,
      route_to: e.target,
      priority: e.priority,
    });
  }
  return map;
}

function edgesFromRoutesMap(map: RoutesMap, graph: ExpertGraph): ExpertEdge[] {
  const edges: ExpertEdge[] = [];
  let id = 0;
  for (const [source, er] of Object.entries(map)) {
    if (source.startsWith('_')) continue;
    for (const r of er.rules) {
      edges.push({
        id: `e_${++id}`,
        source,
        target: r.route_to,
        condition_type: r.type,
        condition_value: r.value,
        priority: r.priority,
        label: edgeLabel(r.type, r.value),
      });
    }
    if (er.on_fail) {
      edges.push({
        id: `e_${++id}`,
        source,
        target: er.on_fail,
        condition_type: 'on_fail',
        condition_value: '',
        priority: 0,
        label: '失败兜底',
      });
    }
  }
  // Add synthetic entry edge
  const entryNode = graph.nodes?.find(n => n.is_entry);
  if (entryNode) {
    edges.push({
      id: 'se',
      source: '_user',
      target: entryNode.id,
      condition_type: 'tag_exists',
      condition_value: '',
      priority: 0,
      label: '任务输入',
    });
  }
  return edges;
}

// ── EdgeEditorPanel ──
function EdgeEditorPanel({
  state, theme, onSave, onDelete, onClose,
}: {
  state: EdgeEditorState;
  theme: 'dark' | 'light';
  onSave: (s: EdgeEditorState) => void;
  onDelete: (s: EdgeEditorState) => void;
  onClose: () => void;
}) {
  const [ct, setCt] = useState(state.conditionType);
  const [cv, setCv] = useState(state.conditionValue);
  const [pri, setPri] = useState(state.priority);
  const isOnFail = ct === 'on_fail';
  const isDark = theme === 'dark';

  const panelW = 260;
  const left = Math.min(state.anchorX, window.innerWidth - panelW - 16);
  const top = Math.max(8, Math.min(state.anchorY - 80, window.innerHeight - 280));

  return createPortal(
    <div
      className={`theme-${theme}`}
      style={{
        position: 'fixed', left, top, zIndex: 2100, width: panelW,
        padding: '16px 18px', borderRadius: 14,
        background: isDark ? 'rgba(10,14,23,0.95)' : 'rgba(253,252,251,0.95)',
        border: '1.5px solid var(--glass-border-strong)',
        backdropFilter: 'blur(20px)',
        boxShadow: isDark ? '0 8px 40px rgba(0,0,0,0.5)' : '0 8px 40px rgba(0,0,0,0.15)',
        display: 'flex', flexDirection: 'column', gap: 10,
      }}
      onClick={e => e.stopPropagation()}
    >
      <div className="flex items-center justify-between">
        <span className="text-[11px] font-bold" style={{ letterSpacing: '0.03em' }}>
          {state.isNew ? '新连线' : '编辑连线'}
        </span>
        <button onClick={onClose} style={{
          width: 20, height: 20, borderRadius: '50%', border: '1px solid var(--glass-border)',
          background: 'transparent', color: 'var(--text-secondary)', cursor: 'pointer', fontSize: 10,
          display: 'flex', alignItems: 'center', justifyContent: 'center',
        }}>✕</button>
      </div>

      <div className="text-[10px] text-[var(--text-secondary)]">
        {state.source} → {state.target}
      </div>

      <div className="flex flex-col gap-1">
        <span className="text-[9px] font-semibold uppercase tracking-wider text-[var(--text-secondary)]">条件类型</span>
        <select
          value={ct}
          onChange={e => setCt(e.target.value)}
          className="form-input text-[11px]"
          style={{ padding: '4px 6px', borderRadius: 6 }}
        >
          <option value="tag_exists">tag_exists (检测标签)</option>
          <option value="tag_value_match">tag_value_match (匹配值)</option>
          <option value="plan_state">plan_state (计划状态)</option>
          <option value="default">default (兜底)</option>
          <option value="on_fail">on_fail (失败兜底)</option>
        </select>
      </div>

      {!isOnFail && (
        <div className="flex flex-col gap-1">
          <span className="text-[9px] font-semibold uppercase tracking-wider text-[var(--text-secondary)]">条件值</span>
          <input
            value={cv}
            onChange={e => setCv(e.target.value)}
            placeholder={ct === 'tag_exists' ? 'done / fail / plan' : '值'}
            className="form-input text-[11px]"
            style={{ padding: '4px 6px', borderRadius: 6 }}
          />
        </div>
      )}

      <div className="flex flex-col gap-1">
        <span className="text-[9px] font-semibold uppercase tracking-wider text-[var(--text-secondary)]">优先级</span>
        <input
          type="number" min={0} max={100}
          value={String(pri)}
          onChange={e => setPri(Number(e.target.value) || 0)}
          className="form-input text-[11px]"
          style={{ width: 60, padding: '4px 6px', borderRadius: 6 }}
        />
      </div>

      <div className="flex items-center gap-2" style={{ marginTop: 4 }}>
        <button
          onClick={() => {
            onSave({ ...state, conditionType: ct, conditionValue: cv, priority: pri });
            onClose();
          }}
          style={{
            flex: 1, height: 28, borderRadius: 7, border: 'none',
            background: 'var(--accent)', color: '#fff', cursor: 'pointer',
            fontSize: 11, fontWeight: 600,
          }}
        >{state.isNew ? '创建' : '保存'}</button>
        {!state.isNew && (
          <button
            onClick={() => { onDelete(state); onClose(); }}
            style={{
              height: 28, padding: '0 10px', borderRadius: 7,
              border: '1px solid rgba(239,68,68,0.4)', background: 'transparent',
              color: '#ef4444', cursor: 'pointer', fontSize: 11,
            }}
          >删除</button>
        )}
      </div>
    </div>,
    document.body,
  );
}

// ══════════════════════════════════════════════
// Main Component
// ══════════════════════════════════════════════

export function ExpertGraphCanvas({ theme, workflowState }: Props) {
  const [graph, setGraph] = useState<ExpertGraph | null>(null);
  const [loading, setLoading] = useState(true);
  const [positions, setPositions] = useState<Record<string, NodePosition>>({});
  const interactionRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLDivElement>(null);
  const canvasSize = useElementSize(canvasRef, !loading && Boolean(graph));

  // ── Zoom / Pan ──
  const [cumPan, setCumPan] = useState({ x: 0, y: 0 });
  const panning = useRef(false);
  const panStart = useRef({ x: 0, y: 0 });
  const panBase = useRef({ x: 0, y: 0 });
  const [zoomTarget, setZoomTarget] = useState(0.85);
  const [zoomRendered, setZoomRendered] = useState(0.85);

  // ── Refs ──
  const positionRef = useRef(positions); positionRef.current = positions;
  const zoomRenderedRef = useRef(zoomRendered); zoomRenderedRef.current = zoomRendered;
  const cumPanRef = useRef(cumPan); cumPanRef.current = cumPan;
  const zoomTargetRef = useRef(zoomTarget); zoomTargetRef.current = zoomTarget;

  // ── Expand / Pin ──
  const [editExpertName, setEditExpertName] = useState<string | null>(null);
  const [graphVersion, setGraphVersion] = useState(0);
  const [expandedNodeIds, setExpandedNodeIds] = useState<Set<string>>(new Set());
  const [pinnedNodeIds, setPinnedNodeIds] = useState<Set<string>>(new Set());
  const [expandedDataMap, setExpandedDataMap] = useState<Record<string, { prompt: string; description: string; tools: string[] }>>({});
  const hoverTimerMap = useRef<Record<string, number>>({});
  const expandedDataRef = useRef(expandedDataMap); expandedDataRef.current = expandedDataMap;

  // ── Node Drag ──
  const mousedownRef = useRef<MousedownInfo | null>(null);
  const dragOffset = useRef({ x: 0, y: 0 });
  const isDraggingNode = useRef(false);
  const suppressLeaveUntil = useRef<Record<string, number>>({});
  const dragOverCount = useRef<Record<string, number>>({});
  const [draggingTool, setDraggingTool] = useState(false);

  // ── Wiring ──
  const wiringRef = useRef<WiringState | null>(null);
  const [wiring, setWiring] = useState<WiringState | null>(null);
  const [hoveredPort, setHoveredPort] = useState<string | null>(null); // nodeId of hovered input port
  const [edgeEditor, setEdgeEditor] = useState<EdgeEditorState | null>(null);

  // ── Optimistic routes cache ──
  const routesMapRef = useRef<RoutesMap>({});
  const [routesVersion, setRoutesVersion] = useState(0);

  const reloadGraph = useCallback(() => setGraphVersion(v => v + 1), []);

  // Smooth zoom
  useEffect(() => {
    let raf: number;
    const step = () => {
      const from = zoomRenderedRef.current, to = zoomTarget;
      if (Math.abs(to - from) < 0.001) { setZoomRendered(to); return; }
      setZoomRendered(from + (to - from) * 0.18);
      raf = requestAnimationFrame(step);
    };
    raf = requestAnimationFrame(step);
    return () => cancelAnimationFrame(raf);
  }, [zoomTarget]);

  // Load graph
  useEffect(() => {
    (async () => {
      try {
        const [g, pos] = await Promise.all([getExpertGraph(), getExpertGraphPositions()]);
        setGraph(g);
        if (pos) setPositions(pos);
        routesMapRef.current = routesMapFromGraph(g);
        setRoutesVersion(v => v + 1);
      } catch { }
      setLoading(false);
    })();
  }, []);
  useEffect(() => {
    if (graphVersion === 0) return;
    (async () => {
      try {
        const [g, pos] = await Promise.all([getExpertGraph(), getExpertGraphPositions()]);
        setGraph(g);
        if (pos) setPositions(pos);
        routesMapRef.current = routesMapFromGraph(g);
        setRoutesVersion(v => v + 1);
      } catch { }
    })();
  }, [graphVersion]);

  const nodePositions = useMemoNodePositions(graph, positions, canvasSize);
  positionRef.current = { ...nodePositions, ...positions };
  // Compute display edges from routes cache
  const displayEdges = useMemoEdges(routesMapRef.current, graph, routesVersion);

  // ── Global tool drag ──
  useEffect(() => {
    const onOver = (e: DragEvent) => e.preventDefault();
    const onStart = (e: DragEvent) => { if (e.dataTransfer?.getData('application/tool-name')) setDraggingTool(true); };
    const onEnd = () => { setDraggingTool(false); dragOverCount.current = {}; };
    window.addEventListener('dragover', onOver); window.addEventListener('dragstart', onStart); window.addEventListener('dragend', onEnd);
    return () => { window.removeEventListener('dragover', onOver); window.removeEventListener('dragstart', onStart); window.removeEventListener('dragend', onEnd); };
  }, []);

  // ── Zoom handler (wheel on any element in the canvas area) ──
  useEffect(() => {
    const onWheel = (e: WheelEvent) => {
      // Allow wheel inside form elements (textarea, input, select) to work normally
      if ((e.target as HTMLElement)?.closest('textarea, input, select')) return;
      // Ignore inside the edge editor panel (it's portaled to body, not in the container)
      if ((e.target as HTMLElement)?.closest('[data-edge-editor]')) return;
      e.preventDefault();

      const oldZoom = zoomTargetRef.current;
      const delta = -e.deltaY * 0.0008;
      const newZoom = Math.min(2, Math.max(0.3, oldZoom + delta * oldZoom));
      const cp = cumPanRef.current;

      // Zoom toward mouse position: world point under cursor stays fixed
      const mx = e.clientX, my = e.clientY;
      const worldX = (mx - cp.x) / oldZoom;
      const worldY = (my - cp.y) / oldZoom;
      setCumPan({
        x: mx - worldX * newZoom,
        y: my - worldY * newZoom,
      });
      setZoomTarget(newZoom);
    };

    const container = interactionRef.current;
    if (!container) return;

    // Capture phase so we get wheel before nodes/textarea do
    container.addEventListener('wheel', onWheel, { passive: false, capture: true });
    return () => container.removeEventListener('wheel', onWheel, { capture: true });
  }, []);

  // ── Global mousemove/mouseup ──
  useEffect(() => {
    const onMove = (e: MouseEvent) => {
      // Wiring
      const wr = wiringRef.current;
      if (wr) {
        wr.currentScreenX = e.clientX;
        wr.currentScreenY = e.clientY;
        setWiring({ ...wr });

        // Check hovered input port
        const z = zoomRenderedRef.current;
        const cp = cumPanRef.current;
        const g = screenToGraph(e.clientX, e.clientY, z, cp);
        const ports = getAllPorts(graph, nodePositions, expandedNodeIds);
        const hit = findInputPort(g.x, g.y, ports, wr.sourceNodeId);
        setHoveredPort(hit?.nodeId || null);
        return;
      }

      // Node drag
      const md = mousedownRef.current;
      if (!md) return;
      const dx = e.clientX - md.clientX, dy = e.clientY - md.clientY;
      const dist = Math.sqrt(dx * dx + dy * dy);
      if (!isDraggingNode.current && dist > DRAG_THRESHOLD) {
        isDraggingNode.current = true;
        // 使用 mousedown 时记录的 zoom/pan 计算初始偏移，防止拖拽瞬移
        dragOffset.current = {
          x: (md.clientX - md.panX) / md.zoom - positionRef.current[md.nodeId]?.x || 0,
          y: (md.clientY - md.panY) / md.zoom - positionRef.current[md.nodeId]?.y || 0,
        };
      }
      if (isDraggingNode.current) {
        const z = zoomRenderedRef.current, cp = cumPanRef.current;
        setPositions(prev => ({
          ...prev,
          [md.nodeId]: { x: (e.clientX - cp.x) / z - dragOffset.current.x, y: (e.clientY - cp.y) / z - dragOffset.current.y },
        }));
      }
    };

    const onUp = (e: MouseEvent) => {
      // Wiring complete
      const wr = wiringRef.current;
      if (wr) {
        wiringRef.current = null;
        setWiring(null);
        const z = zoomRenderedRef.current;
        const cp = cumPanRef.current;
        const g = screenToGraph(e.clientX, e.clientY, z, cp);
        const ports = getAllPorts(graph, nodePositions, expandedNodeIds);
        const hit = findInputPort(g.x, g.y, ports, wr.sourceNodeId);
        if (hit) {
          handleCreateEdge(wr.sourceNodeId, hit.nodeId, e.clientX, e.clientY, e.shiftKey);
        }
        setHoveredPort(null);
        return;
      }

      // Node drag up
      const md = mousedownRef.current;
      if (!md) return;
      if (!isDraggingNode.current) {
        if (!md.nodeId.startsWith('_')) {
          const target = e.target as HTMLElement;
          if (expandedNodeIds.has(md.nodeId)) {
            if (!target.closest('button, textarea, input, select, [data-no-close]')) closeExpanded(md.nodeId);
          } else {
            openExpanded(md.nodeId, 'click');
          }
        }
      } else {
        saveExpertGraphPositions(positionRef.current).catch(() => { });
      }
      mousedownRef.current = null;
      isDraggingNode.current = false;
    };

    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape' && wiringRef.current) {
        wiringRef.current = null;
        setWiring(null);
        setHoveredPort(null);
      }
    };

    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup', onUp);
    window.addEventListener('keydown', onKey);
    return () => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup', onUp);
      window.removeEventListener('keydown', onKey);
    };
  }, [expandedNodeIds, graph, nodePositions]);

  // ── Pan ──
  const handleBgMouseDown = useCallback((e: React.MouseEvent) => {
    if (wiringRef.current) return;
    if (e.button !== 0 || (e.target as HTMLElement).closest('[data-node], [data-port]')) return;
    panning.current = true; panStart.current = { x: e.clientX, y: e.clientY }; panBase.current = { ...cumPanRef.current };
  }, []);
  useEffect(() => {
    const onMove = (e: MouseEvent) => { if (!panning.current) return; setCumPan({ x: panBase.current.x + e.clientX - panStart.current.x, y: panBase.current.y + e.clientY - panStart.current.y }); };
    const onUp = () => { panning.current = false; };
    window.addEventListener('mousemove', onMove); window.addEventListener('mouseup', onUp);
    return () => { window.removeEventListener('mousemove', onMove); window.removeEventListener('mouseup', onUp); };
  }, []);

  // ── Node mouse down ──
  const handleNodeMouseDown = useCallback((nodeId: string, e: React.MouseEvent) => {
    if (wiringRef.current) return;
    e.stopPropagation(); if (e.button !== 0) return;
    const z = zoomRenderedRef.current;
    const cp = cumPanRef.current;
    mousedownRef.current = { nodeId, clientX: e.clientX, clientY: e.clientY, time: Date.now(), zoom: z, panX: cp.x, panY: cp.y };
    isDraggingNode.current = false;
  }, []);

  // ── Port mouse down (start wiring) ──
  const handlePortMouseDown = useCallback((nodeId: string, portType: 'input' | 'output', e: React.MouseEvent) => {
    if (portType !== 'output') return;
    e.stopPropagation(); e.preventDefault();
    if (e.button !== 0) return;
    const z = zoomRenderedRef.current;
    const cp = cumPanRef.current;
    const sizes = getNodeSize(nodeId, expandedNodeIds.has(nodeId));
    const pos = nodePositions[nodeId] || { x: 0, y: 0 };
    const ox = pos.x + sizes.w;
    const oy = pos.y + sizes.h / 2;
    const screen = graphToScreen(ox, oy, z, cp);
    const ws: WiringState = {
      sourceNodeId: nodeId,
      sourceX: ox,
      sourceY: oy,
      currentScreenX: screen.x,
      currentScreenY: screen.y,
    };
    wiringRef.current = ws;
    setWiring(ws);
  }, [nodePositions, expandedNodeIds]);

  // ── Create edge ──
  const handleCreateEdge = useCallback(async (sourceId: string, targetId: string, screenX: number, screenY: number, isShiftKey: boolean) => {
    // Special: _user → node means set entry expert
    if (sourceId === '_user') {
      const prevEntry = graph?.nodes?.find(n => n.is_entry);
      if (prevEntry && prevEntry.id !== targetId) {
        // Update local graph optimistically
        if (graph) {
          const newNodes = graph.nodes.map(n => ({
            ...n,
            is_entry: n.id === targetId ? true : (n.id === prevEntry.id ? false : n.is_entry),
          }));
          setGraph({ ...graph, nodes: newNodes });
        }
        // API calls
        try {
          await Promise.all([
            updateExpert(targetId, { is_entry: true } as Partial<Exponent>),
            updateExpert(prevEntry.id, { is_entry: false } as Partial<Exponent>),
          ]);
          reloadGraph();
        } catch { reloadGraph(); }
      }
      return;
    }

    // Normal edge creation
    const rm = routesMapRef.current;
    const er = rm[sourceId] || { rules: [], on_fail: '' };

    // Check for duplicate
    const exists = er.rules.some(r => r.route_to === targetId);
    if (exists) {
      // Open editor for existing edge
      const existingEdge = displayEdges.find(e => e.source === sourceId && e.target === targetId);
      if (existingEdge) {
        setEdgeEditor({
          edgeId: existingEdge.id,
          source: sourceId,
          target: targetId,
          conditionType: existingEdge.condition_type,
          conditionValue: existingEdge.condition_value,
          priority: existingEdge.priority,
          isNew: false,
          anchorX: screenX,
          anchorY: screenY,
        });
      }
      return;
    }

    // Determine next priority
    const maxPri = er.rules.reduce((m, r) => Math.max(m, r.priority), 0);

    const newRule: ExpertRouteRule = isShiftKey
      ? { type: 'on_fail', value: '', route_to: targetId, priority: 0 }
      : { type: 'tag_exists', value: '', route_to: targetId, priority: maxPri + 5 };

    if (newRule.type === 'on_fail') {
      er.on_fail = targetId;
    } else {
      er.rules.push(newRule);
    }
    rm[sourceId] = er;
    routesMapRef.current = { ...rm };
    setRoutesVersion(v => v + 1);

    // Open editor
    const newEdgeId = `new_${sourceId}_${targetId}_${Date.now()}`;
    setEdgeEditor({
      edgeId: newEdgeId,
      source: sourceId,
      target: targetId,
      conditionType: newRule.type,
      conditionValue: newRule.value,
      priority: newRule.priority,
      isNew: true,
      anchorX: screenX,
      anchorY: screenY,
    });

    // Background save
    try {
      if (newRule.type === 'on_fail') {
        await updateExpert(sourceId, { on_fail: targetId } as Partial<Exponent>);
      } else {
        await setExpertRoutes(sourceId, er.rules);
      }
      reloadGraph();
    } catch {
      // Revert on failure
      const restored = routesMapFromGraph(graph!);
      routesMapRef.current = restored;
      setRoutesVersion(v => v + 1);
    }
  }, [graph, displayEdges, reloadGraph]);

  // ── Edge editor actions ──
  const handleEdgeSave = useCallback(async (s: EdgeEditorState) => {
    const rm = routesMapRef.current;
    const er = rm[s.source] || { rules: [], on_fail: '' };

    if (s.conditionType === 'on_fail') {
      er.on_fail = s.target;
      er.rules = er.rules.filter(r => r.route_to !== s.target);
    } else {
      // Remove old on_fail if target matches
      if (er.on_fail === s.target) er.on_fail = '';
      const ruleIdx = er.rules.findIndex(r => r.route_to === s.target);
      const newRule: ExpertRouteRule = {
        type: s.conditionType,
        value: s.conditionValue,
        route_to: s.target,
        priority: s.priority,
      };
      if (ruleIdx >= 0) {
        er.rules[ruleIdx] = newRule;
      } else {
        // New rule (it was previously on_fail, now normal)
        er.on_fail = '';
        er.rules.push(newRule);
      }
    }

    rm[s.source] = er;
    routesMapRef.current = { ...rm };
    setRoutesVersion(v => v + 1);

    try {
      if (s.conditionType === 'on_fail') {
        await updateExpert(s.source, { on_fail: s.target } as Partial<Exponent>);
      } else {
        await setExpertRoutes(s.source, er.rules);
      }
      reloadGraph();
    } catch {
      reloadGraph();
    }
  }, [reloadGraph]);

  const handleEdgeDelete = useCallback(async (s: EdgeEditorState) => {
    const rm = routesMapRef.current;
    const er = rm[s.source];
    if (!er) return;

    if (s.conditionType === 'on_fail') {
      er.on_fail = '';
    } else {
      er.rules = er.rules.filter(r => r.route_to !== s.target);
    }
    rm[s.source] = { ...er };
    routesMapRef.current = { ...rm };
    setRoutesVersion(v => v + 1);

    try {
      if (s.conditionType === 'on_fail') {
        await updateExpert(s.source, { on_fail: '' } as Partial<Exponent>);
      } else {
        await setExpertRoutes(s.source, er.rules);
      }
      reloadGraph();
    } catch {
      reloadGraph();
    }
  }, [reloadGraph]);

  const handleEdgeClick = useCallback((edge: ExpertEdge, e: React.MouseEvent) => {
    if (wiringRef.current) return;
    e.stopPropagation();
    setEdgeEditor({
      edgeId: edge.id,
      source: edge.source,
      target: edge.target,
      conditionType: edge.condition_type,
      conditionValue: edge.condition_value,
      priority: edge.priority,
      isNew: false,
      anchorX: e.clientX,
      anchorY: e.clientY,
    });
  }, []);

  // ── Expand / collapse ──
  const openExpanded = useCallback(async (nodeId: string, mode: 'click' | 'hover' | 'drag' = 'hover') => {
    setExpandedNodeIds(prev => new Set(prev).add(nodeId));
    if (mode === 'click') setPinnedNodeIds(prev => new Set(prev).add(nodeId));
    suppressLeaveUntil.current[nodeId] = Date.now() + 200;
    try { const e = await getExpert(nodeId); setExpandedDataMap(prev => ({ ...prev, [nodeId]: { prompt: e.context_template || '', description: e.description || '', tools: e.visible_tools || [] } })); } catch { }
  }, []);
  const closeExpanded = useCallback((nodeId: string) => {
    setExpandedNodeIds(prev => { const n = new Set(prev); n.delete(nodeId); return n; });
    setPinnedNodeIds(prev => { const n = new Set(prev); n.delete(nodeId); return n; });
  }, []);
  const isPinned = useCallback((nodeId: string) => pinnedNodeIds.has(nodeId), [pinnedNodeIds]);
  const handleNodeHoverEnter = useCallback((nodeId: string) => {
    if (nodeId.startsWith('_') || mousedownRef.current || wiringRef.current || expandedNodeIds.has(nodeId)) return;
    clearTimeout(hoverTimerMap.current[nodeId]);
    hoverTimerMap.current[nodeId] = window.setTimeout(() => { if (!mousedownRef.current && !wiringRef.current && !expandedNodeIds.has(nodeId)) openExpanded(nodeId, 'hover'); }, 300);
  }, [expandedNodeIds, openExpanded]);
  const handleNodeHoverLeave = useCallback((nodeId: string, e?: React.MouseEvent) => {
    clearTimeout(hoverTimerMap.current[nodeId]);
    if (Date.now() < (suppressLeaveUntil.current[nodeId] || 0)) return;
    // Don't close if mouse moved to a child element within the same node
    if (e && e.relatedTarget instanceof Node && (e.currentTarget as Node).contains(e.relatedTarget)) return;
    if (expandedNodeIds.has(nodeId) && !pinnedNodeIds.has(nodeId)) closeExpanded(nodeId);
  }, [expandedNodeIds, pinnedNodeIds, closeExpanded]);
  const handleNodeDoubleClick = useCallback((nodeId: string) => { if (nodeId.startsWith('_')) return; closeExpanded(nodeId); setEditExpertName(nodeId); }, [closeExpanded]);

  // ── Tool drag onto nodes ──
  const handleNodeDragOver = useCallback((nodeId: string, e: React.DragEvent) => {
    e.preventDefault(); e.stopPropagation(); e.dataTransfer.dropEffect = 'copy';
    dragOverCount.current[nodeId] = (dragOverCount.current[nodeId] || 0) + 1;
    if (!expandedNodeIds.has(nodeId)) openExpanded(nodeId, 'drag');
  }, [expandedNodeIds, openExpanded]);
  const handleNodeDragLeave = useCallback((nodeId: string) => {
    dragOverCount.current[nodeId] = Math.max(0, (dragOverCount.current[nodeId] || 0) - 1);
    if (dragOverCount.current[nodeId] <= 0 && !pinnedNodeIds.has(nodeId)) closeExpanded(nodeId);
  }, [pinnedNodeIds, closeExpanded]);
  const handleNodeDrop = useCallback((nodeId: string, e: React.DragEvent) => {
    e.preventDefault(); e.stopPropagation(); dragOverCount.current[nodeId] = 0;
    const tn = e.dataTransfer.getData('application/tool-name');
    if (tn) { const d = expandedDataRef.current[nodeId]; if (d && !d.tools.includes(tn)) saveTools(nodeId, [...d.tools, tn]); }
  }, []);
  const saveTools = useCallback(async (nodeId: string, newTools: string[]) => {
    setExpandedDataMap(prev => ({ ...prev, [nodeId]: { ...prev[nodeId], tools: newTools } }));
    try { await setExpertTools(nodeId, newTools); reloadGraph(); } catch { try { const e = await getExpert(nodeId); setExpandedDataMap(prev => ({ ...prev, [nodeId]: { prompt: e.context_template || '', description: e.description || '', tools: e.visible_tools || [] } })); } catch { } }
  }, [reloadGraph]);
  const removeTool = useCallback((nodeId: string, tn: string) => { const d = expandedDataRef.current[nodeId]; if (d) saveTools(nodeId, d.tools.filter(t => t !== tn)); }, [saveTools]);
  // ── Edge paths ──
  const edgePaths = useMemoEdgePaths(graph, nodePositions, displayEdges, workflowState);

  if (loading || !graph) return null;
  const tr = `scale(${zoomRendered}) translate(${cumPan.x}px, ${cumPan.y}px)`;
  const z = zoomRendered;
  const cp = cumPan;

  // Compute all port positions for rendering
  const allPorts = computeRenderPorts(graph, nodePositions, expandedNodeIds);

  return (
    <div ref={canvasRef} className={`expert-canvas theme-${theme}`} style={{ position: 'absolute', inset: 0, zIndex: 0, pointerEvents: 'none', overflow: 'hidden' }}>
      <div ref={interactionRef} onMouseDown={handleBgMouseDown} style={{ position: 'absolute', inset: 0, zIndex: 0, pointerEvents: 'auto', cursor: panning.current ? 'grabbing' : 'default' }} />

      {/* SVG layer: edges + temporary wire */}
      <svg style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', pointerEvents: 'none' }}>
        <g style={{ transform: tr, transformOrigin: '0 0' }}>
          {edgePaths.map(ep => (
            <g key={ep.id} data-workflow-edge-status={ep.status} data-edge-source={ep.edge.source} data-edge-target={ep.edge.target}>
              {/* Invisible wide hit area */}
              <path
                d={ep.d} fill="none" stroke="transparent" strokeWidth={14}
                style={{ cursor: 'pointer', pointerEvents: 'auto' }}
                onClick={e => handleEdgeClick(ep.edge, e)}
              />
              <path
                d={ep.d} fill="none"
                stroke={ep.color}
                strokeWidth={ep.isActive ? 3 : 1.8}
                strokeOpacity={ep.isActive ? 0.95 : 0.5}
                strokeDasharray={ep.status === 'pending' ? '8 5' : ep.status === 'failed' ? '3 3' : ep.conditionType === 'default' ? '6 3' : undefined}
                style={{ pointerEvents: 'none', transition: 'stroke-opacity 0.15s, stroke-width 0.15s' }}
              >
                {(ep.status === 'pending' || ep.status === 'running') && <animate attributeName="stroke-dashoffset" from="26" to="0" dur="0.8s" repeatCount="indefinite" />}
              </path>
              {ep.label && (
                <text
                  x={ep.labelX} y={ep.labelY - 6}
                  textAnchor="middle"
                  fill={theme === 'dark' ? 'rgba(255,255,255,0.55)' : 'rgba(0,0,0,0.45)'}
                  fontSize={10} fontFamily="monospace"
                  style={{ pointerEvents: 'none', userSelect: 'none' }}
                >{ep.label}</text>
              )}
            </g>
          ))}

          {/* Temporary wiring line */}
          {wiring && (
            <line
              x1={wiring.sourceX} y1={wiring.sourceY}
              x2={screenToGraph(wiring.currentScreenX, wiring.currentScreenY, z, cp).x}
              y2={screenToGraph(wiring.currentScreenX, wiring.currentScreenY, z, cp).y}
              stroke="var(--accent-light)"
              strokeWidth={2}
              strokeDasharray="8 4"
              strokeOpacity={0.8}
              style={{ pointerEvents: 'none' }}
            >
              <animate attributeName="stroke-dashoffset" from="24" to="0" dur="0.6s" repeatCount="indefinite" />
            </line>
          )}
        </g>
      </svg>

      {/* Node + Port layer */}
      <div style={{ position: 'absolute', inset: 0, transform: tr, transformOrigin: '0 0', pointerEvents: 'none' }}>
        {/* Port circles (render behind nodes but with pointer events) */}
        {allPorts.map(p => {
          const isHighlight = wiring && hoveredPort === p.nodeId;
          const isInput = p.type === 'input';
          return (
            <div
              key={`${p.nodeId}-${p.type}`}
              data-port={p.nodeId}
              data-port-type={p.type}
              onMouseDown={e => handlePortMouseDown(p.nodeId, p.type, e)}
              style={{
                position: 'absolute',
                left: p.x - PORT_RADIUS,
                top: p.y - PORT_RADIUS,
                width: PORT_RADIUS * 2,
                height: PORT_RADIUS * 2,
                borderRadius: '50%',
                background: isHighlight
                  ? 'var(--accent-light)'
                  : isInput
                    ? (theme === 'dark' ? 'rgba(255,255,255,0.2)' : 'rgba(0,0,0,0.12)')
                    : 'var(--accent)',
                border: `2px solid ${
                  isHighlight ? 'var(--accent-light)' :
                  isInput ? (theme === 'dark' ? 'rgba(255,255,255,0.4)' : 'rgba(0,0,0,0.25)') :
                  'var(--accent)'
                }`,
                cursor: p.type === 'output' ? 'crosshair' : (wiring ? 'crosshair' : 'default'),
                pointerEvents: 'auto',
                zIndex: 5,
                boxShadow: isHighlight ? '0 0 12px var(--accent-light)' : 'none',
                transition: 'background 0.15s, border-color 0.15s, box-shadow 0.15s, transform 0.15s',
                transform: isHighlight ? 'scale(1.4)' : 'scale(1)',
              }}
            />
          );
        })}

        {/* Virtual nodes */}
        {graph.virtual_nodes?.map(vn => {
          const pos = nodePositions[vn.id] || { x: 0, y: 0 };
          const isEntry = vn.type === 'entry';
          return (
            <div key={vn.id} data-node={vn.id}
              onMouseDown={e => handleNodeMouseDown(vn.id, e)}
              style={{
                position: 'absolute', left: pos.x, top: pos.y,
                width: VNODE_W, height: VNODE_H,
                borderRadius: 10, display: 'flex', alignItems: 'center', justifyContent: 'center',
                fontSize: 12, fontWeight: 600, letterSpacing: '0.04em',
                cursor: 'grab', pointerEvents: 'auto', userSelect: 'none',
                background: isEntry
                  ? (theme === 'dark' ? 'rgba(34,197,94,0.12)' : 'rgba(21,128,61,0.14)')
                  : (theme === 'dark' ? 'rgba(239,68,68,0.12)' : 'rgba(185,28,28,0.14)'),
                border: `1.5px solid ${
                  isEntry
                    ? (theme === 'dark' ? 'rgba(34,197,94,0.5)' : 'rgba(21,128,61,0.55)')
                    : (theme === 'dark' ? 'rgba(239,68,68,0.5)' : 'rgba(185,28,28,0.55)')
                }`,
                color: isEntry
                  ? (theme === 'dark' ? 'rgba(34,197,94,0.9)' : 'rgba(21,128,61,0.92)')
                  : (theme === 'dark' ? 'rgba(239,68,68,0.9)' : 'rgba(185,28,28,0.92)'),
                backdropFilter: 'blur(12px)',
              }}
            >{vn.label}</div>
          );
        })}

        {/* Expert nodes */}
        {graph.nodes?.map(node => {
          const pos = nodePositions[node.id] || { x: 0, y: 0 };
          const isExpanded = expandedNodeIds.has(node.id);
          const ed = expandedDataMap[node.id];
          const dragHL = draggingTool;
          const isCollapsed = !isExpanded;
          const runtimeStatus = workflowState?.nodeStates[normalizeExpertName(node.id)] || 'idle';
          const runtimeColor = runtimeStatus === 'idle' ? null : WORKFLOW_COLORS[runtimeStatus];
          const runtimeBackground = runtimeStatus === 'pending' ? 'rgba(245,158,11,0.12)'
            : runtimeStatus === 'running' ? 'rgba(59,130,246,0.14)'
              : runtimeStatus === 'completed' ? 'rgba(34,197,94,0.12)'
                : runtimeStatus === 'failed' ? 'rgba(239,68,68,0.13)' : null;
          const runtimeShadow = runtimeColor ? `0 0 28px ${runtimeColor}55` : null;
          return (
            <div key={node.id} data-node={node.id} data-workflow-status={runtimeStatus} draggable={false}
              className={isExpanded ? 'glass-panel' : undefined}
              onDoubleClick={() => handleNodeDoubleClick(node.id)}
              onMouseDown={e => handleNodeMouseDown(node.id, e)}
              onMouseEnter={e => {
                handleNodeHoverEnter(node.id);
                if (!dragHL && !isExpanded) {
                  e.currentTarget.style.boxShadow = runtimeShadow || (theme === 'dark' ? '0 4px 32px rgba(99,147,235,0.25)' : '0 4px 32px rgba(99,147,235,0.12)');
                }
              }}
              onMouseLeave={e => {
                handleNodeHoverLeave(node.id);
                e.currentTarget.style.boxShadow = dragHL && isCollapsed
                  ? (theme === 'dark' ? '0 0 24px rgba(99,147,235,0.35)' : '0 0 24px rgba(99,147,235,0.18)')
                  : runtimeShadow || (theme === 'dark' ? '0 4px 24px rgba(0,0,0,0.3)' : '0 4px 24px rgba(0,0,0,0.08)');
              }}
              onDragOver={e => handleNodeDragOver(node.id, e)}
              onDragLeave={e => { if (!e.currentTarget.contains(e.relatedTarget as Node)) handleNodeDragLeave(node.id); }}
              onDrop={e => handleNodeDrop(node.id, e)}
              style={{
                position: 'absolute', left: pos.x, top: pos.y,
                width: isExpanded ? EXPANDED_W : NODE_W,
                ...(!isExpanded ? { height: NODE_H } : { minHeight: 280, maxHeight: 480 }),
                borderRadius: isExpanded ? 16 : 12,
                display: 'flex', flexDirection: 'column', overflow: isCollapsed ? 'hidden' : 'auto',
                cursor: isDraggingNode.current ? 'grabbing' : 'pointer',
                pointerEvents: 'auto', userSelect: 'none',
                background: isExpanded
                  ? (theme === 'dark' ? 'rgba(10,14,23,0.92)' : 'rgba(253,252,251,0.92)')
                  : runtimeBackground || (theme === 'dark' ? 'rgba(10,14,23,0.75)' : 'rgba(253,252,251,0.75)'),
                border: `2px solid ${isExpanded || dragHL ? 'var(--accent-light)' : runtimeColor || 'var(--glass-border-strong)'}`,
                color: 'var(--text-primary)', backdropFilter: isExpanded ? 'blur(24px)' : 'blur(20px)',
                boxShadow: isExpanded
                  ? (theme === 'dark' ? '0 8px 48px rgba(99,147,235,0.35)' : '0 8px 48px rgba(99,147,235,0.2)')
                  : dragHL
                    ? (theme === 'dark' ? '0 0 24px rgba(99,147,235,0.35)' : '0 0 24px rgba(99,147,235,0.18)')
                    : runtimeShadow || (theme === 'dark' ? '0 4px 24px rgba(0,0,0,0.3)' : '0 4px 24px rgba(0,0,0,0.08)'),
                transition: 'width 0.12s cubic-bezier(0.4, 0, 0.2, 1), border-radius 0.12s cubic-bezier(0.4, 0, 0.2, 1), box-shadow 0.15s cubic-bezier(0.4, 0, 0.2, 1), border-color 0.15s cubic-bezier(0.4, 0, 0.2, 1), min-height 0.12s cubic-bezier(0.4, 0, 0.2, 1)',
                zIndex: isExpanded ? 10 : 0,
                padding: isExpanded ? '16px 18px' : 0,
                gap: isExpanded ? 10 : 0,
                alignItems: isCollapsed ? 'center' : undefined,
                justifyContent: isCollapsed ? 'center' : undefined,
              }}
            >
              {isCollapsed ? (
                <>
                  {runtimeColor && <span title={runtimeStatus} style={{ position: 'absolute', top: 9, right: 9, width: 8, height: 8, borderRadius: '50%', background: runtimeColor, boxShadow: runtimeStatus === 'running' ? `0 0 10px ${runtimeColor}` : 'none' }} />}
                  <span style={{ fontSize: 13, fontWeight: 600, letterSpacing: '0.03em' }}>{node.label}</span>
                </>
              ) : ed ? (
                <>
                  <div className="flex items-center justify-between shrink-0" data-no-close>
                    <span className="text-sm font-bold" style={{ letterSpacing: '0.04em' }}>{node.label}{isPinned(node.id) ? ' 📌' : ''}</span>
                    <div className="flex items-center gap-2">
                      <button onClick={() => { closeExpanded(node.id); setEditExpertName(node.id); }} className="text-[10px] font-medium underline underline-offset-2 cursor-pointer" style={{ color: 'var(--accent-light)', background: 'none', border: 'none' }}>完整配置 ↗</button>
                      <button onClick={ev => { ev.stopPropagation(); closeExpanded(node.id); }} style={{ width: 22, height: 22, borderRadius: '50%', border: '1px solid var(--glass-border-strong)', background: 'var(--surface)', color: 'var(--text-secondary)', cursor: 'pointer', fontSize: 11, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>✕</button>
                    </div>
                  </div>
                  <div className="flex flex-col gap-1.5 shrink-0" data-no-close>
                    <span className="text-[9px] font-semibold uppercase tracking-wider text-[var(--text-secondary)]">角色描述</span>
                    <textarea value={ed.description || ed.prompt} readOnly rows={3} className="form-input" style={{ resize: 'none', fontSize: 10, lineHeight: 1.55, fontFamily: 'monospace', padding: '6px 8px', borderRadius: 8, background: 'var(--bg)', color: 'var(--text-primary)', border: '1px solid var(--glass-border)', cursor: 'default' }} placeholder="只读预览 · 点击「完整配置 ↗」编辑" />
                  </div>
                  <div className="flex flex-col gap-2 flex-1 min-h-0" data-no-close>
                    <div className="flex items-center justify-between shrink-0"><span className="text-[9px] font-semibold uppercase tracking-wider text-[var(--text-secondary)]">可见工具</span><span className="text-[9px] text-[var(--text-secondary)]">{ed.tools.length}</span></div>
                    <div className="flex-1 overflow-y-auto flex flex-col gap-1.5" style={{ maxHeight: 160, paddingRight: 2 }}>
                      {ed.tools.map(tn => (
                        <div key={tn} className="flex items-center gap-1.5 rounded shrink-0" style={{ height: 28, padding: '0 8px', background: 'color-mix(in srgb, var(--accent) 8%, transparent)', border: '1px solid var(--glass-border)' }}>
                          <span style={{ width: 6, height: 6, borderRadius: '50%', background: RISK_COLORS.medium, flexShrink: 0 }} />
                          <span className="text-[10px] truncate flex-1">{tn}</span>
                          <button onClick={() => removeTool(node.id, tn)} style={{ width: 18, height: 18, borderRadius: '50%', flexShrink: 0, border: 'none', background: 'transparent', color: 'var(--text-secondary)', cursor: 'pointer', fontSize: 10, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>✕</button>
                        </div>
                      ))}
                      <div style={{ height: 36, borderRadius: 8, display: 'flex', alignItems: 'center', justifyContent: 'center', border: '1.5px dashed var(--glass-border)', flexShrink: 0 }}><span className="text-[9px] text-[var(--text-secondary)]" style={{ opacity: 0.5 }}>+ 拖入工具</span></div>
                    </div>
                  </div>
                </>
              ) : <div className="text-[10px] text-[var(--text-secondary)] py-4">加载中...</div>}
            </div>
          );
        })}
      </div>

      {/* Edge editor panel */}
      {edgeEditor && (
        <EdgeEditorPanel
          state={edgeEditor}
          theme={theme}
          onSave={handleEdgeSave}
          onDelete={handleEdgeDelete}
          onClose={() => setEdgeEditor(null)}
        />
      )}

      <AddExpertButton theme={theme} canvasSize={canvasSize} onCreate={() => setEditExpertName(CREATE_MODE)} />
      {editExpertName && <ExpertEditModal expertName={editExpertName === CREATE_MODE ? undefined : editExpertName} theme={theme} onClose={() => setEditExpertName(null)} onSaved={() => { reloadGraph(); setEditExpertName(null); }} />}
    </div>
  );
}

// ── Port computation ──
interface PortRenderInfo { nodeId: string; type: 'input' | 'output'; x: number; y: number; }

function computeRenderPorts(
  graph: ExpertGraph | null,
  positions: Record<string, NodePosition>,
  expandedIds: Set<string>,
): PortRenderInfo[] {
  if (!graph) return [];
  const ports: PortRenderInfo[] = [];
  const allNodes = [...(graph.virtual_nodes || []).map(v => ({ id: v.id, isVirt: true })), ...(graph.nodes || []).map(n => ({ id: n.id, isVirt: false }))];

  for (const n of allNodes) {
    const pos = positions[n.id] || { x: 0, y: 0 };
    const sizes = getNodeSize(n.id, expandedIds.has(n.id));
    // _user (entry) only has output, _done (exit) only has input
    if (n.id !== '_user') {
      ports.push({ nodeId: n.id, type: 'input', x: pos.x, y: pos.y + sizes.h / 2 });
    }
    if (n.id !== '_done') {
      ports.push({ nodeId: n.id, type: 'output', x: pos.x + sizes.w, y: pos.y + sizes.h / 2 });
    }
  }
  return ports;
}

function getAllPorts(
  graph: ExpertGraph | null,
  positions: Record<string, NodePosition>,
  expandedIds: Set<string>,
): { nodeId: string; type: 'input' | 'output'; x: number; y: number }[] {
  if (!graph) return [];
  const ports: { nodeId: string; type: 'input' | 'output'; x: number; y: number }[] = [];
  const allNodes = [...(graph.virtual_nodes || []).map(v => ({ id: v.id, isVirt: true })), ...(graph.nodes || []).map(n => ({ id: n.id, isVirt: false }))];

  for (const n of allNodes) {
    const pos = positions[n.id] || { x: 0, y: 0 };
    const sizes = getNodeSize(n.id, expandedIds.has(n.id));
    ports.push({ nodeId: n.id, type: 'input', x: pos.x, y: pos.y + sizes.h / 2 });
    ports.push({ nodeId: n.id, type: 'output', x: pos.x + sizes.w, y: pos.y + sizes.h / 2 });
  }
  return ports;
}

function findInputPort(gx: number, gy: number, ports: { nodeId: string; type: string; x: number; y: number }[], excludeNodeId: string) {
  let best: { nodeId: string } | null = null;
  let bestDist = PORT_HIT;
  for (const p of ports) {
    if (p.type !== 'input') continue;
    if (p.nodeId === excludeNodeId) continue; // can't connect to self
    const dx = p.x - gx, dy = p.y - gy;
    const dist = Math.sqrt(dx * dx + dy * dy);
    if (dist < bestDist) {
      bestDist = dist;
      best = p;
    }
  }
  return best;
}

// ── AddExpertButton ──
function AddExpertButton({ theme, canvasSize, onCreate }: { theme: 'dark' | 'light'; canvasSize: CanvasSize; onCreate: () => void }) {
  const [pos, setPos] = useState<NodePosition | null>(null);
  const [hovered, setHovered] = useState(false);
  const dragging = useRef(false);
  const dragMoved = useRef(false);
  const offset = useRef({ x: 0, y: 0 });
  const canvasOrigin = useRef({ x: 0, y: 0 });
  const startPos = useRef({ x: 0, y: 0 });
  const posRef = useRef(pos); posRef.current = pos;
  useEffect(() => {
    if (canvasSize.width <= 0 || canvasSize.height <= 0) return;
    setPos(previous => previous
      ? correctControlIfOutside(previous, canvasSize)
      : defaultAddExpertPosition(canvasSize));
  }, [canvasSize]);
  const onMD = useCallback((e: React.MouseEvent) => {
    if (e.button !== 0 || !posRef.current) return;
    dragging.current = true;
    dragMoved.current = false;
    startPos.current = { x: e.clientX, y: e.clientY };
    const rect = e.currentTarget.parentElement?.getBoundingClientRect();
    canvasOrigin.current = { x: rect?.left || 0, y: rect?.top || 0 };
    offset.current = {
      x: e.clientX - (rect?.left || 0) - posRef.current.x,
      y: e.clientY - (rect?.top || 0) - posRef.current.y,
    };
    e.stopPropagation();
    e.preventDefault();
  }, []);
  useEffect(() => {
    const onM = (e: MouseEvent) => {
      if (!dragging.current) return;
      const dx = e.clientX - startPos.current.x;
      const dy = e.clientY - startPos.current.y;
      if (Math.abs(dx) > 4 || Math.abs(dy) > 4) dragMoved.current = true;
      if (dragMoved.current) {
        setPos({
          x: e.clientX - canvasOrigin.current.x - offset.current.x,
          y: e.clientY - canvasOrigin.current.y - offset.current.y,
        });
      }
    };
    const onU = () => { dragging.current = false; };
    window.addEventListener('mousemove', onM);
    window.addEventListener('mouseup', onU);
    return () => { window.removeEventListener('mousemove', onM); window.removeEventListener('mouseup', onU); };
  }, []);
  const handleClick = useCallback(() => {
    if (dragMoved.current) return;
    onCreate();
  }, [onCreate]);
  const isDark = theme === 'dark';
  if (!pos) return null;
  return (
    <div
      data-add-expert-control
      onMouseDown={onMD}
      onClick={handleClick}
      onMouseEnter={() => setHovered(true)}
      onMouseLeave={() => setHovered(false)}
      style={{
        position: 'absolute', left: pos.x, top: pos.y, zIndex: 100,
        width: ADD_EXPERT_SIZE.width, height: ADD_EXPERT_SIZE.height, padding: '0 14px', borderRadius: 10,
        display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6,
        fontSize: 14, fontWeight: 600, letterSpacing: '0.03em',
        cursor: 'grab', userSelect: 'none', pointerEvents: 'auto',
        background: isDark ? '#ffffff' : '#0a0e17',
        color: isDark ? '#0a0e17' : '#ffffff',
        border: 'none',
        boxShadow: isDark
          ? (hovered ? '0 4px 32px rgba(255,255,255,0.35), 0 0 60px rgba(255,255,255,0.08)' : '0 4px 24px rgba(255,255,255,0.15)')
          : (hovered ? '0 6px 28px rgba(0,0,0,0.3)' : '0 4px 20px rgba(0,0,0,0.18)'),
        transform: hovered ? 'scale(1.06)' : 'scale(1)',
        transition: 'box-shadow 0.2s, transform 0.2s',
      }}
    >
      <span style={{ fontSize: 16, lineHeight: 1 }}>+</span>
      <span>Expert</span>
    </div>
  );
}

// ── Hooks ──
function useElementSize(ref: React.RefObject<HTMLElement | null>, ready: boolean): CanvasSize {
  const [size, setSize] = useState<CanvasSize>({ width: 0, height: 0 });
  useEffect(() => {
    const element = ref.current;
    if (!ready || !element) return;
    const update = () => {
      const rect = element.getBoundingClientRect();
      setSize(previous => previous.width === rect.width && previous.height === rect.height
        ? previous : { width: rect.width, height: rect.height });
    };
    update();
    const observer = new ResizeObserver(update);
    observer.observe(element);
    return () => observer.disconnect();
  }, [ready, ref]);
  return size;
}

function useMemoNodePositions(graph: ExpertGraph | null, saved: Record<string, NodePosition>, canvasSize: CanvasSize) {
  const [c, setC] = useState<Record<string, NodePosition>>({});
  useEffect(() => {
    if (!graph || canvasSize.width <= 0 || canvasSize.height <= 0) return;
    const r: Record<string, NodePosition> = {};
    const graphWidth = canvasSize.width / 0.85;
    const graphHeight = canvasSize.height / 0.85;
    const bounds: LayoutRect = {
      x: 24,
      y: 24,
      width: Math.max(NODE_W, graphWidth * 0.72 - 48),
      height: Math.max(NODE_H, graphHeight - 48),
    };
    const occupied: LayoutRect[] = [];
    for (const [id, position] of Object.entries(saved)) {
      const size = getNodeSize(id, false);
      const outsideBounds = position.x < bounds.x || position.y < bounds.y ||
        position.x + size.w > bounds.x + bounds.width ||
        position.y + size.h > bounds.y + bounds.height;
      const nextPosition = outsideBounds
        ? findSafeNodePosition(
          {
            x: Math.min(Math.max(position.x, bounds.x), bounds.x + bounds.width - size.w),
            y: Math.min(Math.max(position.y, bounds.y), bounds.y + bounds.height - size.h),
          },
          occupied,
          bounds,
          { width: size.w, height: size.h },
        )
        : position;
      r[id] = nextPosition;
      occupied.push({ ...nextPosition, width: size.w, height: size.h });
    }
    const entryNodes = (graph.virtual_nodes || []).filter(node => node.type === 'entry');
    const exitNodes = (graph.virtual_nodes || []).filter(node => node.type === 'exit');
    const otherVirtualNodes = (graph.virtual_nodes || []).filter(node => node.type !== 'entry' && node.type !== 'exit');
    const ordered = [
      ...entryNodes.map(node => ({ id: node.id, width: VNODE_W, height: VNODE_H })),
      ...(graph.nodes || []).map(node => ({ id: node.id, width: NODE_W, height: NODE_H })),
      ...exitNodes.map(node => ({ id: node.id, width: VNODE_W, height: VNODE_H })),
      ...otherVirtualNodes.map(node => ({ id: node.id, width: VNODE_W, height: VNODE_H })),
    ];
    const gap = Math.max(88, Math.min(VERTICAL_GAP, (bounds.height - VNODE_H) / Math.max(1, ordered.length - 1)));
    const totalHeight = Math.max(VNODE_H, (ordered.length - 1) * gap + VNODE_H);
    const startY = Math.max(bounds.y, bounds.y + (bounds.height - totalHeight) / 2);
    const centerX = bounds.x + Math.max(0, (bounds.width - NODE_W) / 2);
    ordered.forEach((node, index) => {
      if (r[node.id]) return;
      const position = findSafeNodePosition(
        { x: centerX, y: startY + index * gap },
        occupied,
        bounds,
        { width: node.width, height: node.height },
      );
      r[node.id] = position;
      occupied.push({ ...position, width: node.width, height: node.height });
    });
    setC(r);
  }, [canvasSize, graph, saved]);
  return Object.keys(c).length > 0 ? c : {};
}

function useMemoEdges(routesMap: RoutesMap, graph: ExpertGraph | null, version: number): ExpertEdge[] {
  const [edges, setEdges] = useState<ExpertEdge[]>([]);
  useEffect(() => {
    if (!graph) { setEdges([]); return; }
    setEdges(edgesFromRoutesMap(routesMap, graph));
  }, [graph, routesMap, version]);
  return edges;
}

interface EdgePath { id: string; d: string; color: string; label: string; labelX: number; labelY: number; conditionType: string; isActive: boolean; status: WorkflowEdgeStatus; edge: ExpertEdge; }

function useMemoEdgePaths(graph: ExpertGraph | null, positions: Record<string, NodePosition>, displayEdges: ExpertEdge[], workflowState?: WorkflowTaskState) {
  const [paths, setPaths] = useState<EdgePath[]>([]);
  useEffect(() => {
    if (!graph) { setPaths([]); return; }
    const r: EdgePath[] = [];
    const pc: Record<string, number> = {};
    for (const e of displayEdges) {
      const sp = positions[e.source], tp = positions[e.target];
      if (!sp || !tp) continue;
      const sSize = getNodeSize(e.source, false), tSize = getNodeSize(e.target, false);
      const sx = sp.x + sSize.w + PORT_RADIUS;
      const sy = sp.y + sSize.h / 2;
      const tx = tp.x - PORT_RADIUS;
      const ty = tp.y + tSize.h / 2;
      const pk = `${e.source}->${e.target}`;
      const pi = (pc[pk] = (pc[pk] || 0) + 1);
      const co = (pi - 1) * 32;
      const dx = Math.abs(tx - sx) * 0.5 + co;
      const d = `M ${sx} ${sy} C ${sx + dx} ${sy}, ${tx - dx} ${ty}, ${tx} ${ty}`;
      const lx = (sx + tx) / 2 + co * 0.5;
      const ly = (sy + ty) / 2 - 2 + (pi - 1) * 14;
      let status = workflowState?.edgeStates[workflowEdgeKey(e.source, e.target)] || 'idle';
      if (status === 'idle' && e.source === '_user') {
        const targetStatus = workflowState?.nodeStates[normalizeExpertName(e.target)] || 'idle';
        if (targetStatus === 'running') status = 'running';
        if (targetStatus === 'completed') status = 'completed';
        if (targetStatus === 'failed') status = 'failed';
      }
      const col = status === 'pending' ? '#f59e0b'
        : status === 'running' ? '#3b82f6'
          : status === 'completed' ? '#22c55e'
            : status === 'failed' ? '#ef4444'
              : CONDITION_COLORS[e.condition_type] || '#6b7280';
      r.push({
        id: e.id,
        d,
        color: col,
        label: e.label || '',
        labelX: lx,
        labelY: ly,
        conditionType: e.condition_type,
        isActive: status !== 'idle',
        status,
        edge: e,
      });
    }
    setPaths(r);
  }, [graph, positions, displayEdges, workflowState]);
  return paths;
}
