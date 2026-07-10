const API_BASE_URL = "http://127.0.0.1:8080/api/v1";
const DEFAULT_WORKSPACE_PATH = "/workspace";
const FINISHED_STATUSES = ["completed", "failed", "cancelled"];

const endpoints = {
  health: `${API_BASE_URL}/health`,
  sessions: `${API_BASE_URL}/sessions`,
  workspaces: `${API_BASE_URL}/workspaces`,
  tasks: `${API_BASE_URL}/tasks`,
  permissionsPending: `${API_BASE_URL}/permissions/pending`,
  chat: `${API_BASE_URL}/chat`,
  chatHistory: `${API_BASE_URL}/chat/history`,
};

const state = {
  sessionId: "",
  workspaceId: "",
  taskId: "",
  pollingTimer: 0,
  permissionTimer: 0,
  eventSource: null,
  events: [],
  activeMobileView: "run",
};

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => Array.from(document.querySelectorAll(selector));

const refreshButton = $("#refreshButton");
const refreshHistoryButton = $("#refreshHistoryButton");
const historyInlineRefresh = $("#historyInlineRefresh");
const refreshPermissionsButton = $("#refreshPermissionsButton");
const serverStatus = $("#serverStatus");
const serverMeta = $("#serverMeta");
const databaseStatus = $("#databaseStatus");
const databaseMeta = $("#databaseMeta");
const taskStatus = $("#taskStatus");
const taskMeta = $("#taskMeta");
const streamStatus = $("#streamStatus");
const streamMeta = $("#streamMeta");
const taskForm = $("#taskForm");
const sessionTitleInput = $("#sessionTitle");
const workspaceNameInput = $("#workspaceName");
const workspacePathInput = $("#workspacePath");
const taskInput = $("#taskInput");
const autoRunSafeCommands = $("#autoRunSafeCommands");
const requireFileWritePermission = $("#requireFileWritePermission");
const maxSteps = $("#maxSteps");
const submitTaskButton = $("#submitTaskButton");
const cancelTaskButton = $("#cancelTaskButton");
const clearTaskButton = $("#clearTaskButton");
const taskMode = $("#taskMode");
const sessionId = $("#sessionId");
const workspaceId = $("#workspaceId");
const taskId = $("#taskId");
const taskStatusDetail = $("#taskStatusDetail");
const taskResult = $("#taskResult");
const eventTimeline = $("#eventTimeline");
const permissionList = $("#permissionList");
const logList = $("#logList");
const toolCallList = $("#toolCallList");
const fileChangeList = $("#fileChangeList");
const replayList = $("#replayList");
const historyList = $("#historyList");
const responseBody = $("#responseBody");
const lastEndpoint = $("#lastEndpoint");
const statusSession = $("[data-status-session]");
const statusWorkspace = $("[data-status-workspace]");
const statusTask = $("[data-status-task]");

function setState(element, text, variant) {
  if (!element) return;
  element.textContent = text;
  element.classList.remove("ok", "bad", "waiting", "idle");
  if (variant) {
    element.classList.add(variant);
  }
}

function setText(element, text) {
  if (element) {
    element.textContent = text;
  }
}

function showResponse(endpoint, data) {
  setText(lastEndpoint, endpoint);
  setText(responseBody, typeof data === "string" ? data : JSON.stringify(data, null, 2));
}

function normalizeError(data, fallback) {
  return data?.error?.message || data?.message || fallback;
}

function endpointForTask(suffix = "") {
  return `${endpoints.tasks}/${encodeURIComponent(state.taskId)}${suffix}`;
}

async function requestJson(endpoint, options = {}) {
  const response = await fetch(endpoint, {
    cache: "no-store",
    ...options,
    headers: {
      "Content-Type": "application/json",
      ...(options.headers || {}),
    },
  });

  const text = await response.text();
  let data = {};
  try {
    data = text ? JSON.parse(text) : {};
  } catch {
    data = { success: false, error: { message: text || "响应不是合法 JSON" } };
  }
  showResponse(endpoint, data);

  if (!response.ok || data?.success === false) {
    const error = new Error(normalizeError(data, `请求失败：${response.status}`));
    error.response = response;
    error.data = data;
    throw error;
  }

  return data;
}

async function optionalJson(endpoint, fallbackItems = []) {
  try {
    return await requestJson(endpoint);
  } catch (error) {
    if ([404, 405, 501].includes(error.response?.status)) {
      return { success: true, data: { items: fallbackItems, unavailable: true } };
    }
    throw error;
  }
}

