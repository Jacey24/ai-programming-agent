import type { ApiEnvelope } from "../types/api";

export class ApiClientError extends Error {
  readonly status: number;
  readonly data: unknown;

  constructor(message: string, status: number, data: unknown) {
    super(message);
    this.name = "ApiClientError";
    this.status = status;
    this.data = data;
  }
}

function errorMessage(data: unknown, fallback: string) {
  if (data && typeof data === "object") {
    const body = data as { error?: { message?: string }; message?: string };
    return body.error?.message || body.message || fallback;
  }

  if (typeof data === "string" && data.trim()) {
    return data;
  }

  return fallback;
}

export async function requestJson<T>(url: string, init: RequestInit = {}): Promise<T> {
  const response = await fetch(url, {
    cache: "no-store",
    ...init,
    headers: {
      "Content-Type": "application/json",
      ...(init.headers || {}),
    },
  });

  const text = await response.text();
  let parsed: unknown = {};

  try {
    parsed = text ? JSON.parse(text) : {};
  } catch {
    parsed = text;
  }

  if (!response.ok) {
    throw new ApiClientError(errorMessage(parsed, `Request failed: ${response.status}`), response.status, parsed);
  }

  const envelope = parsed as ApiEnvelope<T>;
  if (envelope && typeof envelope === "object" && "success" in envelope) {
    if (envelope.success === false) {
      throw new ApiClientError(errorMessage(envelope, "Request failed"), response.status, envelope);
    }

    return envelope.data;
  }

  return parsed as T;
}

export function isEndpointUnavailable(error: unknown) {
  return error instanceof ApiClientError && [404, 405, 501].includes(error.status);
}
