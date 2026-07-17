import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import ReactMarkdown from 'react-markdown';
import type { SessionRecord, TaskEventRecord, TaskRecord } from '../types';
import { createTask, cancelTask, listTaskEvents, listTasks, approvePermission, rejectPermission } from '../api/api';
import { ToolCallCard } from './ToolCallCard';

interface Props {
  workspaceId: string;
  session: SessionRecord;
  onBack: () => void;
}

type TimelineItem =
  | { kind: 'msg'; data: ChatMessage; time: string }
  | { kind: 'tool'; data: { toolName: string; status: 'running' | 'success' | 'failed'; arguments?: unknown; output?: string; id: string }; time: string }
  | { kind: 'status'; data: { title: string; detail?: string; status: 'running' | 'success' | 'failed'; id: string }; time: string }
  | { kind: 'perm'; data: { permissionId: string; toolName: string; detail: string; id: string }; time: string }
  | { kind: 'streaming'; data: { content: string }; time: string }
  | { kind: 'loading' }
  | { kind: 'dot-loader' };

interface ChatMessage {
  id: string;
  role: 'user' | 'assistant';
  content: string;
  expert?: string;
  timestamp: string;
}

function objMeta(meta: unknown): Record<string, unknown> {
  return meta && typeof meta === 'object' ? meta as Record<string, unknown> : {};
}

function strMeta(meta: Record<string, unknown>, key: string): string {
  const v = meta[key];
  if (typeof v === 'string') return v;
  if (typeof v === 'boolean' || typeof v === 'number') return String(v);
  return '';
}

// ★ 过滤 LLM 输出中的工具调用语法（如 "file.list {\"path\":\".\",\"depth\":1}"）
// 匹配模式: 工具名(可含.号) 空格 {JSON参数}
const TOOL_CALL_RE = /^[a-z_.]+\s+\{.*\}$/;

function stripToolCalls(content: string): string {
  return content
    .split('\n')
    .filter(line => !TOOL_CALL_RE.test(line.trim()))
    .join('\n')
    .replace(/\n{3,}/g, '\n\n') // 压缩多行空行
    .trim();
}

const TOOL_EVENT_TYPES = ['tool_started', 'tool_output', 'tool_finished'];
const PERM_EVENT_TYPES = ['permission_required', 'permission_resolved'];

function dedupeToolEvents(events: TaskEventRecord[]): TaskEventRecord[] {
  const latest = new Map<string, number>();
  const allTypes = [...TOOL_EVENT_TYPES, ...PERM_EVENT_TYPES];

  events.forEach((evt, idx) => {
    const type = evt.type || '';
    if (!allTypes.includes(type)) return;
    const m = objMeta(evt.metadata);

    if (TOOL_EVENT_TYPES.includes(type)) {
      const key = strMeta(m, 'tool_call_id') || strMeta(m, 'tool_name') || `tool_${idx}`;
      latest.set(key, idx);
    } else if (PERM_EVENT_TYPES.includes(type)) {
      const key = strMeta(m, 'permission_id') || strMeta(m, 'request_id') || strMeta(m, 'tool_name') || `perm_${idx}`;
      latest.set(key, idx);
    }
  });

  return events.filter((evt, idx) => {
    const type = evt.type || '';
    if (!allTypes.includes(type)) return true;
    const m = objMeta(evt.metadata);

    if (TOOL_EVENT_TYPES.includes(type)) {
      const key = strMeta(m, 'tool_call_id') || strMeta(m, 'tool_name') || `tool_${idx}`;
      return latest.get(key) === idx;
    } else if (PERM_EVENT_TYPES.includes(type)) {
      const key = strMeta(m, 'permission_id') || strMeta(m, 'request_id') || strMeta(m, 'tool_name') || `perm_${idx}`;
      return latest.get(key) === idx;
    }
    return true;
  });
}

