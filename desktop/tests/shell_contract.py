"""Black-box UI contract; the binary exercises Qt controls in --smoke-test."""
import subprocess
import sys
result = subprocess.run([sys.argv[1], '--smoke-test'], capture_output=True, text=True)
print(result.stdout + result.stderr)
assert result.returncode == 0
assert 'launcher-to-workspace: PASS' in result.stdout + result.stderr, 'Missing functional project launcher'
assert 'workspace-navigation: PASS' in result.stdout + result.stderr, 'Missing functional workspace tabs'
assert 'close-project: PASS' in result.stdout + result.stderr, 'Missing return to launcher'
