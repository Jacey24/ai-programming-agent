import { useEffect } from "react";
import { AlertTriangle, Check, Loader2, ShieldAlert, X } from "lucide-react";

import type { PermissionRecord } from "../../types/api";

interface PermissionPromptProps {
  permission: PermissionRecord | null;
  resolving: boolean;
  error?: string;
  onResolve: (permissionId: string, approved: boolean) => void;
}

export function PermissionPrompt({ permission, resolving, error, onResolve }: PermissionPromptProps) {
  useEffect(() => {
    if (!permission || resolving) {
      return undefined;
    }

    const handleKeyDown = (event: KeyboardEvent) => {
      const key = event.key.toLowerCase();
      if (key !== "y" && key !== "n") {
        return;
      }

      event.preventDefault();
      onResolve(permission.id, key === "y");
    };

    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [onResolve, permission, resolving]);

  if (!permission) {
    return null;
  }

  const action = permission.action || permission.tool_name || "permission request";
  const reason = permission.reason || "No reason was returned by the backend.";

  return (
    <div className="fixed inset-0 z-50 grid place-items-center bg-black/75 p-4 backdrop-blur-sm">
      <div className="w-full max-w-lg overflow-hidden rounded-lg border-2 border-amber-400 bg-[#0f172a] text-slate-100 shadow-[0_0_34px_rgba(245,158,11,0.25)]">
        <div className="flex items-center justify-between bg-amber-400 px-4 py-3 font-mono font-bold uppercase text-black">
          <span className="flex items-center gap-2">
            <ShieldAlert className="h-5 w-5" />
            High Risk Permission
          </span>
          <span className="rounded border border-black/30 px-1.5 py-0.5 text-[10px]">Y / N</span>
        </div>

        <div className="space-y-4 p-5">
          <div className="flex gap-3">
            <AlertTriangle className="h-10 w-10 shrink-0 text-amber-300" />
            <div>
              <h2 className="font-mono text-sm font-bold text-white">Agent requests approval to continue</h2>
              <p className="mt-2 text-sm leading-6 text-slate-400">
                This approval uses the real backend permission id. Closing the dialog is intentionally disabled; choose approve or reject.
              </p>
            </div>
          </div>

          <dl className="space-y-2 rounded-lg border border-slate-800 bg-black/35 p-3 font-mono text-xs">
            <Row label="Permission" value={permission.id} />
            <Row label="Task" value={permission.task_id || "-"} />
            <Row label="Tool" value={permission.tool_name || "-"} />
            <Row label="Risk" value={permission.risk_level || "-"} strong />
          </dl>

          <div className="rounded-lg border border-amber-400/20 bg-black/45 p-3">
            <div className="mb-1 font-mono text-[10px] font-bold uppercase text-amber-300">Action</div>
            <pre className="custom-scrollbar max-h-32 overflow-auto whitespace-pre-wrap font-mono text-xs leading-5 text-cyan-100">
              {action}
            </pre>
          </div>

          <div className="rounded-lg border border-slate-800 bg-black/35 p-3">
            <div className="mb-1 font-mono text-[10px] font-bold uppercase text-slate-500">Reason / Arguments</div>
            <pre className="custom-scrollbar max-h-36 overflow-auto whitespace-pre-wrap font-mono text-xs leading-5 text-slate-300">
              {reason}
            </pre>
          </div>

          {error ? (
            <div className="rounded-lg border border-rose-500/35 bg-rose-500/10 p-3 text-xs leading-5 text-rose-200">
              {error}
            </div>
          ) : null}

          <div className="flex flex-col justify-between gap-3 border-t border-slate-800 pt-4 sm:flex-row sm:items-center">
            <span className="text-[10px] text-slate-500">
              Press <kbd className="rounded border border-slate-700 bg-slate-900 px-1 text-white">Y</kbd> to approve,
              {" "}
              <kbd className="rounded border border-slate-700 bg-slate-900 px-1 text-white">N</kbd> to reject.
            </span>
            <div className="flex gap-2">
              <button
                type="button"
                disabled={resolving}
                onClick={() => onResolve(permission.id, false)}
                className="inline-flex items-center gap-2 rounded-lg border border-rose-500/40 px-4 py-2 text-xs font-bold text-rose-300 disabled:cursor-not-allowed disabled:opacity-50"
              >
                {resolving ? <Loader2 className="h-4 w-4 animate-spin" /> : <X className="h-4 w-4" />}
                Reject [N]
              </button>
              <button
                type="button"
                disabled={resolving}
                onClick={() => onResolve(permission.id, true)}
                className="inline-flex items-center gap-2 rounded-lg bg-amber-400 px-4 py-2 text-xs font-bold text-black disabled:cursor-not-allowed disabled:opacity-50"
              >
                {resolving ? <Loader2 className="h-4 w-4 animate-spin" /> : <Check className="h-4 w-4" />}
                Approve [Y]
              </button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}

function Row({ label, value, strong = false }: { label: string; value: string; strong?: boolean }) {
  return (
    <div className="grid grid-cols-[86px_minmax(0,1fr)] gap-2">
      <dt className="text-slate-600">{label}</dt>
      <dd className={`truncate ${strong ? "font-bold uppercase text-amber-300" : "text-slate-300"}`}>{value}</dd>
    </div>
  );
}
