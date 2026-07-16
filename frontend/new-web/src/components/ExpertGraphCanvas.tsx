import { useCallback, useEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import type { Exponent, ExpertGraph } from '../types';
import { getExpert, getExpertGraph, getExpertGraphPositions, saveExpertGraphPositions, setExpertTools, updateExpert } from '../api/api';
import { ExpertEditModal } from './ExpertEditModal';

interface Props {
  theme: 'dark' | 'light';
}

interface NodePosition { x: number; y: number; }

interface MousedownInfo { nodeId: string; clientX: number; clientY: number; time: number; }

const NODE_W = 180; const NODE_H = 60; const EXPANDED_W = 300;
const VNODE_W = 90; const VNODE_H = 36; const PORT_RADIUS = 6;
const VERTICAL_GAP = 120; const INITIAL_TOP = 100; const INITIAL_LEFT = 80;
const DRAG_THRESHOLD = 4; const LONG_PRESS_MS = 180;
const CREATE_MODE = '<<CREATE>>';

const RISK_COLORS: Record<string, string> = { safe: '#22c55e', medium: '#f59e0b', dangerous: '#ef4444' };

export function ExpertGraphCanvas({ theme }: Props) {
  const [graph, setGraph] = useState<ExpertGraph | null>(null);
  const [loading, setLoading] = useState(true);
  const [positions, setPositions] = useState<Record<string, NodePosition>>({});
  const interactionRef = useRef<HTMLDivElement>(null);

  const [cumPan, setCumPan] = useState({ x: 0, y: 0 });
  const panning = useRef(false);
  const panStart = useRef({ x: 0, y: 0 });
  const panBase = useRef({ x: 0, y: 0 });

  const [zoomTarget, setZoomTarget] = useState(0.85);
  const [zoomRendered, setZoomRendered] = useState(0.85);

  const positionRef = useRef(positions); positionRef.current = positions;
  const zoomRenderedRef = useRef(zoomRendered); zoomRenderedRef.current = zoomRendered;
  const cumPanRef = useRef(cumPan); cumPanRef.current = cumPan;

  const [editExpertName, setEditExpertName] = useState<string | null>(null);
  const [graphVersion, setGraphVersion] = useState(0);

  const [expandedNodeIds, setExpandedNodeIds] = useState<Set<string>>(new Set());
  const [pinnedNodeIds, setPinnedNodeIds] = useState<Set<string>>(new Set());
  const [expandedDataMap, setExpandedDataMap] = useState<Record<string, { prompt: string; tools: string[] }>>({});
  const hoverTimerMap = useRef<Record<string, number>>({});
  const expandedDataRef = useRef(expandedDataMap); expandedDataRef.current = expandedDataMap;

  const mousedownRef = useRef<MousedownInfo | null>(null);
  const dragOffset = useRef({ x: 0, y: 0 });
  const isDraggingNode = useRef(false);
  const suppressLeaveUntil = useRef<Record<string, number>>({});
  const dragOverCount = useRef<Record<string, number>>({});
  const [draggingTool, setDraggingTool] = useState(false);

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
  useEffect(() => { (async () => { try { const [g, pos] = await Promise.all([getExpertGraph(), getExpertGraphPositions()]); setGraph(g); if (pos) setPositions(pos); } catch {} setLoading(false); })(); }, []);
  useEffect(() => { if (graphVersion === 0) return; (async () => { try { const [g, pos] = await Promise.all([getExpertGraph(), getExpertGraphPositions()]); setGraph(g); if (pos) setPositions(pos); } catch {} })(); }, [graphVersion]);

  const nodePositions = useMemoNodePositions(graph, positions);

  // Global tool drag
  useEffect(() => {
    const onOver = (e: DragEvent) => e.preventDefault();
    const onStart = (e: DragEvent) => { if (e.dataTransfer?.getData('application/tool-name')) setDraggingTool(true); };
    const onEnd = () => { setDraggingTool(false); dragOverCount.current = {}; };
    window.addEventListener('dragover', onOver); window.addEventListener('dragstart', onStart); window.addEventListener('dragend', onEnd);
    return () => { window.removeEventListener('dragover', onOver); window.removeEventListener('dragstart', onStart); window.removeEventListener('dragend', onEnd); };
  }, []);

  // Zoom
  const zoomHandler = useCallback((e: WheelEvent) => {
    e.preventDefault();
    const o = zoomTarget; const raw = o - e.deltaY * 0.0008;
    const z = Math.min(2, Math.max(0.3, raw));
    const cp = cumPanRef.current;
    setCumPan({ x: e.clientX - (e.clientX - cp.x) / o * z, y: e.clientY - (e.clientY - cp.y) / o * z });
    setZoomTarget(z);
  }, []);
  useEffect(() => { const el = interactionRef.current; if (!el) return; el.addEventListener('wheel', zoomHandler, { passive: false }); return () => el.removeEventListener('wheel', zoomHandler); }, [zoomHandler]);

  // Global mousemove/mouseup for node drag detection
  useEffect(() => {
    const onMove = (e: MouseEvent) => {
      const md = mousedownRef.current;
      if (!md) return;
      const dx = e.clientX - md.clientX, dy = e.clientY - md.clientY;
      const dist = Math.sqrt(dx * dx + dy * dy);
      if (!isDraggingNode.current && (dist > DRAG_THRESHOLD || Date.now() - md.time > LONG_PRESS_MS)) {
        isDraggingNode.current = true;
        const z = zoomRenderedRef.current, cp = cumPanRef.current;
        const pos = positionRef.current[md.nodeId] || { x: 0, y: 0 };
        dragOffset.current = { x: (md.clientX - cp.x) / z - pos.x, y: (md.clientY - cp.y) / z - pos.y };
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
        saveExpertGraphPositions(positionRef.current).catch(() => {});
      }
      mousedownRef.current = null;
      isDraggingNode.current = false;
    };
    window.addEventListener('mousemove', onMove); window.addEventListener('mouseup', onUp);
    return () => { window.removeEventListener('mousemove', onMove); window.removeEventListener('mouseup', onUp); };
  }, [expandedNodeIds]);

  // Pan
  const handleBgMouseDown = useCallback((e: React.MouseEvent) => {
    if (e.button !== 0 || (e.target as HTMLElement).closest('[data-node]')) return;
    panning.current = true; panStart.current = { x: e.clientX, y: e.clientY }; panBase.current = { ...cumPanRef.current };
  }, []);
  useEffect(() => {
    const onMove = (e: MouseEvent) => { if (!panning.current) return; setCumPan({ x: panBase.current.x + e.clientX - panStart.current.x, y: panBase.current.y + e.clientY - panStart.current.y }); };
    const onUp = () => { panning.current = false; };
    window.addEventListener('mousemove', onMove); window.addEventListener('mouseup', onUp);
    return () => { window.removeEventListener('mousemove', onMove); window.removeEventListener('mouseup', onUp); };
  }, []);

  const handleNodeMouseDown = useCallback((nodeId: string, e: React.MouseEvent) => {
    e.stopPropagation(); if (e.button !== 0) return;
    mousedownRef.current = { nodeId, clientX: e.clientX, clientY: e.clientY, time: Date.now() };
    isDraggingNode.current = false;
  }, []);

  // Expand/collapse
  const openExpanded = useCallback(async (nodeId: string, mode: 'click' | 'hover' | 'drag' = 'hover') => {
    setExpandedNodeIds(prev => new Set(prev).add(nodeId));
    if (mode === 'click') setPinnedNodeIds(prev => new Set(prev).add(nodeId));
    suppressLeaveUntil.current[nodeId] = Date.now() + 200;
    try { const e = await getExpert(nodeId); setExpandedDataMap(prev => ({ ...prev, [nodeId]: { prompt: e.context_template || '', tools: e.visible_tools || [] } })); } catch {}
  }, []);
  const closeExpanded = useCallback((nodeId: string) => {
    setExpandedNodeIds(prev => { const n = new Set(prev); n.delete(nodeId); return n; });
    setPinnedNodeIds(prev => { const n = new Set(prev); n.delete(nodeId); return n; });
  }, []);
  const isPinned = useCallback((nodeId: string) => pinnedNodeIds.has(nodeId), [pinnedNodeIds]);

  const handleNodeHoverEnter = useCallback((nodeId: string) => {
    if (nodeId.startsWith('_') || mousedownRef.current || expandedNodeIds.has(nodeId)) return;
    clearTimeout(hoverTimerMap.current[nodeId]);
    hoverTimerMap.current[nodeId] = window.setTimeout(() => { if (!mousedownRef.current && !expandedNodeIds.has(nodeId)) openExpanded(nodeId, 'hover'); }, 300);
  }, [expandedNodeIds, openExpanded]);
  const handleNodeHoverLeave = useCallback((nodeId: string) => {
    clearTimeout(hoverTimerMap.current[nodeId]);
    if (Date.now() < (suppressLeaveUntil.current[nodeId] || 0)) return;
    if (expandedNodeIds.has(nodeId) && !pinnedNodeIds.has(nodeId)) closeExpanded(nodeId);
  }, [expandedNodeIds, pinnedNodeIds, closeExpanded]);

  const handleNodeDoubleClick = useCallback((nodeId: string) => { if (nodeId.startsWith('_')) return; closeExpanded(nodeId); setEditExpertName(nodeId); }, [closeExpanded]);

  // Tool drag onto nodes
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
    try { await setExpertTools(nodeId, newTools); reloadGraph(); } catch { try { const e = await getExpert(nodeId); setExpandedDataMap(prev => ({ ...prev, [nodeId]: { prompt: e.context_template || '', tools: e.visible_tools || [] } })); } catch {} }
  }, [reloadGraph]);
  const removeTool = useCallback((nodeId: string, tn: string) => { const d = expandedDataRef.current[nodeId]; if (d) saveTools(nodeId, d.tools.filter(t => t !== tn)); }, [saveTools]);
  const savePrompt = useCallback(async (nodeId: string, prompt: string) => { try { await updateExpert(nodeId, { context_template: prompt }); } catch {} }, []);

  const edgePaths = useMemoEdgePaths(graph, nodePositions);
  if (loading || !graph) return null;
  const tr = `scale(${zoomRendered}) translate(${cumPan.x}px, ${cumPan.y}px)`;

  return (
    <div className={`expert-canvas theme-${theme}`} style={{ position: 'absolute', inset: 0, zIndex: 0, pointerEvents: 'none', overflow: 'hidden' }}>
      <div ref={interactionRef} onMouseDown={handleBgMouseDown} style={{ position: 'absolute', inset: 0, zIndex: 0, pointerEvents: 'auto', cursor: panning.current ? 'grabbing' : 'default' }} />
      <svg style={{ position: 'absolute', inset: 0, width: '100%', height: '100%', pointerEvents: 'none' }}>
        <g style={{ transform: tr, transformOrigin: '0 0' }}>
          {edgePaths.map(ep => (
            <g key={ep.id}>
              <path d={ep.d} fill="none" stroke={ep.color} strokeWidth={ep.isActive ? 2.5 : 1.5} strokeOpacity={ep.isActive ? 0.9 : 0.45} strokeDasharray={ep.conditionType === 'default' ? '6 3' : undefined} />
              {ep.label && <text x={ep.labelX} y={ep.labelY - 6} textAnchor="middle" fill={theme === 'dark' ? 'rgba(255,255,255,0.55)' : 'rgba(0,0,0,0.45)'} fontSize={10} fontFamily="monospace" style={{ pointerEvents: 'none', userSelect: 'none' }}>{ep.label}</text>}
            </g>
          ))}
        </g>
      </svg>
      <div style={{ position: 'absolute', inset: 0, transform: tr, transformOrigin: '0 0', pointerEvents: 'none' }}>
        {graph.virtual_nodes?.map(vn => {
          const pos = nodePositions[vn.id] || { x: 0, y: 0 };
          return (
            <div key={vn.id} data-node={vn.id} onMouseDown={e => handleNodeMouseDown(vn.id, e)}
              style={{ position: 'absolute', left: pos.x, top: pos.y, width: VNODE_W, height: VNODE_H, borderRadius: 10, display: 'flex', alignItems: 'center', justifyContent: 'center', fontSize: 12, fontWeight: 600, letterSpacing: '0.04em', cursor: 'grab', pointerEvents: 'auto', userSelect: 'none',
                background: vn.type === 'entry' ? (theme === 'dark' ? 'rgba(34,197,94,0.12)' : 'rgba(34,197,94,0.08)') : (theme === 'dark' ? 'rgba(239,68,68,0.12)' : 'rgba(239,68,68,0.08)'),
                border: `1.5px solid ${vn.type === 'entry' ? (theme === 'dark' ? 'rgba(34,197,94,0.5)' : 'rgba(34,197,94,0.4)') : (theme === 'dark' ? 'rgba(239,68,68,0.5)' : 'rgba(239,68,68,0.4)')}`,
                color: vn.type === 'entry' ? (theme === 'dark' ? 'rgba(34,197,94,0.9)' : 'rgba(34,197,94,0.8)') : (theme === 'dark' ? 'rgba(239,68,68,0.9)' : 'rgba(239,68,68,0.8)'),
                backdropFilter: 'blur(12px)' }}>{vn.label}</div>
          );
        })}
        {graph.nodes?.map(node => {
          const pos = nodePositions[node.id] || { x: 0, y: 0 };
          const isExpanded = expandedNodeIds.has(node.id);
          const ed = expandedDataMap[node.id];
          const dragHL = draggingTool;
          const isCollapsed = !isExpanded;
          return (
            <div key={node.id} data-node={node.id} draggable={false}
              className={isExpanded ? 'glass-panel' : undefined}
              onDoubleClick={() => handleNodeDoubleClick(node.id)}
              onMouseDown={e => handleNodeMouseDown(node.id, e)}
              onMouseEnter={e => {
                handleNodeHoverEnter(node.id);
                if (!dragHL && !isExpanded) { e.currentTarget.style.boxShadow = theme === 'dark' ? '0 4px 32px rgba(99,147,235,0.25)' : '0 4px 32px rgba(99,147,235,0.12)'; }
              }}
              onMouseLeave={e => {
                handleNodeHoverLeave(node.id);
                e.currentTarget.style.boxShadow = dragHL && isCollapsed ? (theme === 'dark' ? '0 0 24px rgba(99,147,235,0.35)' : '0 0 24px rgba(99,147,235,0.18)') : (theme === 'dark' ? '0 4px 24px rgba(0,0,0,0.3)' : '0 4px 24px rgba(0,0,0,0.08)');
              }}
              onDragOver={e => handleNodeDragOver(node.id, e)}
              onDragLeave={e => { if (!e.currentTarget.contains(e.relatedTarget as Node)) handleNodeDragLeave(node.id); }}
              onDrop={e => handleNodeDrop(node.id, e)}
              style={{
                position: 'absolute', left: pos.x, top: pos.y, width: isExpanded ? EXPANDED_W : NODE_W,
                ...(!isExpanded ? { height: NODE_H } : { minHeight: 280, maxHeight: 480 }),
                borderRadius: isExpanded ? 16 : 12,
                display: 'flex', flexDirection: 'column', overflow: isCollapsed ? 'hidden' : 'auto',
                cursor: isDraggingNode.current ? 'grabbing' : 'pointer', pointerEvents: 'auto', userSelect: 'none',
                background: isExpanded ? (theme === 'dark' ? 'rgba(10,14,23,0.92)' : 'rgba(253,252,251,0.92)') : (theme === 'dark' ? 'rgba(10,14,23,0.75)' : 'rgba(253,252,251,0.75)'),
                border: `2px solid ${isExpanded || dragHL ? 'var(--accent-light)' : 'var(--glass-border-strong)'}`,
                color: 'var(--text-primary)', backdropFilter: isExpanded ? 'blur(24px)' : 'blur(20px)',
                boxShadow: isExpanded ? (theme === 'dark' ? '0 8px 48px rgba(99,147,235,0.35)' : '0 8px 48px rgba(99,147,235,0.2)') : dragHL ? (theme === 'dark' ? '0 0 24px rgba(99,147,235,0.35)' : '0 0 24px rgba(99,147,235,0.18)') : (theme === 'dark' ? '0 4px 24px rgba(0,0,0,0.3)' : '0 4px 24px rgba(0,0,0,0.08)'),
                transition: 'width 0.12s cubic-bezier(0.4, 0, 0.2, 1), border-radius 0.12s cubic-bezier(0.4, 0, 0.2, 1), box-shadow 0.15s cubic-bezier(0.4, 0, 0.2, 1), border-color 0.15s cubic-bezier(0.4, 0, 0.2, 1), min-height 0.12s cubic-bezier(0.4, 0, 0.2, 1)',
                zIndex: isExpanded ? 10 : 0, padding: isExpanded ? '16px 18px' : 0, gap: isExpanded ? 10 : 0,
                alignItems: isCollapsed ? 'center' : undefined, justifyContent: isCollapsed ? 'center' : undefined,
              }}>
              {isCollapsed ? (
                <span style={{ fontSize: 13, fontWeight: 600, letterSpacing: '0.03em' }}>{node.label}</span>
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
                    <span className="text-[9px] font-semibold uppercase tracking-wider text-[var(--text-secondary)]">提示词</span>
                    <textarea value={ed.prompt} onChange={e => setExpandedDataMap(prev => ({ ...prev, [node.id]: { ...prev[node.id], prompt: e.target.value } }))} onBlur={e => savePrompt(node.id, e.target.value)} rows={3} className="form-input" style={{ resize: 'vertical', fontSize: 10, lineHeight: 1.55, fontFamily: 'monospace', padding: '6px 8px', borderRadius: 8, background: 'var(--bg)', color: 'var(--text-primary)', border: '1px solid var(--glass-border)' }} placeholder="输入 system prompt..." />
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
      <AddExpertButton theme={theme} onCreate={() => setEditExpertName(CREATE_MODE)} />
      {editExpertName && <ExpertEditModal expertName={editExpertName === CREATE_MODE ? undefined : editExpertName} theme={theme} onClose={() => setEditExpertName(null)} onSaved={() => { reloadGraph(); setEditExpertName(null); }} />}
    </div>
  );
}

// AddExpertButton
function AddExpertButton({ theme, onCreate }: { theme: 'dark' | 'light'; onCreate: () => void }) {
  const [pos, setPos] = useState({ x: window.innerWidth - 160, y: 24 });
  const dragging = useRef(false); const offset = useRef({ x: 0, y: 0 }); const posRef = useRef(pos); posRef.current = pos;
  const onMD = useCallback((e: React.MouseEvent) => { if (e.button !== 0) return; dragging.current = true; offset.current = { x: e.clientX - posRef.current.x, y: e.clientY - posRef.current.y }; e.stopPropagation(); e.preventDefault(); }, []);
  useEffect(() => { const onM = (e: MouseEvent) => { if (!dragging.current) return; setPos({ x: e.clientX - offset.current.x, y: e.clientY - offset.current.y }); }; const onU = () => { dragging.current = false; }; window.addEventListener('mousemove', onM); window.addEventListener('mouseup', onU); return () => { window.removeEventListener('mousemove', onM); window.removeEventListener('mouseup', onU); }; }, []);
  const isDark = theme === 'dark';
  return createPortal(<div onMouseDown={onMD} onClick={onCreate} style={{ position: 'fixed', left: pos.x, top: pos.y, zIndex: 100, minWidth: 38, height: 38, padding: '0 14px', borderRadius: 10, display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6, fontSize: 14, fontWeight: 600, letterSpacing: '0.03em', cursor: 'grab', userSelect: 'none', background: isDark ? '#ffffff' : '#0a0e17', color: isDark ? '#0a0e17' : '#ffffff', border: 'none', boxShadow: isDark ? '0 4px 24px rgba(255,255,255,0.15)' : '0 4px 24px rgba(0,0,0,0.15)', transition: 'box-shadow 0.2s, transform 0.15s' }}><span style={{ fontSize: 16, lineHeight: 1 }}>+</span><span>Expert</span></div>, document.body);
}

// Hooks
function useMemoNodePositions(graph: ExpertGraph | null, saved: Record<string, NodePosition>) {
  const [c, setC] = useState<Record<string, NodePosition>>({});
  useEffect(() => { if (!graph) return; const r: Record<string, NodePosition> = { ...saved }; (graph.virtual_nodes || []).forEach(vn => { if (!r[vn.id]) r[vn.id] = { x: INITIAL_LEFT, y: INITIAL_TOP }; }); (graph.nodes || []).forEach((n, i) => { if (!r[n.id]) r[n.id] = { x: INITIAL_LEFT, y: INITIAL_TOP + (i + 1) * VERTICAL_GAP }; }); const ev = (graph.virtual_nodes || []).find(v => v.type === 'exit'); if (ev && !saved[ev.id]) r[ev.id] = { x: INITIAL_LEFT, y: INITIAL_TOP + (graph.nodes.length + 1) * VERTICAL_GAP }; setC(r); }, [graph, saved]);
  return Object.keys(c).length > 0 ? c : {};
}
interface EdgePath { id: string; d: string; color: string; label: string; labelX: number; labelY: number; conditionType: string; isActive: boolean; }
function useMemoEdgePaths(graph: ExpertGraph | null, positions: Record<string, NodePosition>) {
  const [paths, setPaths] = useState<EdgePath[]>([]);
  useEffect(() => { if (!graph) { setPaths([]); return; } const r: EdgePath[] = []; const ae = [...(graph.edges || [])]; const en = graph.nodes?.find(n => n.is_entry); if (en) ae.push({ id: 'se', source: '_user', target: en.id, condition_type: 'tag_exists', condition_value: '', priority: 0, label: '任务输入' }); const pc: Record<string, number> = {}; for (const e of ae) { const sp = positions[e.source], tp = positions[e.target]; if (!sp || !tp) continue; const sV = e.source.startsWith('_'), tV = e.target.startsWith('_'); const sx = sp.x + (sV ? VNODE_W : NODE_W) + PORT_RADIUS, sy = sp.y + (sV ? VNODE_H : NODE_H) / 2; const tx = tp.x - PORT_RADIUS, ty = tp.y + (tV ? VNODE_H : NODE_H) / 2; const pk = `${e.source}->${e.target}`; const pi = (pc[pk] = (pc[pk] || 0) + 1); const co = (pi - 1) * 32; const dx = Math.abs(tx - sx) * 0.5 + co; const d = `M ${sx} ${sy} C ${sx + dx} ${sy}, ${tx - dx} ${ty}, ${tx} ${ty}`; const lx = (sx + tx) / 2 + co * 0.5, ly = (sy + ty) / 2 - 2 + (pi - 1) * 14; let col: string; switch (e.condition_type) { case 'on_fail': col = '#ef4444'; break; case 'tag_exists': case 'tag_value_match': col = '#6393eb'; break; default: col = '#6b7280'; } r.push({ id: e.id, d, color: col, label: e.label || '', labelX: lx, labelY: ly, conditionType: e.condition_type, isActive: false }); } setPaths(r); }, [graph, positions]);
  return paths;
}