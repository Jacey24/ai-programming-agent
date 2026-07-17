import React, { useCallback, useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import type { LlmProvider } from '../types';
import {
  testLlmConnection,
  listLlmProviders, addLlmProvider, deleteLlmProvider,
  getConfigLlmLocal, setConfigLlmLocal,
  getConfigWorkspace, setConfigWorkspace,
  getConfigLogging, setConfigLogging,
  getExpertLlmDefaults, setExpertLlmDefaults,
  saveExpertGraphPositions,
} from '../api/api';

interface Props {
  show: boolean;
  theme: 'dark' | 'light';
  scale: number;
  onScaleChange: (v: number) => void;
  onClose: () => void;
}

export function SettingsPanel({ show, theme, scale, onScaleChange, onClose }: Props) {
  if (!show) return null;

  return createPortal(
    <div
      className={`theme-${theme} anim-backdrop-in`}
      style={{
        position: 'fixed', inset: 0, zIndex: 2000,
        display: 'flex', alignItems: 'flex-start', justifyContent: 'center',
        background: 'rgba(0,0,0,0.15)', backdropFilter: 'blur(4px)',
      }}
      onClick={onClose}
    >
      <div
        className="glass-panel flex flex-col anim-modal-pop"
        style={{
          marginTop: 80, width: 420, maxHeight: '82vh',
          maxWidth: 'calc(100vw - 48px)',
          padding: '24px 28px', gap: 16, overflow: 'hidden',
        }}
        onClick={e => e.stopPropagation()}
      >
        <div className="flex items-center justify-between shrink-0">
          <span className="text-sm font-semibold text-[var(--text-primary)]">⚙️ 全局设置</span>
          <button onClick={onClose}
            style={{ width: 28, height: 28, borderRadius: '50%', border: '1px solid var(--glass-border-strong)',
              background: 'var(--surface)', color: 'var(--text-secondary)', cursor: 'pointer', fontSize: 14,
              display: 'flex', alignItems: 'center', justifyContent: 'center' }}
          >✕</button>
        </div>

        <div className="overflow-y-auto flex flex-col gap-16" style={{ paddingRight: 4 }}>
          {/* ── 缩放 ── */}
          <Section title="界面缩放">
            <div className="flex items-center justify-between">
              <span className="text-[10px] text-[var(--text-secondary)]">{Math.round(scale * 100)}%</span>
            </div>
            <input type="range" min="75" max="125" step="1"
              value={Math.round(scale * 100)}
              onChange={e => onScaleChange(Number(e.target.value) / 100)}
              style={{ width: '100%', height: 4, appearance: 'none', borderRadius: 2,
                background: 'color-mix(in srgb, var(--accent) 25%, transparent)',
                outline: 'none', cursor: 'pointer' }}
            />
          </Section>

          {/* ── Debug: Reset Expert Positions ── */}
          <DebugResetPositions />

          {/* ── LLM ── */}
          <LlmSection />

          {/* ── LLM Providers ── */}
          <LlmProvidersSection />

          {/* ── API Key ── */}
          <ApiKeySection />

          {/* ── Workspace ── */}
          <ConfigFieldSection
            title="工作区配置"
            load={getConfigWorkspace}
            save={setConfigWorkspace}
          />

          {/* ── Logging ── */}
          <ConfigFieldSection
            title="日志配置"
            load={getConfigLogging}
            save={setConfigLogging}
          />
        </div>
      </div>
    </div>,
    document.body
  );
}

// ============================================================
// Sections
// ============================================================

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="flex flex-col gap-2">
      <span className="text-[10px] font-semibold text-[var(--text-secondary)] uppercase tracking-wider">{title}</span>
      {children}
    </div>
  );
}

