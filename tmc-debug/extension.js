// VS Code extension: TooManyCooks coroutine debugging.
//
// Registers a `cppdbg-tmc` debug type that runs the STOCK cpptools OpenDebugAD7 behind a
// thin DAP proxy (tmcProxy.js). The proxy reconstructs coroutine async frames and their
// locals at the DAP layer, so no patched debug adapter and no `-enable-frame-filters`
// (which stock MIEngine cannot consume) are required.
'use strict';
const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { TmcProxy } = require('./tmcProxy');

function cpptoolsAdapterPath() {
    const ext = vscode.extensions.getExtension('ms-vscode.cpptools');
    if (!ext) {
        throw new Error('The "C/C++" extension (ms-vscode.cpptools) is required for cppdbg-tmc.');
    }
    const exe = process.platform === 'win32' ? 'OpenDebugAD7.exe' : 'OpenDebugAD7';
    const p = path.join(ext.extensionPath, 'debugAdapters', 'bin', exe);
    if (!fs.existsSync(p)) {
        throw new Error('Could not locate cpptools OpenDebugAD7 at ' + p);
    }
    return p;
}

// A vscode.DebugAdapter that wraps TmcProxy and runs inline in the extension host.
class InlineTmcAdapter {
    constructor(ad7Path) {
        this._emitter = new vscode.EventEmitter();
        this.onDidSendMessage = this._emitter.event;
        this._proxy = new TmcProxy((msg) => this._emitter.fire(msg), ad7Path, [], {});
    }
    handleMessage(message) {
        this._proxy.handleClientMessage(message);
    }
    dispose() {
        try { this._proxy._child.kill(); } catch (e) { /* ignore */ }
    }
}

function activate(context) {
    const gdbScript = path.join(context.extensionPath, 'coro_backtrace_gdb.py');

    // Inject the gdb coroutine script (WITHOUT frame filters) and pretty-printing into
    // any cppdbg-tmc launch, so users don't have to hand-edit launch.json.
    const configProvider = {
        resolveDebugConfiguration(_folder, config) {
            if (!config || !config.type) {
                return config; // Empty config: let VS Code surface its own error.
            }
            config.MIMode = config.MIMode || 'gdb';
            const xarg = '-x "' + gdbScript + '"';
            config.miDebuggerArgs = config.miDebuggerArgs ? (config.miDebuggerArgs + ' ' + xarg) : xarg;
            config.setupCommands = Array.isArray(config.setupCommands) ? config.setupCommands : [];
            const hasPretty = config.setupCommands.some(
                (c) => c && typeof c.text === 'string' && c.text.indexOf('enable-pretty-printing') >= 0);
            if (!hasPretty) {
                config.setupCommands.push({ text: '-enable-pretty-printing', ignoreFailures: true });
            }
            // NOTE: deliberately do NOT add -enable-frame-filters; the proxy handles async frames.
            return config;
        }
    };
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider('cppdbg-tmc', configProvider));

    const factory = {
        createDebugAdapterDescriptor(_session) {
            return new vscode.DebugAdapterInlineImplementation(new InlineTmcAdapter(cpptoolsAdapterPath()));
        }
    };
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory('cppdbg-tmc', factory));
}

function deactivate() { }

module.exports = { activate, deactivate };
