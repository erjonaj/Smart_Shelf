const STORAGE_USERS_KEY = "smartShelfUsers";
const STORAGE_CONFIG_KEY = "smartShelfConfig";

const defaultConfig = {
  productName: "Snack",
  unitWeight: 120,
  unitPrice: 3.5,
  changeThreshold: 20,
  calibrationFactor: 1.0
};

const state = {
  port: null,
  reader: null,
  writer: null,
  readLoopActive: false,
  lineBuffer: "",
  latestWeight: null,
  previousWeight: null,
  activeCardId: null,
  pendingCardId: null,
  users: {},
  sessions: {},
  sessionOrder: [],
  config: { ...defaultConfig }
};

const dom = {
  connectBtn: document.getElementById("connectBtn"),
  pingBtn: document.getElementById("pingBtn"),
  tareBtn: document.getElementById("tareBtn"),
  readBtn: document.getElementById("readBtn"),
  saveConfigBtn: document.getElementById("saveConfigBtn"),
  sendCalBtn: document.getElementById("sendCalBtn"),
  serialStatus: document.getElementById("serialStatus"),
  weightValue: document.getElementById("weightValue"),
  activeUser: document.getElementById("activeUser"),
  productName: document.getElementById("productName"),
  unitWeight: document.getElementById("unitWeight"),
  unitPrice: document.getElementById("unitPrice"),
  changeThreshold: document.getElementById("changeThreshold"),
  calibrationFactor: document.getElementById("calibrationFactor"),
  sessionTabs: document.getElementById("sessionTabs"),
  sessionPanels: document.getElementById("sessionPanels"),
  eventLog: document.getElementById("eventLog"),
  registerModal: document.getElementById("registerModal"),
  registerCardText: document.getElementById("registerCardText"),
  registerName: document.getElementById("registerName"),
  registerCancelBtn: document.getElementById("registerCancelBtn"),
  registerSaveBtn: document.getElementById("registerSaveBtn")
};

init();

function init() {
  loadState();
  renderConfig();
  renderSessions();
  bindEvents();
  appendLog("UI ready. Connect your MKR1000 over Web Serial.");
}

function bindEvents() {
  dom.connectBtn.addEventListener("click", async () => {
    if (state.port) {
      await disconnectSerial();
      return;
    }
    await connectSerial();
  });

  dom.pingBtn.addEventListener("click", () => sendCommand("PING"));
  dom.tareBtn.addEventListener("click", () => sendCommand("TARE"));
  dom.readBtn.addEventListener("click", () => sendCommand("READ"));

  dom.saveConfigBtn.addEventListener("click", () => {
    const nextConfig = {
      productName: dom.productName.value.trim() || "Snack",
      unitWeight: Number(dom.unitWeight.value),
      unitPrice: Number(dom.unitPrice.value),
      changeThreshold: Number(dom.changeThreshold.value),
      calibrationFactor: Number(dom.calibrationFactor.value)
    };

    if (nextConfig.unitWeight <= 0 || nextConfig.unitPrice <= 0 || nextConfig.changeThreshold <= 0) {
      alert("Unit weight, unit price, and threshold must be greater than 0.");
      return;
    }

    state.config = nextConfig;
    saveConfig();
    appendLog("Saved product and billing config.");
  });

  dom.sendCalBtn.addEventListener("click", () => {
    const factor = Number(dom.calibrationFactor.value);
    if (!Number.isFinite(factor) || factor === 0) {
      alert("Enter a valid non-zero calibration factor first.");
      return;
    }
    state.config.calibrationFactor = factor;
    saveConfig();
    sendCommand(`CAL|${factor}`);
  });

  dom.registerCancelBtn.addEventListener("click", () => closeRegisterModal());

  dom.registerSaveBtn.addEventListener("click", () => {
    const cardId = state.pendingCardId;
    const customerName = dom.registerName.value.trim();

    if (!cardId) {
      closeRegisterModal();
      return;
    }

    if (!customerName) {
      alert("Please enter a customer name.");
      return;
    }

    state.users[cardId] = {
      name: customerName,
      createdAt: new Date().toISOString(),
      taps: 1
    };

    saveUsers();
    closeRegisterModal();
    appendLog(`Registered new customer ${customerName} (${cardId}).`);
    activateCard(cardId);
  });
}

async function connectSerial() {
  if (!("serial" in navigator)) {
    alert("This browser does not support Web Serial. Use Chrome or Edge on localhost/https.");
    return;
  }

  try {
    const port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });

    state.port = port;
    state.readLoopActive = true;
    setConnectionStatus(true);
    appendLog("Serial connected.");

    startReadLoop();
    await setupWriter();

    dom.connectBtn.textContent = "Disconnect Serial";
    sendCommand("PING");
  } catch (error) {
    appendLog(`Connect failed: ${error.message}`);
    setConnectionStatus(false);
  }
}

