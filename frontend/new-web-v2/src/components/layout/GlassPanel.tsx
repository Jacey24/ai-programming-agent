import type { GlassTab, SessionRecord, ThemeMode, WorkspaceRecord } from '../../types';
import { SessionList } from '../session/SessionList';
import { ChatView } from '../chat/ChatView';
import { FileTree } from '../files/FileTree';
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
  scale: number;
}

export function GlassPanel({
  theme, activeTab, onTabChange, workspace, activeSession,
  onSelectSession, onBackToSessions, onExitWorkspace, scale,
}: Props) {
  return (
    <GlassWindow zIndex={10} position="relative" pointerEvents="auto" style={{
      width: 420 / scale, height: 520 / scale,
      minWidth: 300 / scale,
      maxWidth: `min(${720 / scale}px, calc(${100 / scale}vw - ${48 / scale}px))`,
      minHeight: 260 / scale,
      maxHeight: `calc(${100 / scale}vh - ${96 / scale}px)`,
      resize: 'both',
    }}>
      <div className="glass-panel flex flex-col" style={{ flex: 1 }}>
        <div className="flex shrink-0 items-end" style={{
          padding: `${16 / scale}px ${16 / scale}px 0 ${16 / scale}px`,
          gap: `${2 / scale}px`,
        }}>
          <button onClick={onExitWorkspace} title="切换工作区"
            style={{
              width: 34 / scale, height: 34 / scale,
              display: 'flex', alignItems: 'center', justifyContent: 'center',
              fontSize: 16 / scale, fontWeight: 700,
              color: 'var(--text-secondary)', background: 'transparent',
              border: 'none', borderRadius: 8 / scale, cursor: 'pointer',
              marginRight: 6 / scale, marginBottom: 0, flexShrink: 0,
            }}
            onMouseEnter={e => { e.currentTarget.style.color = 'var(--text-primary)'; e.currentTarget.style.background = 'color-mix(in srgb, var(--accent) 12%, transparent)'; }}
            onMouseLeave={e => { e.currentTarget.style.color = 'var(--text-secondary)'; e.currentTarget.style.background = 'transparent'; }}
          >←</button>

          <div onClick={() => onTabChange('chat')} style={{
            flex: 1, height: 38 / scale, display: 'flex', alignItems: 'center',
            justifyContent: 'center', fontSize: 13 / scale, fontWeight: 600,
            letterSpacing: '0.02em', cursor: 'pointer', userSelect: 'none',
            borderRadius: `${10 / scale}px ${10 / scale}px 0 0`,
            background: activeTab === 'chat' ? 'var(--surface)' : 'color-mix(in srgb, var(--surface) 40%, transparent)',
            color: activeTab === 'chat' ? 'var(--text-primary)' : 'var(--text-secondary)',
            borderTop: activeTab === 'chat' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
            borderLeft: activeTab === 'chat' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
            borderRight: activeTab === 'chat' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
            transition: 'all 0.15s',
          }}>💬 对话</div>

          <div onClick={() => onTabChange('files')} style={{
            flex: 1, height: 38 / scale, display: 'flex', alignItems: 'center',
            justifyContent: 'center', fontSize: 13 / scale, fontWeight: 600,
            letterSpacing: '0.02em', cursor: 'pointer', userSelect: 'none',
            borderRadius: `${10 / scale}px ${10 / scale}px 0 0`,
            background: activeTab === 'files' ? 'var(--surface)' : 'color-mix(in srgb, var(--surface) 40%, transparent)',
            color: activeTab === 'files' ? 'var(--text-primary)' : 'var(--text-secondary)',
            borderTop: activeTab === 'files' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
            borderLeft: activeTab === 'files' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
            borderRight: activeTab === 'files' ? '1px solid var(--glass-border-strong)' : '1px solid transparent',
            transition: 'all 0.15s',
          }}>📁 文件</div>
        </div>

        <div key={activeTab} className="flex-1 min-h-0 flex flex-col overflow-hidden anim-fade-in"
          style={{ borderTop: '1px solid var(--glass-border-strong)' }}>
          {activeTab === 'chat' ? (
            activeSession ? (
              <ChatView workspaceId={workspace?.id || ''} session={activeSession} onBack={onBackToSessions} />
            ) : (
              <SessionList workspaceId={workspace?.id || ''} onSelect={onSelectSession} />
            )
          ) : (
            <FileTree workspace={workspace} onExitWorkspace={onExitWorkspace} theme={theme} />
          )}
        </div>
      </div>
    </GlassWindow>
  );
}