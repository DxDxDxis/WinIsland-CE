const fs = require('node:fs');
const path = require('node:path');
(async () => {
  const { packager } = await import('@electron/packager');
  const staging = path.resolve('../build-electron/settings-app');
  fs.mkdirSync(staging, { recursive: true });
  fs.cpSync('dist', path.join(staging, 'dist'), { recursive: true });
  const source = JSON.parse(fs.readFileSync('package.json', 'utf8'));
  const cache = path.resolve('../../dependency-cache/electron'); fs.mkdirSync(cache, { recursive: true });
  const zip = path.join(cache, 'electron-v' + source.devDependencies.electron + '-win32-x64.zip');
  if (!fs.existsSync(zip)) {
    const { downloadArtifact } = await import('@electron/get');
    const downloaded = await downloadArtifact({ version: source.devDependencies.electron, artifactName: 'electron', platform: 'win32', arch: 'x64', cacheRoot: cache,
      checksums: require('electron/checksums.json') });
    if(downloaded !== zip) fs.copyFileSync(downloaded, zip);
  }
  fs.writeFileSync(path.join(staging, 'package.json'), JSON.stringify({ name: 'winisland-settings', author: 'WinIsland Community', productName: 'WinIslandSettings', version: '1.2.7-alpha', main: 'dist/main.js' }));
  const outputs = await packager({ dir: staging, name: 'WinIslandSettings', executableName: 'WinIslandSettings', platform: 'win32', arch: 'x64',
    electronVersion: source.devDependencies.electron, out: path.resolve('../build-electron/packaged'), overwrite: true, asar: true, prune: true,
    electronZipDir: path.resolve('../../dependency-cache/electron'), tmpdir: path.resolve('../build-electron/tmp'), appVersion: '1.2.7-alpha', buildVersion: '1.2.7.0', download: { cacheRoot: path.resolve('../../dependency-cache/electron') } });
  const release = path.resolve('../release/settings'); fs.mkdirSync(release, { recursive: true });
  fs.cpSync(outputs[0], release, { recursive: true });
  console.log('Electron release: ' + release);
})().catch(e => { console.error(e); process.exitCode = 1; });


