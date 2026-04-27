// ── Claude Shell Remote Client ──

let ws = null;
let currentRole = 'controller';
let attachedSessionId = null;
let sessions = [];
let reconnectTimer = null;
let reconnectDelay = 1000;

// ── Connection ──

function toggleConnection() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.close();
        return;
    }
    connect();
}

function connect() {
    const url = document.getElementById('ws-url').value.trim();
    const token = document.getElementById('ws-token').value.trim();
    currentRole = document.getElementById('ws-role').value;

    if (!url) { setStatus('Enter WebSocket URL'); return; }
    if (!token) { setStatus('Enter auth token'); return; }

    setBadge('connecting');
    setStatus('Connecting...');
    document.getElementById('connect-btn').textContent = 'Connecting...';

    try {
        ws = new WebSocket(url);
    } catch (e) {
        setStatus('Invalid URL');
        setBadge('disconnected');
        return;
    }

    ws.onopen = () => {
        ws.send(JSON.stringify({ type: 'auth', token, role: currentRole }));
        reconnectDelay = 1000;
    };

    ws.onmessage = (e) => {
        try {
            const msg = JSON.parse(e.data);
            handleMessage(msg);
        } catch (err) {
            console.error('Parse error:', err);
        }
    };

    ws.onclose = (e) => {
        setBadge('disconnected');

        // Detect content filter / restrictions blocking WebSocket
        // Code 1006 = abnormal closure (no close frame received)
        // If it happens repeatedly and quickly, it's likely a content filter
        const isSafari = /Safari/.test(navigator.userAgent) && !/Chrome/.test(navigator.userAgent);
        const wasBlocked = (e.code === 1006 && !e.wasClean && reconnectDelay >= 2000);
        if (wasBlocked) {
            let msg = 'WebSocket connection is being blocked. ';
            if (isSafari) {
                msg += 'Safari may have content restrictions enabled. '
                     + 'Go to: System Settings \u2192 Screen Time \u2192 Content & Privacy '
                     + '\u2192 Web Content \u2192 Unrestricted Access. '
                     + 'Then restart Safari.';
            } else {
                msg += 'Check browser extensions, firewall, or proxy settings '
                     + 'that may block WebSocket connections to localhost.';
            }
            setStatus(msg);
            addSystemMessage(msg);
            document.getElementById('connect-btn').textContent = 'Retry';
            disableInput();
            attachedSessionId = null;
            // Slow down reconnect attempts
            reconnectDelay = 30000;
        } else {
            setStatus('Disconnected');
        }
        document.getElementById('connect-btn').textContent = 'Connect';
        document.getElementById('close-tunnel-btn').classList.add('hidden');
        disableInput();
        attachedSessionId = null;
        updateSessionHighlight();

        // Auto-reconnect
        if (reconnectTimer) clearTimeout(reconnectTimer);
        reconnectTimer = setTimeout(() => {
            if (document.getElementById('ws-url').value.trim() &&
                document.getElementById('ws-token').value.trim()) {
                connect();
            }
        }, reconnectDelay);
        reconnectDelay = Math.min(reconnectDelay * 2, 10000);
    };

    ws.onerror = () => {
        setStatus('Connection error');
    };
}

// ── Message Router ──

function handleMessage(msg) {
    switch (msg.type) {
        case 'auth_result':
            if (msg.success) {
                setBadge('connected');
                setStatus(`Authenticated as ${msg.role}`);
                document.getElementById('connect-btn').textContent = 'Disconnect';
                currentRole = msg.role;
                if (currentRole === 'controller') enableInput();
                // Show Close Tunnel button when connected via tunnel (not localhost)
                const connHost = window.location.hostname;
                if (connHost && connHost !== 'localhost' && connHost !== '127.0.0.1' && connHost !== '::1') {
                    document.getElementById('close-tunnel-btn').classList.remove('hidden');
                }
            } else {
                setStatus('Authentication failed');
                ws.close();
            }
            break;

        case 'session_list':
            sessions = msg.sessions || [];
            renderSessionList();
            break;

        case 'attached':
            attachedSessionId = msg.sessionId;
            updateSessionHighlight();
            const info = msg.sessionInfo || {};
            document.getElementById('session-info').innerHTML =
                `<strong>${info.name || msg.sessionId.substring(0, 8)}</strong> &mdash; ${info.cwd || ''}`;
            addSystemMessage(`Attached to session ${info.name || msg.sessionId.substring(0, 8)}`);
            if (currentRole === 'controller') enableInput();
            // Request history
            ws.send(JSON.stringify({ type: 'history_request' }));
            break;

        case 'detached':
            attachedSessionId = null;
            updateSessionHighlight();
            document.getElementById('session-info').innerHTML =
                '<span class="muted">No session attached</span>';
            addSystemMessage('Detached from session');
            disableInput();
            break;

        case 'claude_event':
            handleClaudeEvent(msg.event, msg.sessionId);
            break;

        case 'history':
            if (msg.events) {
                document.getElementById('messages').innerHTML = '';
                for (const ev of msg.events) {
                    if (ev.event) handleClaudeEvent(ev.event, ev.sessionId);
                }
            }
            break;

        case 'input_echo':
            addUserMessage(msg.content);
            break;

        case 'status':
            const parts = [];
            if (msg.viewers !== undefined) parts.push(`${msg.viewers} viewer(s)`);
            if (msg.controllers !== undefined) parts.push(`${msg.controllers} controller(s)`);
            document.getElementById('session-status').textContent = parts.join(' | ');
            break;

        case 'session_ended':
            addSystemMessage(`Session ended (exit code ${msg.exitCode})`);
            break;

        case 'error':
            addSystemMessage(`Error: ${msg.message} (${msg.code})`);
            setStatus(`Error: ${msg.message}`);
            break;

        case 'tunnel_closed':
            addSystemMessage(msg.message || 'Tunnel closed');
            document.getElementById('close-tunnel-btn').classList.add('hidden');
            break;

        case 'pong':
            break;
    }
}

