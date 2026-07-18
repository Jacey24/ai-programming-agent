import {useCallback, useState} from 'react';
import type {Dispatch, SetStateAction} from 'react';

/**
 * 与 localStorage 双向绑定的 useState。
 * - 初始化时从 localStorage 读取，失败则用 defaultValue
 * - setState 时自动同步写入 localStorage
 * - 多个 tab 不互相同步（适合 UI 偏好类状态）
 */
export function useStickyState<T>(
    key: string, defaultValue: T): [T, Dispatch<SetStateAction<T>>] {
  const [value, setValue] = useState<T>(() => {
    try {
      const raw = localStorage.getItem(key);
      if (raw === null) return defaultValue;
      return JSON.parse(raw) as T;
    } catch {
      return defaultValue;
    }
  });

  const setStickyValue: Dispatch<SetStateAction<T>> = useCallback(
      (next: SetStateAction<T>) => {
        setValue((prev) => {
          const resolved = typeof next === 'function' ?
              (next as (prevState: T) => T)(prev) :
              next;
          try {
            localStorage.setItem(key, JSON.stringify(resolved));
          } catch {
            // localStorage 写入失败（如配额满）静默忽略
          }
          return resolved;
        });
      },
      [key],
  );

  return [value, setStickyValue];
}