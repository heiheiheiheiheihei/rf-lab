# Regenerate the embedded Web UI before CMake configures the component.
# Single source: web/index.html + web/app.css + web/app.js -> tools/build_web.py.
Import("env")

import pathlib
import subprocess
import sys

project = pathlib.Path(env.subst("$PROJECT_DIR")).resolve()
root = project.parent
script = root / "tools" / "build_web.py"

print(f"[web] building embedded UI from {script}")
subprocess.check_call([sys.executable, str(script), "--quiet"], cwd=str(root))