// ---- LLM ----
function LlmSection() {
  const [provider, setProvider] = useState('');
  const [model, setModel] = useState('');
  const [saving, setSaving] = useState(false);
  const [testing, setTesting] = useState(false);
  const [testResult, setTestResult] = useState('');

  useEffect(() => {
    const load = async () => {
      try {
        const cfg = await getExpertLlmDefaults();
        const data = cfg as unknown as { provider: string; model: string; timeout: number; temperature: number };
        setProvider(data.provider || '');
        setModel(data.model || '');
      } catch {}
    };
    load();
  }, []);

  const handleSave = async () => {
    setSaving(true);
    try {
      await setExpertLlmDefaults({ provider, model });
    } catch {}
    setSaving(false);
  };

  const handleTest = async () => {
    setTesting(true);
    setTestResult('');
    try {
      const res = await testLlmConnection('ping');
      setTestResult(`✅ 成功 | ${res.model} | ${res.latency_ms}ms`);
    } catch {
      setTestResult('❌ 连接失败');
    }
    setTesting(false);
  };

  return (
    <Section title="LLM 默认值">
      <Field label="Provider">
        <input value={provider} onChange={e => setProvider(e.target.value)}
          placeholder="deepseek / doubao / openai"
          className="form-input" style={{ width: '100%' }} />
      </Field>
      <Field label="Model">
        <input value={model} onChange={e => setModel(e.target.value)}
          placeholder="deepseek-chat / doubao-pro-32k"
          className="form-input" style={{ width: '100%' }} />
      </Field>
      {testResult && <div className="text-[10px] text-[var(--text-secondary)]">{testResult}</div>}
      <div className="flex items-center gap-2">
        <Btn label="保存" primary loading={saving} onClick={handleSave} />
        <Btn label="测试连接" loading={testing} onClick={handleTest} />
      </div>
    </Section>
  );
}

// ---- LLM Providers ----
function LlmProvidersSection() {
  const [providers, setProviders] = useState<LlmProvider[]>([]);
  const [loading, setLoading] = useState(true);
  const [showAdd, setShowAdd] = useState(false);
  const [newId, setNewId] = useState('');
  const [newUrl, setNewUrl] = useState('');
  const [newModel, setNewModel] = useState('');

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const res = await listLlmProviders();
      setProviders(res.items || []);
    } catch {}
    setLoading(false);
  }, []);

  useEffect(() => { refresh(); }, [refresh]);

  const handleAdd = async () => {
    if (!newId.trim()) return;
    try { await addLlmProvider({ id: newId.trim(), base_url: newUrl.trim(), model: newModel.trim() }); setShowAdd(false); setNewId(''); setNewUrl(''); setNewModel(''); refresh(); }
    catch {}
  };

  const handleDelete = async (id: string) => {
    try { await deleteLlmProvider(id); refresh(); } catch {}
  };

  return (
    <Section title="LLM Providers">
      {loading ? <span className="text-[10px] text-[var(--text-secondary)]">加载中...</span> : (
        <div className="flex flex-col gap-1">
          {providers.map(p => (
            <div key={p.id} className="flex items-center gap-2" style={{ padding: '4px 0' }}>
              <span className="text-[10px] text-[var(--text-primary)] font-medium flex-1">{p.id}</span>
              <span className="text-[9px] text-[var(--text-secondary)] truncate max-w-[120px]">{p.model}</span>
              <button onClick={() => handleDelete(p.id)}
                style={{ width: 18, height: 18, borderRadius: '50%', border: '1px solid var(--glass-border)',
                  background: 'var(--surface)', color: 'var(--text-secondary)', cursor: 'pointer', fontSize: 10,
                  display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}
              >✕</button>
            </div>
          ))}
        </div>
      )}
      {!showAdd ? (
        <button onClick={() => setShowAdd(true)}
          className="text-[10px] text-[var(--accent-lighter)]"
          style={{ background: 'none', border: 'none', cursor: 'pointer', alignSelf: 'flex-start' }}
        >+ 添加 Provider</button>
      ) : (
        <div className="flex flex-col gap-1.5">
          <input value={newId} onChange={e => setNewId(e.target.value)} placeholder="ID"
            className="form-input text-[10px]" style={{ width: '100%' }} />
          <input value={newUrl} onChange={e => setNewUrl(e.target.value)} placeholder="Base URL"
            className="form-input text-[10px]" style={{ width: '100%' }} />
          <input value={newModel} onChange={e => setNewModel(e.target.value)} placeholder="Model"
            className="form-input text-[10px]" style={{ width: '100%' }} />
          <div className="flex gap-2">
            <Btn label="添加" onClick={handleAdd} />
            <button onClick={() => setShowAdd(false)}
              className="text-[10px] text-[var(--text-secondary)]"
              style={{ background: 'none', border: 'none', cursor: 'pointer' }}>取消</button>
          </div>
        </div>
      )}
    </Section>
  );
}

