import { useRef } from 'react';
import type { GlassTab, SessionRecord, TaskEventRecord, TaskRecord, ThemeMode, WorkspaceRecord } from '../types';
import { SessionList } from './SessionList';
import { ChatView } from './ChatView';
import { WorkspaceFiles, type WorkspaceFilesHandle } from './WorkspaceFiles';
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
  onFileDirtyChange?: (dirty: boolean) => void;
  scale: number;
}

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
  onFileDirtyChange,
  scale,
}: Props) {
  const workspaceFilesRef = useRef<WorkspaceFilesHandle>(null);
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
        width: '100%',
        height: '100%',
        minWidth: 0,
        minHeight: 0,
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
          className="flex-1 min-h-0 flex flex-col overflow-hidden"
          style={{ borderTop: '1px solid var(--glass-border-strong)' }}
        >
          <div
            className="min-h-0 flex-1 flex-col overflow-hidden anim-fade-in"
            style={{ display: activeTab === 'chat' ? 'flex' : 'none' }}
          >
            {activeSession && activeSession.workspace_id === workspace?.id ? (
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
            )}
          </div>
          <div
            className="min-h-0 flex-1 flex-col overflow-hidden anim-fade-in"
            style={{ display: activeTab === 'files' ? 'flex' : 'none' }}
          >
            <WorkspaceFiles
              ref={workspaceFilesRef}
              workspace={workspace}
              onExitWorkspace={onExitWorkspace}
              onDirtyChange={onFileDirtyChange}
              theme={theme}
            />
          </div>
        </div>
      </div>
    </GlassWindow>
  );
}
