import { useCallback, useEffect, useState } from 'react';
import { createPortal } from 'react-dom';
import type { Exponent, ExpertRouteRule, ToolInfo } from '../types';
import {
  getExpert, createExpert, updateExpert, deleteExpert,
  listTools,
} from '../api/api';

interface Props {
  expertName?: string;         // undefined = create mode
  theme: 'dark' | 'light';
  onClose: () => void;
  onSaved: () => void;         // trigger graph refresh
}

export function ExpertEditModal({ expertName, theme, onClose, onSaved }: Props) {
  const isCreate = !expertName;
  const [loading, setLoading] = useState(!isCreate);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');
  const [availableTools, setAvailableTools] = useState<ToolInfo[]>([]);

  // Form state
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [isEntry, setIsEntry] = useState(false);
  const [contextIsolation, setContextIsolation] = useState(false);
  const [contextTemplate, setContextTemplate] = useState('');
  const [visibleTools, setVisibleTools] = useState<string[]>([]);
  const [canModifyPlan, setCanModifyPlan] = useState(true);
  const [canWriteSummary, setCanWriteSummary] = useState(false);
  const [readGlobalActively, setReadGlobalActively] = useState(false);
  const [nextRules, setNextRules] = useState<ExpertRouteRule[]>([]);
  const [onFail, setOnFail] = useState('');
  const [llmProvider, setLlmProvider] = useState('');
  const [llmModel, setLlmModel] = useState('');
  const [llmTimeout, setLlmTimeout] = useState(0);
  const [llmTemperature, setLlmTemperature] = useState(-1);
  const [maxInternalRounds, setMaxInternalRounds] = useState(5);
  const [toolTimeout, setToolTimeout] = useState(60);

  // Load existing expert data (edit mode)
  useEffect(() => {
    if (!expertName) return;
    const load = async () => {
      setLoading(true);
      try {
        const expert = await getExpert(expertName);
        setName(expert.name || '');
        setDescription(expert.description || '');
        setIsEntry(expert.is_entry || false);
        setContextIsolation(expert.context_isolation || false);
        setContextTemplate(expert.context_template || '');
        setVisibleTools(expert.visible_tools || []);
        setCanModifyPlan(expert.can_modify_plan ?? true);
        setCanWriteSummary(expert.can_write_summary ?? false);
        setReadGlobalActively(expert.read_global_actively ?? false);
        setNextRules(expert.next_rules || []);
        setOnFail(expert.on_fail || '');
        setLlmProvider(expert.llm_provider || '');
        setLlmModel(expert.llm_model || '');
        setLlmTimeout(expert.llm_timeout || 0);
        setLlmTemperature(expert.llm_temperature ?? -1);
        setMaxInternalRounds(expert.max_internal_rounds || 5);
        setToolTimeout(expert.tool_timeout_seconds || 60);
      } catch {
        setError('加载专家配置失败');
      }
      setLoading(false);
    };
    load();
  }, [expertName]);

  // Load available tools
  useEffect(() => {
    const load = async () => {
      try {
        const res = await listTools();
        setAvailableTools(res.items || []);
      } catch {}
    };
    load();
  }, []);

  const handleSave = useCallback(async () => {
    if (!name.trim()) { setError('名称不能为空'); return; }
    setSaving(true);
    setError('');
    try {
      const data: Partial<Exponent> = {
        name: name.trim(), description, is_entry: isEntry,
        context_isolation: contextIsolation, context_template: contextTemplate,
        visible_tools: visibleTools,
        can_modify_plan: canModifyPlan, can_write_summary: canWriteSummary,
        read_global_actively: readGlobalActively,
        next_rules: nextRules, on_fail: onFail,
        llm_provider: llmProvider, llm_model: llmModel,
        llm_timeout: llmTimeout, llm_temperature: llmTemperature,
        max_internal_rounds: maxInternalRounds,
        tool_timeout_seconds: toolTimeout,
      };

      if (isCreate) {
        await createExpert(data);
      } else {
        await updateExpert(expertName!, data);
      }
      onSaved();
      onClose();
    } catch {
      setError('保存失败');
    }
    setSaving(false);
  }, [name, description, isEntry, contextIsolation, contextTemplate, visibleTools,
    canModifyPlan, canWriteSummary, readGlobalActively, nextRules, onFail,
    llmProvider, llmModel, llmTimeout, llmTemperature, maxInternalRounds, toolTimeout,
    isCreate, expertName, onSaved, onClose]);

  const handleDelete = useCallback(async () => {
    if (!confirm(`确定删除 Expert "${expertName}"？`)) return;
    try {
      await deleteExpert(expertName!);
      onSaved();
      onClose();
    } catch {
      setError('删除失败');
    }
  }, [expertName, onSaved, onClose]);

  const toggleTool = (toolName: string) => {
    setVisibleTools(prev =>
      prev.includes(toolName) ? prev.filter(t => t !== toolName) : [...prev, toolName]
    );
  };

  const addRoute = () => {
    setNextRules(prev => [...prev, { type: 'tag_exists', value: '', route_to: '', priority: 0 }]);
  };

  const removeRoute = (idx: number) => {
    setNextRules(prev => prev.filter((_, i) => i !== idx));
  };

  const updateRoute = (idx: number, field: keyof ExpertRouteRule, value: string | number) => {
    setNextRules(prev => prev.map((r, i) => i === idx ? { ...r, [field]: value } : r));
  };

  if (loading) {
    return createPortal(
      <div className={`theme-${theme}`} style={{ position: 'fixed', inset: 0, zIndex: 2200,
        display: 'flex', alignItems: 'center', justifyContent: 'center',
        background: 'rgba(0,0,0,0.15)', backdropFilter: 'blur(4px)' }}>
        <div className="glass-panel" style={{ padding: 32 }}>
          <span className="text-sm text-[var(--text-secondary)]">加载中...</span>
        </div>
      </div>,
      document.body
    );
  }

  return createPortal(
    <div
      className={`theme-${theme}`}
      style={{
        position: 'fixed', inset: 0, zIndex: 2200,
        display: 'flex', alignItems: 'flex-start', justifyContent: 'center',
        background: 'rgba(0,0,0,0.15)', backdropFilter: 'blur(4px)',
        overflow: 'auto', paddingTop: 40, paddingBottom: 40,
      }}
      onClick={onClose}
    >
      <div
        className="glass-panel flex flex-col"
        style={{
          width: 520, maxWidth: 'calc(100vw - 48px)',
          padding: '28px 32px', gap: 20, overflow: 'visible',
        }}
        onClick={e => e.stopPropagation()}
      >
        {/* Header */}
        <div className="flex items-center justify-between">
          <span className="text-sm font-semibold text-[var(--text-primary)]">
            {isCreate ? '+ 新建 Expert' : `编辑 ${expertName}`}
          </span>
          <button
            onClick={onClose} title="取消"
            style={{ width: 28, height: 28, borderRadius: '50%', border: '1px solid var(--glass-border-strong)',
              background: 'var(--surface)', color: 'var(--text-secondary)', cursor: 'pointer', fontSize: 14,
              display: 'flex', alignItems: 'center', justifyContent: 'center' }}
          >✕</button>
        </div>

        {/* ── 基本信息 ── */}
        <Section title="基本信息">
          <Field label="名称" required>
            <input value={name} onChange={e => setName(e.target.value)}
              placeholder="例如：my_expert" disabled={!isCreate}
              className="form-input" style={{ width: '100%' }} />
          </Field>
          <Field label="描述">
            <textarea value={description} onChange={e => setDescription(e.target.value)} rows={3}
              placeholder="Expert 的角色描述，会注入到 prompt 的 {role} 中"
              className="form-input" style={{ width: '100%', resize: 'vertical' }} />
          </Field>
          <div className="flex items-center justify-between">
            <span className="text-[11px] text-[var(--text-secondary)]">入口 Expert</span>
            <Toggle value={isEntry} onChange={setIsEntry} />
          </div>
          <div className="flex items-center justify-between">
            <span className="text-[11px] text-[var(--text-secondary)]">上下文隔离</span>
            <Toggle value={contextIsolation} onChange={setContextIsolation} />
          </div>
          <div className="flex items-center justify-between">
            <span className="text-[11px] text-[var(--text-secondary)]">可修改计划</span>
            <Toggle value={canModifyPlan} onChange={setCanModifyPlan} />
          </div>
          <div className="flex items-center justify-between">
            <span className="text-[11px] text-[var(--text-secondary)]">可写摘要</span>
            <Toggle value={canWriteSummary} onChange={setCanWriteSummary} />
          </div>
          <div className="flex items-center justify-between">
            <span className="text-[11px] text-[var(--text-secondary)]">主动读全局</span>
            <Toggle value={readGlobalActively} onChange={setReadGlobalActively} />
          </div>
        </Section>

        {/* ── 工具可见 ── */}
        <Section title="可见工具">
          <div className="flex flex-wrap gap-1.5">
            {availableTools.map(t => (
              <button key={t.name}
                onClick={() => toggleTool(t.name)}
                style={{
                  fontSize: 10, padding: '3px 8px', borderRadius: 6, cursor: 'pointer',
                  border: `1px solid ${visibleTools.includes(t.name) ? 'var(--accent-light)' : 'var(--glass-border)'}`,
                  background: visibleTools.includes(t.name) ? 'var(--accent)' : 'var(--surface)',
                  color: visibleTools.includes(t.name) ? '#fff' : 'var(--text-secondary)',
                  transition: 'all 0.15s',
                }}
              >{t.name}</button>
            ))}
          </div>
        </Section>

        {/* ── 路由规则 ── */}
        <Section title="路由规则">
          {nextRules.map((rule, idx) => (
            <div key={idx} className="flex items-center gap-1.5">
              <select value={rule.type} onChange={e => updateRoute(idx, 'type', e.target.value)}
                className="form-input text-[10px]" style={{ width: 110 }}>
                <option value="tag_exists">tag_exists</option>
                <option value="tag_value_match">tag_value_match</option>
                <option value="plan_state">plan_state</option>
                <option value="default">default</option>
              </select>
              <input value={rule.value} onChange={e => updateRoute(idx, 'value', e.target.value)}
                placeholder="值" className="form-input text-[10px]" style={{ width: 80 }} />
              <input value={rule.route_to} onChange={e => updateRoute(idx, 'route_to', e.target.value)}
                placeholder="目标" className="form-input text-[10px]" style={{ width: 90 }} />
              <input value={rule.priority} onChange={e => updateRoute(idx, 'priority', Number(e.target.value))}
                type="number" min={0} className="form-input text-[10px]" style={{ width: 40 }} />
              <button onClick={() => removeRoute(idx)}
                style={{ width: 20, height: 20, borderRadius: '50%', border: '1px solid var(--glass-border)',
                  background: 'var(--surface)', color: 'var(--text-secondary)', cursor: 'pointer', fontSize: 12,
                  display: 'flex', alignItems: 'center', justifyContent: 'center', flexShrink: 0 }}
              >✕</button>
            </div>
          ))}
          <button onClick={addRoute}
            className="text-[10px] text-[var(--accent-lighter)] hover:text-[var(--accent)] cursor-pointer"
            style={{ background: 'none', border: 'none', alignSelf: 'flex-start' }}
          >+ 添加路由</button>
          <Field label="失败兜底 (on_fail)">
            <input value={onFail} onChange={e => setOnFail(e.target.value)}
              placeholder="例如：summarizer 或 _done"
              className="form-input" style={{ width: '100%' }} />
          </Field>
        </Section>

        {/* ── LLM 配置 ── */}
        <Section title="LLM 配置（覆盖全局默认值）">
          <Field label="Provider">
            <input value={llmProvider} onChange={e => setLlmProvider(e.target.value)}
              placeholder="deepseek / doubao / openai ..."
              className="form-input" style={{ width: '100%' }} />
          </Field>
          <Field label="Model">
            <input value={llmModel} onChange={e => setLlmModel(e.target.value)}
              placeholder="deepseek-chat / doubao-pro-32k ..."
              className="form-input" style={{ width: '100%' }} />
          </Field>
          <Field label="Timeout (秒)">
            <input value={String(llmTimeout)} onChange={e => setLlmTimeout(Number(e.target.value) || 0)}
              type="number" min={0} className="form-input" style={{ width: 100 }} />
          </Field>
          <Field label="Temperature">
            <input value={String(llmTemperature)} onChange={e => setLlmTemperature(Number(e.target.value))}
              type="number" min={-1} max={2} step={0.1} className="form-input" style={{ width: 100 }} />
          </Field>
        </Section>

        {/* ── 容量限制 ── */}
        <Section title="容量限制">
          <Field label="最大内循环轮次">
            <input value={String(maxInternalRounds)} onChange={e => setMaxInternalRounds(Number(e.target.value) || 0)}
              type="number" min={1} max={20} className="form-input" style={{ width: 80 }} />
          </Field>
          <Field label="工具超时 (秒)">
            <input value={String(toolTimeout)} onChange={e => setToolTimeout(Number(e.target.value) || 0)}
              type="number" min={10} className="form-input" style={{ width: 80 }} />
          </Field>
        </Section>

        {/* Error */}
        {error && <div className="text-[11px] text-red-400" style={{ marginTop: -8 }}>{error}</div>}

        {/* Actions */}
        <div className="flex items-center justify-between" style={{ marginTop: 4 }}>
          {!isCreate && (
            <button onClick={handleDelete}
              style={{
                height: 30, padding: '0 14px', fontSize: 11, borderRadius: 8,
                background: 'transparent', color: '#ef4444',
                border: '1px solid rgba(239,68,68,0.4)', cursor: 'pointer',
              }}
            >删除 Expert</button>
          )}
          <div className="flex items-center gap-2" style={{ marginLeft: isCreate ? 'auto' : 0 }}>
            <button onClick={onClose}
              style={{ height: 30, padding: '0 14px', fontSize: 11, borderRadius: 8,
                background: 'var(--surface)', color: 'var(--text-secondary)',
                border: '1px solid var(--glass-border-strong)', cursor: 'pointer' }}
            >取消</button>
            <button onClick={handleSave} disabled={saving}
              style={{ height: 30, padding: '0 16px', fontSize: 11, borderRadius: 8,
                background: 'var(--accent)', color: '#fff', border: 'none',
                cursor: saving ? 'wait' : 'pointer', fontWeight: 600, opacity: saving ? 0.6 : 1 }}
            >{saving ? '保存中...' : '保存'}</button>
          </div>
        </div>
      </div>
    </div>,
    document.body
  );
}