// ---- API Key ----
function ApiKeySection() {
  const [masked, setMasked] = useState('••••••••');
  const [newKey, setNewKey] = useState('');
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    const load = async () => {
      try {
        const res = await getConfigLlmLocal();
        setMasked(res.api_key || '••••••••');
      } catch {}
    };
    load();
  }, []);

  const handleSave = async () => {
    if (!newKey.trim()) return;
    setSaving(true);
    try {
      await setConfigLlmLocal({ api_key: newKey.trim() });
      setNewKey('');
      setMasked('••••••••');
    } catch {}
    setSaving(false);
  };

  return (
    <Section title="API Key">
      <div className="flex items-center gap-2">
        <span className="text-[10px] text-[var(--text-secondary)] font-mono">{masked}</span>
      </div>
      <input value={newKey} onChange={e => setNewKey(e.target.value)}
        type="password" placeholder="输入新 API Key"
        className="form-input" style={{ width: '100%' }} />
      <Btn label="更新" loading={saving} onClick={handleSave} />
    </Section>
  );
}

// ---- Generic JSON config field section ----
function ConfigFieldSection({ title, load, save }: {
  title: string;
  load: () => Promise<Record<string, unknown>>;
  save: (data: Record<string, unknown>) => Promise<Record<string, unknown>>;
}) {
  const [raw, setRaw] = useState('');
  const [saving, setSaving] = useState(false);

  useEffect(() => {
    const run = async () => {
      try {
        const data = await load();
        setRaw(JSON.stringify(data, null, 2));
      } catch { setRaw('{}'); }
    };
    run();
  }, [load]);

  const handleSave = async () => {
    setSaving(true);
    try {
      const parsed = JSON.parse(raw);
      await save(parsed);
    } catch {}
    setSaving(false);
  };

  return (
    <Section title={title}>
      <textarea value={raw} onChange={e => setRaw(e.target.value)}
        rows={4} className="form-input text-[10px] font-mono"
        style={{ width: '100%', resize: 'vertical' }} />
      <Btn label="保存" loading={saving} onClick={handleSave} />
    </Section>
  );
}

// ---- Debug: Reset Expert Positions ----
function DebugResetPositions() {
  const [resetting, setResetting] = useState(false);
  const [msg, setMsg] = useState('');

  const handleReset = async () => {
    setResetting(true);
    setMsg('');
    try {
      // Pass an empty object to clear all saved positions.
      // The backend will use the default layout logic.
      await saveExpertGraphPositions({});
      setMsg('✅ 位置已重置，刷新画布即可恢复默认布局');
    } catch {
      setMsg('❌ 重置失败');
    }
    setResetting(false);
  };

  return (
    <Section title="🔧 Expert 位置 Debug">
      <p className="text-[9px] text-[var(--text-secondary)] leading-relaxed">
        若卡片被拖到画面外无法找回，点击下方按钮将所有卡片恢复到默认位置。
      </p>
      <div className="flex items-center gap-2">
        <Btn label="重置卡片位置" loading={resetting} onClick={handleReset} />
        {msg && <span className={`text-[9px] ${msg.startsWith('✅') ? 'text-green-400' : 'text-red-400'}`}>{msg}</span>}
      </div>
    </Section>
  );
}

// ============================================================
// Helpers
// ============================================================

function Field({ label, required, children }: { label: string; required?: boolean; children: React.ReactNode }) {
  return (
    <div className="flex flex-col gap-1">
      <span className="text-[10px] text-[var(--text-secondary)]">{label}{required && <span className="text-red-400"> *</span>}</span>
      {children}
    </div>
  );
}

function Btn({ label, primary, loading, onClick }: { label: string; primary?: boolean; loading?: boolean; onClick: () => void }) {
  return (
    <button onClick={onClick} disabled={loading}
      style={{
        height: 26, padding: '0 12px', fontSize: 10, fontWeight: 600, borderRadius: 6,
        background: primary || loading ? 'var(--accent)' : 'var(--surface)',
        color: primary || loading ? '#fff' : 'var(--text-secondary)',
        border: primary || loading ? 'none' : '1px solid var(--glass-border-strong)',
        cursor: loading ? 'wait' : 'pointer', opacity: loading ? 0.6 : 1,
      }}
    >{loading ? '...' : label}</button>
  );
}