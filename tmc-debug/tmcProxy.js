// GDB / cppdbg proxy for TooManyCooks coroutine debugging.
//
// Runs a stock cpptools `OpenDebugAD7` child and, for suspended-coroutine stacks,
// reconstructs the async call frames and their locals entirely at the DAP layer - so no
// MIEngine patch and no `-enable-frame-filters` (which stock MIEngine cannot consume) are
// needed. Normal debugging is passed through untouched.
//
//   * stackTrace: build the full augmented frame list once per stop (real frames with the
//     async continuation frames spliced in after the coroutine frame, via
//     `$__tmc_async_chain()`), cache it, and serve every page as a slice - correct under
//     stack paging and nested (non-coroutine top frame) stops.
//   * scopes/variables for the synthetic frames: synthesized via `$__coro_local_names(addr)`
//     and `(*$__coro_frame_at(addr)).<name>`.

'use strict';
const path = require('path');
const { DapProxyBase } = require('./dapProxyBase');

const SYN_FRAME_BASE = 0x40000000;
const SYN_VARREF_BASE = 0x50000000;
const MAX_FULL_FRAMES = 4096;

function b64json(resultString) {
    // The evaluate result for a char[] value is rendered with surrounding quotes and
    // possibly C escapes. Extract the base64 payload and decode it.
    if (!resultString) return null;
    let s = String(resultString).trim();
    const first = s.indexOf('"');
    const last = s.lastIndexOf('"');
    if (first >= 0 && last > first) s = s.substring(first + 1, last);
    s = s.replace(/\\(.)/g, '$1');
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
    return '0x' + BigInt(addr).toString(16);
}

class TmcProxy extends DapProxyBase {
    constructor(sendToClient, opendebugPath, opendebugArgs, spawnOptions) {
        super(sendToClient, opendebugPath, opendebugArgs, spawnOptions);
        this._stackCache = new Map();     // threadId -> augmented frame array (per stop)
        this._synFrame = new Map();       // synFrameId -> {threadId, coroAddr, evalFrameId}
        this._synVarRef = new Map();      // synVarRef -> {coroAddr, evalFrameId}
        this._nextSynFrame = SYN_FRAME_BASE;
        this._nextSynVarRef = SYN_VARREF_BASE;
    }

    onStopOrContinue() {
        this._synFrame.clear();
        this._synVarRef.clear();
        this._stackCache.clear();
    }

    onClientRequest(req) {
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
        this.forward(req);
    }

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
            this.forward(req);
        }
    }

    async _buildAugmentedStack(threadId, req) {
        const probe = await this._inject('stackTrace', { threadId, startFrame: 0, levels: 1 });
        const probeFrames = (probe && probe.body && probe.body.stackFrames) || [];
        if (!probe || !probe.success || probeFrames.length === 0) {
            this.forward(req);
            return null;
        }
        const chainBody = await this._evaluate('(char*)$__tmc_async_chain()', probeFrames[0].id);
        const chain = chainBody && b64json(chainBody.result);
        if (!chain || !chain.frames || chain.frames.length === 0) {
            this.forward(req); // not a coroutine stack - stay lazy
            return null;
        }
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
}

module.exports = { TmcProxy };
