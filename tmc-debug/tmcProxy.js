// Core Debug Adapter Protocol proxy for TooManyCooks coroutine debugging.
//
// Sits between the client (VS Code, or a test driver) and a stock cpptools
// `OpenDebugAD7` child adapter. It leaves normal debugging untouched and, for
// suspended-coroutine stacks, reconstructs the async call frames and their locals
// entirely at the DAP layer - so no MIEngine patch and no `-enable-frame-filters`
// (which stock MIEngine cannot consume) are needed.
//
// What it does:
//   * stackTrace responses: splice in the coroutine async-continuation frames
//     (obtained via `$__tmc_async_chain()` evaluated in the stopped frame).
//   * scopes/variables for those synthetic frames: synthesized via
//     `$__coro_local_names(addr)` + `(*$__coro_frame_at(addr)).<name>` evaluations.
//   * everything else: passed through verbatim.
//
// The class is transport-agnostic: construct it with a `sendToClient` callback and
// feed it client messages via `handleClientMessage`. A standalone stdio wrapper and
// a VS Code inline-adapter wrapper both reuse it.

'use strict';
const cp = require('child_process');
const path = require('path');

// Synthetic id ranges, kept far above anything the child adapter allocates.
const SYN_FRAME_BASE = 0x40000000;
const SYN_VARREF_BASE = 0x50000000;
// Upper bound on frames fetched when materializing a full coroutine stack.
const MAX_FULL_FRAMES = 4096;

function b64json(resultString) {
    // The evaluate result for a char[] value is rendered with surrounding quotes and
    // possibly C escapes. Extract the base64 payload and decode it.
    if (!resultString) return null;
    let s = String(resultString).trim();
    const first = s.indexOf('"');
    const last = s.lastIndexOf('"');
    if (first >= 0 && last > first) s = s.substring(first + 1, last);
    s = s.replace(/\\(.)/g, '$1'); // unescape
    try {
        return JSON.parse(Buffer.from(s, 'base64').toString('utf8'));
    } catch (e) {
        return null;
    }
}

function parseNames(resultString) {
    if (!resultString) return [];
    let s = String(resultString).trim();
    const first = s.indexOf('"');
    const last = s.lastIndexOf('"');
    if (first >= 0 && last > first) s = s.substring(first + 1, last);
    return s.split(/\s+/).filter(Boolean);
}

function hexAddr(addr) {
    // addr arrives as a JS number (possibly > 2^31); format as unsigned hex.
    return '0x' + BigInt(addr).toString(16);
}

class TmcProxy {
    constructor(sendToClient, opendebugPath, opendebugArgs, spawnOptions) {
        this._toClientRaw = sendToClient;
        this._clientSeq = 0;
        this._childSeq = 0;
        this._childPending = new Map();   // childSeq -> {clientSeq, command}
        this._injectPending = new Map();  // childSeq -> resolve fn
        this._clientPending = new Map();  // clientSeq -> childSeq (child reverse-requests)
        this._stackCache = new Map();     // threadId -> augmented frame array (per stop)
        this._synFrame = new Map();       // synFrameId -> {threadId, coroAddr, evalFrameId}
        this._synVarRef = new Map();      // synVarRef -> {coroAddr, evalFrameId}
        this._nextSynFrame = SYN_FRAME_BASE;
        this._nextSynVarRef = SYN_VARREF_BASE;

        this._child = cp.spawn(opendebugPath, opendebugArgs || [], spawnOptions || {});
        this._buf = Buffer.alloc(0);
        this._child.stdout.on('data', (d) => this._onChildData(d));
        this._child.stderr.on('data', (d) => { if (process.env.TMC_PROXY_DEBUG) process.stderr.write(d); });
        this._child.on('exit', (code) => this._toClientRaw && this._onChildExit(code));
    }

    // ---- framing ----
    _frame(msg) {
        const json = JSON.stringify(msg);
        return `Content-Length: ${Buffer.byteLength(json, 'utf8')}\r\n\r\n${json}`;
    }
    _toClient(msg) {
        msg.seq = ++this._clientSeq;
        this._toClientRaw(msg);
    }
    _toChild(msg) {
        this._child.stdin.write(this._frame(msg));
    }
    _onChildData(chunk) {
        this._buf = Buffer.concat([this._buf, chunk]);
        for (;;) {
            const headerEnd = this._buf.indexOf('\r\n\r\n');
            if (headerEnd < 0) return;
            const header = this._buf.slice(0, headerEnd).toString('utf8');
            const m = /Content-Length:\s*(\d+)/i.exec(header);
            if (!m) { this._buf = this._buf.slice(headerEnd + 4); continue; }
            const len = parseInt(m[1], 10);
            const bodyStart = headerEnd + 4;
            if (this._buf.length < bodyStart + len) return;
            const body = this._buf.slice(bodyStart, bodyStart + len).toString('utf8');
            this._buf = this._buf.slice(bodyStart + len);
            let msg;
            try { msg = JSON.parse(body); } catch (e) { continue; }
            this._onChildMessage(msg);
        }
    }

