import { useCallback, useEffect, useReducer, useRef, useState } from 'react';
import type { PointerEvent as ReactPointerEvent } from 'react';
import type { AppPhase, GlassTab, SessionRecord, TaskEventRecord, TaskRecord, ThemeMode, ToolInfo, WorkspaceRecord } from './types';
import { listWorkspaces, createWorkspace, updateWorkspace, deleteWorkspace, getSession, getWorkspace, listSessionsByWorkspace, listTools } from './api/api';
import { GlassPanel } from './components/GlassPanel';
import { StatusPills } from './components/StatusPills';
import { ToolPanel } from './components/ToolPanel';
import { SettingsPanel } from './components/SettingsPanel';
import { ExpertGraphCanvas } from './components/ExpertGraphCanvas';
import { AmbientBubbles } from './components/AmbientBubbles';
import { initialWorkflowStoreState, workflowStoreReducer } from './workflowState';
import { useStickyState } from './hooks/useStickyState';
import {
  DEFAULT_SESSION_WIDTH,
  LAYOUT_STORAGE_KEY,
  clampSessionWidth,
} from './fileEditor';

const PHASE_TRANSITION_MS = 280;

function parseLocation() {
  const match = window.location.pathname.match(
    /^\/workspaces\/([^/]+)(?:\/sessions\/([^/]+))?\/?$/,
  );
  if (!match) return { workspaceId: null, sessionId: null };
  try {
    return { workspaceId: decodeURIComponent(match[1]), sessionId: match[2] ? decodeURIComponent(match[2]) : null };
  } catch {
    return { workspaceId: null, sessionId: null };
  }
}

function workspacePath(workspaceId: string, sessionId?: string) {
  const base = `/workspaces/${encodeURIComponent(workspaceId)}`;
  return sessionId ? `${base}/sessions/${encodeURIComponent(sessionId)}` : base;
}

// ====================================================================
// App
// ====================================================================

