import { spawn, execSync, ChildProcess } from 'child_process';
import path from 'path';
import fs from 'fs';

const PORT = 3000;

let cServerProcess: ChildProcess | null = null;
let isShuttingDown = false;

function ensureBinary(): string {
  const binaryPath = path.join(process.cwd(), 'bin/server');
  if (!fs.existsSync(binaryPath)) {
    console.log('[Supervisor] Compiling C server with make...');
    try {
      execSync('make', { stdio: 'inherit' });
    } catch (err) {
      console.warn('[Supervisor] Note: make command not present or failed:', err);
    }
  }
  try {
    fs.chmodSync(binaryPath, 0o755);
  } catch (err) {
    console.error('[Supervisor] chmod error:', err);
  }
  return binaryPath;
}

function startCServer() {
  if (isShuttingDown) return;
  const binaryPath = ensureBinary();
  console.log(`[Supervisor] Spawning native POSIX C Linux Server on port ${PORT}...`);
  cServerProcess = spawn(binaryPath, [String(PORT)], {
    stdio: 'inherit',
    cwd: process.cwd(),
  });

  cServerProcess.on('error', (err) => {
    console.error('[Supervisor] Failed to spawn C server:', err);
  });

  cServerProcess.on('exit', (code, signal) => {
    console.log(`[Supervisor] C Server process exited with code ${code}, signal ${signal}`);
    if (!isShuttingDown) {
      console.log('[Supervisor] Respawning C server in 300ms...');
      setTimeout(startCServer, 300);
    }
  });
}

function shutdown() {
  isShuttingDown = true;
  if (cServerProcess && !cServerProcess.killed) {
    console.log('[Supervisor] Forwarding SIGTERM to C Server...');
    cServerProcess.kill('SIGTERM');
  }
  process.exit(0);
}

process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
process.on('SIGHUP', () => {
  if (cServerProcess && !cServerProcess.killed) {
    console.log('[Supervisor] Forwarding SIGHUP (config reload) to C Server...');
    cServerProcess.kill('SIGHUP');
  }
});

startCServer();
