import { AlertTriangle, Bot, Check, Circle, Loader2, ShieldAlert, User, X } from "lucide-react";

import { FileChangeCard } from "./FileChangeCard";
import { ToolCallCard } from "./ToolCallCard";
import type { AgentState } from "../../store/agentReducer";
import type { PermissionRecord, TaskEventRecord, ToolCallRecord } from "../../types/api";

interface ChatMessageListProps {
  state: AgentState;
  onOpenFile: (path: string) => void;
  onResolvePermission: (permissionId: string, approved: boolean) => void;
}

export function ChatMessageList({ state, onOpenFile, onResolvePermission }: ChatMessageListProps) {
  const events = state.events.items.slice().reverse();
  const pendingPermissionIds = new Set(state.permissions.items.map((permission) => permission.id));

  return (
    <div className="custom-scrollbar min-h-0 flex-1 space-y-3 overflow-y-auto p-4">
      {state.activeTask?.goal ? (
        <Bubble icon={User} tone="user">
          {state.activeTask.goal}
        </Bubble>
      ) : null}

      {!events.length && !state.activeTask ? (
        <div className="grid min-h-64 place-items-center text-center">
          <div>
            <Bot className="mx-auto mb-3 h-10 w-10 text-slate-700" />
            <div className="font-mono text-sm font-bold text-slate-300">Code workspace ready</div>
            <p className="mt-2 max-w-xs text-sm leading-6 text-slate-500">Messages and tool activity will appear here.</p>
          </div>
        </div>
      ) : null}

      {events.map((event, index) => (
        <EventView
          key={event.id || `${event.type}-${index}`}
          event={event}
          toolCalls={state.toolCalls.items}
          permissions={state.permissions.items}
          pendingPermissionIds={pendingPermissionIds}
          resolvingPermissionIds={state.resolvingPermissionIds}
          onOpenFile={onOpenFile}
          onResolvePermission={onResolvePermission}
        />
      ))}

      {state.permissions.items
        .filter((permission) => !events.some((event) => event.type === "permission_required" && metadataPermissionId(event.metadata) === permission.id))
        .map((permission) => (
          <PermissionCard
            key={permission.id}
            permission={permission}
            resolving={state.resolvingPermissionIds.includes(permission.id)}
            onResolvePermission={onResolvePermission}
          />
        ))}

      {state.error ? (
        <Bubble icon={AlertTriangle} tone="error">
          {state.error}
        </Bubble>
      ) : null}
    </div>
  );
}

function EventView({
  event,
  toolCalls,
  permissions,
  resolvingPermissionIds,
  onOpenFile,
  onResolvePermission,
}: {
  event: TaskEventRecord;
  toolCalls: ToolCallRecord[];
  permissions: PermissionRecord[];
  pendingPermissionIds: Set<string>;
  resolvingPermissionIds: string[];
  onOpenFile: (path: string) => void;
  onResolvePermission: (permissionId: string, approved: boolean) => void;
}) {
  const type = event.type || "";
  const metadata = objectMetadata(event.metadata);

  if (type === "task_planning") {
    return <StatusMessage title="Planning" detail={event.content || "Agent is planning the task."} status="running" />;
  }

  if (type === "agent_message") {
    return <Bubble icon={Bot} tone="assistant">{event.content || ""}</Bubble>;
  }

  if (type === "tool_started" || type === "tool_finished" || type === "tool_output") {
    const toolName = stringMeta(metadata, "tool_name") || "tool";
    const matchingCall = findToolCall(toolCalls, toolName);
    return (
      <ToolCallCard
        toolName={toolName}
        status={type === "tool_started" ? "running" : stringMeta(metadata, "success") === "false" || metadata.success === false ? "failed" : "success"}
        arguments={metadata.arguments ?? safeJson(matchingCall?.arguments)}
        output={event.content || matchingCall?.result}
      />
    );
  }

  if (type === "permission_required") {
    const permissionId = metadataPermissionId(event.metadata);
    const permission = permissions.find((item) => item.id === permissionId) || eventToPermission(event);
    return (
      <PermissionCard
        permission={permission}
        resolving={resolvingPermissionIds.includes(permission.id)}
        onResolvePermission={onResolvePermission}
      />
    );
  }

  if (type === "file_changed") {
    const path = stringMeta(metadata, "path");
    if (!path) {
      return <StatusMessage title="File changed" detail={event.content || "File change event received."} status="success" />;
    }
    return (
      <FileChangeCard
        path={path}
        changeType={stringMeta(metadata, "change_type")}
        toolName={stringMeta(metadata, "tool_name")}
        onOpenFile={onOpenFile}
      />
    );
  }

  if (type === "task_completed") {
    return <Bubble icon={Bot} tone="assistant">{event.content || "Task completed."}</Bubble>;
  }

  if (type === "task_failed" || type === "task_cancelled") {
    return <Bubble icon={AlertTriangle} tone="error">{event.content || type}</Bubble>;
  }

  if (type === "task_created") {
    return <StatusMessage title="Task created" detail={event.content || event.task_id || ""} status="success" />;
  }

  return <StatusMessage title={type || "Event"} detail={event.content || ""} status="running" />;
}

