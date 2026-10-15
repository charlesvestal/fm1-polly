#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make Polly's site for GitHub Pages (https://charlesvestal.github.io/fm1-polly/):

  index.html                     what Polly is, and the ways in: install, school, manual
  school/index.html              the course (docs/course/README.md); quickstart.html: a redirect to 101 (the quick start's old address), the first
                                 lesson; NNN-name.html (docs/course/NNN-name.md); img/ (docs/course/img)
  manual/index.html              the manual (MANUAL.md), with its contents beside it
  img/                           the manual's pictures (docs/img)
  video/                         the lessons' videos and posters (docs/video)
  install/index.html             the web installer (web/polly_installer.html, with fm1pkg.js, fm1ota.js and the
                                 package's metadata inlined)
  mirror/index.html              the screen mirror (web/mirror/index.html)
  backup/index.html              backup and restore in the browser (web/backup/index.html)
  emu/                           Polly in the browser (build/emu, from web/emu/build.sh; GPL code only: the QY70 bank
                                 is built in the visitor's browser from their own ROM; web/emu/check_published.py
                                 checks the directory). Left out with --no-emu
  words/                         the screen reader's words files (web/words, from firmware/words.tsv: every one ever
                                 made, for the FM-1s that point at them; fm1-reader)
  firmware/polly-VERSION.fwsc    the package the installer writes; also the download

Every page gets Polly's header (the name, home; School, Manual, Install, Mirror, Backup, Emulator). The
docs are rendered as they are at the time (tools/site_md.py) and their links rewritten to the site: the docs to their
pages (anchors kept, GitHub's), the web pages to theirs, other files of the repository to GitHub.

  tools/make_pages.py build/polly-0.1.fwsc 0.1 OUT_DIR [--no-emu]

The package must be a release build (./build.sh --release X.Y): its identity, FM-1_3XXYYZZ, is what the installer
checks the download against and what the FM-1 reports after the install."""
import html
import json
import os
import posixpath
import re
import shutil
import subprocess
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "web"))
sys.path.insert(0, str(SRC / "tools"))
from make_site import product_of, strip_module  # noqa: E402  (Felucca's: the package format)
import site_md  # noqa: E402

REPO = "https://github.com/charlesvestal/fm1-polly"
SITE = "https://charlesvestal.github.io/fm1-polly/"

# a repository path -> its page on the site (directories end in /)
ROUTES = {
    "MANUAL.md": "manual/",
    "docs/course/README.md": "school/",
    "README.md": "",
    "web/polly_installer.html": "install/",
    "web/mirror/index.html": "mirror/",
    "web/backup/index.html": "backup/",
    "web/emu/index.html": "emu/",
}
PREFIXES = [("docs/course/img/", "school/img/"), ("docs/img/", "img/"), ("docs/video/", "video/")]

NAV = [("school", "School", "school/"),
       ("manual", "Manual", "manual/"), ("install", "Install", "install/"), ("mirror", "Mirror", "mirror/"),
       ("backup", "Backup", "backup/"), ("emu", "Emulator", "emu/")]

FONTS = ('<link rel="preconnect" href="https://fonts.googleapis.com">\n'
         '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>\n'
         '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:ital,wght@0,400;0,600;1,400&'
         'family=Barlow+Semi+Condensed:wght@600;700&display=swap">\n')

LOGO = ('<svg class="pl-logo" viewBox="0 0 32 32" width="28" height="28" aria-hidden="true">'
        '<rect x="1" y="1" width="30" height="30" rx="9" fill="#e0683a"/>'
        '<path d="M11 24V8h6.2a5.2 5.2 0 0 1 0 10.4H11" fill="none" stroke="#fffaf1" stroke-width="3.2" '
        'stroke-linecap="round" stroke-linejoin="round"/><circle cx="22.5" cy="23.5" r="2.4" fill="#26a69a"/></svg>')

# the header's own look: its own variables, so it sits the same on every page (the installer's, the mirror's...)
HEADER_CSS = """
:root { --pl-ground: #faf4e8; --pl-ink: #48392f; --pl-muted: #8a7764; --pl-rule: #e6dccb; --pl-accent: #e0683a; --pl-teal: #26a69a; --pl-card: #fffaf1; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --pl-ground: #1c1814; --pl-ink: #f2e8d8; --pl-muted: #b3a28e; --pl-rule: #3a322a; --pl-accent: #f28a5a; --pl-teal: #4cc3b6; --pl-card: #26201b; }
}
:root[data-theme="dark"] { --pl-ground: #1c1814; --pl-ink: #f2e8d8; --pl-muted: #b3a28e; --pl-rule: #3a322a; --pl-accent: #f28a5a; --pl-teal: #4cc3b6; --pl-card: #26201b; }
div.pl-site { max-width: 1100px; margin: 0 auto 1.6rem; padding: 0 0 .7rem; border-bottom: 1px solid var(--pl-rule);
              display: flex; flex-wrap: wrap; align-items: center; gap: .5rem 1.4rem; box-sizing: border-box;
              font: 400 16px/1.4 "Barlow", "Helvetica Neue", Arial, sans-serif; text-align: left; }
div.pl-site a.pl-home { display: inline-flex; align-items: center; gap: .45rem; color: var(--pl-ink); text-decoration: none;
                        font: 700 1.45rem/1 "Barlow Semi Condensed", "Barlow", sans-serif; letter-spacing: .01em; }
div.pl-site a.pl-home:hover { color: var(--pl-accent); }
div.pl-site .pl-logo { display: block; flex: none; }
div.pl-site nav { display: flex; flex-wrap: wrap; gap: .2rem .25rem; margin: 0 0 0 auto; padding: 0; }
div.pl-site nav a { color: var(--pl-ink); text-decoration: none; padding: .28rem .6rem; border-radius: 999px; font-size: .98rem;
                    font-weight: 600; white-space: nowrap; }
div.pl-site nav a:hover { background: var(--pl-card); box-shadow: inset 0 0 0 1px var(--pl-rule); }
div.pl-site nav a[aria-current="page"] { background: var(--pl-accent); color: #fff; box-shadow: none; }
div.pl-site a:focus-visible { outline: 2px solid var(--pl-teal); outline-offset: 2px; }
div.pl-site.pl-pad { padding: 16px 16px .7rem; }
@media (max-width: 720px) {
  div.pl-site { gap: .4rem; }
  div.pl-site nav { margin: 0; width: 100%; gap: .2rem .1rem; }
  div.pl-site nav a { padding: .25rem .5rem; font-size: .95rem; }
}
"""

CSS = """
:root { --ground: #faf4e8; --ink: #48392f; --muted: #8a7764; --rule: #e6dccb; --accent: #e0683a; --teal: #26a69a; --card: #fffaf1;
        --accent-t: #f8dccd; --teal-t: #d5ece8; color-scheme: light; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --ground: #1c1814; --ink: #f2e8d8; --muted: #b3a28e; --rule: #3a322a; --accent: #f28a5a; --teal: #4cc3b6; --card: #26201b;
                                    --accent-t: #4a2d20; --teal-t: #1e3a36; color-scheme: dark; }
}
:root[data-theme="dark"] { --ground: #1c1814; --ink: #f2e8d8; --muted: #b3a28e; --rule: #3a322a; --accent: #f28a5a; --teal: #4cc3b6; --card: #26201b;
                           --accent-t: #4a2d20; --teal-t: #1e3a36; color-scheme: dark; }
* { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; }
body { background: var(--ground); color: var(--ink); margin: 0; padding: 20px 16px 64px;
       font: 400 17px/1.6 "Barlow", "Helvetica Neue", Arial, sans-serif; overflow-wrap: break-word; }
main { max-width: 46rem; margin: 0 auto; }
h1, h2, h3, h4 { font-family: "Barlow Semi Condensed", "Barlow", sans-serif; line-height: 1.15; text-wrap: balance; scroll-margin-top: 4rem; }
h1 { font-size: 2.4rem; font-weight: 700; margin: 0 0 .8rem; }
h2 { font-size: 1.65rem; font-weight: 700; margin: 2.6rem 0 .7rem; padding-top: .9rem; border-top: 2px solid var(--rule); }
h3 { font-size: 1.25rem; font-weight: 600; margin: 1.9rem 0 .5rem; }
h4 { font-size: 1.05rem; font-weight: 600; margin: 1.4rem 0 .4rem; }
p { margin: 0 0 1rem; }
ul, ol { padding-left: 1.4rem; margin: 0 0 1rem; }
li { margin: .25rem 0; }
li > ul, li > ol { margin: .2rem 0 .3rem; }
li::marker { color: var(--accent); }
strong { font-weight: 600; }
a { color: var(--ink); text-decoration-color: var(--accent); text-decoration-thickness: 2px; text-underline-offset: 3px; }
a:hover { color: var(--accent); }
a:focus-visible { outline: 2px solid var(--teal); outline-offset: 2px; border-radius: 2px; }
code, pre, kbd { font-family: ui-monospace, Menlo, Consolas, monospace; font-size: .85em; }
code { background: var(--card); border: 1px solid var(--rule); padding: .05em .35em; border-radius: 4px; }
pre { background: var(--card); border: 1px solid var(--rule); border-radius: 10px; padding: 12px 16px; overflow-x: auto; line-height: 1.45; margin: 0 0 1.2rem; }
pre code { background: none; border: 0; padding: 0; font-size: inherit; }
hr { border: 0; border-top: 1px solid var(--rule); margin: 2rem 0; }
blockquote { margin: 0 0 1.2rem; padding: .7rem 1rem; border-left: 4px solid var(--teal); background: var(--card); border-radius: 0 10px 10px 0; }
blockquote p:last-child { margin: 0; }
.table-wrap { overflow-x: auto; margin: 0 0 1.4rem; }
table.md { border-collapse: collapse; width: 100%; font-size: .95rem; }
table.md th, table.md td { text-align: left; vertical-align: top; padding: .45rem .7rem .45rem 0; border-bottom: 1px solid var(--rule); }
table.md th { font: 600 .82rem "Barlow", sans-serif; text-transform: uppercase; letter-spacing: .06em; color: var(--muted); border-bottom: 2px solid var(--rule); }
img { max-width: 100%; height: auto; }
p.shot { margin: .4rem 0 1.4rem; }
p.shot img[src$=".png"] { width: 360px; image-rendering: pixelated; border-radius: 6px; box-shadow: 0 0 0 1px var(--rule); background: #000; }
p.shot img[src$=".svg"] { width: 100%; background: #fffaf1; border-radius: 10px; padding: 6px; }
figure.video { margin: .4rem 0 1.6rem; }
figure.video video { display: block; width: 100%; height: auto; background: #111; border-radius: 12px; }
figure.video figcaption { color: var(--muted); font-size: .9rem; margin-top: .4rem; }
p.web-alt { background: var(--teal-t); border-radius: 10px; padding: .6rem .9rem; margin: .4rem 0 .9rem; }
table.steps p.web-alt { margin: .8rem 0 .6rem; }
.doc > p:has(> em:only-child) { color: var(--muted); }
footer.pl-foot { max-width: 46rem; margin: 3.5rem auto 0; padding-top: 1rem; border-top: 1px solid var(--rule); color: var(--muted); font-size: .9rem; }
footer.pl-foot a { color: inherit; }

/* the course: steps as a table, text left, picture right; stacked on a phone */
table.steps { border-collapse: separate; border-spacing: 0 14px; width: 100%; margin: 0 0 1rem; }
table.steps td { vertical-align: top; padding: 14px 16px; background: var(--card); border-top: 1px solid var(--rule); border-bottom: 1px solid var(--rule); }
table.steps td:first-child { border-left: 1px solid var(--rule); border-radius: 12px 0 0 12px; }
table.steps td:last-child { border-right: 1px solid var(--rule); border-radius: 0 12px 12px 0; padding-left: 0; }
table.steps td[colspan] { border-radius: 12px; padding-left: 16px; }
table.steps td h3 { margin: 0 0 .4rem; font-size: 1.2rem; }
table.steps td img { display: block; width: 100%; border-radius: 8px; }
table.steps td pre { margin: 0; font-size: .8rem; }
@media (max-width: 700px) {
  table.steps, table.steps tbody, table.steps tr, table.steps td { display: block; width: auto !important; }
  table.steps { border-spacing: 0; }
  table.steps tr { margin: 0 0 14px; background: var(--card); border: 1px solid var(--rule); border-radius: 12px; overflow: hidden; }
  table.steps td, table.steps td:first-child, table.steps td:last-child { border: 0; border-radius: 0; padding: 12px 14px; background: none; }
  table.steps td + td { padding-top: 0; }
}
main.wide { max-width: 64rem; }

/* School: the lessons menu, previous / next */
.lessons { position: relative; margin: 0 0 1.4rem; }
.lessons summary { list-style: none; cursor: pointer; display: inline-flex; align-items: center; gap: .5rem; padding: .35rem .9rem;
                   border: 1px solid var(--rule); border-radius: 999px; background: var(--card); font-weight: 600; }
.lessons summary::-webkit-details-marker { display: none; }
.lessons summary::after { content: ""; width: .45rem; height: .45rem; border-right: 2px solid var(--accent); border-bottom: 2px solid var(--accent);
                          transform: translateY(-2px) rotate(45deg); }
.lessons[open] summary::after { transform: translateY(2px) rotate(-135deg); }
.lessons summary .here { color: var(--muted); font-weight: 400; }
.lessons ol { position: absolute; z-index: 5; left: 0; top: calc(100% + 6px); min-width: min(22rem, calc(100vw - 32px)); list-style: none; margin: 0;
              padding: .4rem; background: var(--card); border: 1px solid var(--rule); border-radius: 12px; box-shadow: 0 10px 30px rgba(40, 25, 10, .18); }
.lessons li { margin: 0; }
.lessons li a { display: block; padding: .35rem .7rem; border-radius: 8px; text-decoration: none; }
.lessons li a:hover { background: var(--ground); color: var(--ink); }
.lessons li a[aria-current="page"] { background: var(--accent-t); font-weight: 600; }
.lessons li a[aria-current="page"]::after { content: "  ·  you are here"; font-weight: 400; color: var(--muted); }
.lessons li.all { border-top: 1px solid var(--rule); margin-top: .3rem; padding-top: .3rem; }
nav.pager { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin: 2.6rem 0 0; }
nav.pager a { display: block; padding: .8rem 1rem; border: 1px solid var(--rule); border-radius: 12px; background: var(--card); text-decoration: none; }
nav.pager a:hover { border-color: var(--accent); color: var(--ink); }
nav.pager span { display: block; color: var(--muted); font-size: .85rem; }
nav.pager strong { font: 600 1.1rem "Barlow Semi Condensed", "Barlow", sans-serif; }
nav.pager .next { grid-column: 2; text-align: right; }

/* the manual: the contents beside it on a wide screen, a menu at the top on a narrow one */
.toc-side { display: none; }
.toc-side nav { position: sticky; top: 16px; max-height: calc(100vh - 32px); overflow-y: auto; padding: .2rem 0; }
.toc-side h2, .toc-drop summary { font: 600 .82rem "Barlow", sans-serif; text-transform: uppercase; letter-spacing: .08em; color: var(--muted); border: 0; margin: 0 0 .4rem; padding: 0; }
.toc-side ol, .toc-drop ol { list-style: none; padding: 0; margin: 0; }
.toc-side li, .toc-drop li { margin: 0; }
.toc-side a, .toc-drop a { display: block; padding: .22rem .6rem; border-radius: 8px; text-decoration: none; font-size: .95rem; line-height: 1.3; }
.toc-side a:hover, .toc-drop a:hover { background: var(--card); color: var(--ink); }
.toc-side a.on { background: var(--accent-t); font-weight: 600; }
.toc-side ol.sub { display: none; margin: .1rem 0 .4rem .7rem; padding-left: .5rem; border-left: 2px solid var(--rule); }
.toc-side li.open > ol.sub { display: block; }
.toc-side ol.sub a { font-size: .88rem; padding: .16rem .55rem; color: var(--muted); }
.toc-side li.open > a { font-weight: 600; }
.toc-side ol.sub a.on { color: var(--ink); }
.toc-drop { position: sticky; top: 0; z-index: 4; margin: 0 -16px 1rem; padding: .5rem 16px; background: var(--ground); border-bottom: 1px solid var(--rule); }
.toc-drop summary { cursor: pointer; margin: 0; padding: .2rem 0; color: var(--ink); }
.toc-drop ol { max-height: 60vh; overflow-y: auto; padding: .4rem 0 .2rem; }
@media (min-width: 1100px) {
  .manual { display: grid; grid-template-columns: 15rem minmax(0, 46rem); justify-content: center; gap: 0 3rem; }
  .manual > main { margin: 0; }
  .toc-side { display: block; }
  .toc-drop { display: none; }
}

/* the landing page */
.hero { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 1.6rem 2.4rem; align-items: center; margin: .6rem 0 2rem; }
.hero h1 { font-size: 3.4rem; margin: 0 0 .1rem; }
.hero .sub { font: 600 1.2rem "Barlow Semi Condensed", "Barlow", sans-serif; color: var(--accent); margin: 0 0 1rem; }
.hero .lede { font-size: 1.12rem; margin: 0 0 1.3rem; }
.hero img { width: 300px; image-rendering: pixelated; border-radius: 10px; background: #000; box-shadow: 0 0 0 10px #17181a, 0 0 0 11px var(--rule); margin: 11px; }
.buttons { display: flex; flex-wrap: wrap; gap: .6rem; }
a.btn { display: inline-flex; flex-direction: column; text-decoration: none; padding: .55rem 1.2rem; border-radius: 14px; border: 1px solid var(--rule); background: var(--card); }
a.btn strong { font: 600 1.1rem "Barlow Semi Condensed", "Barlow", sans-serif; }
a.btn span { font-size: .85rem; color: var(--muted); }
a.btn.primary { background: var(--accent); border-color: var(--accent); color: #fff; }
a.btn.primary span { color: #fff3ec; }
a.btn:hover { border-color: var(--accent); color: inherit; }
a.btn.primary:hover { color: #fff; filter: brightness(1.05); }
.ways { display: grid; grid-template-columns: repeat(auto-fit, minmax(13rem, 1fr)); gap: 12px; margin: 0 0 1rem; }
.ways a { display: block; padding: .9rem 1rem; border: 1px solid var(--rule); border-radius: 14px; background: var(--card); text-decoration: none; }
.ways a:hover { border-color: var(--accent); color: inherit; }
.ways strong { display: block; font: 600 1.2rem "Barlow Semi Condensed", "Barlow", sans-serif; }
.ways span { color: var(--muted); font-size: .95rem; }
p.small, ul.small { color: var(--muted); font-size: .92rem; }
@media (max-width: 720px) {
  .hero { grid-template-columns: 1fr; }
  .hero h1 { font-size: 2.8rem; }
  .hero img { width: min(300px, calc(100vw - 54px)); }
  .hero figure { justify-self: center; margin: 0; }
}
"""

TOC_JS = """<script>
(() => {                                   // the contents beside the manual: the chapter being read opens to its sections
  const links = new Map([...document.querySelectorAll(".toc-side a")].map((a) => [a.getAttribute("href").slice(1), a]));
  const heads = [...document.querySelectorAll(".doc h2[id], .doc h3[id]")].filter((h) => links.has(h.id));
  const nav = document.querySelector(".toc-side nav");
  let on = null, open = null;
  const pick = () => {
    let cur = heads[0];
    for (const h of heads) if (h.getBoundingClientRect().top < 120) cur = h;
    const a = cur && links.get(cur.id);
    if (a === on) return;
    on?.classList.remove("on"); a?.classList.add("on"); on = a;
    const ch = a && (a.closest("ol.sub")?.parentElement || a.parentElement);
    if (ch !== open) { open?.classList.remove("open"); ch?.classList.add("open"); open = ch; }
    if (a && nav) {                        // keep the highlighted line in view
      const r = a.getBoundingClientRect(), n = nav.getBoundingClientRect();
      if (r.top < n.top || r.bottom > n.bottom) nav.scrollTop += r.top - n.top - n.height / 3;
    }
  };
  addEventListener("scroll", pick, { passive: true });
  pick();
  document.querySelectorAll(".toc-drop a").forEach((a) => a.addEventListener("click", () => a.closest("details").open = false));
})();
</script>"""

LESSONS_JS = """<script>
document.addEventListener("click", (e) => {               // the lessons menu closes on a click outside it
  for (const d of document.querySelectorAll("details.lessons[open]")) if (!d.contains(e.target)) d.open = false;
});
document.addEventListener("keydown", (e) => { if (e.key === "Escape") document.querySelectorAll("details.lessons[open]").forEach((d) => d.open = false); });
</script>"""


# ------------------------------------------------------------------------------------------------- links ---

def rel(target, page):
    """the site path target (a directory ends in /, "" the root) as seen from the page at site path page"""
    base = posixpath.dirname(page)
    if target.endswith("index.html"):
        target = target[:-len("index.html")]
    if target in ("", "/"):
        r = posixpath.relpath(".", base or ".")
        return "./" if r == "." else r + "/"
    r = posixpath.relpath(target.rstrip("/") or ".", base or ".")
    if target.endswith("/"):
        r = "./" if r == "." else r + "/"
    return r


def route(repo_path):
    """a repository path -> its site path, or None (not on the site)"""
    if repo_path in ROUTES:
        return ROUTES[repo_path]
    m = re.fullmatch(r"docs/course/(\d{3}-[\w-]+)\.md", repo_path)
    if m:
        return f"school/{m.group(1)}.html"
    for a, b in PREFIXES:
        if repo_path.startswith(a):
            return b + repo_path[len(a):]
    return None


def map_url(url, md_path, page):
    """a link in the doc at repository path md_path, rendered at site path page -> the site's link"""
    u = html.unescape(url)
    if not u or u.startswith(("#", "http://", "https://", "mailto:", "data:", "javascript:")):
        return url
    if u.startswith("repo:"):
        path, _, frag = u[5:].partition("#")
    else:
        path, _, frag = u.partition("#")
        path = posixpath.normpath(posixpath.join(posixpath.dirname(md_path), path)) if path else md_path
    target = route(path)
    if target is None:
        kind = "tree" if (SRC / path).is_dir() else "blob"
        out = f"{REPO}/{kind}/main/{path}"
    elif target == page:
        out = ""
    else:
        out = rel(target, page)
    if frag:
        out += "#" + frag
    return html.escape(out or "./", quote=True)


def rewrite_links(doc, md_path, page):
    return re.sub(r'\b(href|src|poster)="([^"]*)"', lambda m: f'{m.group(1)}="{map_url(m.group(2), md_path, page)}"', doc)


# ------------------------------------------------------------------------------------------ the doc hooks ---

PAGE_MENTIONS = {"web/polly_installer.html": "the Install page", "web/mirror/index.html": "the Mirror page",
                 "web/backup/index.html": "the Backup page"}


def outside_links(frag, fn):
    """fn applied to the text of frag outside tags and outside <a>...</a> (and <code>)"""
    parts, depth, out = re.split(r"(<[^>]+>)", frag), 0, []
    for p in parts:
        if p.startswith("<"):
            t = p[1:].split()[0].rstrip(">").lower() if len(p) > 2 else ""
            if t in ("a", "code", "pre"):
                depth += 1
            elif t in ("/a", "/code", "/pre"):
                depth = max(0, depth - 1)
            out.append(p)
        else:
            out.append(fn(p) if depth == 0 else p)
    return "".join(out)


def mention_pages(frag):
    """the web pages named by their file in the repository -> links to their pages"""
    for path, name in PAGE_MENTIONS.items():
        frag = re.sub(rf"<code>{re.escape(path)}</code>(\s+from\s+(?:Polly's|this|the)\s+repository)?",
                      f'<a href="repo:{path}">{name}</a>', frag)
    return frag


def link_files(text):
    def one(m):
        p = m.group(1)
        return f'<a href="repo:{p}">{p}</a>' if (SRC / p).is_file() else p
    text = re.sub(r"(?<![\w/.-])((?:[\w-]+/)*[\w-]+\.md)\b", one, text)
    return re.sub(r"\b(this repository|Polly's repository|the repository)\b", rf'<a href="{REPO}">\1</a>', text)


class SiteHooks(site_md.Hooks):
    def __init__(self, kind):
        self.kind = kind                                # "manual", "lesson", "school", "quickstart"

    def code_span(self, code):
        if code in PAGE_MENTIONS:
            return f"<code>{code}</code>"               # (mention_pages makes it a link)
        if code.endswith(".md") and (SRC / code).is_file():
            return f'<a href="repo:{code}"><code>{code}</code></a>'
        return None

    def text(self, frag):
        frag = mention_pages(frag)
        frag = outside_links(frag, link_files)
        return frag

    def code_block(self, code, lang):
        return ""

    def raw(self, block):
        block = re.sub(r"<table(?=[\s>])(?![^>]*class=)", '<table class="steps"', block)
        block = mention_pages(block)
        return block

    def paragraph(self, md):
        # a lesson's own "Next: [...](...)" line: the pager at the bottom does it
        return not (self.kind in ("lesson", "quickstart") and re.fullmatch(r"Next:\s*\[[^\]]+\]\([^)]+\)", md.strip()))


# ----------------------------------------------------------------------------------------------- pages ---

def header(page, current, emu=True, pad=False):
    items = []
    for key, label, target in NAV:
        if key == "emu" and not emu:
            continue
        cur = ' aria-current="page"' if key == current else ""
        items.append(f'<a href="{rel(target, page)}"{cur}>{label}</a>')
    return (f'<div class="pl-site{" pl-pad" if pad else ""}" role="banner"><a class="pl-home" href="{rel("", page)}" aria-label="Polly, home">{LOGO}'
            f'<span>Polly</span></a><nav aria-label="Polly">{"".join(items)}</nav></div>')


def footer(page, version):
    return (f'<footer class="pl-foot">Polly {html.escape(version)} for the M-VAVE FM-1 · GPL-3.0 · '
            f'<a href="{REPO}">Source on GitHub</a> · built on <a href="https://github.com/hugelton/Felucca">Felucca</a> by '
            f'Hügelton Instruments</footer>')


def page_html(title, description, page, current, body, version, emu, extra_head="", extra_body=""):
    return ("<!doctype html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            f"<title>{html.escape(title)}</title>\n<meta name=\"description\" content=\"{html.escape(description)}\">\n"
            "<meta name=\"theme-color\" content=\"#faf4e8\">\n" + FONTS +
            f"<style>{CSS}{HEADER_CSS}</style>\n{extra_head}</head>\n<body>\n{header(page, current, emu)}\n{body}\n"
            f"{footer(page, version)}\n{extra_body}</body>\n</html>\n")


def inject_header(src_html, page, current, emu, pad=False):
    """another page (the installer, the mirror, the emulator, the backup page) with Polly's header"""
    if not re.search(r"<body[^>]*>", src_html) or "</head>" not in src_html:
        raise SystemExit(f"{page}: no <body> / </head> to put the header in")
    out = src_html.replace("</head>", f"<style>{HEADER_CSS}</style>\n</head>", 1)
    return re.sub(r"(<body[^>]*>)", lambda m: m.group(1) + "\n" + header(page, current, emu, pad), out, count=1)


def render(md_path, kind, page):
    md = (SRC / md_path).read_text(encoding="utf-8")
    body, heads, title = site_md.convert(md, SiteHooks(kind))
    return rewrite_links(body, md_path, page), heads, title


def lessons():
    """[(md path, site page, short title)] in the course's order: docs/course/NNN-*.md"""
    out = []
    for p in sorted((SRC / "docs" / "course").glob("[0-9][0-9][0-9]-*.md")):
        t = re.search(r"^# (.+)$", p.read_text(encoding="utf-8"), re.M)
        out.append((f"docs/course/{p.name}", f"school/{p.stem}.html", t.group(1).strip() if t else p.stem))
    return out


def lessons_menu(page, current_title, order):
    items = []
    for md, target, title in order[1:]:
        cur = ' aria-current="page"' if target == page else ""
        items.append(f'<li><a href="{rel(target, page)}"{cur}>{html.escape(title)}</a></li>')
    here = f' <span class="here">· {html.escape(current_title)}</span>' if current_title else ""
    cur = ' aria-current="page"' if page == "school/index.html" else ""
    items.append(f'<li class="all"><a href="{rel("school/", page)}"{cur}>All lessons</a></li>')
    return f'<details class="lessons"><summary>Lessons{here}</summary><ol>{"".join(items)}</ol></details>'


def pager(page, order):
    pages = [o[1] for o in order]
    k = pages.index(page)
    out = []
    if k > 0:
        out.append(f'<a class="prev" rel="prev" href="{rel(order[k - 1][1], page)}">'
                   f'<span>← Previous</span><strong>{html.escape(order[k - 1][2])}</strong></a>')
    if k + 1 < len(order):
        out.append(f'<a class="next" rel="next" href="{rel(order[k + 1][1], page)}"><span>Next →</span>'
                   f'<strong>{html.escape(order[k + 1][2])}</strong></a>')
    return f'<nav class="pager" aria-label="Lessons">{"".join(out)}</nav>'


def landing(version, pkg_name, emu):
    emu_way = ('<a href="emu/"><strong>Try it in the browser</strong><span>It runs the same code as the FM-1, and you can hear it.</span></a>'
               if emu else "")
    body = f"""<main class="wide">
<section class="hero">
  <div>
    <h1>Polly</h1>
    <p class="sub">A pocket arranger for the M-VAVE FM-1</p>
    <p class="lede">Polly turns the FM-1 into a pocket arranger. It's inspired by the Yamaha QY70. Pick a style, and a band of eight
    instruments plays along, with drums, bass, guitars and strings. Hold a chord, and the band follows it. Write a few bars of chords,
    put them in order, and Polly plays a whole song from them. It adds fills, an intro and an ending. The sounds are the 128
    General MIDI instruments and a drum kit. They play through the FM-1's speaker or headphones, and MIDI in plays them too.</p>
    <div class="buttons">
      <a class="btn primary" href="install/"><strong>Install Polly {html.escape(version)}</strong><span>Use Chrome or Edge and a USB cable.</span></a>
    </div>
  </div>
  <figure><img src="img/manual/quickstart_song.png" width="240" height="240" alt="Polly's Song page: a song of four sections, verse and chorus, playing section 1"></figure>
</section>

<nav class="ways" aria-label="Learn Polly">
  <a href="school/"><strong>School</strong><span>Start here with ten short lessons. Each one has a video.</span></a>
  <a href="manual/"><strong>Manual</strong><span>It covers every page, knob and setting, and starts with a cheat sheet.</span></a>
  {emu_way}
</nav>
<nav class="ways" aria-label="Tools">
  <a href="backup/"><strong>Backup</strong><span>Save your songs to a file, and load them back later.</span></a>
  <a href="mirror/"><strong>Mirror</strong><span>See Polly's screen and panel live on your computer.</span></a>
  <a href="firmware/{pkg_name}"><strong>Download</strong><span>Get {pkg_name} for the command-line installer.</span></a>
</nav>

<h2>Credits</h2>
<ul class="small">
  <li>Polly is built on <a href="https://github.com/hugelton/Felucca">Felucca</a> by Leo Kuroshita of Hügelton Instruments (GPL-3.0).</li>
  <li>The synth and its General MIDI sounds come from <a href="https://github.com/EmbeddedSynth/sonivox">Sonivox EAS</a> by Sonic Network and the Android Open Source Project (Apache-2.0).</li>
  <li>The drums in Polly's styles come from Google Magenta's <a href="https://magenta.tensorflow.org/datasets/groove">Groove MIDI Dataset</a> (CC BY 4.0).</li>
  <li>The note editor follows <a href="https://github.com/keremimo/melodee">Melodee</a> by Kerem Kilic.</li>
  <li>Running FM-1 firmware in the browser was the idea of <a href="https://www.groove-os.com/">Groove OS</a> by Peter Gombos.</li>
  <li>For the full list, see the manual's <a href="manual/#credits">credits</a>.</li>
</ul>
<p class="small">Polly {html.escape(version)} is free software under the GPL-3.0. It comes with no warranty. The <a href="{REPO}">source is on GitHub</a>.
Polly is inspired by the Yamaha QY70 and is not affiliated with Yamaha or M-VAVE. It contains none of Yamaha's data.
M-VAVE and FM-1 are trademarks of their owners. To go back to the official firmware, use the installer's
"Back to the stock firmware".</p>
</main>"""
    return body


def main(argv):
    emu = "--no-emu" not in argv
    args = [a for a in argv if not a.startswith("--")]
    if len(args) != 3:
        sys.exit(__doc__)
    pkg, version, out = Path(args[0]), args[1], Path(args[2])
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_3\d{6}", product):
        raise SystemExit(f"{pkg}: identity {product!r}: make a release build (./build.sh --release X.Y)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    name = f"polly-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "manual", "school", "firmware", "mirror", "backup"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    shutil.copytree(SRC / "docs" / "img", out / "img")
    shutil.copytree(SRC / "docs" / "course" / "img", out / "school" / "img")
    shutil.copytree(SRC / "docs" / "video", out / "video")
    W = lambda p, s: (out / p).write_text(s, encoding="utf-8")   # noqa: E731
    desc = "Polly is a pocket arranger for the M-VAVE FM-1."

    # the installer
    inst = (SRC / "web" / "polly_installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if inst.count(mark) != 1:
            raise SystemExit(f"polly_installer.html must contain {mark} once")
    inst = inst.replace("/*LIB*/", lib).replace("/*META*/", meta)
    W("install/index.html", inject_header(inst, "install/index.html", "install", emu, pad=True))  # (its body has no padding)
    # the mirror, the backup page
    W("mirror/index.html", inject_header((SRC / "web" / "mirror" / "index.html").read_text(encoding="utf-8"),
                                         "mirror/index.html", "mirror", emu))
    W("backup/index.html", inject_header((SRC / "web" / "backup" / "index.html").read_text(encoding="utf-8"),
                                         "backup/index.html", "backup", emu))
    # the emulator
    if emu:
        e = SRC / "build" / "emu"
        if not (e / "polly.wasm").exists():
            raise SystemExit("no build/emu/polly.wasm: run web/emu/build.sh (needs Emscripten), or pass --no-emu")
        shutil.copytree(e, out / "emu", ignore=shutil.ignore_patterns("__pycache__", ".*"))
        page = (out / "emu" / "index.html").read_text(encoding="utf-8")
        page = re.sub(r'<meta name="robots"[^>]*>\n?', "", page)                  # (on its own site: a page like the others)
        page = re.sub(r'<p class="note">Internal test build[^<]*</p>\n?', "", page)
        page = page.replace("<title>Polly test build</title>", "<title>Polly in the browser</title>")
        page = page.replace("in your browser: an internal test build.", "in your browser.")
        W("emu/index.html", inject_header(page, "emu/index.html", "emu", emu))
        r = subprocess.run([sys.executable, str(SRC / "web" / "emu" / "check_published.py"), str(out / "emu")],
                           capture_output=True, text=True)
        if r.returncode:
            raise SystemExit("emu/: " + r.stdout + r.stderr)

    # the screen reader's words files (fm1-reader): every one ever made, for the devices that point at them
    shutil.copytree(SRC / "web" / "words", out / "words")

    # the manual
    body, heads, title = render("MANUAL.md", "manual", "manual/index.html")
    toc = "".join(f'<li><a href="#{hid}">{html.escape(t)}</a></li>' for lv, t, hid in heads if lv == 2)
    side, subs = [], []                    # beside it: each chapter with its sections, shown while you read it
    for lv, t, hid in heads + [(2, None, None)]:
        if lv == 2 or t is None:
            if side:
                side[-1] += (f'<ol class="sub">{"".join(subs)}</ol>' if subs else "") + "</li>"
            subs = []
            if t is not None:
                side.append(f'<li><a href="#{hid}">{html.escape(t)}</a>')
        elif lv == 3 and side:
            subs.append(f'<li><a href="#{hid}">{html.escape(t)}</a></li>')
    man = (f'<div class="manual"><aside class="toc-side" aria-label="Contents"><nav><h2>Contents</h2><ol>{"".join(side)}</ol></nav></aside>'
           f'<main class="doc"><details class="toc-drop"><summary>Contents</summary><ol>{toc}</ol></details>\n{body}\n</main></div>')
    W("manual/index.html", page_html(f"Polly · {title or 'Manual'}", f"{desc} The manual explains every page, knob and setting.",
                                     "manual/index.html", "manual", man, version, emu, extra_body=TOC_JS))

    # the quick start's old address (v0.1's notes link it): to the first lesson
    W("school/quickstart.html", '<!doctype html><meta charset="utf-8"><meta http-equiv="refresh" content="0; url=101-playing-live.html">'
                                '<link rel="canonical" href="101-playing-live.html"><a href="101-playing-live.html">101 Playing live</a>')
    # School: its index, the lessons
    order = [("docs/course/README.md", "school/index.html", "All lessons")] + lessons()
    for md_path, page, short in order:
        kind = "school" if page == "school/index.html" else "lesson"
        body, heads, title = render(md_path, kind, page)
        cur = "school"
        menu = lessons_menu(page, "" if kind == "school" else short, order)
        doc = f'<main class="doc">\n{menu}\n{body}\n{pager(page, order)}\n</main>'
        t = "Polly · School" if kind == "school" else f"Polly · {short}"
        W(page, page_html(t, f"{title} is a lesson in Polly School. {desc}" if kind == "lesson" else f"Learn Polly in ten short lessons. {desc}", page, cur, doc,
                          version, emu, extra_body=LESSONS_JS))

    # the landing page
    W("index.html", page_html("Polly", "Polly is a pocket arranger for the M-VAVE FM-1, inspired by the Yamaha QY70. A band of eight "
                              "instruments follows the chords you play.", "index.html", "", landing(version, name, emu),
                              version, emu))
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, school/ ({len(order)} pages), manual/, install/ ({product}), mirror/, backup/"
          + (", emu/" if emu else "") + f", firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    main(sys.argv[1:])