    // ---- injected request/response to the child ----
    _inject(command, args) {
        return new Promise((resolve) => {
            const cs = ++this._childSeq;
            this._injectPending.set(cs, resolve);
            this._toChild({ seq: cs, type: 'request', command, arguments: args });
        });
    }
    async _evaluate(expression, frameId) {
        const resp = await this._inject('evaluate', { expression, frameId, context: 'repl' });
        return (resp && resp.success) ? resp.body : null;
    }

    // ---- client -> proxy ----
    handleClientMessage(msg) {
        if (msg.type === 'request') return this._onClientRequest(msg);
        if (msg.type === 'response') {
            // Client responding to a child reverse-request.
            const childSeq = this._clientPending.get(msg.request_seq);
            if (childSeq !== undefined) {
                this._clientPending.delete(msg.request_seq);
                this._toChild(Object.assign({}, msg, { seq: ++this._childSeq, request_seq: childSeq }));
            }
            return;
        }
        // events from client (rare) - forward
        this._toChild(Object.assign({}, msg, { seq: ++this._childSeq }));
    }

    _onClientRequest(req) {
        const cmd = req.command;
        // The child is a stock cppdbg adapter; present our own debug type to it as cppdbg.
        if (cmd === 'initialize' && req.arguments && req.arguments.adapterID) {
            req.arguments.adapterID = 'cppdbg';
        }
        if ((cmd === 'launch' || cmd === 'attach') && req.arguments && req.arguments.type) {
            req.arguments.type = 'cppdbg';
        }
        if (cmd === 'scopes' && this._synFrame.has(req.arguments && req.arguments.frameId)) {
            return this._synthScopes(req);
        }
        if (cmd === 'variables' && this._synVarRef.has(req.arguments && req.arguments.variablesReference)) {
            return this._synthVariables(req);
        }
        if (cmd === 'stackTrace') {
            return this._handleStackTrace(req);
        }
        // Default: forward to the child, remembering the client's seq for correlation.
        const cs = ++this._childSeq;
        this._childPending.set(cs, { clientSeq: req.seq, command: cmd });
        this._toChild(Object.assign({}, req, { seq: cs }));
    }

    // ---- child -> proxy ----
    _onChildMessage(msg) {
        if (msg.type === 'response') {
            const cs = msg.request_seq;
            if (this._injectPending.has(cs)) {
                const resolve = this._injectPending.get(cs);
                this._injectPending.delete(cs);
                resolve(msg);
                return;
            }
            const p = this._childPending.get(cs);
            if (p) {
                this._childPending.delete(cs);
                msg.request_seq = p.clientSeq;
                return this._toClient(msg);
            }
            return this._toClient(msg); // unknown - forward defensively
        }
        if (msg.type === 'event') {
            if (msg.event === 'stopped' || msg.event === 'continued') {
                // Frame ids and stacks are only valid within a stop; drop stale state.
                this._synFrame.clear();
                this._synVarRef.clear();
                this._stackCache.clear();
            }
            return this._toClient(msg);
        }
        if (msg.type === 'request') {
            // Child reverse-request (e.g. runInTerminal): forward, remember mapping.
            const childReqSeq = msg.seq;
            const forwarded = Object.assign({}, msg);
            forwarded.seq = ++this._clientSeq;
            this._clientPending.set(forwarded.seq, childReqSeq);
            return this._toClientRaw(forwarded);
        }
        this._toClient(msg);
    }

