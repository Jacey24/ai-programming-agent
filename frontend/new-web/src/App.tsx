import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { AppPhase, GlassTab, SessionRecord, ThemeMode, ToolInfo, WorkspaceRecord } from './types';
import { listWorkspaces, createWorkspace, updateWorkspace, deleteWorkspace, listTools } from './api/api';
import { GlassPanel } from './components/GlassPanel';
import { StatusPills } from './components/StatusPills';
import { ToolPanel } from './components/ToolPanel';
import { SettingsPanel } from './components/SettingsPanel';
import { ExpertGraphCanvas } from './components/ExpertGraphCanvas';

const PANEL_WIDTH = 420;

/* ################################################################
 * 工作区切换交错动画规范（STAGGER ANIMATION CONVENTION）
 * ################################################################
 * 任何希望参与 workspace_select ↔ normal 阶段切换动画的元素，
 * 必须在其 JSX 上添加 className="stagger-item"。
 * stagger 动画仅控制 opacity（不碰 transform），确保不影响元素
 * 自身的 translate/scale/position 定位。延迟由 nth-child 自动分配。
 * ################################################################ */
// CSS stagger-out 最长延迟 0.11s + 动画持续 0.12s = 0.23s，取 280ms 安全值
const PHASE_TRANSITION_MS = 280;

