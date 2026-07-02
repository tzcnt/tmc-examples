// VS Code extension: TooManyCooks coroutine debugging.
//
// Registers two debug types that run the STOCK debug adapters behind a thin DAP proxy which
// reconstructs / populates coroutine async frames and locals at the DAP layer:
//   * cppdbg-tmc   -> stock cpptools OpenDebugAD7 (GDB)   via TmcProxy
//   * lldb-dap-tmc -> stock lldb-dap (LLDB)               via TmcLldbProxy
// No patched debug adapter is required for either.
'use strict';
const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { TmcProxy } = require('./tmcProxy');
const { TmcLldbProxy } = require('./tmcLldbProxy');

const IS_WIN = process.platform === 'win32';

function cpptoolsAdapterPath() {
    const ext = vscode.extensions.getExtension('ms-vscode.cpptools');
    if (!ext) throw new Error('The "C/C++" extension (ms-vscode.cpptools) is required for cppdbg-tmc.');
    const p = path.join(ext.extensionPath, 'debugAdapters', 'bin', IS_WIN ? 'OpenDebugAD7.exe' : 'OpenDebugAD7');
    if (!fs.existsSync(p)) throw new Error('Could not locate cpptools OpenDebugAD7 at ' + p);
    return p;
}

function lldbDapPath(config) {
    if (config && config.lldbDapPath) return config.lldbDapPath;
    const configured = vscode.workspace.getConfiguration('lldb-dap').get('executable-path');
    if (configured) return configured;
    const ext = vscode.extensions.getExtension('llvm-vs-code-extensions.lldb-dap');
    if (ext) {
        const p = path.join(ext.extensionPath, 'bin', IS_WIN ? 'lldb-dap.exe' : 'lldb-dap');
        if (fs.existsSync(p)) return p;
    }
    return IS_WIN ? 'lldb-dap.exe' : 'lldb-dap'; // rely on PATH
}

// A vscode.DebugAdapter that wraps a proxy and runs inline in the extension host.
class InlineAdapter {
    constructor(proxyFactory) {
        this._emitter = new vscode.EventEmitter();
        this.onDidSendMessage = this._emitter.event;
        this._proxy = proxyFactory((msg) => this._emitter.fire(msg));
    }
    handleMessage(message) { this._proxy.handleClientMessage(message); }
    dispose() { this._proxy.dispose(); }
}

function activate(context) {
    const gdbScript = path.join(context.extensionPath, 'coro_backtrace_gdb.py');
    const lldbScript = path.join(context.extensionPath, 'coro_backtrace_lldb.py');

    // --- cppdbg-tmc (GDB) ---
    context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider('cppdbg-tmc', {
        resolveDebugConfiguration(_folder, config) {
            if (!config || !config.type) return config;
            config.MIMode = config.MIMode || 'gdb';
            const xarg = '-x "' + gdbScript + '"';
            config.miDebuggerArgs = config.miDebuggerArgs ? (config.miDebuggerArgs + ' ' + xarg) : xarg;
            config.setupCommands = Array.isArray(config.setupCommands) ? config.setupCommands : [];
            if (!config.setupCommands.some((c) => c && typeof c.text === 'string' && c.text.indexOf('enable-pretty-printing') >= 0)) {
                config.setupCommands.push({ text: '-enable-pretty-printing', ignoreFailures: true });
            }
            // Deliberately no -enable-frame-filters; the proxy reconstructs async frames.
            return config;
        }
    }));
    context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('cppdbg-tmc', {
        createDebugAdapterDescriptor() {
            const ad7 = cpptoolsAdapterPath();
            return new vscode.DebugAdapterInlineImplementation(
                new InlineAdapter((send) => new TmcProxy(send, ad7, [], {})));
        }
    }));

    // --- lldb-dap-tmc (LLDB) ---
    context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider('lldb-dap-tmc', {
        resolveDebugConfiguration(_folder, config) {
            if (!config || !config.type) return config;
            const importCmd = 'command script import "' + lldbScript + '"';
            config.preRunCommands = Array.isArray(config.preRunCommands) ? config.preRunCommands : [];
            if (!config.preRunCommands.some((c) => typeof c === 'string' && c.indexOf('coro_backtrace_lldb') >= 0)) {
                config.preRunCommands.push(importCmd);
            }
            return config;
        }
    }));
    context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('lldb-dap-tmc', {
        createDebugAdapterDescriptor(session) {
            const dap = lldbDapPath(session && session.configuration);
            return new vscode.DebugAdapterInlineImplementation(
                new InlineAdapter((send) => new TmcLldbProxy(send, dap, [], {})));
        }
    }));
}

function deactivate() { }

module.exports = { activate, deactivate };
