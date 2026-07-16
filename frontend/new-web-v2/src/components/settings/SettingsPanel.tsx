import { createPortal } from 'react-dom';

interface Props {
  show: boolean;
  theme: 'dark' | 'light';
  scale: number;
  onScaleChange: (s: number) => void;
  onClose: () => void;
}

const SCALE_PRESETS = [0.5, 0.75, 1, 1.25, 1.5];

export function SettingsPanel({ show, theme, scale, onScaleChange, onClose }: Props) {
  if (!show) return null;

  return createPortal(
    <div className={`theme-${theme}`}
      style={{
        position: 'fixed', inset: 0, zIndex: 2000,
        display: 'flex', alignItems: 'center', justifyContent: 'center',
        background: 'rgba(0,0,0,0.15)', backdropFilter: 'blur(4px)',
      }}
      onClick={onClose}
    >
      <div className="glass-panel flex flex-col anim-modal-pop"
        style={{ width: 360, padding: '28px 32px', gap: 22 }}
        onClick={e => e.stopPropagation()}>
        <h2 className="text-base font-semibold text-[var(--text-primary)]">全局设置</h2>

        <div className="flex flex-col gap-2">
          <span className="text-xs text-[var(--text-secondary)] font-medium">界面缩放</span>
          <div className="flex gap-2">
            {SCALE_PRESETS.map(s => (
              <button key={s}
                onClick={() => onScaleChange(s)}
                className="btn-secondary"
                style={{
                  flex: 1, height: 34, fontSize: 13, padding: '0 8px',
                  opacity: scale === s ? 1 : 0.5,
                  border: scale === s ? '1px solid var(--accent-light)' : '1px solid transparent',
                }}>
                {Math.round(s * 100)}%
              </button>
            ))}
          </div>
        </div>

        <div className="flex justify-end gap-2" style={{ marginTop: 8 }}>
          <button onClick={onClose} className="btn-secondary"
            style={{ height: 32, padding: '0 18px', fontSize: 12 }}>关闭</button>
        </div>
      </div>
    </div>,
    document.body,
  );
}