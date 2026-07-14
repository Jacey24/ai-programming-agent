import { FormEvent, useState } from "react";
import { AlertTriangle, Bot, Check, CheckCircle2, Circle, Clock3, Loader2, RefreshCw, Send, ShieldAlert, Square, Wrench, X, XCircle } from "lucide-react";

import { PermissionPrompt } from "./PermissionPrompt";
import type { AgentState } from "../../store/agentReducer";
import type { CreateTaskInput, PermissionRecord } from "../../types/api";

interface AgentPanelProps {
  state: AgentState;
  onSubmitTask: (input: CreateTaskInput) => void;
  onCancelTask: () => void;
  onRefreshHistory: () => void;
  onResolvePermission: (permissionId: string, approved: boolean) => void;
  className?: string;
}

const defaultForm: CreateTaskInput = {
  sessionTitle: "CodePilot Web Session",
  workspaceName: "codepilot-workspace",
  workspacePath: "./workspace",
  taskInput: "",
  autoRunSafeCommands: true,
  requireFileWritePermission: true,
  maxSteps: 6,
  executionMode: "auto",
};

export function AgentPanel({ state, onSubmitTask, onCancelTask, onRefreshHistory, onResolvePermission, className = "" }: AgentPanelProps) {
  const [form, setForm] = useState<CreateTaskInput>(defaultForm);
  const activeStatus = state.activeTask?.status || (state.chatFallback ? "chat" : "idle");
  const canCancel = Boolean(state.activeTask?.id && !["completed", "failed", "cancelled"].includes(state.activeTask.status || ""));
  const highRiskPermission = state.permissions.items.find((permission) => isHighRisk(permission));
  const highRiskResolving = highRiskPermission ? state.resolvingPermissionIds.includes(highRiskPermission.id) : false;

  const handleSubmit = (event: FormEvent) => {
    event.preventDefault();
    if (!form.taskInput.trim() || state.submitting) {
      return;
    }
    onSubmitTask(form);
  };

  return (
    <aside className={`w-full shrink-0 flex-col border-l border-slate-800 bg-[#0a101b]/95 xl:w-[390px] ${className || "hidden xl:flex"}`}>
      <div className="flex items-center justify-between border-b border-slate-800 bg-black/25 px-4 py-3">
        <div className="flex min-w-0 items-center gap-2">
          <div className="relative grid h-8 w-8 place-items-center rounded-lg border border-cyan-400/25 bg-cyan-400/10">
            <Bot className="h-4 w-4 text-cyan-300" />
            <span className={`absolute -right-0.5 -top-0.5 h-2 w-2 rounded-full ${state.streamStatus === "connected" ? "bg-emerald-400" : state.polling ? "bg-amber-400" : "bg-slate-600"}`} />
          </div>
          <div className="min-w-0">
            <h2 className="truncate font-mono text-sm font-bold uppercase text-slate-100">CodePilot AI Assistant</h2>
            <p className="truncate text-[11px] text-slate-500">
              {state.activeTask?.id || state.chatFallback?.prompt || "ready for real backend task"}
            </p>
          </div>
        </div>
        <span className="rounded border border-slate-700 px-2 py-1 font-mono text-[10px] font-bold uppercase text-slate-400">
          {activeStatus}
        </span>
      </div>

      <div className="custom-scrollbar min-h-0 flex-1 overflow-y-auto">
        <form className="border-b border-slate-800 p-4" onSubmit={handleSubmit}>
          <div className="mb-3 flex items-center justify-between">
            <h3 className="font-mono text-xs font-bold uppercase text-slate-300">Task Input</h3>
            <button
              type="button"
              onClick={onRefreshHistory}
              className="rounded border border-slate-700 p-1 text-slate-500 hover:border-cyan-400/50 hover:text-cyan-200"
              title="Refresh history"
            >
              <RefreshCw className="h-3.5 w-3.5" />
            </button>
          </div>

          <Field label="Session title">
            <input
              value={form.sessionTitle}
              onChange={(event) => setForm((current) => ({ ...current, sessionTitle: event.target.value }))}
              className="h-9 w-full rounded-lg border border-slate-800 bg-black/30 px-3 text-sm text-slate-200"
            />
          </Field>
          <Field label="Workspace name">
            <input
              value={form.workspaceName}
              onChange={(event) => setForm((current) => ({ ...current, workspaceName: event.target.value }))}
              className="h-9 w-full rounded-lg border border-slate-800 bg-black/30 px-3 text-sm text-slate-200"
            />
          </Field>
          <Field label="Workspace path">
            <input
              value={form.workspacePath}
              onChange={(event) => setForm((current) => ({ ...current, workspacePath: event.target.value }))}
              className="h-9 w-full rounded-lg border border-slate-800 bg-black/30 px-3 text-sm text-slate-200"
            />
          </Field>
          <Field label="Prompt">
            <textarea
              rows={7}
              value={form.taskInput}
              onChange={(event) => setForm((current) => ({ ...current, taskInput: event.target.value }))}
              placeholder="Describe a real backend task to run in /workspace."
              className="w-full resize-none rounded-lg border border-slate-800 bg-black/30 p-3 text-sm text-slate-300 placeholder:text-slate-600"
            />
          </Field>

          <div className="mt-3 space-y-2 rounded-lg border border-slate-800 bg-black/20 p-3">
            <label className="flex items-center justify-between gap-3 text-[11px] text-slate-400">
              <span>Auto-run safe commands</span>
              <input
                type="checkbox"
                checked={form.autoRunSafeCommands}
                onChange={(event) => setForm((current) => ({ ...current, autoRunSafeCommands: event.target.checked }))}
                className="h-4 w-4 accent-cyan-400"
              />
            </label>
            <label className="flex items-center justify-between gap-3 text-[11px] text-slate-400">
              <span>Require file-write permission</span>
              <input
                type="checkbox"
                checked={form.requireFileWritePermission}
                onChange={(event) => setForm((current) => ({ ...current, requireFileWritePermission: event.target.checked }))}
                className="h-4 w-4 accent-cyan-400"
              />
            </label>
            <label className="flex items-center justify-between gap-3 text-[11px] text-slate-400">
              <span>Max steps</span>
              <input
                type="number"
                min={1}
                max={50}
                value={form.maxSteps}
                onChange={(event) => setForm((current) => ({ ...current, maxSteps: Number(event.target.value || 10) }))}
                className="h-8 w-20 rounded border border-slate-800 bg-black/40 px-2 text-right text-slate-200"
              />
            </label>
          </div>

          <div className="mt-3 grid grid-cols-[1fr_auto] gap-2">
            <button
              type="submit"
              disabled={!form.taskInput.trim() || state.submitting}
              className="inline-flex h-9 items-center justify-center gap-2 rounded-lg border border-cyan-400/25 bg-cyan-400/10 px-3 text-xs font-bold text-cyan-300 disabled:cursor-not-allowed disabled:opacity-50"
            >
              {state.submitting ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Send className="h-3.5 w-3.5" />}
              Create task
            </button>
            <button
              type="button"
              disabled={!canCancel || state.cancelling}
              onClick={onCancelTask}
              className="inline-flex h-9 items-center justify-center gap-2 rounded-lg border border-slate-700 px-3 text-xs font-bold text-slate-400 disabled:cursor-not-allowed disabled:opacity-50"
              title="Cancel active task"
            >
              {state.cancelling ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Square className="h-3.5 w-3.5" />}
            </button>
          </div>

          {state.error ? (
            <div className="mt-3 rounded-lg border border-rose-500/30 bg-rose-500/10 p-3 text-xs leading-5 text-rose-200">
              {state.error}
            </div>
          ) : null}
        </form>

        <section className="border-b border-slate-800 p-4">
          <div className="mb-3 flex items-center gap-2">
            {state.polling ? <Loader2 className="h-3.5 w-3.5 animate-spin text-amber-300" /> : <Circle className="h-3.5 w-3.5 text-slate-600" />}
            <h3 className="font-mono text-xs font-bold uppercase text-slate-300">Execution State</h3>
          </div>
          <TaskSummary state={state} />
        </section>

        <section className="border-b border-slate-800 p-4">
          <div className="mb-3 flex items-center justify-between gap-2">
            <div className="flex items-center gap-2">
              <Clock3 className="h-3.5 w-3.5 text-cyan-300" />
              <h3 className="font-mono text-xs font-bold uppercase text-slate-300">SSE Steps</h3>
            </div>
            <span className="rounded border border-slate-700 px-2 py-1 font-mono text-[10px] uppercase text-slate-400">
              {state.streamStatus}
            </span>
          </div>
          <StepList state={state} />
        </section>

        <section className="border-b border-slate-800 p-4">
          <div className="mb-3 flex items-center gap-2">
            <Wrench className="h-3.5 w-3.5 text-cyan-300" />
            <h3 className="font-mono text-xs font-bold uppercase text-slate-300">Lifecycle Mapping</h3>
          </div>
          <div className="space-y-2">
            {["POST /api/v1/sessions", "POST /api/v1/workspaces", "POST /api/v1/tasks", "POST /api/v1/tasks/{id}/cancel", "GET /api/v1/tasks"].map((endpoint) => (
              <div key={endpoint} className="rounded-lg border border-slate-800 bg-black/20 px-3 py-2 font-mono text-[11px] text-slate-400">
                {endpoint}
              </div>
            ))}
          </div>
        </section>

        <section className="p-4">
          <div className="mb-3 flex items-center gap-2">
            <ShieldAlert className="h-3.5 w-3.5 text-amber-300" />
            <h3 className="font-mono text-xs font-bold uppercase text-slate-300">Permission Confirmation</h3>
          </div>
          <PermissionQueue state={state} onResolvePermission={onResolvePermission} />
        </section>
      </div>

      <PermissionPrompt
        permission={highRiskPermission || null}
        resolving={highRiskResolving}
        error={state.permissions.status === "error" ? state.permissions.error : ""}
        onResolve={onResolvePermission}
      />
    </aside>
  );
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <label className="mt-3 block">
      <span className="mb-1.5 block text-[11px] font-bold uppercase text-slate-500">{label}</span>
      {children}
    </label>
  );
}