export default function App() {
  const [theme, setTheme] = useStickyState<ThemeMode>('codepilot.theme', 'dark');
  const [phase, setPhase] = useState<AppPhase>('workspace_select');
  const [activeWorkspace, setActiveWorkspace] = useState<WorkspaceRecord | null>(null);
  const [activeSession, setActiveSession] = useState<SessionRecord | null>(null);
  const [activeTab, setActiveTab] = useStickyState<GlassTab>('codepilot.active-tab', 'chat');
  const [scale, setScale] = useStickyState<number>('codepilot.scale', 1);
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [positionResetVersion, setPositionResetVersion] = useState(0);
  const [toolPanelExpanded, setToolPanelExpanded] = useStickyState<boolean>('codepilot.tool-panel-expanded', false);
  const fileDirtyRef = useRef(false);
  const activeWorkspaceRef = useRef<WorkspaceRecord | null>(null);
  const activeSessionRef = useRef<SessionRecord | null>(null);
  activeWorkspaceRef.current = activeWorkspace;
  activeSessionRef.current = activeSession;
  const [resizingLayout, setResizingLayout] = useState(false);
  const [sessionPaneWidth, setSessionPaneWidth] = useState(() => {
    const saved = Number(localStorage.getItem(LAYOUT_STORAGE_KEY));
    return Number.isFinite(saved) && saved > 0 ? saved : DEFAULT_SESSION_WIDTH;
  });
  const [initializing, setInitializing] = useState(true);
  const [workflowStore, dispatchWorkflow] = useReducer(workflowStoreReducer, initialWorkflowStoreState);
  const [transitioning, setTransitioning] = useState(false);
  const timerRef = useRef<number>(0);
  const restoreRequest = useRef(0);
  const restoreAbort = useRef<AbortController | null>(null);
  const workspaceMainRef = useRef<HTMLElement>(null);
  const sessionPaneWidthRef = useRef(sessionPaneWidth);
  sessionPaneWidthRef.current = sessionPaneWidth;
  useEffect(() => () => clearTimeout(timerRef.current), []);

  useEffect(() => {
    const main = workspaceMainRef.current;
    if (!main) return;
    const applyBounds = () => {
      const toolWidth = toolPanelExpanded ? 240 : 28;
      setSessionPaneWidth(current => clampSessionWidth(current, main.clientWidth, toolWidth));
    };
    applyBounds();
    const observer = new ResizeObserver(applyBounds);
    observer.observe(main);
    return () => observer.disconnect();
  }, [phase, toolPanelExpanded]);

  const handleFileDirtyChange = useCallback((dirty: boolean) => {
    fileDirtyRef.current = dirty;
  }, []);

  const beginLayoutResize = useCallback((event: ReactPointerEvent<HTMLDivElement>) => {
    if (event.button !== 0 || !workspaceMainRef.current) return;
    event.preventDefault();
    const startX = event.clientX;
    const startWidth = sessionPaneWidthRef.current;
    const main = workspaceMainRef.current;
    const toolWidth = toolPanelExpanded ? 240 : 28;
    setResizingLayout(true);
    const handleMove = (moveEvent: PointerEvent) => {
      const desired = startWidth - (moveEvent.clientX - startX);
      setSessionPaneWidth(clampSessionWidth(desired, main.clientWidth, toolWidth));
    };
    const handleUp = () => {
      setResizingLayout(false);
      localStorage.setItem(LAYOUT_STORAGE_KEY, String(Math.round(sessionPaneWidthRef.current)));
      window.removeEventListener('pointermove', handleMove);
      window.removeEventListener('pointerup', handleUp);
    };
    window.addEventListener('pointermove', handleMove);
    window.addEventListener('pointerup', handleUp, { once: true });
  }, [toolPanelExpanded]);

  useEffect(() => {
    dispatchWorkflow({
      type: 'context_changed',
      workspaceId: activeWorkspace?.id || null,
      sessionId: activeSession?.id || null,
    });
  }, [activeSession?.id, activeWorkspace?.id]);

  const handleWorkflowTaskSelected = useCallback((task: TaskRecord | null) => {
    dispatchWorkflow({ type: 'task_selected', taskId: task?.id || null });
  }, []);

  const handleWorkflowEvents = useCallback((task: TaskRecord, events: TaskEventRecord[]) => {
    dispatchWorkflow({ type: 'events_received', taskId: task.id, events });
  }, []);

  const workflowState = workflowStore.workspaceId === (activeWorkspace?.id || null) &&
    workflowStore.sessionId === (activeSession?.id || null) && workflowStore.activeTaskId
    ? workflowStore.tasks[workflowStore.activeTaskId]
    : undefined;

  const restoreFromLocation = useCallback(async () => {
    const requestId = ++restoreRequest.current;
    restoreAbort.current?.abort();
    const controller = new AbortController();
    restoreAbort.current = controller;
    const { workspaceId, sessionId } = parseLocation();
    setInitializing(true);
    if (!workspaceId) {
      if (window.location.pathname !== '/') {
        window.history.replaceState(null, '', '/');
      }
      setActiveWorkspace(null);
      setActiveSession(null);
      setPhase('workspace_select');
      setInitializing(false);
      return;
    }

    try {
      const workspace = await getWorkspace(workspaceId, controller.signal);
      const sessionList = await listSessionsByWorkspace(workspace.id, controller.signal);
      if (requestId !== restoreRequest.current) return;
      setActiveWorkspace(workspace);
      setActiveSession(null);
      setPhase('normal');

      if (sessionId) {
        try {
          const session = await getSession(sessionId, controller.signal);
          const belongs = session.workspace_id === workspace.id &&
            (sessionList.items || []).some(item => item.id === session.id && item.workspace_id === workspace.id);
          if (requestId !== restoreRequest.current) return;
          if (belongs) {
            setActiveSession(session);
          } else {
            window.history.replaceState(null, '', workspacePath(workspace.id));
          }
        } catch {
          if (requestId === restoreRequest.current) {
            setActiveSession(null);
            window.history.replaceState(null, '', workspacePath(workspace.id));
          }
        }
      }
    } catch {
      if (requestId !== restoreRequest.current) return;
      localStorage.removeItem('active_workspace_id');
      localStorage.removeItem('active_session_id');
      window.history.replaceState(null, '', '/');
      setActiveWorkspace(null);
      setActiveSession(null);
      setPhase('workspace_select');
    } finally {
      if (requestId === restoreRequest.current) setInitializing(false);
    }
  }, []);

  useEffect(() => {
    void restoreFromLocation();
    const onPopState = () => {
      if (fileDirtyRef.current && !window.confirm(
        '当前文件有未保存修改。确定放弃修改并切换 Workspace 吗？\n选择"取消"后可返回文件页保存。',
      )) {
        const currentWorkspace = activeWorkspaceRef.current;
        const currentSession = activeSessionRef.current;
        window.history.pushState(
          null,
          '',
          currentWorkspace ? workspacePath(currentWorkspace.id, currentSession?.id) : '/',
        );
        return;
      }
      fileDirtyRef.current = false;
      void restoreFromLocation();
    };
    window.addEventListener('popstate', onPopState);
    return () => {
      window.removeEventListener('popstate', onPopState);
      restoreAbort.current?.abort();
    };
  }, [restoreFromLocation]);

  const enterWorkspace = useCallback((ws: WorkspaceRecord) => {
    if (transitioning) return;
    setTransitioning(true);
    setActiveWorkspace(ws);
    setActiveSession(null);
    localStorage.setItem('codepilot.last-workspace-id', ws.id);
    window.history.pushState(null, '', workspacePath(ws.id));
    timerRef.current = window.setTimeout(() => {
      setPhase('normal');
      setTransitioning(false);
    }, PHASE_TRANSITION_MS);
  }, [transitioning]);

  const selectSession = useCallback((session: SessionRecord) => {
    if (!activeWorkspace || session.workspace_id !== activeWorkspace.id) return;
    setActiveSession(session);
    localStorage.setItem(`codepilot.last-session-id.${activeWorkspace.id}`, session.id);
    window.history.pushState(null, '', workspacePath(activeWorkspace.id, session.id));
  }, [activeWorkspace]);

  const exitWorkspace = useCallback(() => {
    if (transitioning) return;
    setTransitioning(true);
    timerRef.current = window.setTimeout(() => {
      if (activeWorkspaceRef.current) {
        localStorage.removeItem(`codepilot.last-session-id.${activeWorkspaceRef.current.id}`);
      }
      localStorage.removeItem('codepilot.last-workspace-id');
      setActiveWorkspace(null);
      setActiveSession(null);
      window.history.pushState(null, '', '/');
      setPhase('workspace_select');
      setTransitioning(false);
    }, PHASE_TRANSITION_MS);
  }, [transitioning]);

  const toggleTheme = () => {
    const newTheme = theme === 'dark' ? 'light' : 'dark';
    setTheme(newTheme);
    // Sync native title bar theme via WebMessage
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    const wv = (window as any).chrome?.webview;
    if (wv) {
      wv.postMessage(JSON.stringify({ type: 'set-theme', theme: newTheme }));
    }
  };

  const backToSessions = useCallback(() => {
    setActiveSession(null);
    if (activeWorkspace) {
      window.history.pushState(null, '', workspacePath(activeWorkspace.id));
    }
  }, [activeWorkspace]);

  if (initializing) {
    return (
      <div className={`theme-${theme} background-canvas h-screen flex items-center justify-center`}>
        <div className="text-sm text-[var(--text-secondary)]">正在恢复工作区...</div>
      </div>
    );
  }

  // ========== workspace_select phase ==========
  if (phase === 'workspace_select') {
    return (
      <div className={`theme-${theme} background-canvas h-screen flex flex-col overflow-hidden`}>
        <div className="flex-1 flex items-center justify-center">
          <div><WorkspaceSelectOverlay onSelect={enterWorkspace} /></div>
        </div>
      </div>
    );
  }

  // ========== normal phase ==========
  return (
    <div
      className={`theme-${theme} background-canvas flex flex-col overflow-hidden`}
      style={scale !== 1 ? {
        position: 'relative',
        transform: `scale(${scale})`,
        transformOrigin: 'top left',
        width: `${100 / scale}vw`,
        height: `${100 / scale}vh`,
      } : { position: 'relative', width: '100vw', height: '100vh' }}
    >
      <AmbientBubbles />

      {/* Header: ← 返回 [StatusPills] ☀主题 ⚙️设置 */}
      <header style={{ position: 'relative', zIndex: 20, height: 40, margin: `10px clamp(10px, 1.4vw, 18px) 0`, display: 'flex', alignItems: 'center', gap: 12 }}>
        {/* ← Back to workspace select (same circle style as theme/settings) */}
        <button
          onClick={exitWorkspace}
          title="返回工作区选择"
          style={{
            width: 40, height: 40, borderRadius: '50%', display: 'flex',
            alignItems: 'center', justifyContent: 'center', flexShrink: 0,
            fontSize: 18, fontWeight: 700,
            background: 'var(--surface)',
            border: '1px solid var(--glass-border-strong)',
            color: 'var(--text-primary)', cursor: 'pointer',
          }}
        >
          ←
        </button>
        <StatusPills />
        <button
          onClick={toggleTheme}
          title="切换主题"
          style={{
            width: 40, height: 40, borderRadius: '50%', display: 'flex',
            alignItems: 'center', justifyContent: 'center', flexShrink: 0,
            fontSize: 18, background: 'var(--surface)',
            border: '1px solid var(--glass-border-strong)',
            color: 'var(--text-primary)', cursor: 'pointer',
          }}
        >
          {theme === 'dark' ? '☀' : '☾'}
        </button>
        <button
          onClick={() => setSettingsOpen(true)}
          title="全局设置"
          style={{
            width: 40, height: 40, borderRadius: '50%', display: 'flex',
            alignItems: 'center', justifyContent: 'center', flexShrink: 0,
            fontSize: 18, background: 'var(--surface)',
            border: '1px solid var(--glass-border-strong)',
            color: 'var(--text-primary)', cursor: 'pointer',
          }}
        >
          ⚙️
        </button>
      </header>

      <main
        ref={workspaceMainRef}
        className={`workspace-main flex-1 min-h-0 overflow-hidden ${resizingLayout ? 'is-resizing' : ''}`}
      >
        <aside className="workspace-tool-pane" aria-label="Tools">
          <ToolPanelWrapper theme={theme} embedded onExpandedChange={setToolPanelExpanded} />
        </aside>
        <section className="workspace-graph-pane" aria-label="Expert workflow">
          <ExpertGraphCanvas
            theme={theme}
            workspaceId={activeWorkspace?.id || ''}
            positionResetVersion={positionResetVersion}
            workflowState={workflowState}
            cssScale={scale}
          />
        </section>
        <div
          className="workspace-pane-resizer"
          role="separator"
          aria-label="调整流程图和右侧窗口宽度"
          aria-orientation="vertical"
          onPointerDown={beginLayoutResize}
        />
        <section
          className="workspace-session-pane"
          aria-label="Current session"
          style={{ width: sessionPaneWidth }}
        >
          <GlassPanel
            theme={theme}
            activeTab={activeTab}
            onTabChange={setActiveTab}
            workspace={activeWorkspace}
            activeSession={activeSession}
            onSelectSession={selectSession}
            onBackToSessions={backToSessions}
            onExitWorkspace={exitWorkspace}
            onWorkflowTaskSelected={handleWorkflowTaskSelected}
            onWorkflowEvents={handleWorkflowEvents}
            onFileDirtyChange={handleFileDirtyChange}
            scale={scale}
          />
        </section>
      </main>

      <SettingsPanel
        show={settingsOpen}
        theme={theme}
        scale={scale}
        onScaleChange={setScale}
        workspace={activeWorkspace}
        onExpertPositionsReset={() => setPositionResetVersion(version => version + 1)}
        onClose={() => setSettingsOpen(false)}
      />
    </div>
  );
}

