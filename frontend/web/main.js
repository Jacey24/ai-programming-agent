const API_BASE_URL = "http://127.0.0.1:8080/api/v1";
const DEFAULT_WORKSPACE_PATH = "/workspace";

const endpoints = {
  health: `${API_BASE_URL}/health`,
  sessions: `${API_BASE_URL}/sessions`,
  workspaces: `${API_BASE_URL}/workspaces`,
  tasks: `${API_BASE_URL}/tasks`,
  chat: `${API_BASE_URL}/chat`,
  chatHistory: `${API_BASE_URL}/chat/history`,
};

const state = {
  sessionId: "",
  workspaceId: "",
  taskId: "",
  pollingTimer: 0,
};

const refreshButton = document.querySelector("#refreshButton");
const refreshHistoryButton = document.querySelector("#refreshHistoryButton");
const serverStatus = document.querySelector("#serverStatus");
const serverMeta = document.querySelector("#serverMeta");
const databaseStatus = document.querySelector("#databaseStatus");
const databaseMeta = document.querySelector("#databaseMeta");
const taskStatus = document.querySelector("#taskStatus");
const taskMeta = document.querySelector("#taskMeta");
const taskForm = document.querySelector("#taskForm");
const sessionTitleInput = document.querySelector("#sessionTitle");
const workspaceNameInput = document.querySelector("#workspaceName");
const workspacePathInput = document.querySelector("#workspacePath");
const taskInput = document.querySelector("#taskInput");
const autoRunSafeCommands = document.querySelector("#autoRunSafeCommands");
const requireFileWritePermission = document.querySelector("#requireFileWritePermission");
const submitTaskButton = document.querySelector("#submitTaskButton");
const clearTaskButton = document.querySelector("#clearTaskButton");
const taskMode = document.querySelector("#taskMode");
const sessionId = document.querySelector("#sessionId");
const workspaceId = document.querySelector("#workspaceId");
const taskId = document.querySelector("#taskId");
const taskStatusDetail = document.querySelector("#taskStatusDetail");
const taskResult = document.querySelector("#taskResult");
const historyList = document.querySelector("#historyList");
const responseBody = document.querySelector("#responseBody");
const lastEndpoint = document.querySelector("#lastEndpoint");

function setState(element, text, variant) {
  element.textContent = text;
  element.classList.remove("ok", "bad", "waiting");
  if (variant) {
    element.classList.add(variant);
  }
}

function showResponse(endpoint, data) {
  lastEndpoint.textContent = endpoint;
  responseBody.textContent =
    typeof data === "string" ? data : JSON.stringify(data, null, 2);
}