// ── Claude Event Rendering ──

let currentAssistantDiv = null;
let currentAssistantText = '';

function handleClaudeEvent(event, sessionId) {
    if (!event || !event.type) return;

    switch (event.type) {
        case 'assistant':
            handleAssistantEvent(event);
            break;
        case 'result':
            // Final result — finalize current assistant message
            currentAssistantDiv = null;
            currentAssistantText = '';
            break;
        case 'tool_use':
            currentAssistantDiv = null;
            addToolMessage('tool_use', event);
            break;
        case 'tool_result':
            addToolMessage('tool_result', event);
            break;
        default:
            // Unknown event type — show raw
            addToolMessage(event.type, event);
    }
}

function handleAssistantEvent(event) {
    // Extract text content from the message
    let text = '';
    if (event.message) {
        if (typeof event.message === 'string') {
            text = event.message;
        } else if (event.message.content) {
            if (typeof event.message.content === 'string') {
                text = event.message.content;
            } else if (Array.isArray(event.message.content)) {
                for (const block of event.message.content) {
                    if (block.type === 'text') text += block.text;
                }
            }
        }
    }

    if (!text) return;

    if (currentAssistantDiv) {
        // Append to existing streaming message
        currentAssistantText += text;
        currentAssistantDiv.querySelector('.message-body').innerHTML =
            renderMarkdown(currentAssistantText);
    } else {
        // New assistant message
        currentAssistantText = text;
        const div = document.createElement('div');
        div.className = 'message message-assistant';
        div.innerHTML = `<div class="message-label">Claude</div><div class="message-body">${renderMarkdown(text)}</div>`;
        document.getElementById('messages').appendChild(div);
        currentAssistantDiv = div;
    }
    scrollToBottom();
}

function addUserMessage(content) {
    currentAssistantDiv = null;
    const div = document.createElement('div');
    div.className = 'message message-user';
    div.innerHTML = `<div class="message-label">You</div><div class="message-body">${escapeHtml(content)}</div>`;
    document.getElementById('messages').appendChild(div);
    scrollToBottom();
}

function addToolMessage(type, event) {
    const div = document.createElement('div');
    div.className = 'message message-tool';
    let label = type;
    let body = '';

    if (type === 'tool_use') {
        label = `Tool: ${event.name || 'unknown'}`;
        body = `<pre>${escapeHtml(JSON.stringify(event.input || event, null, 2))}</pre>`;
    } else if (type === 'tool_result') {
        label = 'Tool Result';
        const content = event.content || event.output || '';
        body = `<pre>${escapeHtml(typeof content === 'string' ? content : JSON.stringify(content, null, 2))}</pre>`;
    } else {
        label = type;
        body = `<pre>${escapeHtml(JSON.stringify(event, null, 2))}</pre>`;
    }

    div.innerHTML = `<div class="message-label">${escapeHtml(label)}</div><div class="message-body">${body}</div>`;
    document.getElementById('messages').appendChild(div);
    scrollToBottom();
}

function addSystemMessage(text) {
    const div = document.createElement('div');
    div.className = 'message message-system';
    div.textContent = text;
    document.getElementById('messages').appendChild(div);
    scrollToBottom();
}

// ── Session List ──