// ====================================================================
// 工具面板包装器
// ====================================================================
function ToolPanelWrapper({
  theme,
  embedded = false,
  onExpandedChange,
}: {
  theme: 'dark'|'light';
  embedded?: boolean;
  onExpandedChange?: (expanded: boolean) => void;
}) {
  const [tools, setTools] = useState<ToolInfo[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    const load = async () => {
      setLoading(true);
      try {
        const res = await listTools();
        // eslint-disable-next-line @typescript-eslint/no-explicit-any
        const items = ((res as any).items || []).map((item: any) => ({
          ...item,
          enabled: item.enabled != null ? Boolean(item.enabled) : true,
          category: item.group || item.category || 'other',
          params: item.params || {},
        }));
        setTools(items as ToolInfo[]);
      } catch {}
      setLoading(false);
    };
    load();
  }, []);

  return <ToolPanel tools={tools} loading={loading} theme={theme} embedded={embedded} onExpandedChange={onExpandedChange} />;
}

// ====================================================================
// Workspace 选择覆盖层
// ====================================================================
function WorkspaceSelectOverlay({ onSelect }: { onSelect: (ws: WorkspaceRecord) => void }) {
  const [workspaces, setWorkspaces] = useState<WorkspaceRecord[]>([]);
  const [loading, setLoading] = useState(true);
  const [showCreate, setShowCreate] = useState(false);
  const [newName, setNewName] = useState('');
  const [newPath, setNewPath] = useState('');
  const [editingId, setEditingId] = useState<string | null>(null);
  const [editName, setEditName] = useState('');
  const [editPath, setEditPath] = useState('');

  const refresh = useCallback(async () => {
    setLoading(true);
    try { const res = await listWorkspaces(); setWorkspaces(res.items || []); } catch {}
    setLoading(false);
  }, []);

  useEffect(() => { refresh(); }, [refresh]);

  const handleCreate = async () => {
    if (!newName.trim() || !newPath.trim()) return;
    try { await createWorkspace(newName.trim(), newPath.trim()); setNewName(''); setNewPath(''); setShowCreate(false); refresh(); }
    catch { alert('创建工作区失败，请检查路径是否有效'); }
  };

  const startEdit = (ws: WorkspaceRecord) => {
    setEditingId(ws.id);
    setEditName(ws.name || '');
    setEditPath(ws.path || '');
  };

  const saveEdit = async (id: string) => {
    if (!editName.trim() || !editPath.trim()) return;
    try { await updateWorkspace(id, { name: editName.trim(), path: editPath.trim() }); setEditingId(null); refresh(); }
    catch { alert('保存失败'); }
  };

  const handleDelete = async (id: string) => {
    if (!confirm('删除此工作区？')) return;
    try { await deleteWorkspace(id); refresh(); }
    catch { alert('删除失败'); }
  };

  return (
    <div className="glass-panel flex flex-col" style={{ width: 640, maxHeight: '85vh', padding: 40, gap: 24 }}>
      <h1 className="text-xl font-semibold text-[var(--text-primary)] tracking-wide">CodePilot</h1>
      <p className="text-sm text-[var(--text-secondary)]" style={{ marginTop: -16 }}>选择一个工作区开始</p>

      {loading ? (
        <div className="text-sm text-[var(--text-secondary)] p-2">加载中...</div>
      ) : workspaces.length === 0 ? (
        <div className="text-sm text-[var(--text-secondary)] p-2">暂无工作区，创建一个吧</div>
      ) : (
        <div className="flex flex-col gap-2 max-h-72 overflow-y-auto px-1">
          {workspaces.map(ws => editingId === ws.id ? (
            <div key={ws.id} className="flex flex-col gap-2 rounded-xl border border-[var(--accent-light)]" style={{ padding: 10 }}>
              <input value={editName} onChange={e => setEditName(e.target.value)} placeholder="工作区名称"
                style={{ height: 34, padding: '0 12px', fontSize: 13 }}
                className="rounded-lg bg-[var(--bg)] border border-[var(--glass-border-strong)] text-[var(--text-primary)] placeholder:text-[var(--text-secondary)]/50 focus:outline-none focus:border-[var(--accent-light)]" />
              <input value={editPath} onChange={e => setEditPath(e.target.value)} placeholder="工作区路径"
                style={{ height: 34, padding: '0 12px', fontSize: 13 }}
                className="rounded-lg bg-[var(--bg)] border border-[var(--glass-border-strong)] text-[var(--text-primary)] placeholder:text-[var(--text-secondary)]/50 focus:outline-none focus:border-[var(--accent-light)]" />
              <div className="flex gap-2 self-end">
                <button onClick={() => setEditingId(null)} style={{ height: 30, padding: '0 14px', fontSize: 12 }}
                  className="rounded-lg border border-[var(--glass-border-strong)] text-[var(--text-secondary)] hover:text-[var(--text-primary)]">取消</button>
                <button onClick={() => saveEdit(ws.id)} className="btn-primary" style={{ height: 30, padding: '0 14px', fontSize: 12 }}>保存</button>
              </div>
            </div>
          ) : (
            <div key={ws.id} className="flex items-center gap-1.5 group">
              <button
                onClick={() => onSelect(ws)}
                style={{ minHeight: 56, padding: '12px 20px', textAlign: 'left' }}
                className="flex-1 rounded-xl border border-[var(--glass-border)] hover:bg-[var(--accent)]/10 transition-colors"
              >
                <div className="text-sm font-medium text-[var(--text-primary)]">{ws.name}</div>
                <div className="text-xs text-[var(--text-secondary)] mt-1 truncate">{ws.path || '无路径'}</div>
              </button>
              <button onClick={(e) => { e.stopPropagation(); startEdit(ws); }}
                title="编辑" style={{ width: 28, height: 28, flexShrink: 0, borderRadius: '50%',
                  display: 'flex', alignItems: 'center', justifyContent: 'center',
                  fontSize: 13, color: 'var(--text-secondary)', background: 'transparent',
                  border: '1px solid transparent', cursor: 'pointer',
                }}
                className="opacity-0 group-hover:opacity-100 transition-opacity"
                onMouseEnter={e => { e.currentTarget.style.color = 'var(--accent-lighter)'; e.currentTarget.style.borderColor = 'rgba(107,154,239,0.4)'; e.currentTarget.style.background = 'rgba(107,154,239,0.1)'; }}
                onMouseLeave={e => { e.currentTarget.style.color = 'var(--text-secondary)'; e.currentTarget.style.borderColor = 'transparent'; e.currentTarget.style.background = 'transparent'; }}
              >
                ✎
              </button>
              <button onClick={(e) => { e.stopPropagation(); handleDelete(ws.id); }}
                title="删除" style={{ width: 28, height: 28, flexShrink: 0, borderRadius: '50%',
                  display: 'flex', alignItems: 'center', justifyContent: 'center',
                  fontSize: 14, color: 'var(--text-secondary)', background: 'transparent',
                  border: '1px solid transparent', cursor: 'pointer',
                }}
                className="opacity-0 group-hover:opacity-100 transition-opacity"
                onMouseEnter={e => { e.currentTarget.style.color = '#ef4444'; e.currentTarget.style.borderColor = 'rgba(239,68,68,0.4)'; e.currentTarget.style.background = 'rgba(239,68,68,0.1)'; }}
                onMouseLeave={e => { e.currentTarget.style.color = 'var(--text-secondary)'; e.currentTarget.style.borderColor = 'transparent'; e.currentTarget.style.background = 'transparent'; }}
              >
                ✕
              </button>
            </div>
          ))}
        </div>
      )}

      {!showCreate ? (
        <button onClick={() => setShowCreate(true)} className="btn-secondary" style={{ minHeight: 40, fontSize: 14, marginTop: 8 }}>
          + 新建工作区
        </button>
      ) : (
        <div className="flex flex-col gap-3 anim-slide-down" key="ws-create-form" style={{ marginTop: 8 }}>
          <input
            value={newName} onChange={e => setNewName(e.target.value)} placeholder="工作区名称"
            style={{ height: 40, padding: '0 16px', fontSize: 14 }}
            className="rounded-lg bg-[var(--bg)] border border-[var(--glass-border-strong)] text-[var(--text-primary)] placeholder:text-[var(--text-secondary)]/50 focus:outline-none focus:border-[var(--accent-light)]"
          />
          <div className="flex gap-2">
            <input
              value={newPath} onChange={e => setNewPath(e.target.value)} placeholder="工作区路径 (如 D:/Projects)"
              style={{ flex: 1, height: 40, padding: '0 16px', fontSize: 14 }}
              className="rounded-lg bg-[var(--bg)] border border-[var(--glass-border-strong)] text-[var(--text-primary)] placeholder:text-[var(--text-secondary)]/50 focus:outline-none focus:border-[var(--accent-light)]"
            />
            <button onClick={handleCreate} className="btn-primary" style={{ minWidth: 64, height: 40, fontSize: 14 }}>创建</button>
          </div>
          <button onClick={() => setShowCreate(false)} className="text-xs text-[var(--text-secondary)] hover:text-[var(--text-primary)] self-start">
            取消
          </button>
        </div>
      )}
    </div>
  );
}