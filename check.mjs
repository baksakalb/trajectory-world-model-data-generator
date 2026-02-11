/**
 * MCP Automation Bridge Health Check Script
 * ------------------------------------------
 * Non-destructive connectivity test for the Unreal Engine MCP bridge.
 *
 * Tests performed (all read-only / no-op):
 *   1. TCP port reachability on 127.0.0.1:8091
 *   2. WebSocket upgrade + bridge_hello / bridge_ack handshake
 *   3. Safe read-only action: system_control.get_engine_version
 *   4. Safe read-only action: list_light_types  (Pattern C, empty payload)
 *   5. Graceful disconnect
 *
 * Usage:
 *   node check.mjs              # run all checks (default port 8091)
 *   node check.mjs --port 9000  # override port
 *   node check.mjs --json       # machine-readable JSON output
 *
 * Exit codes: 0 = all passed, 1 = one or more failed
 */

import { createConnection } from 'net';
import { WebSocket } from 'ws';

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
const args = process.argv.slice(2);
const portIdx = args.indexOf('--port');
const PORT = portIdx !== -1 ? parseInt(args[portIdx + 1], 10) : 8091;
const HOST = '127.0.0.1';
const JSON_OUTPUT = args.includes('--json');
const TIMEOUT_MS = 8000;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
const results = [];
let requestIdCounter = 1;

function log(msg) {
  if (!JSON_OUTPUT) process.stdout.write(msg + '\n');
}

function record(name, pass, detail = '') {
  results.push({ name, pass, detail });
  const icon = pass ? '[PASS]' : '[FAIL]';
  log(`  ${icon} ${name}${detail ? ' -- ' + detail : ''}`);
}

function generateRequestId() {
  return `health-check-${requestIdCounter++}-${Date.now()}`;
}

// ---------------------------------------------------------------------------
// Test 1: TCP Port Check
// ---------------------------------------------------------------------------
function testTcpPort() {
  return new Promise((resolve) => {
    const socket = createConnection({ host: HOST, port: PORT, timeout: 3000 });
    socket.once('connect', () => {
      socket.destroy();
      record('TCP port reachable', true, `${HOST}:${PORT}`);
      resolve(true);
    });
    socket.once('timeout', () => {
      socket.destroy();
      record('TCP port reachable', false, `Timeout connecting to ${HOST}:${PORT}`);
      resolve(false);
    });
    socket.once('error', (err) => {
      record('TCP port reachable', false, err.message);
      resolve(false);
    });
  });
}

