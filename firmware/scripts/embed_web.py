# Regenerate the embedded Web UI before CMake configures the component.
# Single source: web/index.html + web/app.css + web/app.js -> tools/build_web.py.
#
# Also injects the build fingerprint used by /api/hello, SSE, BLE hello and
# the footer: fw version + short git SHA (+dirty), and the page build id that
# tools/build_web.py writes into <meta name="rflab-ui">.
Import("env")

import pathlib
import re
import subprocess
import sys

project = pathlib.Path(env.subst("$PROJECT_DIR")).resolve()
root = project.parent
script = root / "tools" / "build_web.py"
wiring = root / "tools" / "gen_wiring.py"

# Fail the build if the JSON wiring source, main.c and generated header drift.
subprocess.check_call([sys.executable, str(wiring), "--check"], cwd=str(root))

print(f"[web] building embedded UI from {script}")
subprocess.check_call([sys.executable, str(script), "--quiet"], cwd=str(root))


def git(*args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", *args], cwd=str(root), text=True, stderr=subprocess.DEVNULL
        ).strip()
    except Exception:
        return ""


sha = git("rev-parse", "--short=7", "HEAD") or "nogit"
dirty = git("status", "--porcelain", "--", "web", "firmware/src", "tools/build_web.py")
build_id = sha + ("+dirty" if dirty else "")

page = (root / "web" / "dist" / "index.html").read_text(encoding="utf-8")
m = re.search(r'name="rflab-ui" content="([0-9a-f]{8})"', page)
if not m:
    raise SystemExit("[web] built page has no rflab-ui meta")
ui_hash = m.group(1)

env.Append(BUILD_FLAGS=[
    f'-DRFLAB_BUILD_ID=\\"{build_id}\\"',
    f'-DRFLAB_UI_HASH=\\"{ui_hash}\\"',
])
print(f"[web] ui={ui_hash} build={build_id}")
