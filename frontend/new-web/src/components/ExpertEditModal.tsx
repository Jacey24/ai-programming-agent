import { useCallback, useEffect, useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import type { Exponent } from '../types';
import {
  getExpert, createExpert, updateExpert, deleteExpert, patchExpert,
} from '../api/api';

// ── 模板积木定义 ──
interface PlaceholderChip {
  key: string;
  label: string;
  desc: string;
  bg: string;
  fg: string;
  border: string;
}

const PLACEHOLDER_CHIPS: PlaceholderChip[] = [
  { key: '{role}',           label: '{role}',           desc: 'Expert 角色描述 — 来自「角色描述」字段，定义当前专家的人格与职责',          bg: '#6393eb22', fg: '#6393eb', border: '#6393eb66' },
  { key: '{goal}',           label: '{goal}',           desc: '当前任务目标 — 用户输入或上级专家传递的任务描述',                                   bg: '#22c55e22', fg: '#22c55e', border: '#22c55e66' },
  { key: '{plan}',           label: '{plan}',           desc: '当前执行计划 — 包含步骤列表或「暂无计划」提示',                                     bg: '#22c55e22', fg: '#22c55e', border: '#22c55e66' },
  { key: '{summary}',        label: '{summary}',        desc: '任务上下文/摘要 — 首轮为「暂无摘要」，后续轮次包含之前的执行结果',                   bg: '#6b728022', fg: '#9ca3af', border: '#6b728066' },
  { key: '{tools_desc}',     label: '{tools_desc}',     desc: '可见工具列表描述 — 自动生成的工具用途说明（含参数和分组）',                        bg: '#f59e0b22', fg: '#f59e0b', border: '#f59e0b66' },
  { key: '{tag_protocol}',   label: '{tag_protocol}',   desc: '标签协议定义 — 必须使用的 XML 标签格式（done/fail/plan/write 等）',               bg: '#f59e0b22', fg: '#f59e0b', border: '#f59e0b66' },
  { key: '{output_hint}',    label: '{output_hint}',    desc: '输出格式提示 — 阶段结束方式、禁止事项等输出规范',                                   bg: '#f59e0b22', fg: '#f59e0b', border: '#f59e0b66' },
  { key: '{rounds_left}',    label: '{rounds_left}',    desc: '剩余轮次提示 — 当前剩余的可用工具调用轮次',                                         bg: '#6b728022', fg: '#9ca3af', border: '#6b728066' },
  { key: '{session}',        label: '{session}',        desc: '会话上下文 — 续接历史对话时注入的先前上下文，首发为空',                              bg: '#6b728022', fg: '#9ca3af', border: '#6b728066' },
  { key: '{workspace}',      label: '{workspace}',      desc: '工作目录信息 — 当前工作区路径和标识',                                                bg: '#6b728022', fg: '#9ca3af', border: '#6b728066' },
];

const DEFAULT_CONTEXT_TEMPLATE = '{role}\n\n{goal}\n\n{plan}\n{summary}\n{tools_desc}\n{tag_protocol}\n{output_hint}\n{rounds_left}\n{session}\n{workspace}';

interface Props {
  expertName?: string;         // undefined = create mode
  theme: 'dark' | 'light';
  onClose: () => void;
  onSaved: () => void;         // trigger graph refresh
}

/**
 * ExpertEditModal — 编辑/新建 Expert 基本信息、LLM 配置、容量限制。
 * 工具可见性 和 路由规则 已迁移到 Canvas 上的拖拽/连线编辑，此处不再保留。
 *
 * 注意：后端 GET /api/v1/experts/:name 返回的是 graph node 结构（嵌套
 * permissions/llm/limits），与保存时的扁平字段不同。本组件在加载时做映射。
 */

// 后端 graph node 的 JSON 结构（部分字段）
interface ExpertNodeJson {
  id: string;
  label: string;
  description: string;
  context_template: string;
  is_entry: boolean;
  context_isolation: boolean;
  on_fail: string;
  visible_tools: string[];
  permissions: { can_modify_plan: boolean; can_write_summary: boolean; read_global_actively: boolean };
  llm: { provider: string; model: string; temperature: number; timeout: number };
  limits: { max_internal_rounds: number; tool_timeout_seconds: number };
  position?: { x: number; y: number };
}

function mapNodeToForm(node: ExpertNodeJson) {
  return {
    name: node.id || '',
    description: node.description || '',
    isEntry: node.is_entry || false,
    contextIsolation: node.context_isolation || false,
    contextTemplate: node.context_template
      || '{role}\n\n{goal}\n\n{plan}\n\n{summary}\n{tools_desc}\n{tag_protocol}\n{output_hint}\n{rounds_left}\n{session}',
    canModifyPlan: node.permissions?.can_modify_plan ?? true,
    canWriteSummary: node.permissions?.can_write_summary ?? false,
    readGlobalActively: node.permissions?.read_global_actively ?? false,
    onFail: node.on_fail || '',
    llmProvider: node.llm?.provider || '',
    llmModel: node.llm?.model || '',
    llmTimeout: node.llm?.timeout || 0,
    llmTemperature: node.llm?.temperature ?? -1,
    maxInternalRounds: node.limits?.max_internal_rounds || 5,
    toolTimeout: node.limits?.tool_timeout_seconds || 60,
  };
}

export function ExpertEditModal({ expertName, theme, onClose, onSaved }: Props) {
  const isCreate = !expertName;
  const [loading, setLoading] = useState(!isCreate);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState('');

  // 在编辑模式下保存原始名称，用于 API 调用（允许改名后仍能找到后端资源）
  const originalNameRef = useRef(expertName || '');

  // 模板 textarea 引用，用于插入占位符到光标位置
  const textareaRef = useRef<HTMLTextAreaElement>(null);
  const contextTemplateRef = useRef('');

  // Form state — 扁平结构，与保存 API 的 body 字段一致
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [isEntry, setIsEntry] = useState(false);
  const [contextIsolation, setContextIsolation] = useState(false);
  const [contextTemplate, setContextTemplate] = useState('');
  // Keep ref in sync for insertPlaceholder
  contextTemplateRef.current = contextTemplate;

  const insertPlaceholder = useCallback((placeholder: string) => {
    const ta = textareaRef.current;
    if (!ta) return;
    const start = ta.selectionStart;
    const end = ta.selectionEnd;
    const current = contextTemplateRef.current;
    const newValue = current.slice(0, start) + placeholder + current.slice(end);
    setContextTemplate(newValue);
    // 光标定位到插入内容之后
    requestAnimationFrame(() => {
      ta.focus();
      ta.selectionStart = ta.selectionEnd = start + placeholder.length;
    });
  }, []);
  const [canModifyPlan, setCanModifyPlan] = useState(true);
  const [canWriteSummary, setCanWriteSummary] = useState(false);
  const [readGlobalActively, setReadGlobalActively] = useState(false);
  const [onFail, setOnFail] = useState('');
  const [llmProvider, setLlmProvider] = useState('');
  const [llmModel, setLlmModel] = useState('');
  const [llmTimeout, setLlmTimeout] = useState(0);
  const [llmTemperature, setLlmTemperature] = useState(-1);
  const [maxInternalRounds, setMaxInternalRounds] = useState(5);
  const [toolTimeout, setToolTimeout] = useState(60);

  // Load existing expert data (edit mode) — 映射 graph node → 扁平表单
  useEffect(() => {
    if (!expertName) return;
    const load = async () => {
      setLoading(true);
      setError('');
      try {
        const raw = await getExpert(expertName);
        // 后端返回的是 graph node 结构，需要映射
        const node = raw as unknown as ExpertNodeJson;
        const f = mapNodeToForm(node);
        setName(f.name);
        setDescription(f.description);
        setIsEntry(f.isEntry);
        setContextIsolation(f.contextIsolation);
        setContextTemplate(f.contextTemplate);
        setCanModifyPlan(f.canModifyPlan);
        setCanWriteSummary(f.canWriteSummary);
        setReadGlobalActively(f.readGlobalActively);
        setOnFail(f.onFail);
        setLlmProvider(f.llmProvider);
        setLlmModel(f.llmModel);
        setLlmTimeout(f.llmTimeout);
        setLlmTemperature(f.llmTemperature);
        setMaxInternalRounds(f.maxInternalRounds);
        setToolTimeout(f.toolTimeout);
        originalNameRef.current = expertName;
      } catch {
        setError('加载专家配置失败');
      }
      setLoading(false);
    };
    load();
  }, [expertName]);

  const handleSave = useCallback(async () => {
    if (!name.trim()) { setError('名称不能为空'); return; }
    setSaving(true);
    setError('');
    try {
      const data: Partial<Exponent> = {
        name: name.trim(), description, is_entry: isEntry,
        context_isolation: contextIsolation, context_template: contextTemplate,
        can_modify_plan: canModifyPlan, can_write_summary: canWriteSummary,
        read_global_actively: readGlobalActively,
        on_fail: onFail,
        llm_provider: llmProvider, llm_model: llmModel,
        llm_timeout: llmTimeout, llm_temperature: llmTemperature,
        max_internal_rounds: maxInternalRounds,
        tool_timeout_seconds: toolTimeout,
      };

      if (isCreate) {
        await createExpert(data);
      } else {
        // 使用 PATCH 而非 PUT：仅更新表单内的字段，不覆盖 visible_tools / next_rules
        await patchExpert(originalNameRef.current, data);
      }
      onSaved();
      onClose();
    } catch {
      setError('保存失败');
    }
    setSaving(false);
  }, [name, description, isEntry, contextIsolation, contextTemplate,
    canModifyPlan, canWriteSummary, readGlobalActively, onFail,
    llmProvider, llmModel, llmTimeout, llmTemperature, maxInternalRounds, toolTimeout,
    isCreate, onSaved, onClose]);

  const handleDelete = useCallback(async () => {
    if (!confirm(`确定删除 Expert "${originalNameRef.current}"？`)) return;
    try {
      await deleteExpert(originalNameRef.current);
      onSaved();
      onClose();
    } catch {
      setError('删除失败');
    }
  }, [onSaved, onClose]);

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
            {isCreate ? '+ 新建 Expert' : `编辑 ${originalNameRef.current}`}
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
              placeholder="例如：my_expert"
              className="form-input" style={{ width: '100%' }} />
          </Field>
          <Field label="角色描述 (description → {role})">
            <textarea value={description} onChange={e => setDescription(e.target.value)} rows={3}
              placeholder="Expert 的角色描述，会注入到 prompt 的 {role} 占位符中"
              className="form-input" style={{ width: '100%', resize: 'vertical' }} />
          </Field>
          <Field label="提示词模板 (context_template)">
            <textarea ref={textareaRef} value={contextTemplate} onChange={e => setContextTemplate(e.target.value)} rows={6}
              placeholder="点击下方占位符积木插入，或自由输入..."
              className="form-input" style={{ width: '100%', resize: 'vertical', fontSize: 10, lineHeight: 1.55, fontFamily: 'monospace' }}
            />
            <div className="flex flex-col gap-1.5" style={{ marginTop: -2 }}>
              {/* 占位符积木条 */}
              <div className="flex flex-wrap gap-1">
                {PLACEHOLDER_CHIPS.map(chip => (
                  <button
                    key={chip.key}
                    onClick={() => insertPlaceholder(chip.key)}
                    title={chip.desc}
                    className="text-[9px] font-semibold rounded cursor-pointer transition-all hover:scale-105 active:scale-95 select-none"
                    style={{
                      padding: '2px 7px',
                      background: chip.bg,
                      color: chip.fg,
                      border: `1px solid ${chip.border}`,
                      letterSpacing: '0.02em',
                    }}
                  >{chip.label}</button>
                ))}
              </div>
              {/* 重置按钮 */}
              <button
                onClick={() => setContextTemplate(DEFAULT_CONTEXT_TEMPLATE)}
                className="text-[9px] text-[var(--text-secondary)] hover:text-[var(--accent-lighter)] underline underline-offset-2 cursor-pointer transition-colors self-start"
                style={{ background: 'none', border: 'none', padding: 0, marginTop: -2 }}
              >↺ 重置为默认模板</button>
            </div>
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

        {/* ── 失败兜底 ── */}
        <Section title="失败兜底">
          <Field label="on_fail 目标">
            <input value={onFail} onChange={e => setOnFail(e.target.value)}
              placeholder="例如：summarizer 或 _done"
              className="form-input" style={{ width: '100%' }} />
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