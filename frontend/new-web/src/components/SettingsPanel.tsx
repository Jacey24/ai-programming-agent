import React, { useCallback, useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import type { LlmProvider } from '../types';
import {
  testLlmConnection,
  listLlmProviders, addLlmProvider, updateLlmProvider, deleteLlmProvider,
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
  const [saveResult, setSaveResult] = useState('');

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
    setSaveResult('');
    try {
      await setExpertLlmDefaults({ provider, model });
      setSaveResult('✅ 已保存');
    } catch (error) {
      setSaveResult(`❌ ${error instanceof Error ? error.message : '保存失败'}`);
    }
    setSaving(false);
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
      {saveResult && <div className="text-[10px] text-[var(--text-secondary)]">{saveResult}</div>}
      <div className="flex items-center gap-2">
        <Btn label="保存" primary loading={saving} onClick={handleSave} />
      </div>
    </Section>
  );
}

// ---- LLM Providers ----
function LlmProvidersSection() {
  const [providers, setProviders] = useState<LlmProvider[]>([]);
  const [loading, setLoading] = useState(true);
  const [editingId, setEditingId] = useState<string | null>(null);
  const [providerId, setProviderId] = useState('');
  const [baseUrl, setBaseUrl] = useState('');
  const [model, setModel] = useState('');
  const [apiKey, setApiKey] = useState('');
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState('');

  const refresh = useCallback(async () => {
    setLoading(true);
    try {
      const res = await listLlmProviders();
      setProviders(res.providers || []);
    } catch (error) {
      setMessage(`❌ ${error instanceof Error ? error.message : 'Provider 加载失败'}`);
    }
    setLoading(false);
  }, []);

  useEffect(() => { refresh(); }, [refresh]);

  const clearForm = () => {
    setEditingId(null);
    setProviderId('');
    setBaseUrl('');
    setModel('');
    setApiKey('');
  };

  const validate = (isNew: boolean) => {
    if (!providerId.trim()) return 'Provider ID 不能为空';
    if (!baseUrl.trim()) return 'Base URL 不能为空';
    if (!model.trim()) return 'Model 不能为空';
    if (isNew && !apiKey.trim()) return '新 Provider 必须提供 API Key';
    if (/^(\*+|•+)$/.test(apiKey.trim())) return 'API Key 不能是掩码值';
    if (isNew && providers.some(p => p.id === providerId.trim())) {
      return `Provider [${providerId.trim()}] 已存在`;
    }
    return '';
  };

  const handleSave = async () => {
    const isNew = editingId === '';
    const error = validate(isNew);
    if (error) { setMessage(`❌ ${error}`); return; }
    setBusy(true);
    setMessage('');
    try {
      const payload = {
        id: providerId.trim(), base_url: baseUrl.trim(), model: model.trim(),
        api_key: apiKey.trim(),
      };
      if (isNew) await addLlmProvider(payload);
      else await updateLlmProvider(editingId!, payload);
      await refresh();
      clearForm();
      setMessage(`✅ Provider ${isNew ? '已添加' : '已保存并热加载'}`);
    } catch (error) {
      setMessage(`❌ ${error instanceof Error ? error.message : '保存失败'}`);
    }
    setBusy(false);
  };

  const handleTest = async () => {
    const error = validate(editingId === '');
    if (error) { setMessage(`❌ ${error}`); return; }
    setBusy(true);
    setMessage('');
    try {
      const res = await testLlmConnection({
        id: providerId.trim(), base_url: baseUrl.trim(), model: model.trim(),
        api_key: apiKey.trim(), prompt: 'Reply with OK',
      });
      setMessage(`✅ 连接成功 | ${res.model} | ${res.latency_ms}ms`);
    } catch (error) {
      setMessage(`❌ ${error instanceof Error ? error.message : '连接测试失败'}`);
    }
    setBusy(false);
  };

  const handleDelete = async (id: string) => {
    setBusy(true);
    setMessage('');
    try {
      await deleteLlmProvider(id);
      await refresh();
      if (editingId === id) clearForm();
      setMessage('✅ Provider 已删除并从运行时移除');
    } catch (error) {
      setMessage(`❌ ${error instanceof Error ? error.message : '删除失败'}`);
    }
    setBusy(false);
  };

  const beginEdit = (provider: LlmProvider) => {
    setEditingId(provider.id);
    setProviderId(provider.id);
    setBaseUrl(provider.base_url);
    setModel(provider.model);
    setApiKey('');
    setMessage('');
  };

  return (
    <Section title="LLM Providers">
      {loading ? <span className="text-[10px] text-[var(--text-secondary)]">加载中...</span> : (
        <div className="flex flex-col gap-1">
          {providers.map(p => (
            <div key={p.id} className="flex items-center gap-2" style={{ padding: '4px 0' }}>
              <span className="text-[10px] text-[var(--text-primary)] font-medium flex-1">{p.id}</span>
              <span className="text-[9px] text-[var(--text-secondary)] truncate max-w-[120px]">{p.model}</span>
              <button onClick={() => beginEdit(p)} disabled={busy}
                className="text-[9px] text-[var(--accent-lighter)]"
                style={{ background: 'none', border: 'none', cursor: 'pointer' }}>编辑</button>
              <button onClick={() => handleDelete(p.id)}
                disabled={busy}
                style={{ width: 18, height: 18, borderRadius: '50%', border: '1px solid var(--glass-border)',
                  background: 'var(--surface)', color: 'var(--text-secondary)', cursor: 'pointer', fontSize: 10,
                  display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}
              >✕</button>
            </div>
          ))}
        </div>
      )}
      {editingId === null ? (
        <button onClick={() => { clearForm(); setEditingId(''); setMessage(''); }}
          className="text-[10px] text-[var(--accent-lighter)]"
          style={{ background: 'none', border: 'none', cursor: 'pointer', alignSelf: 'flex-start' }}
        >+ 添加 Provider</button>
      ) : (
        <div className="flex flex-col gap-1.5">
          <input value={providerId} onChange={e => setProviderId(e.target.value)}
            disabled={editingId !== ''} placeholder="Provider ID"
            className="form-input text-[10px]" style={{ width: '100%' }} />
          <input value={baseUrl} onChange={e => setBaseUrl(e.target.value)} placeholder="Base URL"
            className="form-input text-[10px]" style={{ width: '100%' }} />
          <input value={model} onChange={e => setModel(e.target.value)} placeholder="Model"
            className="form-input text-[10px]" style={{ width: '100%' }} />
          <input value={apiKey} onChange={e => setApiKey(e.target.value)} type="password"
            placeholder={editingId === '' ? 'API Key' : 'API Key（留空表示不修改）'}
            className="form-input text-[10px]" style={{ width: '100%' }} />
          <div className="flex gap-2">
            <Btn label={editingId === '' ? '添加' : '保存'} primary loading={busy} onClick={handleSave} />
            <Btn label="测试连接" loading={busy} onClick={handleTest} />
            <button onClick={clearForm} disabled={busy}
              className="text-[10px] text-[var(--text-secondary)]"
              style={{ background: 'none', border: 'none', cursor: 'pointer' }}>取消</button>
          </div>
        </div>
      )}
      {message && <div className="text-[10px] text-[var(--text-secondary)]">{message}</div>}
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
