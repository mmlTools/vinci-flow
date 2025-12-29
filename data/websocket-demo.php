<?php
// index.php SLT Vendor Test (PHP + AJAX + HTML, no external JS libs)
// The browser connects to OBS via WebSocket (JS). PHP only serves the page.
?><!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Smart Lower Thirds WebSocket Vendor Test (PHP)</title>
  <style>
    body { font-family: system-ui, -apple-system, Segoe UI, Roboto, Arial, sans-serif; margin: 20px; }
    .row { display:flex; gap:10px; flex-wrap:wrap; align-items:center; }
    input, button { padding:8px 10px; font-size:14px; }
    button { cursor:pointer; }
    label { display:inline-flex; align-items:center; gap:6px; }
    #status { padding:8px 10px; border:1px solid #ccc; border-radius:6px; margin-top:10px; }
    #items { margin-top:16px; display:grid; gap:10px; }
    .card { border:1px solid #ddd; border-radius:10px; padding:12px; }
    .card h3 { margin:0 0 6px 0; font-size:16px; }
    .muted { opacity:0.75; }
    .pill { display:inline-block; padding:2px 8px; border-radius:999px; font-size:12px; border:1px solid #ccc; }
    .pill.on { border-color:#2b8a3e; }
    .pill.off { border-color:#c92a2a; }
    pre { background:#f6f8fa; padding:10px; border-radius:8px; overflow:auto; }
    code { background: rgba(0,0,0,0.04); padding: 1px 5px; border-radius: 6px; }
    .mini { font-size: 12px; }
  </style>
</head>
<body>
  <h1>Smart Lower Thirds Vendor API Test</h1>

  <div class="row">
    <label>OBS WS URL</label>
    <input id="wsUrl" value="ws://127.0.0.1:4455" size="28">
    <label>Password</label>
    <input id="wsPass" type="password" placeholder="optional" size="16">

    <button id="btnConnect">Connect</button>
    <button id="btnDisconnect" disabled>Disconnect</button>
    <button id="btnList" disabled>List Lower Thirds</button>
  </div>

  <div class="row mini" style="margin-top:10px">
    <label><input id="rememberUrl" type="checkbox" checked> Remember URL</label>
    <label><input id="rememberPass" type="checkbox"> Remember password</label>
    <button id="btnForget" type="button">Forget saved</button>
    <span class="muted">Password is stored in browser localStorage only if you enable it.</span>
  </div>

  <div id="status">Not connected</div>

  <h2>Lower Thirds</h2>
  <div id="items"></div>

  <h2>Raw Log</h2>
  <pre id="log"></pre>

<script>
/**
 * obs-websocket v5 minimal client (no external libs).
 * Implements:
 * - Identify (and optional auth)
 * - Request/Response by requestId
 * - CallVendorRequest
 *
 * op codes: 0 Hello, 1 Identify, 2 Identified, 6 Request, 7 RequestResponse
 */

const VENDOR = "smart-lower-thirds";

// Storage keys
const LS_URL  = "slt_obs_ws_url";
const LS_PASS = "slt_obs_ws_pass";
const LS_REM_URL  = "slt_remember_url";
const LS_REM_PASS = "slt_remember_pass";

const elUrl = document.getElementById("wsUrl");
const elPass = document.getElementById("wsPass");
const elRememberUrl = document.getElementById("rememberUrl");
const elRememberPass = document.getElementById("rememberPass");
const btnForget = document.getElementById("btnForget");

const elStatus = document.getElementById("status");
const elItems = document.getElementById("items");
const elLog = document.getElementById("log");

const btnConnect = document.getElementById("btnConnect");
const btnDisconnect = document.getElementById("btnDisconnect");
const btnList = document.getElementById("btnList");

let ws = null;
let hello = null;
let identified = false;
let reqSeq = 1;
const pending = new Map(); // requestId -> {resolve,reject,timeout}

function log(...args) {
  const line = args.map(a => typeof a === "string" ? a : JSON.stringify(a, null, 2)).join(" ");
  elLog.textContent = (elLog.textContent + line + "\n").slice(-20000);
  elLog.scrollTop = elLog.scrollHeight;
  console.log(...args);
}

function setStatus(t) { elStatus.textContent = t; }

function setUiConnected(on) {
  btnConnect.disabled = on;
  btnDisconnect.disabled = !on;
  btnList.disabled = !on;
}

function wsSend(obj) {
  ws.send(JSON.stringify(obj));
}

function newRequestId() {
  return "req_" + (reqSeq++) + "_" + Date.now();
}

/* -------------------------
   Persistence (localStorage)
-------------------------- */

function loadPrefs() {
  try {
    const remUrl = localStorage.getItem(LS_REM_URL);
    const remPass = localStorage.getItem(LS_REM_PASS);

    elRememberUrl.checked = (remUrl === null) ? true : (remUrl === "1");
    elRememberPass.checked = (remPass === "1");

    if (elRememberUrl.checked) {
      const savedUrl = localStorage.getItem(LS_URL);
      if (savedUrl) elUrl.value = savedUrl;
    }

    if (elRememberPass.checked) {
      const savedPass = localStorage.getItem(LS_PASS);
      if (savedPass) elPass.value = savedPass;
    }
  } catch (e) {
    log("localStorage not available:", String(e));
  }
}

function savePrefs() {
  try {
    localStorage.setItem(LS_REM_URL, elRememberUrl.checked ? "1" : "0");
    localStorage.setItem(LS_REM_PASS, elRememberPass.checked ? "1" : "0");

    if (elRememberUrl.checked) {
      localStorage.setItem(LS_URL, elUrl.value.trim());
    } else {
      localStorage.removeItem(LS_URL);
    }

    if (elRememberPass.checked) {
      localStorage.setItem(LS_PASS, elPass.value);
    } else {
      localStorage.removeItem(LS_PASS);
    }
  } catch (e) {
    log("Failed saving prefs:", String(e));
  }
}

function forgetSaved() {
  try {
    localStorage.removeItem(LS_URL);
    localStorage.removeItem(LS_PASS);
    localStorage.setItem(LS_REM_URL, "1");
    localStorage.setItem(LS_REM_PASS, "0");
  } catch {}
  elRememberUrl.checked = true;
  elRememberPass.checked = false;
  elPass.value = "";
  setStatus("Saved settings cleared.");
  log("Cleared saved URL/password.");
}

elRememberUrl.addEventListener("change", savePrefs);
elRememberPass.addEventListener("change", () => {
  if (!elRememberPass.checked) {
    try { localStorage.removeItem(LS_PASS); } catch {}
  }
  savePrefs();
});
elUrl.addEventListener("change", savePrefs);
elPass.addEventListener("change", savePrefs);
btnForget.addEventListener("click", forgetSaved);

loadPrefs();

/* -------------------------
   Crypto helpers
-------------------------- */

async function sha256Base64(str) {
  const data = new TextEncoder().encode(str);
  const hash = await crypto.subtle.digest("SHA-256", data);
  const bytes = new Uint8Array(hash);
  let bin = "";
  for (let i = 0; i < bytes.length; i++) bin += String.fromCharCode(bytes[i]);
  return btoa(bin);
}

// obs-websocket v5 auth: secret = base64( sha256(password + salt) ), auth = base64( sha256(secret + challenge) )
async function makeAuth(password, salt, challenge) {
  const secret = await sha256Base64(password + salt);
  const auth = await sha256Base64(secret + challenge);
  return auth;
}

/* -------------------------
   Requests
-------------------------- */

function request(requestType, requestData) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    return Promise.reject(new Error("WebSocket not connected"));
  }
  const requestId = newRequestId();

  const payload = {
    op: 6,
    d: {
      requestType,
      requestId,
      requestData: requestData || {}
    }
  };

  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      pending.delete(requestId);
      reject(new Error("Request timeout: " + requestType));
    }, 8000);

    pending.set(requestId, { resolve, reject, timeout });
    wsSend(payload);
  });
}