// ============================================================
// Reusable sub-components
// ============================================================

function Section({ title, children }: { title: string; children: React.ReactNode }) {
  return (
    <div className="flex flex-col gap-2">
      <span className="text-[10px] font-semibold text-[var(--text-secondary)] uppercase tracking-wider">{title}</span>
      {children}
    </div>
  );
}

function Field({ label, required, children }: { label: string; required?: boolean; children: React.ReactNode }) {
  return (
    <div className="flex flex-col gap-1">
      <span className="text-[10px] text-[var(--text-secondary)]">
        {label}{required && <span style={{ color: '#ef4444' }}> *</span>}
      </span>
      {children}
    </div>
  );
}

function Toggle({ value, onChange }: { value: boolean; onChange: (v: boolean) => void }) {
  return (
    <button
      onClick={() => onChange(!value)}
      style={{
        width: 36, height: 20, borderRadius: 10, border: 'none', cursor: 'pointer',
        background: value ? 'var(--accent)' : 'var(--glass-border-strong)',
        position: 'relative', transition: 'background 0.2s',
      }}
    >
      <span style={{
        position: 'absolute', top: 2, left: value ? 18 : 2,
        width: 16, height: 16, borderRadius: '50%',
        background: '#fff', transition: 'left 0.2s',
        boxShadow: '0 1px 3px rgba(0,0,0,0.2)',
      }} />
    </button>
  );
}