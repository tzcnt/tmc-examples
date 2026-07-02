// Standalone stdio wrapper around a proxy, for headless testing with a DAP driver.
// Set $AD7_REAL for the GDB/cppdbg proxy, or $LLDB_DAP for the LLDB proxy.
'use strict';

function sendToClient(msg) {
    const json = JSON.stringify(msg);
    process.stdout.write(`Content-Length: ${Buffer.byteLength(json, 'utf8')}\r\n\r\n${json}`);
}

const opts = { stdio: ['pipe', 'pipe', 'pipe'] };
let proxy;
if (process.env.LLDB_DAP) {
    const { TmcLldbProxy } = require('./tmcLldbProxy');
    proxy = new TmcLldbProxy(sendToClient, process.env.LLDB_DAP, [], opts);
} else if (process.env.AD7_REAL) {
    const { TmcProxy } = require('./tmcProxy');
    proxy = new TmcProxy(sendToClient, process.env.AD7_REAL, [], opts);
} else {
    process.stderr.write('Set AD7_REAL (GDB) or LLDB_DAP (LLDB)\n'); process.exit(2);
}
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