// ---------------------------------------------------------------------------
// Tests 2-4: WebSocket handshake + read-only actions
// ---------------------------------------------------------------------------
function testWebSocket() {
  return new Promise((resolve) => {
    const ws = new WebSocket(`ws://${HOST}:${PORT}`);
    let handshakeDone = false;
    let engineVersionDone = false;
    let listLightsDone = false;
    const pendingRequests = new Map();
    let closed = false;

    const timer = setTimeout(() => {
      const missing = [];
      if (!handshakeDone) missing.push('handshake');
      if (!engineVersionDone) missing.push('get_engine_version');
      if (!listLightsDone) missing.push('list_light_types');
      for (const name of missing) {
        record(name, false, 'Timeout waiting for response');
      }
      cleanup();
    }, TIMEOUT_MS);

    function cleanup() {
      if (closed) return;
      closed = true;
      clearTimeout(timer);
      try { ws.close(); } catch { }
      resolve();
    }

    function trySendAction(action, payload) {
      const id = generateRequestId();
      // Must use the automation_request envelope so the UE plugin
      // routes the message and echoes back the requestId.
      const envelope = {
        type: 'automation_request',
        requestId: id,
        action,
        payload
      };
      pendingRequests.set(id, action);
      ws.send(JSON.stringify(envelope));
      return id;
    }

    function checkAllDone() {
      if (handshakeDone && engineVersionDone && listLightsDone) {
        cleanup();
      }
    }

    ws.on('open', () => {
      // Send bridge_hello to initiate handshake
      const hello = { type: 'bridge_hello' };
      ws.send(JSON.stringify(hello));
    });

    ws.on('message', (raw) => {
      let msg;
      try {
        msg = JSON.parse(typeof raw === 'string' ? raw : raw.toString('utf8'));
      } catch {
        return; // ignore non-JSON
      }

      // --- Handshake response (bridge_ack) ---
      if (msg.type === 'bridge_ack' && !handshakeDone) {
        handshakeDone = true;
        const engineName = msg.engineVersion || msg.engine || '';
        const sessionId = msg.sessionId || '';
        record(
          'WebSocket handshake (bridge_hello/ack)',
          true,
          `engine=${engineName} session=${sessionId}`
        );

        // Now send read-only actions (automation_request envelope)
        trySendAction('system_control', { action: 'get_engine_version' });
        trySendAction('list_light_types', {});
        checkAllDone();
        return;
      }

      // --- bridge_ping (heartbeat) — just ignore ---
      if (msg.type === 'bridge_ping') return;

      // --- Action responses (type: automation_response) ---
      const rid = msg.requestId;
      if (rid && pendingRequests.has(rid)) {
        const actionName = pendingRequests.get(rid);
        pendingRequests.delete(rid);

        const isError = msg.success === false || !!msg.error;

        if (actionName === 'system_control') {
          engineVersionDone = true;
          if (!isError) {
            const ver = msg.result?.version || msg.result?.engineVersion || JSON.stringify(msg.result || {}).substring(0, 120);
            record('Read-only action: get_engine_version', true, `${ver}`);
          } else {
            record('Read-only action: get_engine_version', false, msg.error || 'Unknown error');
          }
        }

        if (actionName === 'list_light_types') {
          listLightsDone = true;
          if (!isError) {
            // Response shape varies; find the array wherever it is
            const r = msg.result || {};
            const arr = Array.isArray(r) ? r
              : Array.isArray(r.lightTypes) ? r.lightTypes
              : Array.isArray(r.types) ? r.types
              : null;
            const detail = arr ? `${arr.length} light types returned` : 'response OK';
            record('Read-only action: list_light_types', true, detail);
          } else {
            record('Read-only action: list_light_types', false, msg.error || 'Unknown error');
          }
        }

        checkAllDone();
        return;
      }

      // --- Unknown/unmatched messages: check for action results without requestId ---
      if (msg.status === 'success' || msg.result) {
        // Could be a response to one of our actions without requestId matching
        // Try to figure out which one
        if (!engineVersionDone && (msg.result?.version || msg.result?.engineVersion)) {
          engineVersionDone = true;
          const ver = msg.result?.version || msg.result?.engineVersion || '';
          record('Read-only action: get_engine_version', true, `${ver}`);
          checkAllDone();
        }
      }
    });

    ws.on('error', (err) => {
      if (!handshakeDone) {
        record('WebSocket handshake (bridge_hello/ack)', false, err.message);
        handshakeDone = true;
      }
      if (!engineVersionDone) {
        record('Read-only action: get_engine_version', false, 'WebSocket error');
        engineVersionDone = true;
      }
      if (!listLightsDone) {
        record('Read-only action: list_light_types', false, 'WebSocket error');
        listLightsDone = true;
      }
      cleanup();
    });

    ws.on('close', () => {
      if (!handshakeDone) {
        record('WebSocket handshake (bridge_hello/ack)', false, 'Connection closed before handshake');
        handshakeDone = true;
      }
      if (!engineVersionDone) {
        record('Read-only action: get_engine_version', false, 'Connection closed');
        engineVersionDone = true;
      }
      if (!listLightsDone) {
        record('Read-only action: list_light_types', false, 'Connection closed');
        listLightsDone = true;
      }
      cleanup();
    });
  });
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
async function main() {
  log('');
  log('=== MCP Automation Bridge Health Check ===');
  log(`Target: ws://${HOST}:${PORT}`);
  log(`Time:   ${new Date().toISOString()}`);
  log('');

  const tcpOk = await testTcpPort();

  if (tcpOk) {
    await testWebSocket();
  } else {
    record('WebSocket handshake (bridge_hello/ack)', false, 'Skipped (TCP unreachable)');
    record('Read-only action: get_engine_version', false, 'Skipped (TCP unreachable)');
    record('Read-only action: list_light_types', false, 'Skipped (TCP unreachable)');
  }

  log('');
  const passed = results.filter(r => r.pass).length;
  const total = results.length;
  const allGood = passed === total;

  if (JSON_OUTPUT) {
    process.stdout.write(JSON.stringify({ passed, total, allGood, results }, null, 2) + '\n');
  } else {
    log(`Result: ${passed}/${total} checks passed` + (allGood ? ' -- ALL OK' : ' -- ISSUES DETECTED'));
    log('');
  }

  process.exit(allGood ? 0 : 1);
}

main().catch((err) => {
  console.error('Unexpected error:', err);
  process.exit(2);
});