function renderDatabaseStatus(database) {
  if (typeof database === "string") {
    setState(databaseStatus, database, "ok");
    setText(databaseMeta, "后端返回数据库类型");
    return;
  }

  if (database?.connected === true) {
    setState(databaseStatus, "已连接", "ok");
    setText(
      databaseMeta,
      database.path ? `${database.type || "sqlite"}: ${database.path}` : database.type || "sqlite",
    );
    return;
  }

  setState(databaseStatus, "未知", "waiting");
  setText(databaseMeta, "健康检查未返回数据库连接详情");
}

async function refreshStatus() {
  refreshButton.disabled = true;
  setState(serverStatus, "检查中", "waiting");
  setState(databaseStatus, "检查中", "waiting");

  try {
    const data = await requestJson(endpoints.health);
    const payload = data?.data || {};
    const backendOk = data?.success === true && ["ok", "healthy"].includes(payload.status);

    setState(serverStatus, backendOk ? "运行中" : "异常", backendOk ? "ok" : "bad");
    setText(
      serverMeta,
      payload.service ? `${payload.service} ${payload.version || ""}`.trim() : endpoints.health,
    );
    renderDatabaseStatus(payload.database);
  } catch (error) {
    setState(serverStatus, "连接失败", "bad");
    setState(databaseStatus, "未知", "bad");
    setText(serverMeta, endpoints.health);
    setText(databaseMeta, "请确认 codepilot-server 已启动");
    showResponse(endpoints.health, String(error.message || error));
  } finally {
    refreshButton.disabled = false;
  }
}

function syncFooterIdentity() {
  setText(statusSession, state.sessionId || "-");
  setText(statusWorkspace, state.workspaceId || "-");
  setText(statusTask, state.taskId || "-");
}

function updateIdentity(session, workspace, task) {
  if (session?.id) {
    state.sessionId = session.id;
    setText(sessionId, session.id);
  }
  if (workspace?.id) {
    state.workspaceId = workspace.id;
    setText(workspaceId, workspace.id);
  }
  if (task?.id) {
    state.taskId = task.id;
    setText(taskId, task.id);
  }
  syncFooterIdentity();
}

function normalizePlan(plan) {
  if (Array.isArray(plan)) {
    return plan;
  }
  if (typeof plan === "string" && plan.trim()) {
    try {
      const parsed = JSON.parse(plan);
      return Array.isArray(parsed) ? parsed : [plan];
    } catch {
      return [plan];
    }
  }
  return [];
}

function renderTask(task) {
  if (!task) {
    return;
  }

  updateIdentity({ id: task.session_id }, { id: task.workspace_id }, task);
  const status = task.status || "created";
  const done = FINISHED_STATUSES.includes(status);
  const failed = ["failed", "cancelled"].includes(status);

  setState(taskStatus, status, failed ? "bad" : done ? "ok" : "waiting");
  setText(taskMeta, task.id ? `Task ${task.id}` : "已创建任务");
  setText(taskStatusDetail, status);
  setText(taskMode, done ? "任务已结束" : "任务已创建，正在执行或等待权限");

  const plan = normalizePlan(task.plan);
  const lines = [
    `目标：${task.goal || task.input || "未返回目标"}`,
    `状态：${status}`,
    task.current_step ? `当前步骤：${task.current_step}` : "",
    plan.length ? `计划：\n${plan.map((item, index) => `${index + 1}. ${item}`).join("\n")}` : "",
  ].filter(Boolean);
  setText(taskResult, lines.join("\n\n"));
}

async function createSession(title) {
  const data = await requestJson(endpoints.sessions, {
    method: "POST",
    body: JSON.stringify({ title }),
  });
  return data.data;
}

async function createWorkspace(name, path) {
  const data = await requestJson(endpoints.workspaces, {
    method: "POST",
    body: JSON.stringify({ name, path }),
  });
  return data.data;
}

async function createTask(input) {
  const data = await requestJson(endpoints.tasks, {
    method: "POST",
    body: JSON.stringify({
      session_id: state.sessionId,
      workspace_id: state.workspaceId,
      input,
      options: {
        auto_run_safe_commands: autoRunSafeCommands.checked,
        require_permission_for_file_write: requireFileWritePermission.checked,
        max_steps: Number(maxSteps.value || 10),
      },
    }),
  });
  return data.data;
}

