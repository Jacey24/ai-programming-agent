import { requestJson } from "./client";
import { endpoints } from "./endpoints";
import type { WorkspaceFileContent, WorkspaceTreePayload } from "../types/api";

export function getWorkspaceTree(workspaceId: string, path = "", depth = 4) {
  const query = new URLSearchParams({
    path,
    depth: String(depth),
  });

  return requestJson<WorkspaceTreePayload>(`${endpoints.workspaceTree(workspaceId)}?${query}`);
}

export function getWorkspaceFileContent(workspaceId: string, path: string) {
  const query = new URLSearchParams({ path });

  return requestJson<WorkspaceFileContent>(`${endpoints.workspaceFileContent(workspaceId)}?${query}`);
}
