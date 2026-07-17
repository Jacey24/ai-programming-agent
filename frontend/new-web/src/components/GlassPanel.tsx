import { useCallback, useEffect, useRef, useState } from 'react';
import type { GlassTab, SessionRecord, TaskEventRecord, TaskRecord, ThemeMode, WorkspaceRecord } from '../types';
import { SessionList } from './SessionList';
import { ChatView } from './ChatView';
import { FileTree } from './FileTree';
import { GlassWindow } from './GlassWindow';

interface Props {
  theme: ThemeMode;
  activeTab: GlassTab;
  onTabChange: (tab: GlassTab) => void;
  workspace: WorkspaceRecord | null;
  activeSession: SessionRecord | null;
  onSelectSession: (s: SessionRecord) => void;
  onBackToSessions: () => void;
  onExitWorkspace: () => void;
  onWorkflowTaskSelected: (task: TaskRecord | null) => void;
  onWorkflowEvents: (task: TaskRecord, events: TaskEventRecord[]) => void;
  scale: number;
}

const MIN_W = 300;
const MAX_W = 720;
const MIN_H = 260;

export function GlassPanel({
  theme,
  activeTab,
  onTabChange,
  workspace,
  activeSession,
  onSelectSession,
  onBackToSessions,
  onExitWorkspace,
  onWorkflowTaskSelected,
  onWorkflowEvents,
  scale,
}: Props) {
  const [size, setSize] = useState(() => {
    const vw = window.innerWidth / scale;
    const vh = window.innerHeight / scale;
    const maxH = vh - (72 + 16) / scale;
    const maxW = Math.min(MAX_W, vw - 48 / scale);
    return {
      w: Math.max(MIN_W, Math.min(maxW, 420)),
      h: Math.max(MIN_H, Math.min(maxH, 520)),
    };
  });
  const dragging = useRef(false);
  const dragStart = useRef({ x: 0, y: 0, w: 420, h: 520 });

  // --- manual resize via bottom-right handle ---
  const handleMouseDown = useCallback(
    (e: React.MouseEvent) => {
      e.preventDefault();
      e.stopPropagation();
      dragging.current = true;
      dragStart.current = { x: e.clientX, y: e.clientY, w: size.w, h: size.h };
    },
    [size],
  );

  useEffect(() => {
    const handleMove = (e: MouseEvent) => {
      if (!dragging.current) return;
      const dx = (e.clientX - dragStart.current.x) / scale;
      const dy = (e.clientY - dragStart.current.y) / scale;
      const vw = window.innerWidth / scale;
      const vh = window.innerHeight / scale;
      // 顶部: header(56) + main paddingTop(16) = 72, 底部留白对齐 ToolPanel 的 bottom:16
      const maxH = vh - (72 + 16) / scale;
      const maxW = Math.min(MAX_W, vw - 48 / scale);
      setSize({
        w: Math.max(MIN_W, Math.min(maxW, dragStart.current.w + dx)),
        h: Math.max(MIN_H, Math.min(maxH, dragStart.current.h + dy)),
      });
    };
    const handleUp = () => {
      dragging.current = false;
    };
    document.addEventListener('mousemove', handleMove);
    document.addEventListener('mouseup', handleUp);
    return () => {
      document.removeEventListener('mousemove', handleMove);
      document.removeEventListener('mouseup', handleUp);
    };
  }, [scale]);

  // guard: keep size within bounds when scale or viewport changes
  useEffect(() => {
    const applyBounds = () => {
      const vw = window.innerWidth / scale;
      const vh = window.innerHeight / scale;
      const maxH = vh - (72 + 16) / scale;
      const maxW = Math.min(MAX_W, vw - 48 / scale);
      setSize((prev) => ({
        w: Math.max(MIN_W, Math.min(maxW, prev.w)),
        h: Math.max(MIN_H, Math.min(maxH, prev.h)),
      }));
    };
    applyBounds();
    window.addEventListener('resize', applyBounds);
    return () => window.removeEventListener('resize', applyBounds);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [scale]);

  // 统一玻璃面板样式参数（与 glass-panel CSS 类保持一致）
  const glassStyle = {
    background: 'color-mix(in srgb, var(--bg) 45%, transparent)',
    border: '1.5px solid var(--glass-border-strong)',
    borderRadius: 16,
    boxShadow: 'inset 0 1px 0 rgba(255,255,255,0.10), inset 0 -1px 0 rgba(0,0,0,0.08), 0 12px 48px var(--shadow)',
  };

  return (
    <GlassWindow
      zIndex={10}
      position="relative"
      pointerEvents="auto"
      style={{
        width: size.w,
        height: size.h,
        display: 'flex',
        flexDirection: 'column',
        ...glassStyle,
      }}
    >
      {/* inner container — flex column, relative for resize handle */}
      <div style={{
        position: 'relative',
        flex: 1,
        display: 'flex',
        flexDirection: 'column',
        overflow: 'hidden',
        borderRadius: 16,
      }}>
        {/* Tab bar — 浏览器风格，顶部圆角 */}
        <div
          className="flex shrink-0 items-end"
          style={{
            padding: `${16 / scale}px ${16 / scale}px 0 ${16 / scale}px`,
            gap: `${2 / scale}px`,
          }}
        >
          {/* ← back button */}
          <button
            onClick={onExitWorkspace}
            title="切换工作区"
            style={{
              width: 34 / scale,
              height: 34 / scale,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              fontSize: 16 / scale,
              fontWeight: 700,
              color: 'var(--text-secondary)',
              background: 'transparent',
              border: 'none',
              borderRadius: 8 / scale,
              cursor: 'pointer',
              marginRight: 6 / scale,
              marginBottom: 0,
              flexShrink: 0,
            }}
            onMouseEnter={(e) => {
              e.currentTarget.style.color = 'var(--text-primary)';
              e.currentTarget.style.background = 'color-mix(in srgb, var(--accent) 12%, transparent)';
            }}
            onMouseLeave={(e) => {
              e.currentTarget.style.color = 'var(--text-secondary)';
              e.currentTarget.style.background = 'transparent';
            }}
          >
            ←
          </button>

          {/* Chat tab */}
          <div
            onClick={() => onTabChange('chat')}
            style={{
              flex: 1,
              height: 38 / scale,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              fontSize: 13 / scale,
              fontWeight: 600,
              letterSpacing: '0.02em',
              cursor: 'pointer',
              userSelect: 'none',
              borderRadius: `${10 / scale}px ${10 / scale}px 0 0`,
              background:
                activeTab === 'chat'
                  ? 'var(--surface)'
                  : 'color-mix(in srgb, var(--surface) 40%, transparent)',
              color: activeTab === 'chat' ? 'var(--text-primary)' : 'var(--text-secondary)',
              borderTop: activeTab === 'chat' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
              borderLeft: activeTab === 'chat' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
              borderRight: activeTab === 'chat' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
              transition: 'all 0.15s',
            }}
          >
            💬 对话
          </div>

          {/* Files tab */}
          <div
            onClick={() => onTabChange('files')}
            style={{
              flex: 1,
              height: 38 / scale,
              display: 'flex',
              alignItems: 'center',
              justifyContent: 'center',
              fontSize: 13 / scale,
              fontWeight: 600,
              letterSpacing: '0.02em',
              cursor: 'pointer',
              userSelect: 'none',
              borderRadius: `${10 / scale}px ${10 / scale}px 0 0`,
              background:
                activeTab === 'files'
                  ? 'var(--surface)'
                  : 'color-mix(in srgb, var(--surface) 40%, transparent)',
              color: activeTab === 'files' ? 'var(--text-primary)' : 'var(--text-secondary)',
              borderTop: activeTab === 'files' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
              borderLeft: activeTab === 'files' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
              borderRight: activeTab === 'files' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
              transition: 'all 0.15s',
            }}
          >
            📁 文件
          </div>
        </div>

        {/* Content area */}
        <div
          key={activeTab}
          className="flex-1 min-h-0 flex flex-col overflow-hidden anim-fade-in"
          style={{ borderTop: '1px solid var(--glass-border-strong)' }}
        >
          {activeTab === 'chat' ? (
            activeSession && activeSession.workspace_id === workspace?.id ? (
              <ChatView
                workspaceId={workspace?.id || ''}
                session={activeSession}
                onBack={onBackToSessions}
                onWorkflowTaskSelected={onWorkflowTaskSelected}
                onWorkflowEvents={onWorkflowEvents}
              />
            ) : (
              <SessionList
                workspaceId={workspace?.id || ''}
                onSelect={onSelectSession}
              />
            )
          ) : (
            <FileTree
              workspace={workspace}
              onExitWorkspace={onExitWorkspace}
              theme={theme}
            />
          )}
        </div>

        {/* Manual resize handle — bottom-right corner */}
        <div
          onMouseDown={handleMouseDown}
          style={{
            position: 'absolute',
            right: 2,
            bottom: 2,
            width: 18,
            height: 18,
            cursor: 'nwse-resize',
            zIndex: 15,
            pointerEvents: 'auto',
            display: 'flex',
            alignItems: 'flex-end',
            justifyContent: 'flex-end',
            borderRadius: '0 0 14px 0',
          }}
        >
          {/* Visual grip lines */}
          <svg
            width={14}
            height={14}
            viewBox="0 0 16 16"
            style={{ display: 'block', opacity: 0.45 }}
          >
            <line x1={14} y1={4} x2={4} y2={14} stroke="var(--text-secondary)" strokeWidth={1} />
            <line x1={14} y1={8} x2={8} y2={14} stroke="var(--text-secondary)" strokeWidth={1} />
            <line x1={14} y1={12} x2={12} y2={14} stroke="var(--text-secondary)" strokeWidth={1} />
          </svg>
        </div>
      </div>
    </GlassWindow>
  );
}