async function setupWriter() {
  if (!state.port || !state.port.writable) {
    return;
  }

  state.writer = state.port.writable.getWriter();
}

async function disconnectSerial() {
  try {
    state.readLoopActive = false;

    if (state.reader) {
      await state.reader.cancel();
      state.reader.releaseLock();
      state.reader = null;
    }

    if (state.writer) {
      state.writer.releaseLock();
      state.writer = null;
    }

    if (state.port) {
      await state.port.close();
      state.port = null;
    }

    dom.connectBtn.textContent = "Connect Serial";
    setConnectionStatus(false);
    appendLog("Serial disconnected.");
  } catch (error) {
    appendLog(`Disconnect failed: ${error.message}`);
  }
}

async function startReadLoop() {
  if (!state.port || !state.port.readable) {
    return;
  }

  const textDecoder = new TextDecoderStream();
  state.port.readable.pipeTo(textDecoder.writable).catch(() => {});
  state.reader = textDecoder.readable.getReader();

  while (state.readLoopActive) {
    try {
      const { value, done } = await state.reader.read();
      if (done) {
        break;
      }

      if (!value) {
        continue;
      }

      state.lineBuffer += value;
      processLineBuffer();
    } catch (error) {
      appendLog(`Read error: ${error.message}`);
      break;
    }
  }
}

function processLineBuffer() {
  const lines = state.lineBuffer.split("\n");
  state.lineBuffer = lines.pop() || "";

  lines.forEach((line) => {
    const trimmed = line.trim();
    if (!trimmed) {
      return;
    }
    processSerialLine(trimmed);
  });
}

function processSerialLine(line) {
  const separator = line.indexOf("|");
  if (separator < 0) {
    appendLog(`Raw: ${line}`);
    return;
  }

  const type = line.slice(0, separator);
  const payload = line.slice(separator + 1).trim();

  if (type === "CARD") {
    onCardScanned(payload);
    return;
  }

  if (type === "WEIGHT") {
    const weight = Number(payload);
    if (Number.isFinite(weight)) {
      onWeightEvent(weight);
    }
    return;
  }

  if (type === "SYS") {
    appendLog(`Device: ${payload}`);
    return;
  }

  if (type === "PONG") {
    appendLog("Device heartbeat OK.");
    return;
  }

  if (type === "CAL") {
    appendLog(`Device calibration updated to ${payload}.`);
    return;
  }

  appendLog(`Device ${type}: ${payload}`);
}

function onCardScanned(cardId) {
  if (!cardId) {
    return;
  }

  const existingUser = state.users[cardId];
  if (!existingUser) {
    openRegisterModal(cardId);
    return;
  }

  existingUser.taps = (existingUser.taps || 0) + 1;
  saveUsers();
  activateCard(cardId);
}

function onWeightEvent(weight) {
  state.latestWeight = weight;
  dom.weightValue.textContent = `${weight.toFixed(2)} g`;

  if (state.previousWeight === null) {
    state.previousWeight = weight;
    return;
  }

  const delta = weight - state.previousWeight;
  const threshold = state.config.changeThreshold;

  if (Math.abs(delta) < threshold) {
    return;
  }

  if (!state.activeCardId) {
    appendLog(`Weight changed by ${delta.toFixed(2)}g, but no active customer tab.`);
    state.previousWeight = weight;
    return;
  }

  const units = Math.round(Math.abs(delta) / state.config.unitWeight);
  if (units < 1) {
    state.previousWeight = weight;
    return;
  }

  const session = state.sessions[state.activeCardId];
  if (!session) {
    state.previousWeight = weight;
    return;
  }

  const unitPrice = state.config.unitPrice;
  const lineTotal = units * unitPrice;

  if (delta < 0) {
    session.items += units;
    session.total += lineTotal;
    session.events.unshift(
      `${timestamp()} Took ${units} x ${state.config.productName} (+$${lineTotal.toFixed(2)})`
    );
    appendLog(`${session.name} took ${units} item(s).`);
  } else {
    const returnUnits = Math.min(units, session.items);
    const refund = returnUnits * unitPrice;
    session.items -= returnUnits;
    session.total -= refund;
    session.events.unshift(
      `${timestamp()} Returned ${returnUnits} x ${state.config.productName} (-$${refund.toFixed(2)})`
    );
    appendLog(`${session.name} returned ${returnUnits} item(s).`);
  }

  session.lastWeight = weight;
  session.lastDelta = delta;
  state.previousWeight = weight;
  renderSessions();
}

function activateCard(cardId) {
  const user = state.users[cardId];
  if (!user) {
    return;
  }

  if (!state.sessions[cardId]) {
    state.sessions[cardId] = {
      cardId,
      name: user.name,
      items: 0,
      total: 0,
      events: [],
      openedAt: timestamp(),
      lastWeight: state.latestWeight,
      lastDelta: 0
    };
    state.sessionOrder.push(cardId);
  }

  state.activeCardId = cardId;
  dom.activeUser.textContent = `${user.name} (${cardId})`;
  if (state.latestWeight !== null) {
    state.previousWeight = state.latestWeight;
  }

  appendLog(`Welcome ${user.name}. Customer tab is active.`);
  renderSessions();
}

