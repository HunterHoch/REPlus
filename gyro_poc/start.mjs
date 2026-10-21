import { spawn } from 'node:child_process';
import { createApp } from './server.mjs';

const app = createApp();
await new Promise((resolve, reject) => {
  app.server.once('error', reject);
  app.server.listen(3210, '127.0.0.1', resolve);
});
console.log('Starting HTTPS connection for the phone…');
const tunnel = process.platform === 'win32'
  ? spawn('powershell.exe', ['-NoProfile', '-Command', 'cf tunnels quick-start http://127.0.0.1:3210'], { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] })
  : spawn('cf', ['tunnels', 'quick-start', 'http://127.0.0.1:3210'], { stdio: ['ignore', 'pipe', 'pipe'] });
let output = '', ready = false;
function receive(chunk) {
  output = (output + chunk.toString()).slice(-16000);
  const match = output.match(/https:\/\/[a-z0-9-]+\.trycloudflare\.com/);
  if (match && !ready) {
    ready = true;
    app.setPublicOrigin(match[0]);
    console.log(`\nOpen on this PC: http://localhost:3210\nPhone connection: ${match[0]}\nScan the QR on the PC page. Keep this terminal running.\n`);
  }
}
tunnel.stdout.on('data', receive);
tunnel.stderr.on('data', receive);
tunnel.on('error', error => { console.error(error.message); shutdown(1); });
tunnel.on('exit', code => { if (!stopping) { console.error('Tunnel stopped.', output.slice(-2000)); shutdown(code || 1); } });
let stopping = false;
async function shutdown(code = 0) {
  if (stopping) return;
  stopping = true;
  if (process.platform === 'win32' && tunnel.pid) spawn('taskkill', ['/PID', String(tunnel.pid), '/T', '/F'], { windowsHide: true, stdio: 'ignore' });
  else tunnel.kill();
  await app.close();
  process.exit(code);
}
process.on('SIGINT', () => shutdown());
process.on('SIGTERM', () => shutdown());
