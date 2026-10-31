#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the built site (tools/make_pages.py): every href, src and poster in every page resolves to a file of the
site, and every #anchor to an id on the page it points at; links into Polly's GitHub repository name files that
exist in the repository; every inline script parses (node --check, when there is node: a placeholder left unfilled
stops a whole page). Other sites are not fetched.

    python3 tools/check_pages.py build/pages
"""
import html
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from urllib.parse import unquote, urlsplit

SRC = Path(__file__).resolve().parents[1]
REPO = "https://github.com/charlesvestal/fm1-polly/"
ATTR = re.compile(r"""\b(href|src|poster)\s*=\s*("([^"]*)"|'([^']*)')""", re.I)


def ids_of(path, cache={}):
    if path not in cache:
        t = path.read_text(encoding="utf-8", errors="replace")
        cache[path] = set(re.findall(r"""\b(?:id|name)\s*=\s*["']([^"']+)["']""", t))
    return cache[path]


def strip_scripts(t):
    """the page without its scripts' code (their strings are not links), but with the script tags' src"""
    return re.sub(r"(<script\b[^>]*>)[\s\S]*?</script>", r"\1</script>", t, flags=re.I)


def script_errors(page, root):
    """the page's inline scripts that don't parse (node --check, modules as modules), or [] without node"""
    node = shutil.which("node")
    if not node:
        return []
    out = []
    t = page.read_text(encoding="utf-8", errors="replace")
    for i, m in enumerate(re.finditer(r"<script\b([^>]*)>([\s\S]*?)</script>", t, re.I)):
        attrs, code = m.group(1), m.group(2)
        if re.search(r"\bsrc\s*=", attrs) or not code.strip() or \
                re.search(r"""type\s*=\s*["'](?!module|text/javascript)""", attrs):
            continue
        ext = ".mjs" if re.search(r"""type\s*=\s*["']module""", attrs) else ".cjs"
        with tempfile.NamedTemporaryFile("w", suffix=ext, delete=False, encoding="utf-8") as f:
            f.write(code)
        r = subprocess.run([node, "--check", f.name], capture_output=True, text=True)
        Path(f.name).unlink()
        if r.returncode:
            msg = next((l for l in r.stderr.splitlines() if "Error" in l), r.stderr.strip()[:200])
            out.append(f"{page.relative_to(root)}: script {i + 1} doesn't parse: {msg}")
    return out


def main(root):
    root = Path(root).resolve()
    pages = sorted(root.rglob("*.html"))
    bad, n, outside = [], 0, set()
    for page in pages:
        text = strip_scripts(page.read_text(encoding="utf-8", errors="replace"))
        for m in ATTR.finditer(text):
            url = html.unescape(m.group(3) if m.group(3) is not None else m.group(4))
            n += 1
            where = f"{page.relative_to(root)}: {m.group(1)}={url!r}"
            if url.startswith(REPO):
                rest = url[len(REPO):].split("#")[0]
                mm = re.match(r"(blob|tree)/main/(.+)", rest)
                if mm and not (SRC / unquote(mm.group(2))).exists():
                    bad.append(f"{where}: not in the repository")
                continue
            if re.match(r"^[a-z][a-z0-9+.-]*:", url, re.I) or url.startswith("//"):
                if not url.startswith(("http://", "https://", "mailto:", "data:")):
                    bad.append(f"{where}: unexpected scheme")
                elif url.startswith("http"):
                    outside.add(urlsplit(url).netloc)
                continue
            parts = urlsplit(url)
            target = page if not parts.path else (page.parent / unquote(parts.path)).resolve()
            if parts.path.endswith("/") or (target.is_dir()):
                target = target / "index.html"
            if root not in target.parents and target != root:
                bad.append(f"{where}: outside the site")
                continue
            if not target.exists():
                bad.append(f"{where}: no such file")
                continue
            if parts.fragment and target.suffix == ".html" and unquote(parts.fragment) not in ids_of(target):
                bad.append(f"{where}: no #{parts.fragment} in {target.relative_to(root)}")
        bad += script_errors(page, root)
    for b in bad:
        print("BAD  " + b)
    print(f"{len(pages)} pages, {n} links: {len(bad)} broken" + (f"; other sites (not fetched): {', '.join(sorted(outside))}"
                                                                if outside else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
