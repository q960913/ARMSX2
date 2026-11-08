import { hapTasks } from '@ohos/hvigor-ohos-plugin';
import * as fs from 'fs';
import * as path from 'path';
import * as crypto from 'crypto';

// Runtime resources (GameDB, shaders, fonts, icons) have one canonical copy, at
// <repo>/bin/resources, shared with every other ARMSX2 build. Rather than commit a
// copy here, mirror it into rawfile/resources on every build, like the Android
// build does for its assets: minus the Windows-only DX11 shaders and the desktop
// emoji font, plus the mobile GameDB overlay. The stamp file tells the app
// (ohos_resources.cpp) whether the copy it unpacked last time is still current.
const repoRoot = path.resolve(__dirname, '..', '..', '..');
const srcDir = path.join(repoRoot, 'bin', 'resources');
const overlay = path.join(repoRoot, 'bin', 'resources-overlay', 'armsx2_overrides.yaml');
const destDir = path.join(__dirname, 'src', 'main', 'resources', 'rawfile', 'resources');
const STAMP = '.armsx2-resources';

function excluded(rel: string): boolean {
  const p = rel.split(path.sep).join('/');
  return p.startsWith('shaders/dx11/') || p.startsWith('fonts/Twemoji');
}

function listFiles(dir: string, base: string = dir, out: string[] = []): string[] {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      listFiles(full, base, out);
    } else if (!excluded(path.relative(base, full))) {
      out.push(full);
    }
  }
  return out;
}

function syncResources(): void {
  if (!fs.existsSync(srcDir)) {
    throw new Error(`ARMSX2: ${srcDir} is missing; build from a full checkout of the repository.`);
  }

  const files = listFiles(srcDir).sort();
  const hash = crypto.createHash('sha256');
  for (const f of files.concat(fs.existsSync(overlay) ? [overlay] : [])) {
    hash.update(path.relative(repoRoot, f));
    hash.update(fs.readFileSync(f));
  }
  const stamp = hash.digest('hex');

  const stampPath = path.join(destDir, STAMP);
  if (fs.existsSync(stampPath) && fs.readFileSync(stampPath, 'utf8') === stamp) {
    return;
  }

  fs.rmSync(destDir, { recursive: true, force: true });
  for (const f of files) {
    const target = path.join(destDir, path.relative(srcDir, f));
    fs.mkdirSync(path.dirname(target), { recursive: true });
    fs.copyFileSync(f, target);
  }
  if (fs.existsSync(overlay)) {
    fs.copyFileSync(overlay, path.join(destDir, path.basename(overlay)));
  }
  fs.writeFileSync(stampPath, stamp);
}

syncResources();

export default {
  system: hapTasks,
  plugins: []
}