function TaskSummary({ state }: { state: AgentState }) {
  if (state.chatFallback) {
    return (
      <div className="rounded-lg border border-slate-800 bg-black/20 p-4 text-sm leading-6 text-slate-400">
        <div className="mb-2 flex items-center gap-2 text-cyan-300">
          <CheckCircle2 className="h-4 w-4" />
          Chat fallback completed
        </div>
        <div className="font-mono text-[11px] text-slate-500">/chat response: {state.chatFallback.response || "empty"}</div>
      </div>
    );
  }

  if (!state.activeTask) {
    return (
      <div className="rounded-lg border border-dashed border-slate-700/80 bg-black/20 p-4 text-sm leading-6 text-slate-500">
        No active task. Submit a prompt to create a real database-backed task.
      </div>
    );
  }

  const failed = state.activeTask.status === "failed" || state.activeTask.status === "cancelled";
  const done = state.activeTask.status === "completed";
  const Icon = failed ? XCircle : done ? CheckCircle2 : Loader2;

  return (
    <div className="space-y-3 rounded-lg border border-slate-800 bg-black/20 p-4 text-sm">
      <div className="flex items-center gap-2">
        <Icon className={`h-4 w-4 ${failed ? "text-rose-300" : done ? "text-emerald-300" : "animate-spin text-amber-300"}`} />
        <span className="font-mono font-bold uppercase text-slate-200">{state.activeTask.status || "created"}</span>
      </div>
      <dl className="space-y-2 font-mono text-[11px] text-slate-400">
        <Row label="Task" value={state.activeTask.id} />
        <Row label="Session" value={state.activeTask.session_id || state.session?.id || "-"} />
        <Row label="Workspace" value={state.activeTask.workspace_id || state.workspace?.id || "-"} />
        <Row label="Step" value={state.activeTask.current_step || "-"} />
      </dl>
    </div>
  );
}

