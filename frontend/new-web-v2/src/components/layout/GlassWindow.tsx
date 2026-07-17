import type { CSSProperties, ReactNode } from 'react';

interface GlassWindowProps {
  zIndex: number;
  position?: 'absolute' | 'relative' | 'fixed';
  style?: CSSProperties;
  className?: string;
  children: ReactNode;
  pointerEvents?: CSSProperties['pointerEvents'];
}

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