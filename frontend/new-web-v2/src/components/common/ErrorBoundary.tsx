import { Component, type ReactNode } from 'react';

interface Props {
  children: ReactNode;
  fallback?: ReactNode;
}

interface State {
  hasError: boolean;
  error: Error | null;
}

export class ErrorBoundary extends Component<Props, State> {
  constructor(props: Props) {
    super(props);
    this.state = { hasError: false, error: null };
  }

  static getDerivedStateFromError(error: Error): State {
    return { hasError: true, error };
  }

  render() {
    if (this.state.hasError) {
      return this.props.fallback || (
        <div style={{
          padding: 24,
          textAlign: 'center',
          color: 'var(--text-secondary)',
          fontSize: 13,
        }}>
          <div style={{ marginBottom: 8 }}>⚠️ 组件加载异常</div>
          <div style={{ fontSize: 11, opacity: 0.6 }}>
            {this.state.error?.message || '未知错误'}
          </div>
          <button
            onClick={() => this.setState({ hasError: false, error: null })}
            className="btn-secondary"
            style={{ marginTop: 12, fontSize: 11, padding: '4px 14px', height: 28 }}
          >
            重试
          </button>
        </div>
      );
    }
    return this.props.children;
  }
}