async function submitChatFallback(prompt) {
  const data = await requestJson(endpoints.chat, {
    method: "POST",
    body: JSON.stringify({ prompt }),
  });

  setText(taskMode, "当前后端使用 Sprint 1 chat 接口");
  setState(taskStatus, "chat 完成", "ok");
  setText(taskStatusDetail, "chat");
  setText(taskResult, data?.data?.response || "后端未返回 response 字段");
}

function resetContainer(container, text) {
  container.replaceChildren();
  container.textContent = text;
  container.classList.add("muted");
}

function resetTaskPanels() {
  state.events = [];
  resetContainer(eventTimeline, "暂无执行事件");
  resetContainer(permissionList, "暂无待确认权限");
  resetContainer(logList, "暂无日志");
  resetContainer(toolCallList, "暂无工具调用");
  resetContainer(fileChangeList, "暂无文件变更");
  resetContainer(replayList, "暂无回放数据");
}

async function submitTask(event) {
  event.preventDefault();
  const input = taskInput.value.trim();

  if (!input) {
    setText(taskResult, "请输入任务内容");
    taskInput.focus();
    return;
  }

  submitTaskButton.disabled = true;
  setState(taskStatus, "提交中", "waiting");
  setText(taskMeta, "正在调用后端接口");
  setText(taskMode, "创建 session、workspace 和 task");
  setText(taskResult, "请求中...");
  closeEventStream();
  resetTaskPanels();
  activateMobileView("run");

  try {
    const session = await createSession(sessionTitleInput.value.trim() || "CodePilot 任务");
    updateIdentity(session, null, null);

    const workspace = await createWorkspace(
      workspaceNameInput.value.trim() || "codepilot-workspace",
      workspacePathInput.value.trim() || DEFAULT_WORKSPACE_PATH,
    );
    updateIdentity(null, workspace, null);

    const task = await createTask(input);
    renderTask(task);
    await refreshHistory();

    if (task?.id) {
      connectEventStream(task.id);
      await refreshTaskArtifacts();
      pollTask(task.id);
      schedulePermissionRefresh();
    }
  } catch (error) {
    if ([404, 405].includes(error.response?.status)) {
      try {
        await submitChatFallback(input);
        await refreshHistory();
      } catch (fallbackError) {
        setState(taskStatus, "提交失败", "bad");
        setText(taskMeta, "任务接口和 chat 接口均不可用");
        setText(taskMode, "接口调用失败");
        setText(taskResult, fallbackError.message);
      }
      return;
    }

    setState(taskStatus, "提交失败", "bad");
    setText(taskMeta, "请查看接口 JSON");
    setText(taskMode, "接口调用失败");
    setText(taskResult, error.message);
  } finally {
    submitTaskButton.disabled = false;
  }
}

async function pollTask(taskIdValue) {
  window.clearTimeout(state.pollingTimer);

  try {
    const data = await requestJson(`${endpoints.tasks}/${encodeURIComponent(taskIdValue)}`);
    renderTask(data.data);
    await refreshTaskArtifacts();

    const status = data?.data?.status;
    if (!FINISHED_STATUSES.includes(status)) {
      state.pollingTimer = window.setTimeout(() => pollTask(taskIdValue), 2500);
    } else {
      closeEventStream();
      window.clearTimeout(state.permissionTimer);
    }
  } catch (error) {
    setText(taskMode, "任务详情轮询失败");
    setText(taskResult, error.message);
  }
}

function closeEventStream() {
  if (state.eventSource) {
    state.eventSource.close();
    state.eventSource = null;
  }
  setState(streamStatus, "未连接", "idle");
  setText(streamMeta, "创建任务后自动订阅 SSE");
}

function connectEventStream(taskIdValue) {
  closeEventStream();
  const url = `${endpoints.tasks}/${encodeURIComponent(taskIdValue)}/events`;

  if (!window.EventSource) {
    setState(streamStatus, "不支持", "bad");
    setText(streamMeta, "当前浏览器不支持 EventSource");
    return;
  }

  const source = new EventSource(url);
  state.eventSource = source;
  setState(streamStatus, "连接中", "waiting");
  setText(streamMeta, url);

  source.onopen = () => {
    setState(streamStatus, "已连接", "ok");
  };

  source.onmessage = (message) => appendEventFromSse("message", message.data);

  for (const eventName of [
    "task_created",
    "task_planning",
    "agent_message",
    "tool_started",
    "tool_output",
    "tool_finished",
    "permission_required",
    "permission_resolved",
    "file_changed",
    "task_completed",
    "task_failed",
    "task_cancelled",
  ]) {
    source.addEventListener(eventName, (message) => appendEventFromSse(eventName, message.data));
  }

  source.onerror = () => {
    setState(streamStatus, "已断开", "bad");
    setText(streamMeta, "SSE 不可用时使用轮询和历史接口");
  };
}

