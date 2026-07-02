// LLDB / lldb-dap proxy for TooManyCooks coroutine debugging.
//
// Unlike the GDB path, lldb-dap already shows the coroutine async frames natively (the script
// registers an LLDB ScriptedFrameProvider). The only gap is LOCALS: LLDB 22.x defines the
// ScriptedFrame `get_variables` hook but does not yet invoke it for the Variables pane
// (LLVM PR #178575). This proxy fills that gap as a stopgap:
//
//   * stackTrace: pass through untouched, but note which frames are async (name starts with
//     "[async]"/"[fork]") and their order, so their scopes can be intercepted.
//   * scopes/variables for those frames: synthesized by evaluating the `tmc-coro-locals`
//     command (which reuses the exact get_variables logic) and slicing out this frame's locals.
//
// Locals are currently flat (scalars show values; aggregates show "{...}"); full expansion
// arrives automatically once PR #178575 ships and this proxy can be dropped.

'use strict';
const { DapProxyBase } = require('./dapProxyBase');

const SYN_VARREF_BASE = 0x50000000;
const MAX_FULL_FRAMES = 4096;

function isAsyncName(name) {
    return typeof name === 'string' && name.indexOf('[async]') >= 0;
}

function decodeLocals(resultString) {
    // lldb-dap returns command output verbatim in `result` (e.g. "<base64>\n").
    if (!resultString) return null;
    const m = String(resultString).match(/[A-Za-z0-9+/=]{8,}/);
    if (!m) return null;
    try {
        return JSON.parse(Buffer.from(m[0], 'base64').toString('utf8'));
    } catch (e) {
        return null;
    }
}

class TmcLldbProxy extends DapProxyBase {
    constructor(sendToClient, lldbDapPath, lldbDapArgs, spawnOptions) {
        super(sendToClient, lldbDapPath, lldbDapArgs, spawnOptions);
        this._asyncFrame = new Map();   // frameId -> {threadId, ordinal, frame0Id}
        this._asyncBuilt = new Set();   // threadId with async map built (per stop)
        this._synVarRef = new Map();    // varRef -> {threadId, ordinal, frame0Id}
        this._localsCache = new Map();  // threadId -> [{function, locals}]
        this._stackReqThread = new Map(); // client stackTrace seq -> threadId
        this._nextSynVarRef = SYN_VARREF_BASE;
    }

    onStopOrContinue() {
        this._asyncFrame.clear();
        this._asyncBuilt.clear();
        this._synVarRef.clear();
        this._localsCache.clear();
    }

    onClientRequest(req) {
        const cmd = req.command;
        if (cmd === 'initialize' && req.arguments && req.arguments.adapterID) {
            req.arguments.adapterID = 'lldb-dap';
        }
        if ((cmd === 'launch' || cmd === 'attach') && req.arguments && req.arguments.type) {
            req.arguments.type = 'lldb-dap';
        }
        if (cmd === 'scopes' && this._asyncFrame.has(req.arguments && req.arguments.frameId)) {
            return this._synthScopes(req);
        }
        if (cmd === 'variables' && this._synVarRef.has(req.arguments && req.arguments.variablesReference)) {
            return this._synthVariables(req);
        }
        if (cmd === 'stackTrace' && req.arguments) {
            this._stackReqThread.set(req.seq, req.arguments.threadId);
        }
        this.forward(req);
    }

    onChildResponse(msg, pending) {
        if (pending.command === 'stackTrace' && msg.success) {
            const threadId = this._stackReqThread.get(pending.clientSeq);
            this._stackReqThread.delete(pending.clientSeq);
            // Ensure the thread's async-frame id->ordinal map is built (once per stop), then
            // forward the (possibly paged) response unchanged - the async frames are already
            // registered by id, so their scopes get intercepted wherever they appear.
            this._ensureAsyncMap(threadId).then(() => this._toClient(msg),
                () => this._toClient(msg));
            return;
        }
        this._toClient(msg);
    }

    async _ensureAsyncMap(threadId) {
        if (threadId === undefined || this._asyncBuilt.has(threadId)) return;
        this._asyncBuilt.add(threadId);
        const full = await this._inject('stackTrace', { threadId, startFrame: 0, levels: MAX_FULL_FRAMES });
        const frames = (full && full.body && full.body.stackFrames) || [];
        if (!frames.length) return;
        const frame0Id = frames[0].id;
        let ordinal = 0;
        for (const f of frames) {
            if (isAsyncName(f.name)) {
                this._asyncFrame.set(f.id, { threadId, ordinal, frame0Id });
                ordinal++;
            }
        }
    }

    _synthScopes(req) {
        const info = this._asyncFrame.get(req.arguments.frameId);
        const ref = this._nextSynVarRef++;
        this._synVarRef.set(ref, info);
        this._toClient({
            type: 'response', request_seq: req.seq, success: true, command: 'scopes',
            body: { scopes: [{ name: 'Locals', variablesReference: ref, expensive: false }] },
        });
    }

    async _synthVariables(req) {
        const info = this._synVarRef.get(req.arguments.variablesReference);
        const variables = [];
        try {
            let chain = this._localsCache.get(info.threadId);
            if (!chain) {
                const body = await this._evaluate(`tmc-coro-locals ${info.threadId}`, info.frame0Id);
                chain = decodeLocals(body && body.result) || [];
                this._localsCache.set(info.threadId, chain);
            }
            const entry = chain[info.ordinal];
            const locals = (entry && entry.locals) || [];
            for (const l of locals) {
                let value = l.value;
                if (!value) value = (l.numChildren > 0) ? '{...}' : '';
                variables.push({ name: l.name, value, type: l.type, variablesReference: 0 });
            }
        } catch (e) {
            if (process.env.TMC_PROXY_DEBUG) process.stderr.write('lldb variables error: ' + e + '\n');
        }
        this._toClient({
            type: 'response', request_seq: req.seq, success: true, command: 'variables',
            body: { variables },
        });
    }
}

module.exports = { TmcLldbProxy };