async function callVendor(requestType, requestData) {
  return await request("CallVendorRequest", {
    vendorName: VENDOR,
    requestType,
    requestData: requestData || {}
  });
}

function normalizeItems(res) {
  const top = res?.d?.responseData;
  if (top && Array.isArray(top.items)) return top.items;

  const nested = top?.responseData;
  if (nested && Array.isArray(nested.items)) return nested.items;

  const nested2 = nested?.responseData;
  if (nested2 && Array.isArray(nested2.items)) return nested2.items;

  return [];
}

/* -------------------------
   UI rendering
-------------------------- */

function escapeHtml(s) {
  return String(s ?? "").replace(/[&<>"']/g, c => ({
    "&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#039;"
  }[c]));
}

function renderItems(items) {
  elItems.innerHTML = "";
  if (!items || !items.length) {
    elItems.innerHTML = `<div class="muted">No items returned. Check output_dir + lt-state.json loaded.</div>`;
    return;
  }

  for (const it of items) {
    const vis = !!it.isVisible;
    const pillClass = vis ? "pill on" : "pill off";
    const pillText = vis ? "Visible" : "Hidden";

    const card = document.createElement("div");
    card.className = "card";
    card.innerHTML = `
      <h3>${escapeHtml(it.title || it.id)}</h3>
      <div class="muted">${escapeHtml(it.subtitle || "")}</div>
      <div style="margin-top:8px" class="${pillClass}">${pillText}</div>
      <div class="muted" style="margin-top:8px">
        Repeat: <code>${escapeHtml(it.repeatEverySec)}</code> sec • Visible: <code>${escapeHtml(it.repeatVisibleSec)}</code> sec • Hotkey: <code>${escapeHtml(it.hotkey || "")}</code>
      </div>
      <div style="margin-top:10px" class="row">
        <button data-act="toggle">Toggle</button>
        <button data-act="show">Show</button>
        <button data-act="hide">Hide</button>
        <span class="muted">id: <code>${escapeHtml(it.id)}</code></span>
      </div>
    `;

    card.querySelector('[data-act="toggle"]').addEventListener("click", async () => {
      await doToggle(it.id);
      await doList();
    });
    card.querySelector('[data-act="show"]').addEventListener("click", async () => {
      await doSetVisible(it.id, true);
      await doList();
    });
    card.querySelector('[data-act="hide"]').addEventListener("click", async () => {
      await doSetVisible(it.id, false);
      await doList();
    });

    elItems.appendChild(card);
  }
}

