import { useCallback, useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import type { ToolInfo } from '../types';
import { getConfigTools, setConfigTools } from '../api/api';
import { GlassWindow } from './GlassWindow';

const RISK_COLORS: Record<string, string> = {
  safe: '#22c55e',
  medium: '#f59e0b',
  dangerous: '#ef4444',
};

const RISK_LABELS: Record<string, string> = {
  safe: '安全',
  medium: '中危',
  dangerous: '高危',
};

const PANEL_WIDTH = 240;
const TAB_WIDTH = 28;

// ====================================================================
// Tool Edit Modal
// ====================================================================
interface ToolEditModalProps {
  tool: ToolInfo;
  theme: 'dark' | 'light';
  onClose: () => void;
  onSaved: () => void;
}

function ToolEditModal({ tool, theme, onClose, onSaved }: ToolEditModalProps) {
  const [enabled, setEnabled] = useState(tool.enabled);
  const [params, setParams] = useState<Record<string, { type: string; required: boolean }>>(
    () => {
      const copy: Record<string, { type: string; required: boolean }> = {};
      for (const [key, val] of Object.entries(tool.params || {})) {
        copy[key] = {
          type: String((val as Record<string, unknown>).type || 'string'),
          required: !!(val as Record<string, unknown>).required,
        };
      }
      return copy;
    }
  );
  const [saving, setSaving] = useState(false);
  const [saveError, setSaveError] = useState('');

  const handleSave = useCallback(async () => {
    setSaving(true);
    setSaveError('');
    try {
      const cfg = await getConfigTools();
      const items = (cfg as unknown as { items: ToolInfo[] }).items || [];
      const idx = items.findIndex(t => t.name === tool.name);
      if (idx >= 0) {
        items[idx] = { ...items[idx], enabled, params };
      }
      const updatedCfg: Record<string, unknown> = {};
      for (const t of items) {
        (updatedCfg as Record<string, { enabled: boolean; params: Record<string, unknown> }>)[t.name] = {
          enabled: t.enabled,
          params: (t.params || {}) as Record<string, unknown>,
        };
      }
      await setConfigTools(updatedCfg);
      onSaved();
      onClose();
    } catch {
      setSaveError('保存失败，请稍后重试');
    }
    setSaving(false);
  }, [enabled, params, tool.name, onSaved, onClose]);

  const dotColor = RISK_COLORS[tool.risk_level] || '#6b7280';

  return createPortal(
    <div
      className={`theme-${theme}`}
      style={{
        position: 'fixed', inset: 0, zIndex: 2100,
        display: 'flex', alignItems: 'center', justifyContent: 'center',
        background: 'rgba(0,0,0,0.15)', backdropFilter: 'blur(4px)',
      }}
      onClick={onClose}
    >
      <div
        className="glass-panel flex flex-col"
        style={{
          width: 380, maxHeight: '80vh', maxWidth: 'calc(100vw - 48px)',
          padding: '24px 28px', gap: 18, overflow: 'hidden',
        }}
        onClick={e => e.stopPropagation()}
      >
        <div className="flex items-center gap-2">
          <span style={{ width: 8, height: 8, borderRadius: '50%', background: dotColor, flexShrink: 0 }} />
          <span className="text-sm font-semibold text-[var(--text-primary)] truncate flex-1">{tool.name}</span>
          <button
            onClick={onClose}
            style={{
              width: 28, height: 28, borderRadius: '50%',
              border: '1px solid var(--glass-border-strong)',
              background: 'var(--surface)', color: 'var(--text-secondary)',
              cursor: 'pointer', fontSize: 14,
              display: 'flex', alignItems: 'center', justifyContent: 'center',
              flexShrink: 0,
            }}
          >✕</button>
        </div>

        <div className="text-[11px] text-[var(--text-secondary)] leading-relaxed">
          {tool.description}
        </div>

        <div className="flex items-center gap-3">
          <span className="text-[9px] rounded px-1.5 py-0.5" style={{
            color: dotColor, background: `${dotColor}18`, border: `1px solid ${dotColor}33`,
          }}>
            {RISK_LABELS[tool.risk_level] || tool.risk_level}
          </span>
          <span className="text-[10px] text-[var(--text-secondary)]">{tool.category}</span>
        </div>

        <div className="flex items-center justify-between border-t border-b border-[var(--glass-border)]" style={{ padding: '10px 0' }}>
          <span className="text-xs text-[var(--text-primary)] font-medium">启用</span>
          <button
            onClick={() => setEnabled(e => !e)}
            style={{
              width: 40, height: 22, borderRadius: 11, border: 'none', cursor: 'pointer',
              background: enabled ? 'var(--accent)' : 'var(--glass-border-strong)',
              position: 'relative', transition: 'background 0.2s',
            }}
          >
            <span style={{
              position: 'absolute', top: 2, left: enabled ? 20 : 2,
              width: 18, height: 18, borderRadius: '50%',
              background: '#fff', transition: 'left 0.2s',
              boxShadow: '0 1px 3px rgba(0,0,0,0.2)',
            }} />
          </button>
        </div>

        <div className="flex flex-col gap-2">
          <span className="text-[10px] text-[var(--text-secondary)] font-medium">参数配置</span>
          <div className="flex flex-col gap-2 max-h-48 overflow-y-auto" style={{ paddingRight: 4 }}>
            {Object.keys(params).length === 0 ? (
              <span className="text-[10px] text-[var(--text-secondary)] italic">此工具无配置参数</span>
            ) : (
              Object.entries(params).map(([key, val]) => (
                <div key={key} className="flex items-center gap-2"
                  style={{ padding: '6px 8px', borderRadius: 8, background: 'var(--bg)', border: '1px solid var(--glass-border)' }}
                >
                  <span className="text-[11px] font-medium text-[var(--accent-lighter)] min-w-0 truncate flex-1">{key}</span>
                  <select
                    value={val.type}
                    onChange={e => setParams(p => ({ ...p, [key]: { ...p[key], type: e.target.value } }))}
                    className="text-[10px] rounded px-1 py-0.5"
                    style={{
                      background: 'var(--surface)', color: 'var(--text-primary)',
                      border: '1px solid var(--glass-border)', outline: 'none',
                    }}
                  >
                    <option value="string">string</option>
                    <option value="integer">integer</option>
                    <option value="boolean">boolean</option>
                  </select>
                  <label className="flex items-center gap-1 text-[10px] text-[var(--text-secondary)] cursor-pointer">
                    <input type="checkbox" checked={val.required}
                      onChange={e => setParams(p => ({ ...p, [key]: { ...p[key], required: e.target.checked } }))}
                      style={{ width: 12, height: 12 }} />
                    必填
                  </label>
                </div>
              ))
            )}
          </div>
        </div>

        {saveError && <div className="text-[10px] text-red-400">{saveError}</div>}

        <div className="flex items-center justify-end gap-2" style={{ marginTop: 4 }}>
          <button onClick={onClose}
            style={{
              height: 30, padding: '0 14px', fontSize: 11, borderRadius: 8,
              background: 'var(--surface)', color: 'var(--text-secondary)',
              border: '1px solid var(--glass-border-strong)', cursor: 'pointer',
            }}
          >取消</button>
          <button onClick={handleSave} disabled={saving}
            style={{
              height: 30, padding: '0 16px', fontSize: 11, borderRadius: 8,
              background: 'var(--accent)', color: '#fff', border: 'none', cursor: saving ? 'wait' : 'pointer',
              fontWeight: 600, opacity: saving ? 0.6 : 1,
            }}
          >{saving ? '保存中...' : '保存'}</button>
        </div>
      </div>
    </div>,
    document.body
  );
}

// ====================================================================
// Tool Card (draggable)
// ====================================================================
function ToolCard({ tool, theme, onConfigRefresh }: { tool: ToolInfo; theme: 'dark' | 'light'; onConfigRefresh: () => void }) {
  const [showModal, setShowModal] = useState(false);
  const [isDrag, setIsDrag] = useState(false);
  const dotColor = RISK_COLORS[tool.risk_level] || '#6b7280';

  const handleDragStart = (e: React.DragEvent) => {
    e.dataTransfer.setData('application/tool-name', tool.name);
    e.dataTransfer.effectAllowed = 'copy';
    setIsDrag(true);
  };

  const handleDragEnd = () => setIsDrag(false);

  return (
    <>
      <div
        draggable
        onClick={() => setShowModal(true)}
        onDragStart={handleDragStart}
        onDragEnd={handleDragEnd}
        className="flex items-center gap-2 rounded-lg cursor-grab select-none transition-colors active:cursor-grabbing"
        style={{
          padding: '6px 10px',
          background: isDrag
            ? 'color-mix(in srgb, var(--accent) 18%, transparent)'
            : 'color-mix(in srgb, var(--accent) 5%, transparent)',
          border: `1px solid ${isDrag ? 'var(--accent-light)' : 'var(--glass-border)'}`,
          opacity: isDrag ? 0.6 : 1,
        }}
        onMouseEnter={e => {
          e.currentTarget.style.background = 'color-mix(in srgb, var(--accent) 12%, transparent)';
          e.currentTarget.style.borderColor = 'var(--accent-light)';
        }}
        onMouseLeave={e => {
          e.currentTarget.style.background = isDrag
            ? 'color-mix(in srgb, var(--accent) 18%, transparent)'
            : 'color-mix(in srgb, var(--accent) 5%, transparent)';
          e.currentTarget.style.borderColor = isDrag ? 'var(--accent-light)' : 'var(--glass-border)';
        }}
      >
        <span style={{
          width: 7, height: 7, borderRadius: '50%', background: dotColor,
          flexShrink: 0, boxShadow: `0 0 4px ${dotColor}66`,
        }} />
        <span className="text-[11px] text-[var(--text-primary)] font-medium truncate flex-1">
          {tool.name}
        </span>
        <span style={{
          fontSize: 9, opacity: 0.6, flexShrink: 0,
          color: tool.enabled ? dotColor : 'var(--text-secondary)',
        }}>
          {tool.enabled ? 'ON' : 'OFF'}
        </span>
      </div>

      {showModal && (
        <ToolEditModal
          tool={tool}
          theme={theme}
          onClose={() => setShowModal(false)}
          onSaved={onConfigRefresh}
        />
      )}
    </>
  );
}

// ====================================================================
// Tool Panel (main export)
// ====================================================================
interface Props {
  tools: ToolInfo[];
  loading: boolean;
  theme: 'dark' | 'light';
}

export function ToolPanel({ tools: initialTools, loading: initialLoading, theme }: Props) {
  const [expanded, setExpanded] = useState(false);
  const [tools, setTools] = useState<ToolInfo[]>(initialTools);
  const [loading, setLoading] = useState(initialLoading);
  const [refreshKey, setRefreshKey] = useState(0);

  useEffect(() => {
    setTools(initialTools);
    setLoading(initialLoading);
  }, [initialTools, initialLoading]);

  const handleToggle = useCallback(() => setExpanded(prev => !prev), []);

  const handleConfigRefresh = useCallback(async () => {
    setLoading(true);
    try {
      const { getConfigTools } = await import('../api/api');
      const cfg = await getConfigTools();
      const items = (cfg as unknown as { items: ToolInfo[] }).items || [];
      setTools(items);
    } catch {}
    setLoading(false);
    setRefreshKey(k => k + 1);
  }, []);

  const grouped = tools.reduce<Record<string, ToolInfo[]>>((acc, tool) => {
    const cat = tool.category || 'other';
    if (!acc[cat]) acc[cat] = [];
    acc[cat].push(tool);
    return acc;
  }, {});

  return (
    <GlassWindow
      zIndex={30}
      style={{
        right: 0,
        top: 56,
        bottom: 16,
        transform: expanded ? 'translateX(0)' : `translateX(${PANEL_WIDTH - TAB_WIDTH}px)`,
        transition: 'transform 0.15s ease-out',
        display: 'flex',
      }}
    >
      <div
        onClick={handleToggle}
        style={{
          width: TAB_WIDTH,
          height: '100%',
          flexShrink: 0,
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center',
          gap: 8,
          cursor: 'pointer',
          background: 'var(--surface)',
          backdropFilter: 'blur(20px)',
          border: '1px solid var(--glass-border-strong)',
          borderRight: 'none',
          borderRadius: '16px 0 0 16px',
          writingMode: 'vertical-rl',
          fontSize: 11,
          fontWeight: 600,
          color: 'var(--text-secondary)',
          letterSpacing: 2,
          transition: 'color 0.2s',
        }}
      >
        {expanded ? '✕' : '🛠 工具'}
      </div>

      <div
        className="glass-panel flex flex-col shrink-0 overflow-hidden"
        style={{
          width: PANEL_WIDTH - TAB_WIDTH,
          height: '100%',
          borderTopLeftRadius: 0,
          borderBottomLeftRadius: 0,
          borderLeft: 'none',
          opacity: expanded ? 1 : 0,
          transition: 'opacity 0.12s ease-out',
          pointerEvents: expanded ? 'auto' : 'none',
        }}
      >
        <div
          className="flex items-center shrink-0 border-b border-[var(--glass-border-strong)]"
          style={{ height: 40, padding: '0 14px' }}
        >
          <span className="text-xs font-semibold text-[var(--text-primary)]">🛠 工具列表</span>
          <span className="text-[10px] text-[var(--text-secondary)] ml-auto">{tools.length}</span>
        </div>

        <div
          className="flex-1 overflow-y-auto"
          style={{ padding: '10px 12px', display: 'flex', flexDirection: 'column', gap: 12 }}
        >
          {loading ? (
            <div className="text-[10px] text-[var(--text-secondary)] text-center py-8">加载中...</div>
          ) : tools.length === 0 ? (
            <div className="text-[10px] text-[var(--text-secondary)] text-center py-8">无可用工具</div>
          ) : (
            Object.entries(grouped).map(([cat, catTools]) => (
              <div key={cat}>
                <div className="text-[9px] font-semibold uppercase tracking-wider mb-1.5" style={{ color: 'var(--text-secondary)', paddingLeft: 2 }}>
                  {cat}
                </div>
                <div className="flex flex-col gap-1">
                  {catTools.map(tool => (
                    <ToolCard key={tool.name} tool={tool} theme={theme} onConfigRefresh={handleConfigRefresh} />
                  ))}
                </div>
              </div>
            ))
          )}
        </div>
      </div>
    </GlassWindow>
  );
}