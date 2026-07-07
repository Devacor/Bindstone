import * as vscode from 'vscode';

// JaiScript uses the attach model: there is NO bundled debug adapter process.
// The Debug Adapter Protocol (DAP) server lives INSIDE the C++ host — jai::debug_connector
// opens a raw TCP port (engine->set_debug_connector(make_shared<jai::debug_connector>(port)))
// and speaks DAP directly. So all this extension does is tell VS Code to connect its DAP
// client straight to that in-process socket via a DebugAdapterServer descriptor.
// No adapter to spawn, no stdio pipe — just host + port from the launch.json "attach" config.

export function activate(context: vscode.ExtensionContext): void {
	const factory: vscode.DebugAdapterDescriptorFactory = {
		createDebugAdapterDescriptor(
			session: vscode.DebugSession
		): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
			const cfg = session.configuration;
			const port: number = cfg.port ?? 1234;
			const host: string = cfg.host ?? '127.0.0.1';
			// Connect directly to the app's in-process DAP TCP server.
			return new vscode.DebugAdapterServer(port, host);
		}
	};

	context.subscriptions.push(
		vscode.debug.registerDebugAdapterDescriptorFactory('jaiscript', factory)
	);
}

export function deactivate(): void {
	// Nothing to tear down — the socket connection is owned by VS Code's DAP client.
}
