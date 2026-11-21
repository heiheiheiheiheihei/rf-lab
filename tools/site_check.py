#!/usr/bin/env python3
"""Static acceptance checks for the RF-LAB microsite (site/).

No browser, no third-party packages. Checks:
  - local asset/link existence and duplicate IDs
  - no external resources / fetches (the page must work offline)
  - required sections and interactive element IDs
  - module prices and preset budgets stay mutually consistent
  - image alt text, lang, viewport, reduced-motion support

Run: python3 tools/site_check.py
"""

from __future__ import annotations

import gzip
import pathlib
import re
import sys
from html.parser import HTMLParser

ROOT = pathlib.Path(__file__).resolve().parent.parent
SITE = ROOT / "site"
HTML = SITE / "index.html"
CSS = SITE / "style.css"
JS = SITE / "app.js"

BASE_PRICE = 79
# Mirror of site/app.js PRESETS. Keep in sync; this is the acceptance fixture.
PRESETS = {
    "scout": (100, ["ys"]),
    "assault": (299, ["ys", "cc1101", "nrf", "backplane", "antenna"]),
    "tactical": (599, ["ys", "cc1101", "nrf", "si4463", "lr1121", "backplane", "antenna", "case"]),
    "allin": (999, ["ys", "cc1101", "nrf", "si4463", "lr1121", "backplane", "antenna", "case", "dock"]),
}
REQUIRED_IDS = {
    "hero", "budget", "modules", "terminal", "uplink", "status",
    "budgetRange", "total", "buildCode", "presetName", "sheetList",
    "demoPanel", "demoViewport", "demoFrame", "demoLearn", "demoReset",
    "demoOpen", "feedBody", "helpDialog", "helpBtn", "copyBom",
}
DEMO = SITE / "assets" / "terminal.html"
DEMO_JS = SITE / "assets" / "terminal-demo.js"
# Canonical/OG URLs point at the deployed site; they are metadata, not fetched resources.
ALLOWED_EXTERNAL_HOSTS = {"github.com", "heiheiheiheiheihei.github.io"}


class SiteParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.ids: list[str] = []
        self.images: list[dict] = []
        self.links: list[dict] = []
        self.scripts: list[dict] = []
        self.labels: list[dict] = []
        self.modules: list[dict] = []
        self.inputs: list[dict] = []
        self.buttons: list[dict] = []
        self.html_attrs: dict = {}
        self._in_label = 0

    def handle_starttag(self, tag: str, attrs) -> None:
        a = dict(attrs)
        if tag == "html":
            self.html_attrs = a
        if "id" in a:
            self.ids.append(a["id"])
        if tag == "img":
            self.images.append(a)
        elif tag == "a":
            self.links.append(a)
        elif tag == "script":
            self.scripts.append(a)
        elif tag == "link":
            self.links.append(a)
        elif tag == "label":
            self.labels.append(a)
            self._in_label += 1
        elif tag == "input":
            self.inputs.append(a)
        elif tag == "button":
            self.buttons.append(a)

    def handle_endtag(self, tag: str) -> None:
        if tag == "label" and self._in_label:
            self._in_label -= 1


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def local_target(ref: str) -> pathlib.Path | None:
    if not ref or ref.startswith(("#", "mailto:", "tel:", "data:", "javascript:")):
        return None
    if ref.startswith(("http://", "https://", "//")):
        return None
    clean = ref.split("#", 1)[0].split("?", 1)[0]
    if not clean:
        return None
    return (SITE / clean).resolve()


