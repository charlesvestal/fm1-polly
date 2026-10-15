#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Polly's docs (Markdown, as GitHub shows them) to HTML for the site (tools/make_pages.py). No dependencies.

Handles what the docs use: headings (with GitHub's anchors: lower case, punctuation dropped, spaces to hyphens, -1,
-2 for repeats), paragraphs, **bold**, *italic*, `code`, links, autolinks, images, a linked image of a video
([![alt](x.jpg)](x.mp4): a player), lists (nested, ordered with their start), pipe tables, fenced code, block quotes,
rules, and raw HTML blocks (the course's step tables, <details>) passed through. Links stay as written; make_pages.py
rewrites every href / src afterwards.

    convert(markdown, hooks) -> (html, headings [(level, text, id)], title)
"""
import html
import re

INLINE_TAG = re.compile(r"</?[A-Za-z][A-Za-z0-9-]*(\s[^<>]*)?/?>")
BLOCK_HTML = re.compile(r"\s*<(/?)(table|div|details|summary|p|figure|video|img|pre|section|ul|ol|dl|h[1-6]|hr|br|blockquote|center|picture)\b", re.I)
LIST_ITEM = re.compile(r"^( *)([-*+]|\d{1,9}[.)])( +|$)(.*)$")
FENCE = re.compile(r"^ {0,3}(```+|~~~+)\s*([\w+-]*)")


def github_slug(text):
    """GitHub's heading anchor from the heading's text (markup already stripped)"""
    s = text.strip().lower()
    s = re.sub(r"[^\w\- ]", "", s)
    return s.replace(" ", "-")


def plain(fragment):
    """the text of an HTML fragment"""
    return html.unescape(re.sub(r"<[^>]+>", "", fragment))


class Hooks:
    """what make_pages.py adds while rendering; the default changes nothing"""

    def code_span(self, code):            # -> HTML for `code` (escaped text given), or None for <code>code</code>
        return None

    def text(self, frag):                 # the finished HTML of a paragraph / cell / item -> HTML
        return frag

    def code_block(self, code, lang):     # -> HTML to put before a fenced block, or ""
        return ""

    def raw(self, block):                 # a raw HTML block -> HTML
        return block

    def paragraph(self, text_md):         # a paragraph's Markdown -> False to leave it out
        return True

    def video(self, alt, poster, src):    # a linked poster's player -> HTML, or None for the plain one
        return None


def inline(t, hooks):
    codes = []

    def keep(h):
        codes.append(h)
        return f"\x00{len(codes) - 1}\x00"

    def code(m):
        c = html.escape(m.group(2).strip() if m.group(2).strip() else m.group(2), quote=False)
        return keep(hooks.code_span(c) or f"<code>{c}</code>")
    t = re.sub(r"(`+)(.+?)\1", code, t)
    t = re.sub(r"<(https?://[^\s>]+)>", lambda m: keep(f'<a href="{html.escape(m.group(1))}">{html.escape(m.group(1))}</a>'), t)
    t = INLINE_TAG.sub(lambda m: keep(m.group(0)), t)                       # inline HTML (<b>, <br>, <kbd>...)
    t = re.sub(r"\\([\\`*_{}\[\]()#+\-.!|<>])", lambda m: keep(html.escape(m.group(1))), t)   # escapes
    t = re.sub(r"&(?![A-Za-z][A-Za-z0-9]*;|#\d+;|#x[0-9A-Fa-f]+;)", "&amp;", t)
    t = t.replace("<", "&lt;").replace(">", "&gt;")

    def img(m):
        return keep(f'<img src="{html.escape(m.group(2), quote=True)}" alt="{html.escape(plain(m.group(1)), quote=True)}" loading="lazy">')
    t = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)", img, t)

    def link(m):
        return f'<a href="{html.escape(html.unescape(m.group(2)), quote=True)}">{m.group(1)}</a>'
    t = re.sub(r"\[((?:[^\[\]]|\x00\d+\x00)+)\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)", link, t)
    t = re.sub(r"\*\*(?=\S)(.+?)(?<=\S)\*\*", r"<strong>\1</strong>", t)
    t = re.sub(r"(?<![\w])__(?=\S)(.+?)(?<=\S)__(?![\w])", r"<strong>\1</strong>", t)
    t = re.sub(r"\*(?=[^\s*])([^*]+?)(?<=[^\s*])\*", r"<em>\1</em>", t)
    t = re.sub(r"(?<![\w])_(?=[^\s_])([^_]+?)(?<=[^\s_])_(?![\w])", r"<em>\1</em>", t)
    while "\x00" in t:
        t = re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], t)
    return t


def split_row(line):
    s = line.strip()
    if s.startswith("|"):
        s = s[1:]
    if s.endswith("|") and not s.endswith("\\|"):
        s = s[:-1]
    cells, cur, tick = [], "", False
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s) and s[i + 1] == "|":
            cur += "|"
            i += 2
            continue
        if c == "`":
            tick = not tick
        if c == "|" and not tick:
            cells.append(cur.strip())
            cur = ""
        else:
            cur += c
        i += 1
    cells.append(cur.strip())
    return cells


class Doc:
    def __init__(self, hooks):
        self.hooks = hooks
        self.slugs = {}
        self.headings = []
        self.title = None

    def anchor(self, text):
        base = github_slug(text)
        n = self.slugs.get(base, 0)
        self.slugs[base] = n + 1
        return base if n == 0 else f"{base}-{n}"

    def para_html(self, md):
        return self.hooks.text(inline(md, self.hooks))

    def blocks(self, lines):
        out, i, n = [], 0, len(lines)
        while i < n:
            ln = lines[i]
            if not ln.strip():
                i += 1
                continue
            m = FENCE.match(ln)
            if m:
                fence, lang = m.group(1), m.group(2)
                j = i + 1
                while j < n and not lines[j].strip().startswith(fence[0] * len(fence)):
                    j += 1
                code = "\n".join(lines[i + 1:j])
                cls = f' class="language-{lang}"' if lang else ""
                out.append(self.hooks.code_block(code, lang) + f"<pre><code{cls}>{html.escape(code, quote=False)}</code></pre>")
                i = j + 1
                continue
            m = re.match(r"^(#{1,6})\s+(.*?)\s*#*\s*$", ln)
            if m:
                level, raw = len(m.group(1)), m.group(2)
                h = inline(raw, self.hooks)
                text = plain(h)
                if level == 1 and self.title is None:
                    self.title = text
                hid = self.anchor(text)
                self.headings.append((level, text, hid))
                out.append(f'<h{level} id="{hid}">{h}</h{level}>')
                i += 1
                continue
            if re.match(r"^ {0,3}([-*_])( *\1){2,}\s*$", ln):
                out.append("<hr>")
                i += 1
                continue
            m = re.match(r"^\s*\[!\[([^\]]*)\]\(([^)\s]+)\)\]\(([^)\s]+\.(?:mp4|webm|mov))\)\s*$", ln)
            if m:                                         # a poster linked to a video: a player
                alt, poster, video = m.groups()
                out.append(self.hooks.video(plain(alt), poster, video) or
                           f'<figure class="video"><video controls preload="none" playsinline poster="{html.escape(poster)}" '
                           f'src="{html.escape(video)}" aria-label="{html.escape(plain(alt))}"></video>'
                           f'<figcaption>{html.escape(plain(alt))}</figcaption></figure>')
                i += 1
                continue
            if ln.lstrip().startswith("|") and i + 1 < n and re.match(r"^\s*\|?\s*:?-{2,}", lines[i + 1]):
                head = split_row(ln)
                aligns = []
                for c in split_row(lines[i + 1]):
                    c = c.strip()
                    aligns.append("center" if c.startswith(":") and c.endswith(":") else "right" if c.endswith(":") else "")
                i += 2
                rows = []
                while i < n and lines[i].strip().startswith("|"):
                    rows.append(split_row(lines[i]))
                    i += 1

                def cell(tag, c, k):
                    a = aligns[k] if k < len(aligns) else ""
                    st = f' style="text-align:{a}"' if a else ""
                    return f"<{tag}{st}>{self.para_html(c)}</{tag}>"
                out.append('<div class="table-wrap"><table class="md"><thead><tr>' +
                           "".join(cell("th", c, k) for k, c in enumerate(head)) + "</tr></thead><tbody>" +
                           "".join("<tr>" + "".join(cell("td", c, k) for k, c in enumerate(r[:len(head)] + [""] * (len(head) - len(r)))) +
                                   "</tr>" for r in rows) + "</tbody></table></div>")
                continue
            if BLOCK_HTML.match(ln):
                j = i
                while j < n and lines[j].strip():
                    j += 1
                out.append(self.hooks.raw("\n".join(lines[i:j])))
                i = j
                continue
            if ln.lstrip().startswith(">"):
                body = []
                while i < n and lines[i].strip() and (lines[i].lstrip().startswith(">") or body):
                    body.append(re.sub(r"^\s*> ?", "", lines[i]))
                    i += 1
                out.append(f'<blockquote>{self.blocks(body)}</blockquote>')
                continue
            m = LIST_ITEM.match(ln)
            if m and len(m.group(1)) < 4:
                html_list, i = self.list_block(lines, i)
                out.append(html_list)
                continue
            para = [ln]
            i += 1
            while i < n and lines[i].strip() and not (FENCE.match(lines[i]) or re.match(r"^#{1,6}\s", lines[i]) or
                                                      lines[i].lstrip().startswith(("|", ">")) or LIST_ITEM.match(lines[i]) or
                                                      BLOCK_HTML.match(lines[i])):
                para.append(lines[i])
                i += 1
            md = "\n".join(p.strip() for p in para)
            if not self.hooks.paragraph(md):
                continue
            body = self.para_html(md.replace("\n", " "))
            if re.fullmatch(r'<img [^>]*>', body):
                out.append(f'<p class="shot">{body}</p>')
            else:
                out.append(f"<p>{body}</p>")
        return "\n".join(out)

    def list_block(self, lines, i):
        n = len(lines)
        m = LIST_ITEM.match(lines[i])
        indent, marker = len(m.group(1)), m.group(2)
        ordered = marker[0].isdigit()
        start = int(marker[:-1]) if ordered else 1
        items, loose = [], False
        while i < n:
            m = LIST_ITEM.match(lines[i])
            if not m or len(m.group(1)) != indent or m.group(2)[0].isdigit() != ordered:
                break
            content_col = len(m.group(1)) + len(m.group(2)) + max(1, len(m.group(3)))
            body = [m.group(4)]
            i += 1
            while i < n:
                ln = lines[i]
                if not ln.strip():
                    if i + 1 < n and lines[i + 1].strip() and (len(lines[i + 1]) - len(lines[i + 1].lstrip())) >= content_col:
                        body.append("")
                        loose = loose or not LIST_ITEM.match(lines[i + 1])
                        i += 1
                        continue
                    break
                lead = len(ln) - len(ln.lstrip())
                lm = LIST_ITEM.match(ln)
                if lm and lead <= indent:
                    break
                if lead >= content_col or (lead > indent and lm):
                    body.append(ln[min(lead, content_col):] if not lm else ln[content_col:] if lead >= content_col else ln.lstrip())
                elif lm is None and not FENCE.match(ln) and not re.match(r"^#{1,6}\s", ln) and not ln.lstrip().startswith("|"):
                    body.append(ln.strip())                 # a lazy continuation line
                else:
                    break
                i += 1
            items.append(body)
            if i < n and not lines[i].strip():
                if i + 1 < n and LIST_ITEM.match(lines[i + 1]) and len(LIST_ITEM.match(lines[i + 1]).group(1)) == indent:
                    loose = True
                    i += 1
        rendered = []
        for body in items:
            h = self.blocks(body)
            if not loose:
                h = re.sub(r"^<p>(.*?)</p>", r"\1", h, count=1, flags=re.S)
            rendered.append(f"<li>{h}</li>")
        tag = "ol" if ordered else "ul"
        st = f' start="{start}"' if ordered and start != 1 else ""
        return f"<{tag}{st}>" + "".join(rendered) + f"</{tag}>", i


def convert(md, hooks=None):
    d = Doc(hooks or Hooks())
    body = d.blocks(md.replace("\t", "    ").splitlines())
    return body, d.headings, d.title
