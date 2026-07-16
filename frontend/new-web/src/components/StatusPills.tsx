import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { getActiveTasks, getHealth } from '../api/api';
import type { ActiveTaskState, HealthResponse, PillStatus, PillTone } from '../types';

const toneColors: Record<PillTone, string> = {
  ok: '#22c55e',
  warning: '#f59e0b',
  danger: '#ef4444',
  idle: '#6b7280',
};

// 单个模块在不同宽度阀值下的展示级别
type ShowLevel = 'full' | 'compact' | 'minimal' | 'dot';

// 阀值：容器总宽 / 4 对应每模块可用宽度
const LEVEL_FULL = 220;    // ≥220px → 完整模式
const LEVEL_COMPACT = 140; // ≥140px → 紧凑模式
const LEVEL_MINIMAL = 75;  // ≥75px → 迷你模式
// <75px → 仅圆点

function getLevel(containerWidth: number, sectionCount: number): ShowLevel {
  const perSection = containerWidth / sectionCount;
  if (perSection >= LEVEL_FULL) return 'full';
  if (perSection >= LEVEL_COMPACT) return 'compact';
  if (perSection >= LEVEL_MINIMAL) return 'minimal';
  return 'dot';
}

export function StatusPills() {
  const [health, setHealth] = useState<HealthResponse | null>(null);
  const [activeTasks, setActiveTasks] = useState<ActiveTaskState[]>([]);
  const [healthError, setHealthError] = useState('');
  const containerRef = useRef<HTMLDivElement>(null);
  const [containerWidth, setContainerWidth] = useState(800);

  const refresh = useCallback(async () => {
    try {
      const h = await getHealth();
      setHealth(h);
      setHealthError('');
    } catch (e: unknown) {
      setHealthError(e instanceof Error ? e.message : 'offline');
    }
    try {
      const res = await getActiveTasks();
      setActiveTasks(res.items || []);
    } catch {}
  }, []);

  useEffect(() => {
    refresh();
    const timer = setInterval(refresh, 30_000);
    return () => clearInterval(timer);
  }, [refresh]);

  // ResizeObserver 追踪容器宽度
  useEffect(() => {
    const el = containerRef.current;
    if (!el) return;
    const ro = new ResizeObserver(entries => {
      for (const entry of entries) {
        setContainerWidth(entry.contentRect.width);
      }
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  const pills: PillStatus[] = useMemo(() => {
    const backendOk = health && (health.status === 'healthy' || health.status === 'ok');
    const dbOk = health?.database?.connected;
    const backendStatus: PillTone = healthError ? 'danger' : !health ? 'idle' : backendOk ? 'ok' : 'danger';
    const dbStatus: PillTone = !health ? 'idle' : dbOk ? 'ok' : 'danger';
    const taskCount = activeTasks.length;
    const taskStatus: PillTone = taskCount > 0 ? 'warning' : 'idle';

    return [
      {
        label: 'Backend',
        value: healthError ? 'offline' : !health ? 'checking' : backendOk ? 'online' : 'error',
        detail: health?.service ? `${health.service} ${health.version || ''}`.trim() : healthError || 'GET /api/v1/health',
        tone: backendStatus,
      },
      {
        label: 'Database',
        value: !health ? 'checking' : dbOk ? 'connected' : 'disconnected',
        detail: health?.database?.path || health?.database?.type || (healthError ? 'health check failed' : 'waiting'),
        tone: dbStatus,
      },
      {
        label: 'LLM',
        value: health?.llm ? health.llm.model : '--',
        detail: health?.llm?.provider || 'check config',
        tone: health?.llm ? 'ok' : 'idle',
      },
      {
        label: 'Tasks',
        value: taskCount > 0 ? `${taskCount} active` : 'idle',
        detail: activeTasks.map(t => t.current_expert || 'agent').slice(0, 2).join(', ') || 'no active task',
        tone: taskStatus,
      },
    ];
  }, [health, healthError, activeTasks]);

  const level = getLevel(containerWidth, pills.length);

  return (
    <div
      ref={containerRef}
      style={{
        flex: 1,
        display: 'flex', alignItems: 'center',
        background: 'color-mix(in srgb, var(--bg) 40%, transparent)',
        backdropFilter: 'blur(12px)',
        WebkitBackdropFilter: 'blur(12px)',
        border: '1.5px solid var(--glass-border-strong)',
        borderRadius: 999,
        height: 40,
        maxWidth: '100%',
        padding: '0 12px',
        boxShadow: 'inset 0 1px 0 rgba(255,255,255,0.05), 0 4px 16px var(--shadow)',
      }}
    >
      {pills.map((pill, idx) => (
        <div key={pill.label} style={{
          flex: '1 1 0',
          minWidth: 0,
          display: 'flex', alignItems: 'center', justifyContent: 'flex-start',
          gap: 0,
        }}>
          {/* 分界线（非首项） */}
          {idx > 0 && (
            <div style={{
              width: 1, height: 22,
              background: 'var(--glass-border-strong)',
              marginRight: 12, flexShrink: 0,
            }} />
          )}
          {/* 状态圆点 */}
          <span
            style={{
              width: 8, height: 8, borderRadius: '50%',
              background: toneColors[pill.tone],
              boxShadow: `0 0 6px ${toneColors[pill.tone]}44`,
              flexShrink: 0,
              marginRight: 8,
            }}
          />
          {/* full 模式：标签 + 值 + 详情 */}
          {level === 'full' && (
            <span style={{
              display: 'flex', alignItems: 'baseline', gap: 6,
              minWidth: 0, overflow: 'hidden',
            }}>
              <span style={{ fontSize: 11, fontWeight: 600, color: 'var(--text-secondary)', whiteSpace: 'nowrap', flexShrink: 0 }}>
                {pill.label}
              </span>
              <span style={{
                fontSize: 11, fontWeight: 700, whiteSpace: 'nowrap', flexShrink: 0,
                color: toneColors[pill.tone],
              }}>
                {pill.value}
              </span>
              <span style={{
                fontSize: 10, fontWeight: 400, color: 'var(--text-secondary)', opacity: 0.7,
                whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis',
              }}>
                {pill.detail}
              </span>
            </span>
          )}
          {/* compact 模式：标签 + 值 */}
          {level === 'compact' && (
            <span style={{
              display: 'flex', alignItems: 'baseline', gap: 6,
              minWidth: 0, overflow: 'hidden',
            }}>
              <span style={{ fontSize: 11, fontWeight: 600, color: 'var(--text-secondary)', whiteSpace: 'nowrap', flexShrink: 0 }}>
                {pill.label}
              </span>
              <span style={{
                fontSize: 11, fontWeight: 700, whiteSpace: 'nowrap',
                color: toneColors[pill.tone],
                overflow: 'hidden', textOverflow: 'ellipsis',
              }}>
                {pill.value}
              </span>
            </span>
          )}
          {/* minimal 模式：仅值 */}
          {level === 'minimal' && (
            <span style={{
              fontSize: 11, fontWeight: 700, whiteSpace: 'nowrap',
              color: toneColors[pill.tone],
              overflow: 'hidden', textOverflow: 'ellipsis',
            }}>
              {pill.value}
            </span>
          )}
          {/* dot 模式：仅圆点，无文字 */}
        </div>
      ))}
    </div>
  );
}