/* -------------------------
   Actions
-------------------------- */

async function doList() {
  try {
    setStatus("Listing lower thirds…");
    const res = await callVendor("ListLowerThirds", {});
    log("ListLowerThirds:", res);

    const items = normalizeItems(res);
    renderItems(items);

    setStatus(`Listed ${items.length} item(s).`);
  } catch (e) {
    log("List error:", String(e));
    setStatus("List failed (see log).");
  }
}

async function doToggle(id) {
  try {
    setStatus(`Toggling ${id}…`);
    const res = await callVendor("ToggleVisible", { id });
    log("ToggleVisible:", res);
    setStatus(`Toggled ${id}.`);
  } catch (e) {
    log("Toggle error:", String(e));
    setStatus("Toggle failed (see log).");
  }
}

async function doSetVisible(id, visible) {
  try {
    setStatus(`${visible ? "Showing" : "Hiding"} ${id}…`);
    const res = await callVendor("SetVisible", { id, visible });
    log("SetVisible:", res);
    setStatus(`${visible ? "Shown" : "Hidden"} ${id}.`);
  } catch (e) {
    log("SetVisible error:", String(e));
    setStatus("SetVisible failed (see log).");
  }
}

/* -------------------------
   Connect / Disconnect
-------------------------- */

async function connectObs() {
  const url = elUrl.value.trim();
  const password = elPass.value;

  savePrefs();

  identified = false;
  hello = null;

  setStatus("Connecting…");
  ws = new WebSocket(url);

  ws.onopen = () => {
    setStatus("Socket open. Waiting for Hello…");
  };

  ws.onclose = () => {
    setStatus("Disconnected.");
    setUiConnected(false);
    ws = null;
    identified = false;
  };

  ws.onerror = () => {
    log("WebSocket error event (browser provides no detail).");
  };

  ws.onmessage = async (msg) => {
    let data;
    try { data = JSON.parse(msg.data); } catch { return; }

    if (data.op === 0) {
      hello = data.d;
      log("Hello:", hello);

      const auth = hello?.authentication;
      const identifyPayload = {
        op: 1,
        d: {
          rpcVersion: hello.rpcVersion || 1,
          eventSubscriptions: 0
        }
      };

      if (auth && password) {
        identifyPayload.d.authentication = await makeAuth(password, auth.salt, auth.challenge);
      }

      wsSend(identifyPayload);
      setStatus("Sent Identify…");
      return;
    }

    if (data.op === 2) {
      identified = true;
      setUiConnected(true);
      setStatus("Connected & Identified.");
      log("Identified:", data.d);
      await doList();
      return;
    }

    if (data.op === 7) {
      const requestId = data?.d?.requestId;
      const entry = pending.get(requestId);
      if (entry) {
        clearTimeout(entry.timeout);
        pending.delete(requestId);

        const status = data?.d?.requestStatus;
        if (!status || status.result !== true) {
          entry.reject(new Error(`${data.d.requestType} failed: ${status?.code} ${status?.comment || ""}`));
        } else {
          entry.resolve(data);
        }
      }
    }
  };
}

btnConnect.addEventListener("click", async () => {
  try {
    await connectObs();
  } catch (e) {
    log("Connect failed:", String(e));
    setStatus("Connect failed (see log).");
    setUiConnected(false);
  }
});

btnDisconnect.addEventListener("click", () => {
  try { ws && ws.close(); } catch {}
});

btnList.addEventListener("click", doList);

setUiConnected(false);
</script>
</body>
</html>
