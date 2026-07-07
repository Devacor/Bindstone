"use strict";
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.activate = activate;
exports.deactivate = deactivate;
const vscode = __importStar(require("vscode"));
// JaiScript uses the attach model: there is NO bundled debug adapter process.
// The Debug Adapter Protocol (DAP) server lives INSIDE the C++ host — jai::debug_connector
// opens a raw TCP port (engine->set_debug_connector(make_shared<jai::debug_connector>(port)))
// and speaks DAP directly. So all this extension does is tell VS Code to connect its DAP
// client straight to that in-process socket via a DebugAdapterServer descriptor.
// No adapter to spawn, no stdio pipe — just host + port from the launch.json "attach" config.
function activate(context) {
    const factory = {
        createDebugAdapterDescriptor(session) {
            const cfg = session.configuration;
            const port = cfg.port ?? 1234;
            const host = cfg.host ?? '127.0.0.1';
            // Connect directly to the app's in-process DAP TCP server.
            return new vscode.DebugAdapterServer(port, host);
        }
    };
    context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('jaiscript', factory));
}
function deactivate() {
    // Nothing to tear down — the socket connection is owned by VS Code's DAP client.
}
//# sourceMappingURL=extension.js.map