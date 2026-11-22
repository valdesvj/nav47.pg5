#!/usr/bin/env python3
"""make_manual.py - the manuals (docs/manual/*.md) as PDFs: build/manual/NAV47-manual.pdf (all four) and one PDF each.
Markdown to HTML (python-markdown, the links and headings as on GitHub), printed by headless Chrome.
    python3 tools/make_manual.py [OUTDIR]          (make manual)
"""
import base64, glob, html, os, re, subprocess, sys, shutil
import markdown

TOP = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(TOP, "docs", "manual")
OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(TOP, "build", "manual"))
CSS = """
@page { size: A4; margin: 16mm 15mm 18mm 15mm; @bottom-center { content: counter(page); } }
body { font: 10.5pt/1.45 "Noto Sans", "DejaVu Sans", system-ui, sans-serif; color: #1d2329; }
h1 { font-size: 22pt; color: #0d3b66; border-bottom: 3px solid #0d3b66; padding-bottom: 4px; margin-top: 0; }
h1.part { page-break-before: always; }
h1, h2, h3 { break-after: avoid; page-break-after: avoid; }
h2 { font-size: 15pt; color: #0d3b66; margin-top: 22px; border-bottom: 1px solid #c9d3dd; padding-bottom: 2px; }
h3 { font-size: 12pt; color: #12507f; margin-top: 16px; }
p, li { orphans: 3; widows: 3; }
a { color: #12507f; text-decoration: none; }
code { font: 9pt "DejaVu Sans Mono", monospace; background: #eef2f5; padding: 0 3px; border-radius: 3px; }
pre { background: #f3f6f8; border: 1px solid #d6dee5; border-radius: 6px; padding: 8px 10px; font-size: 8.6pt;
      line-height: 1.35; white-space: pre-wrap; page-break-inside: avoid; }
pre code { background: none; padding: 0; }
table { border-collapse: collapse; margin: 8px 0 12px; width: 100%; page-break-inside: avoid; font-size: 9.6pt; }
th, td { border: 1px solid #cfd8e0; padding: 4px 7px; vertical-align: top; text-align: left; }
th { background: #e6edf3; }
img.lcd { max-width: 58%; }
td img.lcd { max-width: 100%; }
img { max-width: 100%; border: 1px solid #cfd8e0; border-radius: 4px; display: block; margin: 6px auto 10px; page-break-inside: avoid; }
td img { margin: 2px auto 4px; }
hr { border: 0; border-top: 1px solid #d6dee5; margin: 14px 0; }
.cover { text-align: center; padding-top: 60mm; page-break-after: always; }
.cover h1 { border: 0; font-size: 34pt; }
.cover p { font-size: 13pt; color: #4a5866; }
"""
ORDER = ["README.md", "01-calculator.md", "02-gps.md", "03-web.md", "04-formulas.md"]


def slug(t):
    """GitHub's heading ids: lower case, punctuation dropped, each space a hyphen"""
    t = re.sub(r"<[^>]+>", "", html.unescape(t)).strip().lower()
    t = re.sub(r"[^\w\- ]", "", t, flags=re.UNICODE)
    return t.replace(" ", "-")


def md_html(text, one):
    """the HTML of a manual; one: links to the other manuals become links inside the one document"""
    h = markdown.markdown(text, extensions=["tables", "fenced_code", "sane_lists"])
    seen = {}
    def hid(m):
        s = slug(m.group(3)); n = seen.get(s, 0); seen[s] = n + 1
        return '<h%s%s id="%s">%s</h%s>' % (m.group(1), m.group(2), s + ("-%d" % n if n else ""), m.group(3), m.group(1))
    h = re.sub(r"<h([1-6])([^>]*)>(.*?)</h\1>", hid, h)
    def img(m):                                          # the pictures inside the file
        p = os.path.join(SRC, m.group(1))
        if not os.path.exists(p): return m.group(0)
        cls = "" if os.path.basename(p).startswith("web_") else ' class="lcd"'   # the calculator's screens: smaller
        return 'src="data:image/png;base64,%s"%s' % (base64.b64encode(open(p, "rb").read()).decode(), cls)
    h = re.sub(r'src="([^"]+\.png)"', img, h)
    if one:
        h = re.sub(r'href="(0\d-[a-z]+)\.md#([^"]+)"', r'href="#\2"', h)
        h = re.sub(r'href="(0\d-[a-z]+)\.md"', r'href="#\1"', h)
    return h


def page(title, body):
    return "<!doctype html><meta charset=utf-8><title>%s</title><style>%s</style>%s" % (html.escape(title), CSS, body)


def pdf(html_path, pdf_path):
    ch = shutil.which("google-chrome-stable") or shutil.which("chromium") or shutil.which("google-chrome")
    if not ch:
        print("no Chrome: the HTML is in", html_path); return
    subprocess.run([ch, "--headless=new", "--disable-gpu", "--no-sandbox", "--no-pdf-header-footer",
                    "--print-to-pdf=" + pdf_path, "file://" + html_path], check=True, stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
    print(pdf_path)


def main():
    os.makedirs(OUT, exist_ok=True)
    parts = []
    for i, f in enumerate(ORDER):
        text = open(os.path.join(SRC, f)).read()
        name = os.path.splitext(f)[0]
        body = md_html(text, False)
        hp = os.path.join(OUT, name + ".html")
        open(hp, "w").write(page(name, body))
        if f != "README.md": pdf(hp, os.path.join(OUT, "NAV47-" + name + ".pdf"))
        part = md_html(text, True)
        part = part.replace("<h1", '<h1 class="part" id="%s"' % name if i else "<h1", 1) if i else part
        parts.append(part)
    cover = '<div class="cover"><h1>NAV47</h1><p>The DM42n navigation computer and its web page</p><p>Almanac · Tides · Zones · Passage planning · DP · GPS</p><p>Not for navigation: check against the official publications.</p></div>'
    hp = os.path.join(OUT, "NAV47-manual.html")
    open(hp, "w").write(page("NAV47 manual", cover + "\n".join(parts)))
    pdf(hp, os.path.join(OUT, "NAV47-manual.pdf"))


if __name__ == "__main__":
    main()
