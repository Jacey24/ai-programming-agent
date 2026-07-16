import type { CSSProperties, ReactNode } from 'react';

interface GlassWindowProps {
  zIndex: number;
  position?: 'absolute' | 'relative' | 'fixed';
  style?: CSSProperties;
  className?: string;
  children: ReactNode;
  pointerEvents?: CSSProperties['pointerEvents'];
}

/**
 * 玻璃窗口模板 — 抽象自 ToolPanel 的成功实现。
 * 外层 wrapper 负责 position + zIndex + backdrop-filter，
 * 确保毛玻璃效果在 root stacking context 中稳定工作。
 */
export function GlassWindow({
  zIndex,
  position = 'absolute',
  style,
  className,
  children,
  pointerEvents,
}: GlassWindowProps) {
  return (
    <div
      style={{
        position,
        zIndex,
        pointerEvents,
        overflow: 'auto',
        backdropFilter: 'blur(80px) saturate(1.2) brightness(1.05)',
        WebkitBackdropFilter: 'blur(80px) saturate(1.2) brightness(1.05)',
        ...style,
      }}
      className={className}
    >
      {children}
    </div>
  );
}