function Bubble({ icon: Icon, tone, children }: { icon: typeof Bot; tone: "user" | "assistant" | "error"; children: React.ReactNode }) {
  const user = tone === "user";
  const error = tone === "error";
  return (
    <div className={`flex gap-3 ${user ? "justify-end" : ""}`}>
      {!user ? <Icon className={`mt-1 h-4 w-4 shrink-0 ${error ? "text-rose-300" : "text-cyan-300"}`} /> : null}
      <div className={`max-w-[88%] rounded px-3 py-2 text-sm leading-6 ${
        user ? "bg-cyan-400/15 text-cyan-50" : error ? "border border-rose-500/30 bg-rose-500/10 text-rose-100" : "border border-slate-800 bg-black/25 text-slate-200"
      }`}>
        {children}
      </div>
      {user ? <Icon className="mt-1 h-4 w-4 shrink-0 text-cyan-300" /> : null}
    </div>
  );
}

function StatusMessage({ title, detail, status }: { title: string; detail?: string; status: "running" | "success" | "failed" }) {
  const Icon = status === "running" ? Loader2 : status === "success" ? Circle : AlertTriangle;
  return (
    <div className="flex items-start gap-2 rounded border border-slate-800 bg-black/15 p-3">
      <Icon className={`mt-0.5 h-4 w-4 ${status === "running" ? "animate-spin text-cyan-300" : status === "success" ? "text-emerald-300" : "text-rose-300"}`} />
      <div className="min-w-0">
        <div className="font-mono text-xs font-bold text-slate-200">{title}</div>
        {detail ? <div className="mt-1 whitespace-pre-wrap text-xs leading-5 text-slate-500">{detail}</div> : null}
      </div>
    </div>
  );
}

function PermissionCard({
  permission,
  resolving,
  onResolvePermission,
}: {
  permission: PermissionRecord;
  resolving: boolean;
  onResolvePermission: (permissionId: string, approved: boolean) => void;
}) {
  return (
    <article className="rounded border border-amber-500/30 bg-amber-500/10 p-3">
      <div className="mb-2 flex items-start gap-2">
        <ShieldAlert className="mt-0.5 h-4 w-4 shrink-0 text-amber-300" />
        <div className="min-w-0 flex-1">
          <div className="truncate font-mono text-xs font-bold text-amber-100">{permission.tool_name || permission.action || "Permission required"}</div>
          <div className="mt-1 truncate font-mono text-[10px] text-amber-200/60">{permission.id}</div>
        </div>
      </div>
      <pre className="custom-scrollbar mb-3 max-h-32 overflow-auto whitespace-pre-wrap rounded border border-slate-800 bg-black/35 p-2 font-mono text-[11px] leading-5 text-slate-200">
        {permission.reason || permission.action || "Approval is required to continue."}
      </pre>
      <div className="grid grid-cols-2 gap-2">
        <button
          type="button"
          disabled={resolving}
          onClick={() => onResolvePermission(permission.id, false)}
          className="inline-flex h-8 items-center justify-center gap-1.5 rounded border border-rose-500/35 text-xs font-bold text-rose-300 disabled:opacity-50"
        >
          {resolving ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <X className="h-3.5 w-3.5" />}
          Reject
        </button>
        <button
          type="button"
          disabled={resolving}
          onClick={() => onResolvePermission(permission.id, true)}
          className="inline-flex h-8 items-center justify-center gap-1.5 rounded border border-emerald-500/35 bg-emerald-500/10 text-xs font-bold text-emerald-300 disabled:opacity-50"
        >
          {resolving ? <Loader2 className="h-3.5 w-3.5 animate-spin" /> : <Check className="h-3.5 w-3.5" />}
          Approve
        </button>
      </div>
    </article>
  );
}

function objectMetadata(metadata: unknown): Record<string, unknown> {
  return metadata && typeof metadata === "object" ? (metadata as Record<string, unknown>) : {};
}

function stringMeta(metadata: Record<string, unknown>, key: string) {
  const value = metadata[key];
  if (typeof value === "string") return value;
  if (typeof value === "boolean") return String(value);
  if (typeof value === "number") return String(value);
  return "";
}

function metadataPermissionId(metadata: unknown) {
  const object = objectMetadata(metadata);
  return stringMeta(object, "permission_id") || stringMeta(object, "id");
}

function eventToPermission(event: TaskEventRecord): PermissionRecord {
  const metadata = objectMetadata(event.metadata);
  return {
    id: metadataPermissionId(event.metadata) || event.id || "permission",
    task_id: event.task_id,
    tool_name: stringMeta(metadata, "tool_name"),
    reason: event.content,
    status: "pending",
  };
}

function findToolCall(toolCalls: ToolCallRecord[], toolName: string) {
  return toolCalls
    .slice()
    .reverse()
    .find((call) => call.tool_name === toolName);
}

function safeJson(value: string | undefined) {
  if (!value) return undefined;
  try {
    return JSON.parse(value) as unknown;
  } catch {
    return value;
  }
}
