import type { LucideIcon } from "lucide-react";

export type ActivityView = "explorer" | "history" | "permissions" | "settings";

export type EditorTabId = "overview" | "logs" | "tools" | "changes" | "replay";

export type ConnectionTone = "idle" | "ok" | "warning" | "danger";

export interface HeaderStatus {
  label: string;
  value: string;
  detail: string;
  tone: ConnectionTone;
}

export interface ActivityItem {
  id: ActivityView;
  label: string;
  icon: LucideIcon;
}

export interface WorkspaceSection {
  id: string;
  title: string;
  description: string;
  emptyText: string;
}

export interface EditorTab {
  id: EditorTabId;
  label: string;
  filename: string;
}

export interface AppState {
  activeView: ActivityView;
  activeEditorTab: EditorTabId;
}

export type AppAction =
  | { type: "setActiveView"; view: ActivityView }
  | { type: "setActiveEditorTab"; tab: EditorTabId };
