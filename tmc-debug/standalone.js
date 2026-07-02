// Standalone stdio wrapper around TmcProxy, for headless testing with a DAP driver.
// The real OpenDebugAD7 path comes from $AD7_REAL.
'use strict';
const { TmcProxy } = require('./tmcProxy');

const ad7 = process.env.AD7_REAL;
if (!ad7) { process.stderr.write('AD7_REAL not set\n'); process.exit(2); }

function sendToClient(msg) {
    const json = JSON.stringify(msg);
    process.stdout.write(`Content-Length: ${Buffer.byteLength(json, 'utf8')}\r\n\r\n${json}`);
}

const proxy = new TmcProxy(sendToClient, ad7, [], { stdio: ['pipe', 'pipe', 'pipe'] });
proxy._onExit = (code) => process.exit(code || 0);

// Parse Content-Length framed DAP from our stdin (the client) into the proxy.
let buf = Buffer.alloc(0);
process.stdin.on('data', (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
        const he = buf.indexOf('\r\n\r\n');
        if (he < 0) return;
        const m = /Content-Length:\s*(\d+)/i.exec(buf.slice(0, he).toString('utf8'));
        if (!m) { buf = buf.slice(he + 4); continue; }
        const len = parseInt(m[1], 10);
        const start = he + 4;
        if (buf.length < start + len) return;
        const body = buf.slice(start, start + len).toString('utf8');
        buf = buf.slice(start + len);
        try { proxy.handleClientMessage(JSON.parse(body)); } catch (e) {}
    }
});
process.stdin.on('end', () => process.exit(0));
