#!/usr/bin/env python3
"""Build the single-file RF-Lab Web UI.

Sources:
  web/index.html   page shell with /*__CSS__*/ and /*__JS__*/ placeholders
  web/app.css      tactical skin
  web/app.js       HTTP/SSE + BLE transport

Outputs:
  web/dist/index.html                  human-readable build (debug / screenshot)
  firmware/src/web/index.html.gz       embedded into the firmware (gzip, mtime=0)
  android/app/src/main/assets/index.html  bundled into the APK (same bytes)

Run automatically by firmware/scripts/embed_web.py before a PlatformIO build.
`--check` verifies the generated artifacts match the sources without writing.
"""

from __future__ import annotations

import argparse
import gzip
import hashlib
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HTML = ROOT / "web" / "index.html"
CSS = ROOT / "web" / "app.css"
JS = ROOT / "web" / "app.js"
DIST = ROOT / "web" / "dist" / "index.html"
FW_GZ = ROOT / "firmware" / "src" / "web" / "index.html.gz"
ANDROID = ROOT / "android" / "app" / "src" / "main" / "assets" / "index.html"

# Only these absolute URLs are allowed in the generated page. They are XML
# namespaces (data: favicon), not network requests.
ALLOWED_URL_PREFIXES = ("http://www.w3.org/", "data:image/svg+xml")


def build_html() -> bytes:
    html = HTML.read_text(encoding="utf-8")
    css = CSS.read_text(encoding="utf-8")
    js = JS.read_text(encoding="utf-8")

    if "/*__CSS__*/" not in html or "/*__JS__*/" not in html:
        raise SystemExit("web/index.html is missing /*__CSS__*/ or /*__JS__*/")
    if "</script" in js.lower():
        # An inline script must not terminate early. Escaping the slash is
        # valid JS inside strings and regexes; our source has no such literal.
        js = re.sub(r"</script", r"<\\/script", js, flags=re.IGNORECASE)

    html = html.replace("/*__CSS__*/", css)
    html = html.replace("/*__JS__*/", js)

    external = []
    for match in re.finditer(r"(?:href|src|url)\s*=\s*[\"']([^\"']+)", html):
        url = match.group(1)
        if url.startswith(("http://", "https://")) and not url.startswith(ALLOWED_URL_PREFIXES):
            external.append(url)
    if external:
        raise SystemExit("external resource(s) not allowed: " + ", ".join(sorted(set(external))))

    return html.encode("utf-8")


def gz(data: bytes) -> bytes:
    # mtime=0 keeps the embedded asset reproducible.
    return gzip.compress(data, compresslevel=9, mtime=0)


def write_or_check(path: pathlib.Path, data: bytes, check: bool) -> bool:
    if check:
        if not path.exists():
            print(f"MISSING {path.relative_to(ROOT)}")
            return False
        if path.read_bytes() != data:
            print(f"STALE   {path.relative_to(ROOT)}")
            return False
        return True
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="verify generated files only")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    html = build_html()
    packed = gz(html)

    ok = True
    ok &= write_or_check(DIST, html, args.check)
    ok &= write_or_check(FW_GZ, packed, args.check)
    # Android may not be checked out; only verify if the file already exists.
    if ANDROID.exists() or not args.check:
        ok &= write_or_check(ANDROID, html, args.check)

    if not args.quiet:
        print(f"html   {len(html):7d} B  sha256 {hashlib.sha256(html).hexdigest()[:16]}")
        print(f"gzip   {len(packed):7d} B  sha256 {hashlib.sha256(packed).hexdigest()[:16]}")
        print(f"ratio  {100.0 * len(packed) / len(html):5.1f}%")
    if args.check and not ok:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