function StepList({ state }: { state: AgentState }) {
  if (state.streamStatus === "error") {
    return (
      <div className="rounded-lg border border-amber-500/30 bg-amber-500/10 p-3 text-xs leading-5 text-amber-100">
        <div className="mb-1 flex items-center gap-2 font-bold">
          <AlertTriangle className="h-3.5 w-3.5" />
          SSE fallback active
        </div>
        {state.streamError || "Polling is being used for task status and artifacts."}
      </div>
    );
  }

  if (!state.steps.length) {
    return (
      <div className="rounded-lg border border-dashed border-slate-700/80 bg-black/20 p-4 text-sm leading-6 text-slate-500">
        No SSE events yet. Existing logs and artifacts are still loaded from REST endpoints.
      </div>
    );
  }

  return (
    <div className="space-y-2">
      {state.steps.map((step, index) => {
        const Icon = iconForStep(step.status);
        return (
          <article key={step.id} className="rounded-lg border border-slate-800 bg-black/20 p-3">
            <div className="flex items-start gap-2">
              <Icon className={`mt-0.5 h-4 w-4 shrink-0 ${colorForStep(step.status)}`} />
              <div className="min-w-0 flex-1">
                <div className="flex items-center justify-between gap-2">
                  <h4 className="truncate font-mono text-xs font-bold text-slate-200">
                    {index + 1}. {step.title}
                  </h4>
                  <span className="shrink-0 font-mono text-[9px] uppercase text-slate-600">{step.eventType}</span>
                </div>
                <p className="mt-1 line-clamp-3 whitespace-pre-wrap text-[11px] leading-5 text-slate-500">{step.description}</p>
                {step.createdAt ? <div className="mt-1 font-mono text-[9px] text-slate-600">{step.createdAt}</div> : null}
              </div>
            </div>
          </article>
        );
      })}
    </div>
  );
}