export default function App() {
  const [theme, setTheme] = useState<ThemeMode>('dark');
  const [phase, setPhase] = useState<AppPhase>('workspace_select');
  const [activeWorkspace, setActiveWorkspace] = useState<WorkspaceRecord | null>(null);
  const [activeSession, setActiveSession] = useState<SessionRecord | null>(null);
  const [activeTab, setActiveTab] = useState<GlassTab>('chat');
  const [vw, setVw] = useState(window.innerWidth);
  const [scale, setScale] = useState(1);
  const [settingsOpen, setSettingsOpen] = useState(false);
  // ★ 阶段过渡锁：过渡期间忽视重复点击
  const [transitioning, setTransitioning] = useState(false);
  const timerRef = useRef<number>(0);
  useEffect(() => {
    const onResize = () => setVw(window.innerWidth);
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, []);

  useEffect(() => () => clearTimeout(timerRef.current), []);

  const enterWorkspace = useCallback((ws: WorkspaceRecord) => {
    if (transitioning) return;
    setTransitioning(true);
    setActiveWorkspace(ws);
    timerRef.current = window.setTimeout(() => {
      setPhase('normal');
      setTransitioning(false);
    }, PHASE_TRANSITION_MS);
  }, [transitioning]);

  const exitWorkspace = useCallback(() => {
    if (transitioning) return;
    setTransitioning(true);
    timerRef.current = window.setTimeout(() => {
      setActiveWorkspace(null);
      setActiveSession(null);
      setPhase('workspace_select');
      setTransitioning(false);
    }, PHASE_TRANSITION_MS);
  }, [transitioning]);

  const toggleTheme = () => setTheme(t => t === 'dark' ? 'light' : 'dark');

  // 水平偏移：减小留白，左右对称且紧凑
  const leftOffset = useMemo(() => Math.max(16, Math.round((vw - PANEL_WIDTH) * 0.03)), [vw]);

    // ========== workspace_select phase ==========
  if (phase === 'workspace_select') {
    return (
      <div className={`theme-${theme} background-canvas h-screen flex items-center justify-center ${transitioning ? 'stagger-fade-out' : 'stagger-fade-in'}`}>
        <div className="stagger-item"><WorkspaceSelectOverlay onSelect={enterWorkspace} /></div>
      </div>
    );
  }

  // ========== normal phase ==========
  return (
    <div
      className={`theme-${theme} background-canvas flex flex-col overflow-hidden ${transitioning ? 'stagger-fade-out' : 'stagger-fade-in'}`}
      style={{
        position: 'relative',
        transform: `scale(${scale})`,
        transformOrigin: 'top left',
        width: `${100 / scale}vw`,
        height: `${100 / scale}vh`,
      }}
    >
      {/* Expert 画布 — 背景层，在所有面板之下 */}
      <ExpertGraphCanvas theme={theme} />

      {/* 顶部栏 — StatusPills + 圆形按钮 */}
      <header
        className="shrink-0 flex items-center stagger-item"
        style={{ height: 56, paddingLeft: leftOffset, paddingRight: `max(24px, ${leftOffset}px)`, gap: 12, position: 'relative', zIndex: 20 }}
      >
        <StatusPills />
        {/* 主题切换 — 圆形仅图标 */}
        <button
          onClick={toggleTheme}
          title="切换主题"
          style={{
            width: 40,
            height: 40,
            borderRadius: '50%',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            flexShrink: 0,
            fontSize: 18,
            background: 'var(--surface)',
            border: '1px solid var(--glass-border-strong)',
            color: 'var(--text-primary)',
            cursor: 'pointer',
          }}
        >
          {theme === 'dark' ? '☀' : '☾'}
        </button>
        {/* 全局设置 — 圆形 */}
        <button
          onClick={() => setSettingsOpen(true)}
          title="全局设置"
          style={{
            width: 40,
            height: 40,
            borderRadius: '50%',
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'center',
            flexShrink: 0,
            fontSize: 18,
            background: 'var(--surface)',
            border: '1px solid var(--glass-border-strong)',
            color: 'var(--text-primary)',
            cursor: 'pointer',
          }}
        >
          ⚙️
        </button>
      </header>

      {/* 装饰元素 — 最底层 (z-index: 0，仅作毛玻璃模糊源) */}
      {/* 元素1：彩虹渐变矩形 */}
      <div className="stagger-item" style={{
        position: 'absolute', top: '40%', left: '50%',
        transform: 'translate(-50%, -50%)',
        width: 300, height: 200, zIndex: 0,
        borderRadius: 32,
        background: theme === 'dark'
          ? 'linear-gradient(135deg, #ff6b6b 0%, #ffa726 25%, #4ecdc4 50%, #6c5ce7 75%, #e040fb 100%)'
          : 'linear-gradient(135deg, #ff7664 0%, #ffb344 25%, #54e3c7 50%, #7b6cf6 100%, #f062e8 100%)',
        opacity: 0.85,
        pointerEvents: 'none',
      }} />
      {/* 元素2：大号装饰圆形 — 鲜艳色彩，边界分明，适合验证模糊 */}
      <div className="stagger-item" style={{
        position: 'absolute', top: '28%', left: '32%',
        width: 220, height: 220, zIndex: 0,
        borderRadius: '50%',
        background: theme === 'dark'
          ? 'radial-gradient(circle, #ff6b6b 0%, #e040fb 100%)'
          : 'radial-gradient(circle, #ff7664 0%, #f062e8 100%)',
        opacity: 0.7,
        pointerEvents: 'none',
      }} />
      {/* 元素3：第二装饰圆 */}
      <div className="stagger-item" style={{
        position: 'absolute', top: '55%', left: '62%',
        width: 180, height: 180, zIndex: 0,
        borderRadius: '50%',
        background: theme === 'dark'
          ? 'radial-gradient(circle, #4ecdc4 0%, #6c5ce7 100%)'
          : 'radial-gradient(circle, #54e3c7 0%, #7b6cf6 100%)',
        opacity: 0.65,
        pointerEvents: 'none',
      }} />
      {/* 元素4：条纹矩形 — 有清晰边缘供验证模糊 */}
      <div className="stagger-item" style={{
        position: 'absolute', top: '48%', left: '28%',
        width: 160, height: 100, zIndex: 0,
        borderRadius: 16,
        background: `repeating-linear-gradient(
          45deg,
          transparent,
          transparent 10px,
          ${theme === 'dark' ? 'rgba(255,255,255,0.15)' : 'rgba(0,0,0,0.10)'} 10px,
          ${theme === 'dark' ? 'rgba(255,255,255,0.15)' : 'rgba(0,0,0,0.10)'} 20px
        )`,
        border: `2px solid ${theme === 'dark' ? 'rgba(255,255,255,0.2)' : 'rgba(0,0,0,0.15)'}`,
        pointerEvents: 'none',
      }} />

      {/* 主区域 — z-index: 10，位于装饰元素之上、header/toolpanel 之下 */}
      <main
        className="flex-1 min-h-0 overflow-hidden stagger-item"
        style={{ position: 'relative', zIndex: 10, paddingTop: 16, paddingLeft: leftOffset, paddingRight: leftOffset, pointerEvents: 'none' }}
      >
        <GlassPanel
          theme={theme}
          activeTab={activeTab}
          onTabChange={setActiveTab}
          workspace={activeWorkspace}
          activeSession={activeSession}
          onSelectSession={setActiveSession}
          onBackToSessions={() => setActiveSession(null)}
          onExitWorkspace={exitWorkspace}
          scale={scale}
        />
      </main>

      {/* 右侧工具面板 — position: fixed 独立定位 */}
      <ToolPanelWrapper theme={theme} />

      {/* 全局设置面板 — 最顶层 Portal */}
      <SettingsPanel
        show={settingsOpen}
        theme={theme}
        scale={scale}
        onScaleChange={setScale}
        onClose={() => setSettingsOpen(false)}
      />
    </div>
  );
}

// ====================================================================
// 工具面板包装器（加载工具列表）
// ====================================================================
function ToolPanelWrapper({ theme }: { theme: 'dark' | 'light' }) {
  const [tools, setTools] = useState<ToolInfo[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    const load = async () => {
      setLoading(true);
      try { const res = await listTools(); setTools(res.items || []); } catch {}
      setLoading(false);
    };
    load();
  }, []);

  return <ToolPanel tools={tools} loading={loading} theme={theme} />;
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
    try { const ws = await createWorkspace(newName.trim(), newPath.trim()); setNewName(''); setNewPath(''); setShowCreate(false); refresh(); }
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