function appendEventFromSse(type, rawData) {
  let event = { type, content: rawData, metadata: {} };
  try {
    event = JSON.parse(rawData);
  } catch {
    event.type = type;
  }
  state.events.unshift(event);
  renderEvents(state.events);
  if (event.type === "permission_required") {
    activateMobileView("security");
    refreshPermissions();
  }
  if (["tool_finished", "file_changed", "task_completed", "task_failed"].includes(event.type)) {
    refreshTaskArtifacts();
  }
}

function renderEvents(items) {
  if (!items.length) {
    resetContainer(eventTimeline, "暂无执行事件");
    return;
  }

  eventTimeline.classList.remove("muted");
  eventTimeline.replaceChildren();
  for (const item of items.slice(0, 80)) {
    eventTimeline.append(createTimelineItem(item.type || "event", item.content || "", item.created_at, item.metadata));
  }
}

function createTimelineItem(type, content, time, metadata) {
  const row = document.createElement("article");
  row.className = "timeline-item";

  const marker = document.createElement("span");
  marker.className = `event-dot ${statusClass(type)}`;

  const body = document.createElement("div");
  const header = document.createElement("header");
  const name = document.createElement("strong");
  name.textContent = type;
  const when = document.createElement("time");
  when.textContent = time || "";
  header.append(name, when);

  const text = document.createElement("p");
  text.textContent = content || "无内容";
  body.append(header, text);

  if (metadata && Object.keys(metadata).length) {
    const meta = document.createElement("pre");
    meta.className = "inline-json";
    meta.textContent = JSON.stringify(metadata, null, 2);
    body.append(meta);
  }

  row.append(marker, body);
  return row;
}

function statusClass(value) {
  const text = String(value);
  if (text.includes("failed") || text.includes("reject") || text.includes("cancelled")) return "bad";
  if (text.includes("completed") || text.includes("approve") || text.includes("success")) return "ok";
  if (text.includes("permission") || text.includes("running") || text.includes("started")) return "waiting";
  return "idle";
}

async function cancelCurrentTask() {
  if (!state.taskId) {
    setText(taskResult, "当前没有可取消的任务");
    return;
  }

  try {
    const data = await requestJson(endpointForTask("/cancel"), {
      method: "POST",
      body: JSON.stringify({ reason: "用户手动取消" }),
    });
    renderTask(data.data);
    closeEventStream();
  } catch (error) {
    setText(taskMode, "取消任务失败");
    setText(taskResult, error.message);
  }
}

async function refreshTaskArtifacts() {
  if (!state.taskId) {
    return;
  }
  await Promise.allSettled([
    refreshLogs(),
    refreshToolCalls(),
    refreshFileChanges(),
    refreshReplay(),
  ]);
}

async function refreshLogs() {
  const data = await optionalJson(endpointForTask("/logs"));
  renderStackList(
    logList,
    data.data.items || [],
    (item) => ({
      title: item.type || "log",
      meta: item.created_at || "",
      body: item.content || "",
    }),
    "暂无日志",
    data.data.unavailable,
  );
}

async function refreshToolCalls() {
  const data = await optionalJson(endpointForTask("/tool-calls"));
  renderStackList(
    toolCallList,
    data.data.items || [],
    (item) => ({
      title: item.tool_name || "tool",
      meta: item.created_at || "",
      body: JSON.stringify({ arguments: item.arguments, success: item.success, result: item.result }, null, 2),
      code: true,
    }),
    "暂无工具调用",
    data.data.unavailable,
  );
}

async function refreshFileChanges() {
  const data = await optionalJson(endpointForTask("/file-changes"));
  renderStackList(
    fileChangeList,
    data.data.items || [],
    (item) => ({
      title: `${item.change_type || "changed"} · ${item.file_path || "-"}`,
      meta: item.created_at || "",
      body: item.diff || item.content || "未返回 diff",
      code: Boolean(item.diff),
    }),
    "暂无文件变更",
    data.data.unavailable,
  );
}

