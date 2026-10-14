// Keep the original audit entry point as an actual regression check.
const path = require('path');
const { spawnSync } = require('child_process');
const result = spawnSync(process.execPath, [path.join(__dirname, 'regression-ui.cjs')], { stdio: 'inherit' });
process.exit(result.status ?? 1);