function normalizeError(data, fallback) {
  return data?.error?.message || data?.message || fallback;
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

function renderDatabaseStatus(database) {
  if (typeof database === "string") {
    setState(databaseStatus, database, "ok");
    databaseMeta.textContent = "后端返回数据库类型";
    return;
  }

  if (database?.connected === true) {
    setState(databaseStatus, "已连接", "ok");
    databaseMeta.textContent = database.path
      ? `${database.type || "sqlite"}: ${database.path}`
      : database.type || "sqlite";
    return;
  }

  setState(databaseStatus, "未知", "waiting");
  databaseMeta.textContent = "健康检查未返回数据库连接详情";
}

async function refreshStatus() {
  refreshButton.disabled = true;
  setState(serverStatus, "检查中", "waiting");
  setState(databaseStatus, "检查中", "waiting");

  try {
    const data = await requestJson(endpoints.health);
    const payload = data?.data || {};
    const backendOk = data?.success === true && (payload.status === "ok" || payload.status === "healthy");

    setState(serverStatus, backendOk ? "运行中" : "异常", backendOk ? "ok" : "bad");
    serverMeta.textContent = payload.service
      ? `${payload.service} ${payload.version || ""}`.trim()
      : endpoints.health;
    renderDatabaseStatus(payload.database);
  } catch (error) {
    setState(serverStatus, "连接失败", "bad");
    setState(databaseStatus, "未知", "bad");
    serverMeta.textContent = endpoints.health;
    databaseMeta.textContent = "请确认 codepilot-server 已启动";
    showResponse(endpoints.health, String(error.message || error));
  } finally {
    refreshButton.disabled = false;
  }
}

function updateIdentity(session, workspace, task) {
  if (session?.id) {
    state.sessionId = session.id;
    sessionId.textContent = session.id;
  }

  if (workspace?.id) {
    state.workspaceId = workspace.id;
    workspaceId.textContent = workspace.id;
  }

  if (task?.id) {
    state.taskId = task.id;
    taskId.textContent = task.id;
  }
}

function renderTask(task) {
  if (!task) {
    return;
  }

  updateIdentity({ id: task.session_id }, { id: task.workspace_id }, task);
  const status = task.status || "created";
  const done = ["completed", "failed", "cancelled"].includes(status);
  const failed = ["failed", "cancelled"].includes(status);

  setState(taskStatus, status, failed ? "bad" : done ? "ok" : "waiting");
  taskMeta.textContent = task.id ? `Task ${task.id}` : "已创建任务";
  taskStatusDetail.textContent = status;
  taskMode.textContent = done ? "任务已结束" : "任务已创建，正在等待或执行";

  const plan = Array.isArray(task.plan) ? task.plan : [];
  const lines = [
    `目标：${task.goal || task.input || "未返回目标"}`,
    `状态：${status}`,
    task.current_step ? `当前步骤：${task.current_step}` : "",
    plan.length ? `计划：\n${plan.map((item, index) => `${index + 1}. ${item}`).join("\n")}` : "",
  ].filter(Boolean);
  taskResult.textContent = lines.join("\n\n");
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
        max_steps: 10,
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

  taskMode.textContent = "当前后端使用 Sprint 1 chat 接口";
  setState(taskStatus, "chat 完成", "ok");
  taskStatusDetail.textContent = "chat";
  taskResult.textContent = data?.data?.response || "后端未返回 response 字段";
}

async function pollTask(taskIdValue) {
  window.clearTimeout(state.pollingTimer);

  try {
    const data = await requestJson(`${endpoints.tasks}/${encodeURIComponent(taskIdValue)}`);
    renderTask(data.data);

    const status = data?.data?.status;
    if (!["completed", "failed", "cancelled"].includes(status)) {
      state.pollingTimer = window.setTimeout(() => pollTask(taskIdValue), 2500);
    }
  } catch (error) {
    taskMode.textContent = "任务详情轮询失败";
    taskResult.textContent = error.message;
  }
}

async function submitTask(event) {
  event.preventDefault();
  const input = taskInput.value.trim();

  if (!input) {
    taskResult.textContent = "请输入任务内容";
    taskInput.focus();
    return;
  }

  submitTaskButton.disabled = true;
  setState(taskStatus, "提交中", "waiting");
  taskMeta.textContent = "正在调用后端接口";
  taskMode.textContent = "创建 session、workspace 和 task";
  taskResult.textContent = "请求中...";

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
      pollTask(task.id);
    }
  } catch (error) {
    if (error.response?.status === 404 || error.response?.status === 405) {
      try {
        await submitChatFallback(input);
        await refreshHistory();
      } catch (fallbackError) {
        setState(taskStatus, "提交失败", "bad");
        taskMeta.textContent = "任务接口和 chat 接口均不可用";
        taskMode.textContent = "接口调用失败";
        taskResult.textContent = fallbackError.message;
      }
      return;
    }

    setState(taskStatus, "提交失败", "bad");
    taskMeta.textContent = "请查看接口 JSON";
    taskMode.textContent = "接口调用失败";
    taskResult.textContent = error.message;
  } finally {
    submitTaskButton.disabled = false;
  }
}

function renderHistory(items, mode) {
  if (!items.length) {
    historyList.textContent = "暂无历史记录";
    historyList.classList.add("muted");
    return;
  }

  historyList.classList.remove("muted");
  historyList.innerHTML = "";

  for (const item of items) {
    const row = document.createElement("article");
    row.className = "history-item";

    const header = document.createElement("header");
    const time = document.createElement("time");
    time.textContent = item.created_at || item.updated_at || "";
    const badge = document.createElement("span");
    badge.className = "badge";
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

    row.append(header, title, meta);
    historyList.append(row);
  }
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
  try {
    await refreshTaskHistory();
  } catch (taskError) {
    try {
      await refreshChatHistory();
    } catch (chatError) {
      historyList.textContent = `历史记录加载失败：${chatError.message || taskError.message}`;
      historyList.classList.add("muted");
    }
  } finally {
    refreshHistoryButton.disabled = false;
  }
}

function clearTask() {
  taskInput.value = "";
  taskResult.textContent = "暂无任务结果";
  taskMode.textContent = "等待任务提交";
  taskInput.focus();
}

refreshButton.addEventListener("click", refreshStatus);
refreshHistoryButton.addEventListener("click", refreshHistory);
taskForm.addEventListener("submit", submitTask);
clearTaskButton.addEventListener("click", clearTask);

refreshStatus();
refreshHistory();
