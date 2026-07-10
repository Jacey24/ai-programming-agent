import { FormEvent, useState } from "react";
import { Loader2, Send, Square } from "lucide-react";

import type { CreateTaskInput } from "../../types/api";

const defaultForm: CreateTaskInput = {
  sessionTitle: "CodePilot Web Session",
  workspaceName: "codepilot-workspace",
  workspacePath: "/workspace",
  taskInput: "",
  autoRunSafeCommands: true,
  requireFileWritePermission: true,
  maxSteps: 10,
};

interface ChatComposerProps {
  submitting: boolean;
  canCancel: boolean;
  cancelling: boolean;
  onSubmit: (input: CreateTaskInput) => void;
  onCancel: () => void;
}

export function ChatComposer({ submitting, canCancel, cancelling, onSubmit, onCancel }: ChatComposerProps) {
  const [form, setForm] = useState<CreateTaskInput>(defaultForm);
  const [advancedOpen, setAdvancedOpen] = useState(false);

  const handleSubmit = (event: FormEvent) => {
    event.preventDefault();
    if (!form.taskInput.trim() || submitting) {
      return;
    }
    onSubmit(form);
    setForm((current) => ({ ...current, taskInput: "" }));
  };

  return (
    <form className="border-t border-slate-800 bg-black/20 p-3" onSubmit={handleSubmit}>
      <textarea
        rows={4}
        value={form.taskInput}
        onChange={(event) => setForm((current) => ({ ...current, taskInput: event.target.value }))}
        placeholder="Ask the agent to create or modify files in /workspace."
        className="custom-scrollbar w-full resize-none rounded border border-slate-800 bg-black/35 p-3 text-sm leading-6 text-slate-200 placeholder:text-slate-600"
      />
      <div className="mt-2 flex items-center gap-2">
        <button
          type="submit"
          disabled={!form.taskInput.trim() || submitting}
          className="inline-flex h-9 flex-1 items-center justify-center gap-2 rounded border border-cyan-400/30 bg-cyan-400/10 px-3 text-xs font-bold text-cyan-200 disabled:cursor-not-allowed disabled:opacity-50"
        >
          {submitting ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Send className="h-3.5 w-3.5" />}
          Send
        </button>
        <button
          type="button"
          disabled={!canCancel || cancelling}
          onClick={onCancel}
          className="grid h-9 w-10 place-items-center rounded border border-slate-700 text-slate-400 disabled:cursor-not-allowed disabled:opacity-50"
          title="Stop active task"
        >
          {cancelling ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Square className="h-3.5 w-3.5" />}
        </button>
      </div>

      <button
        type="button"
        onClick={() => setAdvancedOpen((value) => !value)}
        className="mt-2 font-mono text-[10px] uppercase text-slate-500 hover:text-slate-300"
      >
        Advanced settings
      </button>
      {advancedOpen ? (
        <div className="mt-2 space-y-2 rounded border border-slate-800 bg-black/25 p-3">
          <SmallInput label="Session" value={form.sessionTitle} onChange={(value) => setForm((current) => ({ ...current, sessionTitle: value }))} />
          <SmallInput label="Workspace" value={form.workspaceName} onChange={(value) => setForm((current) => ({ ...current, workspaceName: value }))} />
          <SmallInput label="Path" value={form.workspacePath} onChange={(value) => setForm((current) => ({ ...current, workspacePath: value }))} />
          <label className="flex items-center justify-between gap-3 text-[11px] text-slate-400">
            <span>Auto-run safe commands</span>
            <input type="checkbox" checked={form.autoRunSafeCommands} onChange={(event) => setForm((current) => ({ ...current, autoRunSafeCommands: event.target.checked }))} className="h-4 w-4 accent-cyan-400" />
          </label>
          <label className="flex items-center justify-between gap-3 text-[11px] text-slate-400">
            <span>Require file write permission</span>
            <input type="checkbox" checked={form.requireFileWritePermission} onChange={(event) => setForm((current) => ({ ...current, requireFileWritePermission: event.target.checked }))} className="h-4 w-4 accent-cyan-400" />
          </label>
          <SmallInput label="Max steps" type="number" value={String(form.maxSteps)} onChange={(value) => setForm((current) => ({ ...current, maxSteps: Number(value || 10) }))} />
        </div>
      ) : null}
    </form>
  );
}

function SmallInput({ label, value, onChange, type = "text" }: { label: string; value: string; onChange: (value: string) => void; type?: string }) {
  return (
    <label className="grid grid-cols-[72px_minmax(0,1fr)] items-center gap-2 text-[11px] text-slate-500">
      <span>{label}</span>
      <input
        type={type}
        value={value}
        onChange={(event) => onChange(event.target.value)}
        className="h-7 min-w-0 rounded border border-slate-800 bg-black/40 px-2 text-xs text-slate-200"
      />
    </label>
  );
}
