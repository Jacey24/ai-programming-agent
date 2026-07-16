interface Props {
  theme: 'dark' | 'light';
}

export function ExpertGraphCanvas({ theme: _theme }: Props) {
  return (
    <div
      style={{
        position: 'absolute',
        inset: 0,
        zIndex: 0,
        pointerEvents: 'none',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
        opacity: 0.15,
      }}
    >
      <svg width="600" height="400" viewBox="0 0 600 400">
        <circle cx="300" cy="100" r="4" fill="var(--accent-light)" />
        <circle cx="200" cy="200" r="4" fill="var(--accent-light)" />
        <circle cx="400" cy="200" r="4" fill="var(--accent-light)" />
        <circle cx="150" cy="300" r="4" fill="var(--accent-light)" />
        <circle cx="300" cy="300" r="4" fill="var(--accent-light)" />
        <circle cx="450" cy="300" r="4" fill="var(--accent-light)" />
        <line x1="300" y1="100" x2="200" y2="200" stroke="var(--accent-light)" strokeWidth="0.5" opacity="0.5" />
        <line x1="300" y1="100" x2="400" y2="200" stroke="var(--accent-light)" strokeWidth="0.5" opacity="0.5" />
        <line x1="200" y1="200" x2="150" y2="300" stroke="var(--accent-light)" strokeWidth="0.5" opacity="0.5" />
        <line x1="200" y1="200" x2="300" y2="300" stroke="var(--accent-light)" strokeWidth="0.5" opacity="0.5" />
        <line x1="400" y1="200" x2="300" y2="300" stroke="var(--accent-light)" strokeWidth="0.5" opacity="0.5" />
        <line x1="400" y1="200" x2="450" y2="300" stroke="var(--accent-light)" strokeWidth="0.5" opacity="0.5" />
      </svg>
    </div>
  );
}