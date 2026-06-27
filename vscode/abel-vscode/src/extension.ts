import * as vscode from 'vscode';
import {
    LanguageClient,
    LanguageClientOptions,
    ServerOptions,
    TransportKind
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

export function activate(context: vscode.ExtensionContext) {
    const config = vscode.workspace.getConfiguration('abel');
    const command = config.get<string>('lsp.path') || 'abel-lsp';

    const serverOptions: ServerOptions = {
        command,
        transport: TransportKind.stdio
    };

    const clientOptions: LanguageClientOptions = {
        documentSelector: [{ scheme: 'file', language: 'abel' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.abel')
        }
    };

    client = new LanguageClient('abel-lsp', 'Abel Language Server', serverOptions, clientOptions);
    context.subscriptions.push(client.start());
}

export function deactivate(): Thenable<void> | undefined {
    if (!client) {
        return undefined;
    }
    return client.stop();
}
