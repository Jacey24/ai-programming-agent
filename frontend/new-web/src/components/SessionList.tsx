import { useEffect, useRef, useState } from 'react';
import type { SessionRecord } from '../types';
import { listSessionsByWorkspace, createSession, deleteSession } from '../api/api';

interface Props {
  workspaceId: string;
  onSelect: (s: SessionRecord) => void;
}

export function SessionList({ workspaceId, onSelect }: Props) {
  const [sessions, setSessions] = useState<SessionRecord[]>([]);
  const [loading, setLoading] = useState(true);
  const [showNew, setShowNew] = useState(false);
  const [newTitle, setNewTitle] = useState('');
  const requestId = useRef(0);

  useEffect(() => {
    if (!workspaceId) return;
    const currentRequest = ++requestId.current;
    const controller = new AbortController();
    const load = async () => {
      setLoading(true);
      try {
        const res = await listSessionsByWorkspace(workspaceId, controller.signal);
        if (currentRequest === requestId.current) {
          setSessions((res.items || []).filter(s => s.workspace_id === workspaceId));
        }
      } catch {}
      if (currentRequest === requestId.current) setLoading(false);
    };
    void load();
    return () => {
      controller.abort();
      ++requestId.current;
    };
  }, [workspaceId]);

  const handleCreate = async () => {
    if (!newTitle.trim()) return;
    try {
      const s = await createSession(newTitle.trim(), workspaceId);
      if (s.workspace_id !== workspaceId) throw new Error('Session workspace mismatch');
      setNewTitle('');
      setShowNew(false);
      onSelect(s);
    }
    catch { alert('创建会话失败'); }
  };

  return (
    <div className="flex flex-col h-full" style={{ padding: 24, gap: 20 }}>
      {/* 标题栏 */}
      <div className="flex items-center justify-between">
        <h2 className="text-sm font-medium text-[var(--text-primary)]">会话列表</h2>
        <button
          onClick={() => setShowNew(v => !v)}
          className="btn-secondary"
          style={{ minWidth: 64, height: 32, fontSize: 12, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '0 12px' }}
        >
          {showNew ? '取消' : '+ 新建'}
        </button>
      </div>

      {showNew && (
        <div className="flex anim-slide-down" key="new-session-form" style={{ gap: 8 }}>
          <input
            value={newTitle} onChange={e => setNewTitle(e.target.value)}
            onKeyDown={e => e.key === 'Enter' && handleCreate()}
            placeholder="会话标题..." autoFocus
            style={{ flex: 1, height: 36, padding: '0 12px', fontSize: 12 }}
            className="rounded-lg bg-[var(--bg)] border border-[var(--glass-border-strong)] text-[var(--text-primary)] placeholder:text-[var(--text-secondary)]/50 focus:outline-none focus:border-[var(--accent-light)]"
          />
          <button onClick={handleCreate} className="btn-primary" style={{ minWidth: 56, height: 36, fontSize: 12 }}>创建</button>
        </div>
      )}

      {/* 列表 */}
      <div className="flex-1 overflow-y-auto" style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
        {loading ? (
          <div className="text-xs text-[var(--text-secondary)]" style={{ padding: '8px 0' }}>加载中...</div>
        ) : sessions.length === 0 ? (
          <div className="text-xs text-[var(--text-secondary)]" style={{ padding: '8px 0' }}>暂无会话</div>
        ) : (
          sessions.map(s => (
            <div key={s.id} className="flex items-center gap-1.5 group">
              <button
                onClick={() => s.workspace_id === workspaceId && onSelect(s)}
                style={{ minHeight: 52, padding: '10px 16px', textAlign: 'left' }}
                className="flex-1 rounded-xl border border-transparent hover:border-[var(--glass-border)] hover:bg-[var(--accent)]/5 transition-colors"
              >
                <div className="text-xs font-medium text-[var(--text-primary)] truncate">{s.title || '未命名会话'}</div>
                <div className="text-[10px] text-[var(--text-secondary)]" style={{ marginTop: 4 }}>
                  {s.created_at ? new Date(s.created_at).toLocaleString() : ''}
                </div>
              </button>
              <button
                onClick={async (e) => {
                  e.stopPropagation();
                  if (!confirm('删除此会话？')) return;
                  try {
                    await deleteSession(s.id);
                    setSessions(prev => prev.filter(x => x.id !== s.id));
                  } catch { alert('删除失败'); }
                }}
                title="删除会话"
                className="opacity-0 group-hover:opacity-100 transition-opacity"
                style={{
                  width: 28, height: 28, flexShrink: 0,
                  borderRadius: '50%',
                  display: 'flex', alignItems: 'center', justifyContent: 'center',
                  fontSize: 14,
                  color: 'var(--text-secondary)',
                  background: 'transparent',
                  border: '1px solid transparent',
                  cursor: 'pointer',
                }}
                onMouseEnter={e => { e.currentTarget.style.color = '#ef4444'; e.currentTarget.style.borderColor = 'rgba(239,68,68,0.4)'; e.currentTarget.style.background = 'rgba(239,68,68,0.1)'; }}
                onMouseLeave={e => { e.currentTarget.style.color = 'var(--text-secondary)'; e.currentTarget.style.borderColor = 'transparent'; e.currentTarget.style.background = 'transparent'; }}
              >
                ✕
              </button>
            </div>
          ))
        )}
      </div>
    </div>
  );
}