def main() -> int:
    errors: list[str] = []
    warnings: list[str] = []
    for path in (HTML, CSS, JS):
        if not path.exists():
            print(f"MISSING {path.relative_to(ROOT)}")
            return 1

    html = read(HTML)
    css = read(CSS)
    js = read(JS)
    parser = SiteParser()
    parser.feed(html)

    # ids / required anchors
    dupes = sorted({x for x in parser.ids if parser.ids.count(x) > 1})
    if dupes:
        errors.append(f"duplicate ids: {dupes}")
    missing = sorted(REQUIRED_IDS - set(parser.ids))
    if missing:
        errors.append(f"missing required ids: {missing}")
    anchors = {x for x in parser.ids}
    for link in parser.links:
        href = link.get("href", "")
        if href.startswith("#") and len(href) > 1 and href[1:] not in anchors:
            errors.append(f"anchor target missing: {href}")

    # local files
    for tag in parser.images + parser.links + parser.scripts:
        ref = tag.get("src") or tag.get("href") or ""
        target = local_target(ref)
        if target is not None and not target.exists():
            errors.append(f"local file missing: {ref}")
        if re.match(r"^(?:https?:)?//", ref) and not any(h in ref for h in ALLOWED_EXTERNAL_HOSTS):
            errors.append(f"unexpected external resource: {ref}")

    # no network from CSS/JS
    if re.search(r"url\(\s*['\"]?https?://", css, re.I):
        errors.append("CSS contains an external url()")
    if re.search(r"@import", css, re.I):
        errors.append("CSS contains @import")
    if re.search(r"\b(?:fetch|XMLHttpRequest|WebSocket)\s*\(", js):
        errors.append("JS contains a network primitive (fetch/XHR/WebSocket)")

    # live terminal demo: real built page + local transport shim, no screenshots
    if not DEMO.exists():
        errors.append("missing site/assets/terminal.html (run tools/build_web.py)")
    else:
        demo_html = read(DEMO)
        if "terminal-demo.js" not in demo_html:
            errors.append("terminal.html is missing the terminal-demo.js shim")
        if len(demo_html) > 120_000:
            warnings.append(f"terminal.html is {len(demo_html):,} B")
    if not DEMO_JS.exists():
        errors.append("missing site/assets/terminal-demo.js")
    if "assets/terminal.html" not in html:
        errors.append("terminal section does not embed assets/terminal.html")
    if "terminal-web.webp" in html:
        errors.append("terminal section still references a screenshot")

    # image alt text
    for image in parser.images:
        if not image.get("alt", "").strip():
            errors.append(f"image without alt: {image.get('src', '?')}")

    # document basics
    if parser.html_attrs.get("lang") != "zh-CN":
        errors.append("html lang must be zh-CN")
    if "width=device-width" not in html:
        errors.append("missing responsive viewport meta")
    if "prefers-reduced-motion" not in css:
        errors.append("CSS missing prefers-reduced-motion handling")

    # module cards + prices
    modules: list[dict] = []
    for label in parser.labels:
        classes = label.get("class", "").split()
        if "mod-card" in classes:
            mid = label.get("data-id", "")
            price = label.get("data-price", "")
            if not mid or not price.isdigit():
                errors.append(f"bad module card: {label}")
                continue
            modules.append({"id": mid, "price": int(price)})
    if len(modules) != 9:
        errors.append(f"expected 9 module cards, got {len(modules)}")
    if len({m['id'] for m in modules}) != len(modules):
        errors.append("duplicate module data-id")
    prices = {m["id"]: m["price"] for m in modules}
    all_ids = [m["id"] for m in modules]
    base_match = re.search(r'class="base-side"><b>¥(\d+)</b>', html)
    if not base_match or int(base_match.group(1)) != BASE_PRICE:
        errors.append(f"base price must be ¥{BASE_PRICE}")
    for key, (budget, ids) in PRESETS.items():
        missing_ids = sorted(set(ids) - set(prices))
        if missing_ids:
            errors.append(f"preset {key} references missing ids: {missing_ids}")
            continue
        total = BASE_PRICE + sum(prices[i] for i in ids)
        if total > budget:
            errors.append(f"preset {key} over budget: ¥{total} > ¥{budget}")
        if budget - total > 30:
            warnings.append(f"preset {key} leaves ¥{budget - total} unused (check bundle copy)")
    all_in = BASE_PRICE + sum(prices.values())
    if not (900 <= all_in <= 999):
        errors.append(f"all-in total should sit inside the ¥999 envelope, got ¥{all_in}")

    # selected/opened defaults in markup
    checked = [i for i in parser.inputs if i.get("type") == "checkbox" and "checked" in i]
    if len(checked) != 1 or checked[0].get("data-id") != "ys":
        errors.append("exactly one checked module (ys) is expected in static HTML")
    if 'aria-pressed="true"' not in html:
        errors.append("no preset has aria-pressed=true")
    preset_buttons = [b for b in parser.buttons if "preset" in b.get("class", "").split()]
    if len(preset_buttons) != len(PRESETS):
        errors.append(f"expected {len(PRESETS)} preset buttons, got {len(preset_buttons)}")
    if {b.get("data-preset") for b in preset_buttons} != set(PRESETS):
        errors.append("preset buttons do not match the PRESETS fixture")
    if {b.get("data-budget") for b in preset_buttons} != {str(v[0]) for v in PRESETS.values()}:
        errors.append("preset data-budget does not match the PRESETS fixture")

    # visible defaults + social image
    if not re.search(r'id="total"[^>]*>100<', html):
        errors.append("static total must start at ¥100 (scout)")
    if "SCOUT-100" not in html:
        errors.append("static build code must start as SCOUT-100")
    og = re.search(r'<meta property="og:image" content="([^"]+)"', html)
    if og:
        target = local_target(og.group(1))
        if target is not None and not target.exists():
            errors.append(f"og:image missing: {og.group(1)}")

    # informational sizes
    gz = len(gzip.compress(html.encode("utf-8"), 9, mtime=0))
    print(f"site        {HTML.relative_to(ROOT)}")
    print(f"ids         {len(parser.ids)} unique / {len(parser.ids)} total")
    print(f"images      {len(parser.images)} (local, alt present)")
    print(f"modules     {len(modules)}  base ¥{BASE_PRICE}  all-in ¥{all_in}")
    print(f"presets     " + "  ".join(f"{k}=¥{v[0]}" for k, v in PRESETS.items()))
    print(f"html        {len(html):,} B  gzip {gz:,} B")
    print(f"css / js    {len(css):,} B / {len(js):,} B")
    if DEMO.exists():
        print(f"demo        {len(read(DEMO)):,} B html + {DEMO_JS.stat().st_size:,} B shim")

    if warnings:
        for warning in warnings:
            print(f"WARN  {warning}")
    if errors:
        for error in errors:
            print(f"FAIL  {error}")
        return 1
    print("SITE OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