// 所有需要从持久化记录回放的事件类型
const PERSISTED_EVENT_TYPES = [
  'tool_started', 'tool_output', 'tool_finished',
  'task_planning', 'permission_required', 'permission_resolved',
  'file_changed', 'task_completed', 'task_failed', 'task_cancelled',
];

export function ChatView({ workspaceId, session, onBack }: Props) {
  const [messages, setMessages] = useState<ChatMessage[]>([]);
  const [input, setInput] = useState('');
  const [activeTask, setActiveTask] = useState<TaskRecord | null>(null);
  const [streaming, setStreaming] = useState(false);
  const [loadingHistory, setLoadingHistory] = useState(true);
  const eventSource = useRef<EventSource | null>(null);
  const seenIds = useRef<Set<string>>(new Set());
  const scrollRef = useRef<HTMLDivElement>(null);
  const lastHistoryKey = useRef<string>('');
  const [rawEvents, setRawEvents] = useState<TaskEventRecord[]>([]);

  const [streamingContent, setStreamingContent] = useState<string>('');
  const streamingRef = useRef<string>('');

  const addMessage = useCallback((msg: ChatMessage) => {
    // ★ 过滤工具调用语法
    const cleanMsg = { ...msg, content: stripToolCalls(msg.content) };
    setMessages(prev => prev.find(m => m.id === cleanMsg.id) ? prev : [...prev, cleanMsg]);
  }, []);

  const appendEvents = useCallback((evts: TaskEventRecord[]) => {
    setRawEvents(prev => {
      const existing = new Set(prev.map(e => e.id));
      const next = [...prev];
      for (const e of evts) {
        if (e.id && existing.has(e.id)) continue;
        if (e.id) existing.add(e.id);
        next.push(e);
      }
      return next;
    });
  }, []);

  const closeStream = useCallback(() => {
    eventSource.current?.close();
    eventSource.current = null;
    setStreaming(false);
    streamingRef.current = '';
    setStreamingContent('');
  }, []);

  const startSSE = useCallback((taskId: string) => {
    closeStream();
    seenIds.current.clear();
    setRawEvents([]);
    streamingRef.current = '';
    setStreamingContent('');
    setStreaming(true);
    const source = new EventSource(`/api/v1/tasks/${taskId}/events`);
    eventSource.current = source;

    const handler = (e: MessageEvent) => {
      try {
        const evt: TaskEventRecord = JSON.parse(e.data);
        const evtId = evt.id || `${evt.type}:${evt.created_at}`;
        if (seenIds.current.has(evtId)) return;
        seenIds.current.add(evtId);

        if (evt.type === 'agent_message_chunk' && evt.content) {
          streamingRef.current += evt.content;
          setStreamingContent(streamingRef.current);
          return;
        }

        if (evt.type === 'agent_message' && ((evt.metadata as { channel?: string })?.channel) === 'dialog' && evt.content) {
          const isStreamEnd = (evt.metadata as { stream_end?: boolean })?.stream_end === true;
          if (isStreamEnd && streamingRef.current) {
            addMessage({ id: evtId, role: 'assistant', content: streamingRef.current, expert: (evt.metadata as { expert?: string })?.expert, timestamp: evt.created_at || '' });
            streamingRef.current = '';
            setStreamingContent('');
          } else if (!isStreamEnd) {
            addMessage({ id: evtId, role: 'assistant', content: evt.content, expert: (evt.metadata as { expert?: string })?.expert, timestamp: evt.created_at || '' });
          }
        }

        appendEvents([evt]);
      } catch {}
    };

    source.addEventListener('agent_message', handler);
    source.addEventListener('agent_message_chunk', handler);
    source.addEventListener('tool_started', handler);
    source.addEventListener('tool_output', handler);
    source.addEventListener('tool_finished', handler);
    source.addEventListener('task_planning', handler);
    source.addEventListener('permission_required', handler);
    source.addEventListener('permission_resolved', handler);
    source.addEventListener('file_changed', handler);
    source.addEventListener('task_completed', handler);
    source.addEventListener('task_failed', handler);
    source.addEventListener('task_cancelled', handler);
    source.addEventListener('stream_end', handler);
    source.onerror = () => { setStreaming(false); closeStream(); };
  }, [addMessage, appendEvents, closeStream]);

  useEffect(() => {
    const key = session.id;
    if (key === lastHistoryKey.current) return;
    lastHistoryKey.current = key;
    streamingRef.current = '';
    setStreamingContent('');
    const loadHistory = async () => {
      setLoadingHistory(true);
      try {
        const taskRes = await listTasks(session.id);
        const tasks = (taskRes.items || []).sort((a, b) => (a.created_at || '').localeCompare(b.created_at || ''));
        const historyEvents: TaskEventRecord[] = [];
        for (const t of tasks) {
          if (t.goal) addMessage({ id: `user_${t.id}`, role: 'user', content: t.goal, timestamp: t.created_at || '' });
          try {
            const evtRes = await listTaskEvents(t.id);
            for (const evt of evtRes.items || []) {
              const channel = ((evt.metadata as { channel?: string })?.channel) || '';
              // dialog 消息走 addMessage
              if (channel === 'dialog' && evt.content && evt.type === 'agent_message') {
                addMessage({ id: evt.id || `evt_${t.id}_${evt.created_at}`, role: 'assistant', content: evt.content, expert: (evt.metadata as { expert?: string })?.expert, timestamp: evt.created_at || '' });
              }
              // ★ 工具/权限/状态事件走 appendEvents（关闭后回来也能看到）
              if (PERSISTED_EVENT_TYPES.includes(evt.type || '')) {
                historyEvents.push(evt);
              }
              // ★ 后端将工具调用记录为 agent_message (channel=debug, source=tool)
              // 从 metadata 中提取 tool_name，构造 tool_finished 事件
              if (evt.type === 'agent_message' && channel === 'debug' && evt.content) {
                const src = strMeta((evt.metadata as Record<string, unknown>) || {}, 'source');
                const toolName = strMeta((evt.metadata as Record<string, unknown>) || {}, 'tool_name');
                if (src === 'tool' && toolName) {
                  // 判断成功/失败
                  const content = evt.content || '';
                  const success = !content.includes('[EXECUTION_ERROR]') && !content.includes('异常:');
                  historyEvents.push({
                    ...evt,
                    type: 'tool_finished',
                    metadata: {
                      tool_name: toolName,
                      success,
                    },
                  } as TaskEventRecord);
                }
              }
            }
          } catch {}
        }
        if (historyEvents.length > 0) {
          appendEvents(historyEvents);
        }
      } catch {}
      setLoadingHistory(false);
    };
    setMessages([]);
    setRawEvents([]);
    loadHistory();
  }, [session.id, addMessage, appendEvents]);

  useEffect(() => () => closeStream(), [closeStream]);

  useEffect(() => {
    if (loadingHistory) return;
    const el = scrollRef.current;
    if (el) {
      el.scrollTo({ top: el.scrollHeight, behavior: 'instant' as ScrollBehavior });
    }
  }, [messages, rawEvents, loadingHistory, streamingContent]);

  const handleSubmit = async () => {
    const text = input.trim();
    if (!text || streaming) return;
    setInput('');
    const userMsgId = `user_${Date.now()}`;
    addMessage({ id: userMsgId, role: 'user', content: text, timestamp: new Date().toISOString() });
    try {
      const task = await createTask(session.id, workspaceId, text);
      setActiveTask(task);
      // ★ 用服务端时间戳修正用户消息排序
      if (task.created_at) {
        setMessages(prev => prev.map(m => m.id === userMsgId ? { ...m, timestamp: task.created_at! } : m));
      }
      startSSE(task.id);
    } catch {
      addMessage({ id: `err_${Date.now()}`, role: 'assistant', content: '⚠️ 创建任务失败，请检查后端是否运行', timestamp: new Date().toISOString() });
    }
  };

  const handleCancel = async () => {
    if (!activeTask) return;
    try { await cancelTask(activeTask.id); closeStream(); } catch {}
  };

  const [resolvedPerms, setResolvedPerms] = useState<Set<string>>(new Set());

  const handleApprove = useCallback(async (permId: string) => {
    if (!permId) return;
    try {
      await approvePermission(permId);
      setResolvedPerms(prev => new Set(prev).add(permId));
    } catch {}
  }, []);

  const handleReject = useCallback(async (permId: string) => {
    if (!permId) return;
    try {
      await rejectPermission(permId);
      setResolvedPerms(prev => new Set(prev).add(permId));
    } catch {}
  }, []);

  const unifiedTimeline: TimelineItem[] = useMemo(() => {
    if (loadingHistory) {
      return [{ kind: 'loading' }];
    }

    const items: TimelineItem[] = [];

    for (const msg of messages) {
      items.push({ kind: 'msg', data: msg, time: msg.timestamp });
    }

    const deduped = dedupeToolEvents(rawEvents);
    for (const evt of deduped) {
      const type = evt.type || '';
      const meta = objMeta(evt.metadata);
      const id = evt.id || `${type}:${evt.created_at}`;
      const time = evt.created_at || '';

      if (type === 'tool_started' || type === 'tool_output' || type === 'tool_finished') {
        const toolName = strMeta(meta, 'tool_name') || 'tool';
        let status: 'running' | 'success' | 'failed' = 'running';
        if (type === 'tool_finished') {
          status = strMeta(meta, 'success') === 'false' || meta.success === false ? 'failed' : 'success';
        }
        items.push({ kind: 'tool', data: { toolName, status, arguments: meta.arguments, output: evt.content || undefined, id }, time });
      } else if (type === 'task_planning') {
        items.push({ kind: 'status', data: { title: 'Planning', detail: evt.content || 'Agent is planning the task.', status: 'success', id }, time });
      } else if (type === 'permission_required') {
        const permId = strMeta(meta, 'permission_id') || strMeta(meta, 'request_id') || '';
        const toolName = strMeta(meta, 'tool_name') || 'tool';
        items.push({ kind: 'perm', data: { permissionId: permId, toolName, detail: evt.content || `需要权限确认: ${toolName}`, id }, time });
      } else if (type === 'permission_resolved') {
        items.push({ kind: 'status', data: { title: 'Permission resolved', detail: evt.content || '权限已处理', status: 'success', id }, time });
      } else if (type === 'task_completed') {
        items.push({ kind: 'status', data: { title: '✓ 任务完成', status: 'success', id }, time });
      } else if (type === 'task_failed' || type === 'task_cancelled') {
        const interrupted = type === 'task_failed' && strMeta(meta, 'status') === 'interrupted';
        items.push({ kind: 'status', data: { title: interrupted ? '任务中断' : (evt.content || type), detail: interrupted ? evt.content : undefined, status: 'failed', id }, time });
      } else if (type === 'stream_end') {
        items.push({ kind: 'status', data: { title: 'Stream ended', detail: evt.content || '', status: 'success', id }, time });
      }
    }

    if (streamingContent) {
      items.push({ kind: 'streaming', data: { content: streamingContent }, time: 'z' });
    }

    items.sort((a, b) => {
      if ('time' in a && 'time' in b) {
        return a.time.localeCompare(b.time);
      }
      return 0;
    });

    if (streaming && items.length === 0) {
      items.push({ kind: 'dot-loader' });
    }

    return items;
  }, [messages, rawEvents, loadingHistory, streaming, streamingContent]);

  const TOP_HEIGHT = 44;

  return (
    <div className="flex flex-col h-full">
      <div className="flex items-center shrink-0 border-b border-[var(--glass-border-strong)]" style={{ height: TOP_HEIGHT, padding: '0 20px', gap: 8 }}>
        <button onClick={onBack} className="btn-secondary" style={{ minWidth: 56, height: 30, fontSize: 12, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '0 12px' }}>
          ← 返回
        </button>
        <span className="text-xs text-[var(--text-primary)] truncate flex-1">{session.title || '对话'}</span>
        {streaming && <span className="w-2 h-2 rounded-full bg-[var(--accent-light)] animate-pulse" />}
      </div>

      <div ref={scrollRef} className="flex-1 overflow-y-auto" style={{ padding: '16px 20px', display: 'flex', flexDirection: 'column', gap: 12 }}>
        {loadingHistory && (
          <div className="text-xs text-[var(--text-secondary)] text-center" style={{ padding: '16px 0' }}>加载历史记录...</div>
        )}

        {unifiedTimeline.map(item => {
          if (item.kind === 'loading') return null;

          if (item.kind === 'msg') {
            const msg = item.data;
            return (
              <div key={msg.id} className={`flex anim-slide-up ${msg.role === 'user' ? 'justify-end' : 'justify-start'}`}>
                <div
                  className="max-w-[85%] rounded-xl text-xs leading-relaxed"
                  style={{
                    padding: '8px 12px',
                    background: msg.role === 'user' ? 'var(--bubble-user)' : 'var(--bubble-ai)',
                    border: msg.role === 'user' ? 'none' : '1px solid var(--glass-border)',
                    color: msg.role === 'user' ? '#fff' : 'var(--text-primary)',
                  }}
                >
                  {msg.expert && (
                    <div className="text-[10px] text-[var(--accent-lighter)] font-medium" style={{ marginBottom: 2 }}>
                      [{msg.expert}]
                    </div>
                  )}
                  <div className="markdown-body">
                    <ReactMarkdown>{msg.content}</ReactMarkdown>
                  </div>
                </div>
              </div>
            );
          }

          if (item.kind === 'streaming') {
            return (
              <div key="streaming-bubble" className="flex justify-start">
                <div
                  className="max-w-[85%] rounded-xl text-xs leading-relaxed"
                  style={{
                    padding: '8px 12px',
                    background: 'var(--bubble-ai)',
                    border: '1px solid var(--glass-border)',
                    color: 'var(--text-primary)',
                  }}
                >
                  <div className="markdown-body">
                    <ReactMarkdown>{item.data.content}</ReactMarkdown>
                    <span style={{
                      display: 'inline-block',
                      width: 1,
                      height: 14,
                      background: 'var(--accent-light)',
                      marginLeft: 1,
                      verticalAlign: 'text-bottom',
                      animation: 'blink 1s step-end infinite',
                    }} />
                  </div>
                </div>
              </div>
            );
          }

          if (item.kind === 'dot-loader') {
            return (
              <div key="dot-loader" className="flex justify-start">
                <div className="rounded-xl bg-[var(--bubble-ai)] border border-[var(--glass-border)]" style={{ padding: '8px 12px', display: 'inline-flex', gap: 4 }}>
                  <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--accent-light)', animation: 'bounce 0.6s infinite', animationDelay: '0ms' }} />
                  <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--accent-light)', animation: 'bounce 0.6s infinite', animationDelay: '150ms' }} />
                  <span style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--accent-light)', animation: 'bounce 0.6s infinite', animationDelay: '300ms' }} />
                </div>
              </div>
            );
          }

          if (item.kind === 'tool') {
            return <ToolCallCard key={item.data.id} {...item.data} />;
          }

          if (item.kind === 'perm') {
            const isResolved = resolvedPerms.has(item.data.permissionId);
            return (
              <div key={item.data.id} className="flex justify-start">
                <div className="rounded-xl border flex flex-col gap-2" style={{
                  padding: '10px 14px',
                  maxWidth: '85%',
                  background: 'rgba(59,130,246,0.08)',
                  borderColor: 'rgba(59,130,246,0.35)',
                }}>
                  <div className="flex items-start gap-2">
                    <span style={{
                      width: 8, height: 8, borderRadius: '50%',
                      background: '#3b82f6', flexShrink: 0, marginTop: 2,
                      animation: 'pulse 1.5s infinite',
                    }} />
                    <div className="flex-1">
                      <div className="text-xs font-bold text-[var(--text-primary)]">需要权限确认</div>
                      <div className="text-[10px] text-[var(--text-secondary)] mt-0.5">{item.data.detail}</div>
                      <div className="text-[10px] text-[var(--accent-lighter)] mt-0.5">工具: {item.data.toolName}</div>
                    </div>
                  </div>
                  {!isResolved && item.data.permissionId && (
                    <div className="flex gap-2" style={{ paddingLeft: 16 }}>
                      <button
                        onClick={() => handleApprove(item.data.permissionId)}
                        className="btn-primary"
                        style={{ height: 26, fontSize: 11, padding: '0 14px', minWidth: 52, borderRadius: 6 }}
                      >
                        批准
                      </button>
                      <button
                        onClick={() => handleReject(item.data.permissionId)}
                        className="btn-danger"
                        style={{ height: 26, fontSize: 11, padding: '0 14px', minWidth: 52, borderRadius: 6 }}
                      >
                        拒绝
                      </button>
                    </div>
                  )}
                  {isResolved && (
                    <div className="text-[10px] text-[var(--accent-lighter)]" style={{ paddingLeft: 16 }}>已处理</div>
                  )}
                </div>
              </div>
            );
          }

          if (item.kind === 'status') {
            return (
              <div key={item.data.id} className="flex justify-start">
                <div className="rounded-xl border flex items-start gap-2" style={{
                  padding: '8px 12px',
                  background: item.data.status === 'running' ? 'rgba(59,130,246,0.1)' : item.data.status === 'failed' ? 'rgba(239,68,68,0.1)' : 'rgba(34,197,94,0.1)',
                  borderColor: item.data.status === 'running' ? 'rgba(59,130,246,0.3)' : item.data.status === 'failed' ? 'rgba(239,68,68,0.3)' : 'rgba(34,197,94,0.3)',
                }}>
                  <span style={{
                    width: 8, height: 8, borderRadius: '50%',
                    background: item.data.status === 'running' ? '#3b82f6' : item.data.status === 'failed' ? '#ef4444' : '#22c55e',
                    flexShrink: 0, marginTop: 2,
                    animation: item.data.status === 'running' ? 'pulse 1.5s infinite' : 'none',
                  }} />
                  <div>
                    <div className="text-xs font-bold text-[var(--text-primary)]">{item.data.title}</div>
                    {item.data.detail && <div className="text-[10px] text-[var(--text-secondary)] mt-0.5">{item.data.detail}</div>}
                  </div>
                </div>
              </div>
            );
          }

          return null;
        })}

        {!loadingHistory && messages.length === 0 && rawEvents.length === 0 && !streaming && (
          <div className="text-xs text-[var(--text-secondary)] text-center" style={{ padding: '32px 0' }}>发送消息开始</div>
        )}
      </div>

      <div className="shrink-0 border-t border-[var(--glass-border-strong)]" style={{ padding: '16px 20px' }}>
        <div className="flex" style={{ gap: 8 }}>
          <input
            value={input} onChange={e => setInput(e.target.value)}
            onKeyDown={e => e.key === 'Enter' && !e.shiftKey && handleSubmit()}
            placeholder="输入你的需求..." disabled={streaming}
            style={{ flex: 1, height: 40, padding: '0 16px', fontSize: 12 }}
            className="rounded-lg bg-[var(--bg)] border border-[var(--glass-border-strong)] text-[var(--text-primary)] placeholder:text-[var(--text-secondary)]/50 focus:outline-none focus:border-[var(--accent-light)] disabled:opacity-50"
          />
          {streaming ? (
            <button onClick={handleCancel} className="btn-danger" style={{ minWidth: 56, height: 40, fontSize: 12 }}>停止</button>
          ) : (
            <button onClick={handleSubmit} className="btn-primary" style={{ minWidth: 56, height: 40, fontSize: 12 }}>发送</button>
          )}
        </div>
      </div>
    </div>
  );
}