function renderSessionList() {
    const container = document.getElementById('session-list');
    if (sessions.length === 0) {
        container.innerHTML = '<p class="muted" style="padding:8px">No sessions found</p>';
        return;
    }

    container.innerHTML = sessions.map(s => {
        const name = s.name || s.sessionId.substring(0, 8);
        const dotClass = s.proxied ? 'dot-proxied' : (s.alive ? 'dot-alive' : 'dot-dead');
        const isActive = s.sessionId === attachedSessionId;
        const startTime = s.startedAt ? new Date(s.startedAt).toLocaleTimeString() : '';
        // Shorten cwd for display
        const cwd = s.cwd ? (s.cwd.length > 35 ? '...' + s.cwd.slice(-32) : s.cwd) : '';

        return `
            <div class="session-card ${isActive ? 'active' : ''}"
                 onclick="attachToSession('${s.sessionId}')"
                 title="${escapeHtml(s.cwd || '')}">
                <div class="session-name">
                    <span class="dot ${dotClass}"></span>
                    ${escapeHtml(name)}
                </div>
                <div class="session-detail">${escapeHtml(cwd)}</div>
                <div class="session-detail">PID ${s.pid} &middot; ${startTime}</div>
            </div>
        `;
    }).join('');
}

function updateSessionHighlight() {
    document.querySelectorAll('.session-card').forEach(card => {
        card.classList.remove('active');
    });
    // Re-render to update highlights
    renderSessionList();
}

function attachToSession(sessionId) {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;

    if (attachedSessionId === sessionId) {
        // Already attached — detach
        ws.send(JSON.stringify({ type: 'detach' }));
        return;
    }

    // Clear messages when switching sessions
    document.getElementById('messages').innerHTML = '';
    currentAssistantDiv = null;
    currentAssistantText = '';

    ws.send(JSON.stringify({ type: 'attach', sessionId }));
}

function requestSessionList() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'list_sessions' }));
    }
}

// ── Input ──

function sendMessage() {
    const input = document.getElementById('user-input');
    const content = input.value.trim();
    if (!content || !ws || ws.readyState !== WebSocket.OPEN || !attachedSessionId) return;

    ws.send(JSON.stringify({ type: 'user_message', content }));
    input.value = '';
    input.focus();
}

function sendAbort() {
    if (ws && ws.readyState === WebSocket.OPEN && attachedSessionId) {
        ws.send(JSON.stringify({ type: 'control', action: 'abort' }));
        addSystemMessage('Abort requested');
    }
}

function closeTunnel() {
    if (!ws || ws.readyState !== WebSocket.OPEN) return;
    if (!confirm('Close the tunnel? You will lose remote access.')) return;
    ws.send(JSON.stringify({ type: 'close_tunnel', host: window.location.host }));
}

document.getElementById('user-input').addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && !e.shiftKey) {
        e.preventDefault();
        sendMessage();
    }
});

function enableInput() {
    if (currentRole !== 'controller' || !attachedSessionId) return;
    document.getElementById('user-input').disabled = false;
    document.getElementById('send-btn').disabled = false;
    document.getElementById('abort-btn').disabled = false;
}

function disableInput() {
    document.getElementById('user-input').disabled = true;
    document.getElementById('send-btn').disabled = true;
    document.getElementById('abort-btn').disabled = true;
}

// ── URL Parsing ──

(function parseUrlParams() {
    const params = new URLSearchParams(window.location.search);
    const token = params.get('token');
    const urlParam = params.get('url');

    // Auto-fill URL from current page location (works when served by proxy)
    const host = window.location.host || 'localhost';
    const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = urlParam || `${proto}//${host}/ws`;
    document.getElementById('ws-url').value = wsUrl;

    if (token) {
        document.getElementById('ws-token').value = token;
        // Auto-connect if both URL and token are provided
        setTimeout(() => connect(), 300);
    }
})();

// ── Helpers ──

function setBadge(state) {
    const badge = document.getElementById('connection-badge');
    badge.className = 'badge badge-' + state;
    badge.textContent = state.charAt(0).toUpperCase() + state.slice(1);
}

function setStatus(text) {
    document.getElementById('status-text').textContent = text;
}

function scrollToBottom() {
    const el = document.getElementById('messages');
    requestAnimationFrame(() => { el.scrollTop = el.scrollHeight; });
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function renderMarkdown(text) {
    // Minimal markdown: code blocks, inline code, bold, newlines
    let html = escapeHtml(text);

    // Code blocks: ```...```
    html = html.replace(/```(\w*)\n([\s\S]*?)```/g, '<pre><code>$2</code></pre>');

    // Inline code: `...`
    html = html.replace(/`([^`]+)`/g, '<code>$1</code>');

    // Bold: **...**
    html = html.replace(/\*\*(.+?)\*\*/g, '<strong>$1</strong>');

    // Newlines
    html = html.replace(/\n/g, '<br>');

    return html;
}

// Keepalive ping
setInterval(() => {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ type: 'ping' }));
    }
}, 30000);
