import type {ApiEnvelope} from '../types';

export class ApiClientError extends Error {
  readonly status: number;
  readonly data: unknown;

  constructor(message: string, status: number, data: unknown) {
    super(message);
    this.name = 'ApiClientError';
    this.status = status;
    this.data = data;
  }
}

function errorMessage(data: unknown, fallback: string) {
  if (data && typeof data === 'object') {
    const err = data as {
      error?: {message?: string};
      message?: string
    };
    return err.error?.message || err.message || fallback;
  }
  if (typeof data === 'string' && data.trim()) return data;
  return fallback;
}

export async function requestJson<T>(
    url: string,
    init: RequestInit = {},
    ): Promise<T> {
  const hasBody = !!init.body;
  const res = await fetch(url, {
    cache: 'no-store',
    ...init,
    headers: hasBody ?
        {'Content-Type': 'application/json', ...(init.headers || {})} :
        (init.headers || {}),
  });
  const text = await res.text();
  let parsed: unknown = {};
  try {
    parsed = text ? JSON.parse(text) : {};
  } catch {
    parsed = text;
  }
  if (!res.ok) {
    throw new ApiClientError(
        errorMessage(parsed, `Request failed: ${res.status}`),
        res.status,
        parsed,
    );
  }
  const envelope = parsed as ApiEnvelope<T>;
  if (envelope && typeof envelope === 'object' && 'success' in envelope) {
    if (envelope.success === false) {
      throw new ApiClientError(
          errorMessage(envelope, 'Request failed'),
          res.status,
          envelope,
      );
    }
    return envelope.data;
  }
  return parsed as T;
}