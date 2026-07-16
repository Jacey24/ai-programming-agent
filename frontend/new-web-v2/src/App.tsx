import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import type { AppPhase, GlassTab, SessionRecord, ThemeMode, WorkspaceRecord } from './types';
import { listTools } from './api/api';
import { ErrorBoundary } from './components/common/ErrorBoundary';
import { GlassPanel } from './components/layout/GlassPanel';
import { StatusPills } from './components/status/StatusPills';
import { ToolPanel } from './components/tools/ToolPanel';
import { SettingsPanel } from './components/settings/SettingsPanel';
import { ExpertGraphCanvas } from './components/experts/ExpertGraphCanvas';
import { WorkspaceSelectOverlay } from './components/workspace/WorkspaceSelectOverlay';

const PANEL_WIDTH = 420;
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

  const leftOffset = useMemo(
    () => Math.max(16, Math.round((vw - PANEL_WIDTH) * 0.03)),
    [vw]);

  // ========== workspace_select phase ==========
  if (phase === 'workspace_select') {
    return (
      <div className={`theme-${theme} background-canvas h-screen flex items-center justify-center ${transitioning ? 'stagger-fade-out' : 'stagger-fade-in'}`}>
        <div className="stagger-item">
          <ErrorBoundary>
            <WorkspaceSelectOverlay onSelect={enterWorkspace} />
          </ErrorBoundary>
        </div>
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
      <ErrorBoundary>
        <ExpertGraphCanvas theme={theme} />
      </ErrorBoundary>

      <header
        className="shrink-0 flex items-center stagger-item"
        style={{ height: 56, paddingLeft: leftOffset, paddingRight: `max(24px, ${leftOffset}px)`, gap: 12, position: 'relative', zIndex: 20 }}
      >
        <ErrorBoundary>
          <StatusPills />
        </ErrorBoundary>
        <button
          onClick={toggleTheme} title="切换主题"
          style={{ width: 40, height: 40, borderRadius: '50%', display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0, fontSize: 18, background: 'var(--surface)', border: '1px solid var(--glass-border-strong)', color: 'var(--text-primary)', cursor: 'pointer' }}
        >
          {theme === 'dark' ? '☀' : '☾'}
        </button>
        <button
          onClick={() => setSettingsOpen(true)} title="全局设置"
          style={{ width: 40, height: 40, borderRadius: '50%', display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0, fontSize: 18, background: 'var(--surface)', border: '1px solid var(--glass-border-strong)', color: 'var(--text-primary)', cursor: 'pointer' }}
        >
          ⚙️
        </button>
      </header>

      {/* Decorative elements */}
      <div className="stagger-item" style={{ position: 'absolute', top: '40%', left: '50%', transform: 'translate(-50%, -50%)', width: 300, height: 200, zIndex: 0, borderRadius: 32, background: theme === 'dark' ? 'linear-gradient(135deg, #ff6b6b 0%, #ffa726 25%, #4ecdc4 50%, #6c5ce7 75%, #e040fb 100%)' : 'linear-gradient(135deg, #ff7664 0%, #ffb344 25%, #54e3c7 50%, #7b6cf6 100%, #f062e8 100%)', opacity: 0.85, pointerEvents: 'none' }} />
      <div className="stagger-item" style={{ position: 'absolute', top: '28%', left: '32%', width: 220, height: 220, zIndex: 0, borderRadius: '50%', background: theme === 'dark' ? 'radial-gradient(circle, #ff6b6b 0%, #e040fb 100%)' : 'radial-gradient(circle, #ff7664 0%, #f062e8 100%)', opacity: 0.7, pointerEvents: 'none' }} />
      <div className="stagger-item" style={{ position: 'absolute', top: '55%', left: '62%', width: 180, height: 180, zIndex: 0, borderRadius: '50%', background: theme === 'dark' ? 'radial-gradient(circle, #4ecdc4 0%, #6c5ce7 100%)' : 'radial-gradient(circle, #54e3c7 0%, #7b6cf6 100%)', opacity: 0.65, pointerEvents: 'none' }} />
      <div className="stagger-item" style={{ position: 'absolute', top: '48%', left: '28%', width: 160, height: 100, zIndex: 0, borderRadius: 16, background: `repeating-linear-gradient(45deg, transparent, transparent 10px, ${theme === 'dark' ? 'rgba(255,255,255,0.15)' : 'rgba(0,0,0,0.10)'} 10px, ${theme === 'dark' ? 'rgba(255,255,255,0.15)' : 'rgba(0,0,0,0.10)'} 20px)`, border: `2px solid ${theme === 'dark' ? 'rgba(255,255,255,0.2)' : 'rgba(0,0,0,0.15)'}`, pointerEvents: 'none' }} />

      <main
        className="flex-1 min-h-0 overflow-hidden stagger-item"
        style={{ position: 'relative', zIndex: 10, paddingTop: 16, paddingLeft: leftOffset, paddingRight: leftOffset, pointerEvents: 'none' }}
      >
        <ErrorBoundary>
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
        </ErrorBoundary>
      </main>

      <ErrorBoundary>
        <ToolPanelWrapper theme={theme} />
      </ErrorBoundary>

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
// Tool Panel Wrapper — loads tools independently
// ====================================================================
function ToolPanelWrapper({ theme }: { theme: 'dark' | 'light' }) {
  const [tools, setTools] = useState<import('./types').ToolInfo[]>([]);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    const load = async () => {
      setLoading(true);
      try { const res = await listTools(); setTools(res.items || []); } catch { /* empty */ }
      setLoading(false);
    };
    load();
  }, []);

  return <ToolPanel tools={tools} loading={loading} theme={theme} />;
}