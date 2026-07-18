import React, { useCallback, useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import type { LlmProvider, PermissionPolicy, ToolInfo, WorkspaceRecord } from '../types';
import {
  testLlmConnection,
  listLlmProviders, addLlmProvider, updateLlmProvider, deleteLlmProvider,
  getConfigWorkspace, setConfigWorkspace,
  getConfigLogging, setConfigLogging,
  getExpertLlmDefaults, setExpertLlmDefaults,
  saveExpertGraphPositions,
  getWorkspace, updateWorkspace, listTools,
} from '../api/api';

interface Props {
  show: boolean;
  theme: 'dark' | 'light';
  scale: number;
  onScaleChange: (v: number) => void;
  workspace: WorkspaceRecord | null;
  onExpertPositionsReset: () => void;
  onClose: () => void;
}

export function SettingsPanel({ show, theme, scale, onScaleChange, workspace, onExpertPositionsReset, onClose }: Props) {
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
        className="glass-panel settings-panel flex flex-col anim-modal-pop"
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

        <ApiKeyStatusBanner />

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
          <DebugResetPositions workspaceId={workspace?.id || ''} onReset={onExpertPositionsReset} />

          {/* ── LLM ── */}
          <LlmSection />

          {/* ── LLM Providers ── */}
          <LlmProvidersSection />

          <WorkspacePermissionsSection workspaceId={workspace?.id || ''} />

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
  const [providers, setProviders] = useState<LlmProvider[]>([]);
  const [provider, setProvider] = useState('');
  const [model, setModel] = useState('');
  const [apiKey, setApiKey] = useState('');
  const [saving, setSaving] = useState(false);
  const [saveResult, setSaveResult] = useState('');

  useEffect(() => {
    const load = async () => {
      try {
        const [cfg, providerResult] = await Promise.all([
          getExpertLlmDefaults(), listLlmProviders(),
        ]);
        const data = cfg as unknown as { provider: string; model: string; timeout: number; temperature: number };
        const items = providerResult.providers || [];
        const selected = data.provider || providerResult.default || items[0]?.id || '';
        setProviders(items);
        setProvider(selected);
        setModel(data.model || items.find(item => item.id === selected)?.model || '');
      } catch (error) {
        setSaveResult(`❌ ${error instanceof Error ? error.message : 'LLM 配置加载失败'}`);
      }
    };
    void load();
  }, []);

  const handleSave = async () => {
    const selected = providers.find(item => item.id === provider);
    const trimmedKey = apiKey.trim();
    if (!selected) { setSaveResult('❌ 请选择有效的 Provider'); return; }
    if (!model.trim()) { setSaveResult('❌ Model 不能为空'); return; }
    if (/^(?:\*+|•+)$/.test(trimmedKey)) {
      setSaveResult('❌ API Key 不能是掩码值');
      return;
    }
    setSaving(true);
    setSaveResult('');
    try {
      await updateLlmProvider(selected.id, {
        id: selected.id,
        base_url: selected.base_url,
        model: model.trim(),
        api_key: trimmedKey,
        set_default: true,
      });
      await setExpertLlmDefaults({ provider, model: model.trim() });
      setApiKey('');
      setSaveResult('✅ 已保存并热加载');
    } catch (error) {
      setSaveResult(`❌ ${error instanceof Error ? error.message : '保存失败'}`);
    }
    setSaving(false);
  };

  return (
    <Section title="LLM 默认值">
      <Field label="Provider">
        <select value={provider} onChange={e => {
          const id = e.target.value;
          setProvider(id);
          setModel(providers.find(item => item.id === id)?.model || '');
          setApiKey('');
        }} className="form-input" style={{ width: '100%' }}>
          {providers.map(item => <option key={item.id} value={item.id}>{item.id}</option>)}
        </select>
      </Field>
      <Field label="Model">
        <input value={model} onChange={e => setModel(e.target.value)}
          placeholder="deepseek-chat / doubao-pro-32k"
          className="form-input" style={{ width: '100%' }} />
      </Field>
      <Field label="API Key">
        <input value={apiKey} onChange={e => setApiKey(e.target.value)}
          type="password" autoComplete="new-password"
          placeholder="留空表示不修改当前 Provider 的 Key"
          className="form-input" style={{ width: '100%' }} />
      </Field>
      {saveResult && <div className="settings-message text-[var(--text-secondary)]">{saveResult}</div>}
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
  const [deletingId, setDeletingId] = useState<string | null>(null);
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
    setDeletingId(id);
    setMessage('');
    try {
      await deleteLlmProvider(id);
      await refresh();
      if (editingId === id) clearForm();
      setMessage('✅ Provider 已删除并从运行时移除');
    } catch (error) {
      setMessage(`❌ ${error instanceof Error ? error.message : '删除失败'}`);
    }
    setDeletingId(null);
  };

  const beginEdit = (provider: LlmProvider) => {
    setEditingId(provider.id);
    setProviderId(provider.id);
    setBaseUrl(provider.base_url);
    setModel(provider.model);
    setApiKey('');
    setMessage('');
  };

  const editForm = (
    <div className="provider-inline-form flex flex-col gap-2">
      <input value={providerId} onChange={e => setProviderId(e.target.value)}
        disabled={editingId !== ''} placeholder="Provider ID"
        className="form-input" style={{ width: '100%' }} />
      <input value={baseUrl} onChange={e => setBaseUrl(e.target.value)} placeholder="Base URL"
        className="form-input" style={{ width: '100%' }} />
      <input value={model} onChange={e => setModel(e.target.value)} placeholder="Model"
        className="form-input" style={{ width: '100%' }} />
      <input value={apiKey} onChange={e => setApiKey(e.target.value)} type="password"
        autoComplete="new-password"
        placeholder={editingId === '' ? 'API Key' : 'API Key（留空表示不修改）'}
        className="form-input" style={{ width: '100%' }} />
      {editingId !== '' && (() => {
        const editingProvider = providers.find(p => p.id === editingId);
        if (editingProvider?.api_key_env) {
          return (
            <div className="text-[10px] text-[var(--text-secondary)]" style={{ marginTop: -4 }}>
              或设置环境变量 {editingProvider.api_key_env}
            </div>
          );
        }
        return null;
      })()}
      <div className="flex flex-wrap gap-2">
        <Btn label={editingId === '' ? '添加' : '保存'} primary loading={busy} onClick={handleSave} />
        <Btn label="测试连接" loading={busy} onClick={handleTest} />
        <button onClick={clearForm} disabled={busy}
          className="settings-action text-[var(--text-secondary)]"
          style={{ background: 'none', border: 'none', cursor: 'pointer' }}>取消</button>
      </div>
    </div>
  );

  return (
    <Section title="LLM Providers">
      {loading ? <span className="settings-message text-[var(--text-secondary)]">加载中...</span> : (
        <div className="flex flex-col gap-1">
          {providers.map(p => (
            <React.Fragment key={p.id}>
              <div className="provider-row flex items-center gap-2" style={{ padding: '5px 0' }}>
                {p.api_key_masked && <span style={{ fontSize: 12, flexShrink: 0 }}>🔑</span>}
                <span className="provider-name font-medium flex-1" style={{
                  color: p.api_key_masked ? '#4ade80' : 'var(--text-primary)'
                }}>{p.id}</span>
                <span className="provider-model text-[var(--text-secondary)] truncate max-w-[120px]">{p.model}</span>
                <button onClick={() => beginEdit(p)} disabled={busy || deletingId !== null}
                  className="settings-action text-[var(--accent-lighter)]"
                  style={{ background: 'none', border: 'none', cursor: 'pointer' }}>编辑</button>
                <button onClick={() => handleDelete(p.id)}
                  disabled={busy || deletingId !== null}
                  className="provider-delete"
                  style={{ borderRadius: '50%', border: '1px solid var(--glass-border)',
                    background: 'var(--surface)', color: 'var(--text-secondary)', cursor: 'pointer',
                    display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}
                >{deletingId === p.id ? '…' : '✕'}</button>
              </div>
              {editingId === p.id && editForm}
            </React.Fragment>
          ))}
        </div>
      )}
      {editingId === null ? (
        <button onClick={() => { clearForm(); setEditingId(''); setMessage(''); }}
          className="settings-action text-[var(--accent-lighter)]"
          style={{ background: 'none', border: 'none', cursor: 'pointer', alignSelf: 'flex-start' }}
        >+ 添加 Provider</button>
      ) : editingId === '' ? editForm : null}
      {message && <div className="settings-message text-[var(--text-secondary)]">{message}</div>}
    </Section>
  );
}

// ---- Workspace permission policies (real backend policy IDs/actions only) ----
function WorkspacePermissionsSection({ workspaceId }: { workspaceId: string }) {
  const [tools, setTools] = useState<ToolInfo[]>([]);
  const [policies, setPolicies] = useState<Record<string, PermissionPolicy>>({});
  const [saving, setSaving] = useState(false);
  const [message, setMessage] = useState('');

  useEffect(() => {
    if (!workspaceId) return;
    const load = async () => {
      try {
        const [workspace, toolResult] = await Promise.all([
          getWorkspace(workspaceId), listTools(),
        ]);
        const raw = workspace.permissions_config;
        const parsed = typeof raw === 'string' ? JSON.parse(raw || '{}') : (raw || {});
        setPolicies(parsed as Record<string, PermissionPolicy>);
        setTools(toolResult.items || []);
      } catch (error) {
        setMessage(`❌ ${error instanceof Error ? error.message : '权限配置加载失败'}`);
      }
    };
    void load();
  }, [workspaceId]);

  const setPolicy = (toolName: string, value: string) => {
    setPolicies(previous => {
      const next = { ...previous };
      if (value === 'inherit') delete next[toolName];
      else next[toolName] = value as PermissionPolicy;
      return next;
    });
  };

  const handleSave = async () => {
    if (!workspaceId) { setMessage('❌ 当前没有可用的 Workspace'); return; }
    setSaving(true);
    setMessage('');
    try {
      await updateWorkspace(workspaceId, { permissions_config: policies });
      setMessage('✅ 权限策略已保存，后端将在下一次工具调用时使用');
    } catch (error) {
      setMessage(`❌ ${error instanceof Error ? error.message : '权限保存失败'}`);
    }
    setSaving(false);
  };

  return (
    <Section title="当前 Workspace 工具权限">
      {!workspaceId ? (
        <span className="settings-help text-[var(--text-secondary)]">进入 Workspace 后可配置</span>
      ) : (
        <>
          <p className="settings-help text-[var(--text-secondary)] leading-relaxed">
            后端仅支持按工具 ID 的 ask、auto_approve、deny。高危或阻止级工具不提供自动批准。
          </p>
          <div className="flex flex-col gap-1.5">
            {tools.map(tool => {
              const canAutoApprove = tool.risk_level === 'safe' && !tool.name.startsWith('shell.');
              return (
                <div key={tool.name} className="flex items-center gap-2 min-w-0">
                  <span className="provider-name text-[var(--text-primary)] truncate flex-1" title={tool.name}>{tool.name}</span>
                  <span className="provider-model text-[var(--text-secondary)]">{tool.risk_level}</span>
                  <select className="form-input" style={{ width: 132 }}
                    value={policies[tool.name] || 'inherit'}
                    onChange={event => setPolicy(tool.name, event.target.value)}>
                    <option value="inherit">继承默认</option>
                    <option value="ask">每次询问</option>
                    <option value="deny">拒绝</option>
                    <option value="auto_approve" disabled={!canAutoApprove}>自动批准</option>
                  </select>
                </div>
              );
            })}
          </div>
          <Btn label="保存权限" primary loading={saving} onClick={handleSave} />
          {message && <div className="settings-message text-[var(--text-secondary)]">{message}</div>}
        </>
      )}
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
function DebugResetPositions({ workspaceId, onReset }: { workspaceId: string; onReset: () => void }) {
  const [resetting, setResetting] = useState(false);
  const [msg, setMsg] = useState('');

  const handleReset = async () => {
    setResetting(true);
    setMsg('');
    try {
      if (!workspaceId) throw new Error('当前没有可用的 Workspace');
      await saveExpertGraphPositions({}, workspaceId);
      onReset();
      setMsg('✅ 当前 Workspace 的卡片位置已恢复默认布局');
    } catch (error) {
      setMsg(`❌ ${error instanceof Error ? error.message : '重置失败'}`);
    }
    setResetting(false);
  };

  return (
    <Section title="🔧 Expert 位置 Debug">
      <p className="settings-help text-[var(--text-secondary)] leading-relaxed">
        若卡片被拖到画面外无法找回，点击下方按钮将所有卡片恢复到默认位置。
      </p>
      <div className="flex items-center gap-2">
        <Btn label="重置卡片位置" loading={resetting} onClick={handleReset} />
        {msg && <span className={`settings-message ${msg.startsWith('✅') ? 'text-green-400' : 'text-red-400'}`}>{msg}</span>}
      </div>
    </Section>
  );
}

// ============================================================
// ApiKeyStatusBanner — shows default provider key status at top
// ============================================================

function ApiKeyStatusBanner() {
  const [status, setStatus] = useState<{ hasKey: boolean; source: string; masked: string; envVar?: string } | null>(null);

  useEffect(() => {
    const load = async () => {
      try {
        const res = await listLlmProviders();
        const providers = res.providers || [];
        const defaultId = res.default || providers[0]?.id;
        if (!defaultId) return;
        const dp = providers.find(p => p.id === defaultId);
        if (!dp) return;
        if (dp.api_key_masked) {
          setStatus({
            hasKey: true,
            source: dp.api_key_source === 'env' ? '环境变量' : '系统内配置',
            masked: dp.api_key || '••••••',
            envVar: dp.api_key_env,
          });
        } else {
          setStatus({ hasKey: false, source: '', masked: '' });
        }
      } catch {}
    };
    void load();
  }, []);

  if (!status) return null;

  return (
    <div style={{
      fontSize: 11, padding: '8px 12px', borderRadius: 8,
      background: status.hasKey ? 'rgba(74,222,128,0.08)' : 'rgba(239,68,68,0.06)',
      border: `1px solid ${status.hasKey ? 'rgba(74,222,128,0.25)' : 'rgba(239,68,68,0.20)'}`,
      display: 'flex', alignItems: 'center', gap: 6, flexShrink: 0,
    }}>
      {status.hasKey ? (
        <>
          <span style={{ color: '#4ade80' }}>🔑 Key 已配置</span>
          <span className="text-[var(--text-secondary)]">
            · 来源：{status.source}
            {status.envVar && <span style={{ marginLeft: 4, opacity: 0.7 }}>({status.envVar})</span>}
          </span>
          <span className="text-[var(--text-secondary)]" style={{ fontFamily: 'monospace', fontSize: 10 }}>
            {status.masked}
          </span>
        </>
      ) : (
        <span className="text-[var(--text-secondary)]">
          ⚠️ 默认 Provider 未配置 API Key — 请设置环境变量或在下方 Provider 编辑中填入
        </span>
      )}
    </div>
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
        height: 30, padding: '0 13px', fontSize: 12, fontWeight: 600, borderRadius: 7,
        background: primary || loading ? 'var(--accent)' : 'var(--surface)',
        color: primary || loading ? '#fff' : 'var(--text-secondary)',
        border: primary || loading ? 'none' : '1px solid var(--glass-border-strong)',
        cursor: loading ? 'wait' : 'pointer', opacity: loading ? 0.6 : 1,
      }}
    >{loading ? '...' : label}</button>
  );
}