    // Serve a (possibly paged) stackTrace. For a coroutine stack we build the full augmented
    // frame list once per stop - real frames with the async continuation frames spliced in after
    // the coroutine frame - cache it, and serve every page as a slice of that stable model. This
    // is correct regardless of where the coroutine frame sits (nested calls) and regardless of
    // how the client pages the stack. Non-coroutine stacks are forwarded verbatim (stay lazy).
    async _handleStackTrace(req) {
        const args = req.arguments || {};
        const threadId = args.threadId;
        const startFrame = args.startFrame || 0;
        const levels = args.levels || 0;
        try {
            let aug = this._stackCache.get(threadId);
            if (!aug) {
                aug = await this._buildAugmentedStack(threadId, req);
                if (aug === null) return; // handled by pass-through
                this._stackCache.set(threadId, aug);
            }
            const end = levels > 0 ? startFrame + levels : undefined;
            this._toClient({
                type: 'response', request_seq: req.seq, success: true, command: 'stackTrace',
                body: { stackFrames: aug.slice(startFrame, end), totalFrames: aug.length },
            });
        } catch (e) {
            if (process.env.TMC_PROXY_DEBUG) process.stderr.write('stackTrace error: ' + e + '\n');
            this._passThroughStack(req);
        }
    }

    // Returns the augmented frame array for a thread, or null if it forwarded the request
    // verbatim (non-coroutine stack) and there is nothing to cache.
    async _buildAugmentedStack(threadId, req) {
        // Cheap probe: fetch just the top frame to test whether this is a coroutine stack.
        const probe = await this._inject('stackTrace', { threadId, startFrame: 0, levels: 1 });
        const probeFrames = (probe && probe.body && probe.body.stackFrames) || [];
        if (!probe || !probe.success || probeFrames.length === 0) {
            this._passThroughStack(req);
            return null;
        }
        const chainBody = await this._evaluate('(char*)$__tmc_async_chain()', probeFrames[0].id);
        const chain = chainBody && b64json(chainBody.result);
        if (!chain || !chain.frames || chain.frames.length === 0) {
            this._passThroughStack(req); // not a coroutine stack - stay lazy
            return null;
        }
        // Coroutine stack: materialize the whole real stack and splice the async frames in.
        const full = await this._inject('stackTrace', { threadId, startFrame: 0, levels: MAX_FULL_FRAMES });
        const realFrames = (full && full.body && full.body.stackFrames) || [];
        const evalFrameId = realFrames.length ? realFrames[0].id : probeFrames[0].id;
        const synFrames = chain.frames.map((f) => {
            const id = this._nextSynFrame++;
            this._synFrame.set(id, { threadId, coroAddr: f.address, evalFrameId });
            const frame = { id, name: f.function, line: f.line || 0, column: 0 };
            if (f.file) frame.source = { name: path.basename(f.file), path: f.file };
            return frame;
        });
        const aug = realFrames.slice();
        const insertAt = Math.min((chain.start || 0) + 1, aug.length);
        aug.splice(insertAt, 0, ...synFrames);
        return aug;
    }

    _passThroughStack(req) {
        const cs = ++this._childSeq;
        this._childPending.set(cs, { clientSeq: req.seq, command: 'stackTrace' });
        this._toChild(Object.assign({}, req, { seq: cs }));
    }

    _synthScopes(req) {
        const info = this._synFrame.get(req.arguments.frameId);
        const ref = this._nextSynVarRef++;
        this._synVarRef.set(ref, { coroAddr: info.coroAddr, evalFrameId: info.evalFrameId });
        this._toClient({
            type: 'response', request_seq: req.seq, success: true, command: 'scopes',
            body: { scopes: [{ name: 'Locals', variablesReference: ref, expensive: false }] },
        });
    }

    async _synthVariables(req) {
        const info = this._synVarRef.get(req.arguments.variablesReference);
        const variables = [];
        try {
            const addr = hexAddr(info.coroAddr);
            const namesBody = await this._evaluate(`(char*)$__coro_local_names(${addr})`, info.evalFrameId);
            const names = parseNames(namesBody && namesBody.result);
            for (const name of names) {
                const b = await this._evaluate(`(*$__coro_frame_at(${addr})).${name}`, info.evalFrameId);
                if (!b) continue;
                variables.push({
                    name,
                    value: b.result != null ? b.result : '',
                    type: b.type,
                    variablesReference: b.variablesReference || 0,
                    memoryReference: b.memoryReference,
                });
            }
        } catch (e) {
            if (process.env.TMC_PROXY_DEBUG) process.stderr.write('variables error: ' + e + '\n');
        }
        this._toClient({
            type: 'response', request_seq: req.seq, success: true, command: 'variables',
            body: { variables },
        });
    }

    _onChildExit(code) {
        try { this._toClientRaw({ seq: ++this._clientSeq, type: 'event', event: 'terminated', body: {} }); } catch (e) {}
        if (this._onExit) this._onExit(code);
    }
}

module.exports = { TmcProxy };
