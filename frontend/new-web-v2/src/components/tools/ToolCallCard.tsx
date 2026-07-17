import { useState } from 'react';

interface Props {
  toolName: string;
  status: 'running' | 'success' | 'failed';
  arguments?: unknown;
  output?: string;
}

const statusColors: Record<string, { bg: string; text: string; border: string; glow: string }> = {
  running: { bg: 'rgba(59,130,246,0.12)', text: '#93c5fd', border: 'rgba(59,130,246,0.25)', glow: '#3b82f6' },
  success: { bg: 'rgba(34,197,94,0.12)', text: '#86efac', border: 'rgba(34,197,94,0.25)', glow: '#22c55e' },
  failed: { bg: 'rgba(239,68,68,0.12)', text: '#fca5a5', border: 'rgba(239,68,68,0.25)', glow: '#ef4444' },
};

export function ToolCallCard({ toolName, status, arguments: args, output }: Props) {
  const [open, setOpen] = useState(false);
  const c = statusColors[status];

  return (
    <div style={{ borderRadius: 12, border: `1.5px solid ${c.border}`, background: c.bg, padding: 12 }}>
      <button type="button" onClick={() => setOpen(v => !v)}
        style={{ display: 'flex', alignItems: 'center', gap: 8, width: '100%', textAlign: 'left', background: 'none', border: 'none', cursor: 'pointer', padding: 0 }}>
        <span style={{ fontSize: 10, color: 'var(--text-secondary)', flexShrink: 0, width: 12 }}>
          {open ? '▾' : '▸'}
        </span>
        <span style={{ width: 10, height: 10, borderRadius: '50%', background: c.glow, flexShrink: 0,
          animation: status === 'running' ? 'pulse 1.5s infinite' : 'none',
          boxShadow: `0 0 8px ${c.glow}66` }} />
        <span style={{ fontSize: 12, fontWeight: 700, color: 'var(--text-primary)', flex: 1, minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
          {toolName}
        </span>
        <span style={{ fontSize: 10, fontWeight: 700, textTransform: 'uppercase', color: c.text, border: `1px solid ${c.border}`, borderRadius: 6, padding: '2px 8px', flexShrink: 0 }}>
          {status}
        </span>
      </button>

      {open && (
        <div style={{ marginTop: 10, display: 'flex', flexDirection: 'column', gap: 8 }}>
          {args !== undefined && (
            <div>
              <div style={{ fontSize: 10, fontWeight: 700, color: 'var(--text-secondary)', marginBottom: 4, textTransform: 'uppercase' }}>Arguments</div>
              <pre style={{ maxHeight: 160, overflow: 'auto', borderRadius: 8, padding: 10, background: 'var(--bg)', border: '1px solid var(--glass-border)', fontSize: 11, lineHeight: 1.5, color: 'var(--text-secondary)', margin: 0, whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>
                {JSON.stringify(args, null, 2)}
              </pre>
            </div>
          )}
          {output && (
            <div>
              <div style={{ fontSize: 10, fontWeight: 700, color: 'var(--text-secondary)', marginBottom: 4, textTransform: 'uppercase' }}>Result</div>
              <pre style={{ maxHeight: 160, overflow: 'auto', borderRadius: 8, padding: 10, background: 'var(--bg)', border: '1px solid var(--glass-border)', fontSize: 11, lineHeight: 1.5, color: 'var(--text-secondary)', margin: 0, whiteSpace: 'pre-wrap', wordBreak: 'break-word' }}>
                {output}
              </pre>
            </div>
          )}
        </div>
      )}
    </div>
  );
}