function iconForStep(status: AgentState["steps"][number]["status"]) {
  if (status === "success") return CheckCircle2;
  if (status === "failed") return XCircle;
  if (status === "waiting") return AlertTriangle;
  if (status === "running") return Loader2;
  return Circle;
}

function colorForStep(status: AgentState["steps"][number]["status"]) {
  if (status === "success") return "text-emerald-300";
  if (status === "failed") return "text-rose-300";
  if (status === "waiting") return "text-amber-300";
  if (status === "running") return "animate-spin text-cyan-300";
  return "text-slate-600";
}

function Row({ label, value }: { label: string; value: string }) {
  return (
    <div className="grid grid-cols-[76px_minmax(0,1fr)] gap-2">
      <dt className="text-slate-600">{label}</dt>
      <dd className="truncate text-slate-300">{value}</dd>
    </div>
  );
}

function PermissionQueue({
  state,
  onResolvePermission,
}: {
  state: AgentState;
  onResolvePermission: (permissionId: string, approved: boolean) => void;
}) {
  if (state.permissions.status === "loading") {
    return (
      <div className="rounded-lg border border-slate-800 bg-black/20 p-4 text-sm text-slate-500">
        Loading pending permissions from GET /api/v1/permissions/pending.
      </div>
    );
  }

  if (state.permissions.status === "error") {
    return (
      <div className="rounded-lg border border-rose-500/30 bg-rose-500/10 p-4 text-sm leading-6 text-rose-200">
        {state.permissions.error}
      </div>
    );
  }

  if (!state.permissions.items.length) {
    return (
      <div className="rounded-lg border border-dashed border-amber-500/25 bg-amber-500/5 p-4 text-sm leading-6 text-slate-500">
        No pending permissions. New requests will appear here from the backend permission queue.
      </div>
    );
  }

  return (
    <div className="space-y-3">
      {state.permissions.items.map((permission) => {
        const resolving = state.resolvingPermissionIds.includes(permission.id);
        return (
          <article
            key={permission.id}
            className={`rounded-lg border p-3 ${
              isHighRisk(permission)
                ? "border-amber-500/35 bg-amber-500/10"
                : "border-slate-800 bg-black/20"
            }`}
          >
            <div className="mb-2 flex items-start justify-between gap-2">
              <div className="min-w-0">
                <h4 className="truncate font-mono text-xs font-bold text-slate-100">
                  {permission.tool_name || permission.action || "Permission request"}
                </h4>
                <p className="mt-1 truncate font-mono text-[10px] text-slate-500">{permission.id}</p>
              </div>
              <span className={`shrink-0 rounded border px-1.5 py-0.5 font-mono text-[10px] font-bold uppercase ${
                isHighRisk(permission)
                  ? "border-amber-500/40 text-amber-300"
                  : "border-slate-700 text-slate-400"
              }`}>
                {permission.risk_level || "risk"}
              </span>
            </div>
            <pre className="custom-scrollbar mb-3 max-h-24 overflow-auto whitespace-pre-wrap rounded border border-slate-800 bg-black/35 p-2 font-mono text-[10px] leading-4 text-slate-300">
              {permission.reason || permission.action || "No permission details returned."}
            </pre>
            <div className="grid grid-cols-2 gap-2">
              <button
                type="button"
                disabled={resolving}
                onClick={() => onResolvePermission(permission.id, false)}
                className="inline-flex h-8 items-center justify-center gap-1.5 rounded border border-rose-500/35 text-xs font-bold text-rose-300 disabled:cursor-not-allowed disabled:opacity-50"
              >
                {resolving ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <X className="h-3.5 w-3.5" />}
                Reject
              </button>
              <button
                type="button"
                disabled={resolving}
                onClick={() => onResolvePermission(permission.id, true)}
                className="inline-flex h-8 items-center justify-center gap-1.5 rounded border border-emerald-500/35 bg-emerald-500/10 text-xs font-bold text-emerald-300 disabled:cursor-not-allowed disabled:opacity-50"
              >
                {resolving ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Check className="h-3.5 w-3.5" />}
                Approve
              </button>
            </div>
          </article>
        );
      })}
    </div>
  );
}

function isHighRisk(permission: PermissionRecord) {
  const risk = (permission.risk_level || "").toLowerCase();
  return risk.includes("high") || risk.includes("critical") || risk.includes("danger");
}
