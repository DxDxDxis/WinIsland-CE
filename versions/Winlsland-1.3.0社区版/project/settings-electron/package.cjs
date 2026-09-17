const fs = require('node:fs');
const path = require('node:path');
process.chdir(__dirname);
(async () => {
  const { packager } = await import('@electron/packager');
  const staging = path.resolve('../build-electron/settings-app');
  fs.mkdirSync(staging, { recursive: true });
  fs.cpSync('dist', path.join(staging, 'dist'), { recursive: true });
  const source = JSON.parse(fs.readFileSync('package.json', 'utf8'));
  const cache = path.resolve('../dependency-cache/electron'); fs.mkdirSync(cache, { recursive: true });
  const zip = path.join(cache, 'electron-v' + source.devDependencies.electron + '-win32-x64.zip');
  if (!fs.existsSync(zip)) {
    require('node:child_process').execFileSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', path.resolve('prepare-runtime.ps1'), '-Version', source.devDependencies.electron], {stdio:'inherit', windowsHide:true});
  }
  fs.writeFileSync(path.join(staging, 'package.json'), JSON.stringify({ name: 'winisland-settings', author: 'WinIsland Community', productName: 'WinIslandSettings', version: '1.3.0', main: 'dist/main.js' }));
  const outputs = await packager({ dir: staging, name: 'WinIslandSettings', executableName: 'WinIslandSettings', platform: 'win32', arch: 'x64',
    electronVersion: source.devDependencies.electron, out: path.resolve('../build-electron/packaged'), overwrite: true, asar: true, prune: true,
    electronZipDir: path.resolve('../dependency-cache/electron'), tmpdir: path.resolve('../build-electron/tmp'), appVersion: '1.3.0', buildVersion: '1.3.0.0', win32metadata: {ProductName:'WinIsland 1.3.0 社区版',FileDescription:'WinIsland 1.3.0 社区版设置'}, download: { cacheRoot: path.resolve('../dependency-cache/electron') } });
  const release = path.resolve('../release/settings'); fs.mkdirSync(release, { recursive: true });
  fs.cpSync(outputs[0], release, { recursive: true });
  console.log('Electron release: ' + release);
})().catch(e => { console.error(e); process.exitCode = 1; });


