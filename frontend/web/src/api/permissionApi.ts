import { requestJson } from "./client";
import { endpoints } from "./endpoints";
import type { PermissionRecord } from "../types/api";

export function listPendingPermissions(taskId?: string) {
  const query = new URLSearchParams({ status: "pending" });
  if (taskId) {
    query.set("task_id", taskId);
  }
  return requestJson<{ items: PermissionRecord[] }>(`${endpoints.permissionsPending}?${query}`);
}

export function approvePermission(permissionId: string) {
  return requestJson<{ id: string; status: string }>(endpoints.permissionApprove(permissionId), {
    method: "POST",
    body: JSON.stringify({ remember: false }),
  });
}

export function rejectPermission(permissionId: string) {
  return requestJson<{ id: string; status: string }>(endpoints.permissionReject(permissionId), {
    method: "POST",
    body: JSON.stringify({ reason: "user_rejected" }),
  });
}