async function refreshReplay() {
  const data = await optionalJson(endpointForTask("/replay"));
  const timeline = data.data.timeline || [];
  if (!timeline.length) {
    resetContainer(replayList, data.data.unavailable ? "历史回放接口暂未实现" : "暂无回放数据");
    return;
  }

  replayList.classList.remove("muted");
  replayList.replaceChildren();
  for (const item of timeline) {
    replayList.append(
      createTimelineItem(
        item.type || item.tool_name || "replay",
        item.content || item.file_path || "",
        item.created_at,
        item,
      ),
    );
  }
}

function renderStackList(container, items, mapItem, emptyText, unavailable) {
  if (!items.length) {
    resetContainer(container, unavailable ? `${emptyText}，接口暂未实现` : emptyText);
    return;
  }

  container.classList.remove("muted");
  container.replaceChildren();
  for (const item of items) {
    const view = mapItem(item);
    const article = document.createElement("article");
    article.className = "stack-item";

    const header = document.createElement("header");
    const title = document.createElement("strong");
    title.textContent = view.title;
    const meta = document.createElement("span");
    meta.textContent = view.meta;
    header.append(title, meta);

    const body = document.createElement(view.code ? "pre" : "p");
    if (view.code) {
      body.className = "inline-json";
    }
    body.textContent = view.body || "无内容";
    article.append(header, body);
    container.append(article);
  }
}

async function refreshPermissions() {
  refreshPermissionsButton.disabled = true;
  try {
    const data = await optionalJson(endpoints.permissionsPending);
    renderPermissions(data.data.items || [], data.data.unavailable);
  } catch (error) {
    resetContainer(permissionList, `权限加载失败：${error.message}`);
  } finally {
    refreshPermissionsButton.disabled = false;
  }
}

function renderPermissions(items, unavailable) {
  const visibleItems = state.taskId ? items.filter((item) => item.task_id === state.taskId) : items;
  if (!visibleItems.length) {
    resetContainer(permissionList, unavailable ? "权限接口暂未实现" : "暂无待确认权限");
    return;
  }

  permissionList.classList.remove("muted");
  permissionList.replaceChildren();
  for (const item of visibleItems) {
    const article = document.createElement("article");
    article.className = "permission-card";

    const header = document.createElement("header");
    const title = document.createElement("strong");
    title.textContent = item.tool_name || "permission";
    const badge = document.createElement("span");
    badge.className = `risk ${item.risk_level || "medium"}`;
    badge.textContent = item.risk_level || "medium";
    header.append(title, badge);

    const action = document.createElement("p");
    action.textContent = item.action || item.reason || "等待确认";
    const reason = document.createElement("small");
    reason.textContent = item.reason || item.id || "";

    const actions = document.createElement("div");
    actions.className = "small-actions";
    const approve = document.createElement("button");
    approve.type = "button";
    approve.textContent = "授权通过";
    approve.addEventListener("click", () => resolvePermission(item.id, true));
    const reject = document.createElement("button");
    reject.type = "button";
    reject.className = "secondary";
    reject.textContent = "阻断拒绝";
    reject.addEventListener("click", () => resolvePermission(item.id, false));
    actions.append(approve, reject);

    article.append(header, action, reason, actions);
    permissionList.append(article);
  }
}

async function resolvePermission(permissionId, approved) {
  if (!permissionId) {
    return;
  }
  const endpoint = `${API_BASE_URL}/permissions/${encodeURIComponent(permissionId)}/${approved ? "approve" : "reject"}`;
  const body = approved ? { remember: false } : { reason: "用户拒绝" };
  try {
    await requestJson(endpoint, {
      method: "POST",
      body: JSON.stringify(body),
    });
    await refreshPermissions();
    await refreshTaskArtifacts();
  } catch (error) {
    resetContainer(permissionList, `权限处理失败：${error.message}`);
  }
}

function schedulePermissionRefresh() {
  window.clearTimeout(state.permissionTimer);
  const tick = async () => {
    await refreshPermissions();
    if (state.taskId) {
      state.permissionTimer = window.setTimeout(tick, 3500);
    }
  };
  tick();
}

