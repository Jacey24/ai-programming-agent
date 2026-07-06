const API_BASE_URL = "http://127.0.0.1:8080/api/v1";
const HEALTH_URL = `${API_BASE_URL}/health`;
const CHAT_URL = `${API_BASE_URL}/chat`;
const HISTORY_URL = `${API_BASE_URL}/chat/history`;

const refreshButton = document.querySelector("#refreshButton");
const serverStatus = document.querySelector("#serverStatus");
const serverMeta = document.querySelector("#serverMeta");
const databaseStatus = document.querySelector("#databaseStatus");
const databaseMeta = document.querySelector("#databaseMeta");
const responseBody = document.querySelector("#responseBody");
const promptForm = document.querySelector("#promptForm");
const promptInput = document.querySelector("#promptInput");
const submitPromptButton = document.querySelector("#submitPromptButton");
const clearPromptButton = document.querySelector("#clearPromptButton");
const aiResult = document.querySelector("#aiResult");
const refreshHistoryButton = document.querySelector("#refreshHistoryButton");
const historyList = document.querySelector("#historyList");

function setState(element, text, ok) {
  element.textContent = text;
  element.classList.remove("ok", "bad");
  element.classList.add(ok ? "ok" : "bad");
}

function showResponse(data) {
  responseBody.textContent =
    typeof data === "string" ? data : JSON.stringify(data, null, 2);
}

async function refreshStatus() {
  refreshButton.disabled = true;
  serverStatus.textContent = "检查中";
  databaseStatus.textContent = "检查中";

  try {
    const response = await fetch(HEALTH_URL, { cache: "no-store" });
    const data = await response.json();
    showResponse(data);

    const backendOk = response.ok && data?.success === true && data?.data?.status === "ok";
    const database = data?.data?.database;
    const databaseOk = database?.connected === true;

    setState(serverStatus, backendOk ? "运行中" : "异常", backendOk);
    serverMeta.textContent = data?.data?.service
      ? `${data.data.service} ${data.data.version ?? ""}`.trim()
      : HEALTH_URL;

    setState(databaseStatus, databaseOk ? "已连接" : "未连接", databaseOk);
    databaseMeta.textContent = database?.path
      ? `${database.type ?? "sqlite"}: ${database.path}`
      : "未返回数据库状态";
  } catch (error) {
    setState(serverStatus, "连接失败", false);
    setState(databaseStatus, "未知", false);
    serverMeta.textContent = HEALTH_URL;
    databaseMeta.textContent = "请确认后端容器已启动";
    showResponse(String(error));
  } finally {
    refreshButton.disabled = false;
  }
}

function renderHistory(items) {
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

    const time = document.createElement("time");
    time.textContent = item.created_at ?? "";

    const prompt = document.createElement("div");
    prompt.className = "history-prompt";
    prompt.textContent = `用户：${item.prompt ?? ""}`;

    const response = document.createElement("div");
    response.className = "history-response";
    response.textContent = `AI：${item.response ?? ""}`;

    row.append(time, prompt, response);
    historyList.append(row);
  }
}

async function refreshHistory() {
  refreshHistoryButton.disabled = true;
  try {
    const response = await fetch(HISTORY_URL, { cache: "no-store" });
    const data = await response.json();
    showResponse(data);
    renderHistory(data?.data?.items ?? []);
  } catch (error) {
    historyList.textContent = "历史记录加载失败";
    historyList.classList.add("muted");
    showResponse(String(error));
  } finally {
    refreshHistoryButton.disabled = false;
  }
}

async function submitPrompt(event) {
  event.preventDefault();
  const prompt = promptInput.value.trim();

  if (!prompt) {
    aiResult.textContent = "请输入提示词";
    aiResult.classList.add("muted");
    return;
  }

  submitPromptButton.disabled = true;
  aiResult.textContent = "处理中...";
  aiResult.classList.add("muted");

  try {
    const response = await fetch(CHAT_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ prompt }),
    });
    const data = await response.json();
    showResponse(data);

    if (!response.ok || data?.success !== true) {
      aiResult.textContent = data?.error?.message ?? "请求失败";
      return;
    }

    aiResult.classList.remove("muted");
    aiResult.textContent = data.data.response;
    await refreshHistory();
  } catch (error) {
    aiResult.textContent = "请求失败";
    aiResult.classList.add("muted");
    showResponse(String(error));
  } finally {
    submitPromptButton.disabled = false;
  }
}

function clearPrompt() {
  promptInput.value = "";
  aiResult.textContent = "等待输入...";
  aiResult.classList.add("muted");
  promptInput.focus();
}

refreshButton.addEventListener("click", refreshStatus);
promptForm.addEventListener("submit", submitPrompt);
clearPromptButton.addEventListener("click", clearPrompt);
refreshHistoryButton.addEventListener("click", refreshHistory);

aiResult.classList.add("muted");
refreshStatus();
refreshHistory();