function renderSessions() {
  dom.sessionTabs.innerHTML = "";
  dom.sessionPanels.innerHTML = "";

  if (state.sessionOrder.length === 0) {
    const empty = document.createElement("p");
    empty.className = "hint";
    empty.textContent = "No active customer tabs yet.";
    dom.sessionPanels.appendChild(empty);
    return;
  }

  state.sessionOrder.forEach((cardId) => {
    const session = state.sessions[cardId];
    if (!session) {
      return;
    }

    const tabBtn = document.createElement("button");
    tabBtn.className = "tab-btn";
    if (cardId === state.activeCardId) {
      tabBtn.classList.add("active");
    }
    tabBtn.textContent = `${session.name}`;
    tabBtn.addEventListener("click", () => {
      state.activeCardId = cardId;
      dom.activeUser.textContent = `${session.name} (${cardId})`;
      if (state.latestWeight !== null) {
        state.previousWeight = state.latestWeight;
      }
      renderSessions();
    });
    dom.sessionTabs.appendChild(tabBtn);

    const panel = document.createElement("article");
    panel.className = "session-panel";
    if (cardId === state.activeCardId) {
      panel.classList.add("active");
    }

    panel.innerHTML = `
      <h3>${session.name}</h3>
      <div class="session-kpis">
        <div class="session-kpi"><span>Card ID</span><strong>${session.cardId}</strong></div>
        <div class="session-kpi"><span>Items Taken</span><strong>${session.items}</strong></div>
        <div class="session-kpi"><span>Current Total</span><strong>$${session.total.toFixed(2)}</strong></div>
        <div class="session-kpi"><span>Opened</span><strong>${session.openedAt}</strong></div>
      </div>
      <ol class="session-events">
        ${session.events.slice(0, 12).map((event) => `<li>${event}</li>`).join("") || "<li>No weight events yet.</li>"}
      </ol>
    `;

    dom.sessionPanels.appendChild(panel);
  });
}

function openRegisterModal(cardId) {
  state.pendingCardId = cardId;
  dom.registerCardText.textContent = `Card ${cardId} is not registered yet.`;
  dom.registerName.value = "";
  dom.registerModal.classList.remove("hidden");
  dom.registerModal.setAttribute("aria-hidden", "false");
  dom.registerName.focus();
  appendLog(`First tap detected for ${cardId}. Waiting for registration.`);
}

function closeRegisterModal() {
  state.pendingCardId = null;
  dom.registerModal.classList.add("hidden");
  dom.registerModal.setAttribute("aria-hidden", "true");
}

async function sendCommand(command) {
  if (!state.writer) {
    appendLog("Cannot send command: serial is not connected.");
    return;
  }

  try {
    await state.writer.write(`${command}\n`);
  } catch (error) {
    appendLog(`Write failed: ${error.message}`);
  }
}

function setConnectionStatus(isOnline) {
  dom.serialStatus.textContent = isOnline ? "Online" : "Offline";
  dom.serialStatus.classList.toggle("status-online", isOnline);
  dom.serialStatus.classList.toggle("status-offline", !isOnline);
}

function appendLog(message) {
  const item = document.createElement("li");
  item.textContent = `${timestamp()} ${message}`;
  dom.eventLog.prepend(item);

  const maxItems = 70;
  while (dom.eventLog.children.length > maxItems) {
    dom.eventLog.removeChild(dom.eventLog.lastChild);
  }
}

function loadState() {
  try {
    const usersRaw = localStorage.getItem(STORAGE_USERS_KEY);
    state.users = usersRaw ? JSON.parse(usersRaw) : {};
  } catch {
    state.users = {};
  }

  try {
    const configRaw = localStorage.getItem(STORAGE_CONFIG_KEY);
    state.config = configRaw ? { ...defaultConfig, ...JSON.parse(configRaw) } : { ...defaultConfig };
  } catch {
    state.config = { ...defaultConfig };
  }
}

function saveUsers() {
  localStorage.setItem(STORAGE_USERS_KEY, JSON.stringify(state.users));
}

function saveConfig() {
  localStorage.setItem(STORAGE_CONFIG_KEY, JSON.stringify(state.config));
}

function renderConfig() {
  dom.productName.value = state.config.productName;
  dom.unitWeight.value = state.config.unitWeight;
  dom.unitPrice.value = state.config.unitPrice;
  dom.changeThreshold.value = state.config.changeThreshold;
  dom.calibrationFactor.value = state.config.calibrationFactor;
}

function timestamp() {
  return new Date().toLocaleTimeString();
}
