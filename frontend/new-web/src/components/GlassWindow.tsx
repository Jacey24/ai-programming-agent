import type { CSSProperties, ReactNode } from 'react';

interface GlassWindowProps {
  zIndex: number;
  position?: 'absolute' | 'relative' | 'fixed';
  style?: CSSProperties;
  className?: string;
  children: ReactNode;
  pointerEvents?: CSSProperties['pointerEvents'];
  overflow?: CSSProperties['overflow'];
}

/**
 * 玻璃窗口模板 — 负责 position + zIndex + backdrop-filter，
 * 确保毛玻璃效果在 root stacking context 中稳定工作。
 * overflow 默认为 hidden，由调用方按需覆盖。
 */
export function GlassWindow({
  zIndex,
  position = 'absolute',
  style,
  className,
  children,
  pointerEvents,
  overflow,
}: GlassWindowProps) {
  return (
    <div
      style={{
        position,
        zIndex,
        pointerEvents,
        overflow: overflow ?? 'hidden',
        ...style,
      }}
      className={`glass-surface ${className || ''}`}
    >
      {children}
    </div>
  );
}