function renderHistory(items, mode) {
  if (!items.length) {
    resetContainer(historyList, "暂无历史记录");
    return;
  }

  historyList.classList.remove("muted");
  historyList.replaceChildren();

  for (const item of items) {
    const row = document.createElement("article");
    row.className = "history-item";
    row.tabIndex = 0;

    const header = document.createElement("header");
    const time = document.createElement("time");
    time.textContent = item.created_at || item.updated_at || "";
    const badge = document.createElement("span");
    badge.className = `badge ${statusClass(item.status || mode)}`;
    badge.textContent = mode === "task" ? item.status || "task" : "chat";
    header.append(time, badge);

    const title = document.createElement("div");
    title.className = "history-title";
    title.textContent = mode === "task" ? item.goal || item.input || item.id : item.prompt || "";

    const meta = document.createElement("div");
    meta.className = "history-meta";
    meta.textContent =
      mode === "task"
        ? `task: ${item.id || "-"} · session: ${item.session_id || "-"}`
        : item.response || "";

    if (mode === "task" && item.id) {
      row.addEventListener("click", () => selectHistoryTask(item));
      row.addEventListener("keydown", (event) => {
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          selectHistoryTask(item);
        }
      });
    }

    row.append(header, title, meta);
    historyList.append(row);
  }
}

async function selectHistoryTask(task) {
  updateIdentity({ id: task.session_id }, { id: task.workspace_id }, task);
  renderTask(task);
  resetTaskPanels();
  connectEventStream(task.id);
  activateMobileView("run");
  await refreshTaskArtifacts();
  await refreshPermissions();
}

async function refreshTaskHistory() {
  const data = await requestJson(`${endpoints.tasks}?page=1&page_size=20`);
  renderHistory(data?.data?.items || [], "task");
}

async function refreshChatHistory() {
  const data = await requestJson(endpoints.chatHistory);
  renderHistory(data?.data?.items || [], "chat");
}

async function refreshHistory() {
  refreshHistoryButton.disabled = true;
  if (historyInlineRefresh) {
    historyInlineRefresh.disabled = true;
  }
  try {
    await refreshTaskHistory();
  } catch (taskError) {
    try {
      await refreshChatHistory();
    } catch (chatError) {
      resetContainer(historyList, `历史记录加载失败：${chatError.message || taskError.message}`);
    }
  } finally {
    refreshHistoryButton.disabled = false;
    if (historyInlineRefresh) {
      historyInlineRefresh.disabled = false;
    }
  }
}

function clearTask() {
  taskInput.value = "";
  setText(taskResult, "暂无任务结果");
  setText(taskMode, "等待任务提交");
  taskInput.focus();
}

function activateTab(button) {
  $$(".tab").forEach((tab) => tab.classList.toggle("active", tab === button));
  const activePanel = `${button.dataset.tab}Panel`;
  $$(".tab-panel").forEach((panel) => panel.classList.toggle("active", panel.id === activePanel));
}

function activateMobileView(view) {
  state.activeMobileView = view;
  $$(".activity-button").forEach((button) => {
    button.classList.toggle("active", button.dataset.view === view);
  });

  const panelMap = {
    setup: [".side-panel"],
    history: [".side-panel"],
    run: [".workbench"],
    security: [".agent-panel"],
    json: [".workbench"],
  };

  $$(".side-panel, .workbench, .agent-panel").forEach((panel) => {
    panel.classList.remove("mobile-active");
  });
  for (const selector of panelMap[view] || [".workbench"]) {
    const panel = $(selector);
    if (panel) {
      panel.classList.add("mobile-active");
    }
  }

  if (view === "json") {
    const jsonTab = $('.tab[data-tab="json"]');
    if (jsonTab) activateTab(jsonTab);
  }
}

function bindEvents() {
  refreshButton.addEventListener("click", refreshStatus);
  refreshHistoryButton.addEventListener("click", refreshHistory);
  if (historyInlineRefresh) {
    historyInlineRefresh.addEventListener("click", refreshHistory);
  }
  refreshPermissionsButton.addEventListener("click", refreshPermissions);
  taskForm.addEventListener("submit", submitTask);
  cancelTaskButton.addEventListener("click", cancelCurrentTask);
  clearTaskButton.addEventListener("click", clearTask);
  $$(".tab").forEach((tab) => tab.addEventListener("click", () => activateTab(tab)));
  $$(".activity-button").forEach((button) => {
    button.addEventListener("click", () => activateMobileView(button.dataset.view));
  });
}

bindEvents();
activateMobileView("run");
syncFooterIdentity();
refreshStatus();
refreshHistory();
refreshPermissions();
