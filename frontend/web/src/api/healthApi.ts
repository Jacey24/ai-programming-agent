import { requestJson } from "./client";
import { endpoints } from "./endpoints";
import type { HealthPayload } from "../types/api";

export function getHealth() {
  return requestJson<HealthPayload>(endpoints.health);
}
