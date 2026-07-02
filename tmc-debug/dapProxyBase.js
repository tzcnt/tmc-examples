// Shared Debug Adapter Protocol proxy machinery.
//
// Spawns a child DAP adapter, relays messages between it and the client, and provides
// request injection (issuing the proxy's own requests to the child and awaiting their
// responses). Subclasses override the hooks to transform specific traffic:
//
//   onClientRequest(req)          - a request arrived from the client (default: forward)
//   onChildResponse(msg, pending) - the child answered a forwarded client request
//                                   (request_seq already restored; default: send to client)
//   onStopOrContinue(msg)         - a stopped/continued event passed through (default: no-op)
//
// Correlation is by seq: forwarded client requests and injected requests share one child-facing
// seq space; the client-facing seq space is rewritten so it stays monotonic.

'use strict';
const cp = require('child_process');

class DapProxyBase {
    constructor(sendToClient, childPath, childArgs, spawnOptions) {
        this._toClientRaw = sendToClient;
        this._clientSeq = 0;
        this._childSeq = 0;
        this._childPending = new Map();   // childSeq -> {clientSeq, command}
        this._injectPending = new Map();  // childSeq -> resolve fn
        this._clientPending = new Map();  // clientSeq -> childSeq (child reverse-requests)

        this._child = cp.spawn(childPath, childArgs || [], spawnOptions || {});
        this._buf = Buffer.alloc(0);
        this._child.stdout.on('data', (d) => this._onChildData(d));
        this._child.stderr.on('data', (d) => { if (process.env.TMC_PROXY_DEBUG) process.stderr.write(d); });
        this._child.on('exit', (code) => this._onChildExit(code));
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
    async _evaluate(expression, frameId, context) {
        const resp = await this._inject('evaluate', { expression, frameId, context: context || 'repl' });
        return (resp && resp.success) ? resp.body : null;
    }

    // Forward a client request to the child verbatim, remembering the client's seq.
    forward(req) {
        const cs = ++this._childSeq;
        this._childPending.set(cs, { clientSeq: req.seq, command: req.command });
        this._toChild(Object.assign({}, req, { seq: cs }));
    }

    // ---- client -> proxy ----
    handleClientMessage(msg) {
        if (msg.type === 'request') return this.onClientRequest(msg);
        if (msg.type === 'response') {
            // Client responding to a child reverse-request.
            const childSeq = this._clientPending.get(msg.request_seq);
            if (childSeq !== undefined) {
                this._clientPending.delete(msg.request_seq);
                this._toChild(Object.assign({}, msg, { seq: ++this._childSeq, request_seq: childSeq }));
            }
            return;
        }
        this._toChild(Object.assign({}, msg, { seq: ++this._childSeq }));
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
                return this.onChildResponse(msg, p);
            }
            return this._toClient(msg); // unknown - forward defensively
        }
        if (msg.type === 'event') {
            if (msg.event === 'stopped' || msg.event === 'continued') {
                this.onStopOrContinue(msg);
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

    _onChildExit(code) {
        try { this._toClientRaw({ seq: ++this._clientSeq, type: 'event', event: 'terminated', body: {} }); } catch (e) {}
        if (this._onExit) this._onExit(code);
    }

    dispose() {
        try { this._child.kill(); } catch (e) { /* ignore */ }
    }

    // ---- overridable hooks ----
    onClientRequest(req) { this.forward(req); }
    onChildResponse(msg, _pending) { this._toClient(msg); }
    onStopOrContinue(_msg) { }
}

module.exports = { DapProxyBase };
