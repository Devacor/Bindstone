#!/usr/bin/env python3
"""Rewrite the shared chrome of every docs page.

The chrome is the part of a page nobody writes by hand: <head> metadata, the header,
the sidebar, the prev/next footer nav, and the site footer. Everything between
<main> and </main> is copied through byte-for-byte, except the one <nav class="prevnext">
block, which is chrome that happens to live inside the article.

Run it from anywhere:  python assets/tools/apply_chrome.py [--check]
--check reports which files would change and exits 1 if any would, touching nothing.
"""

import argparse
import pathlib
import re
import sys

SITE = pathlib.Path(__file__).resolve().parents[2]

VERSION = "0.1.0"
REPO = "https://github.com/Devacor/JaiScript"
BUILD_DATE = "2026-09-26"

THEME_BOOTSTRAP = (
    '<script>(function(){var t=null;try{t=localStorage.getItem("jai-theme")}catch(e){}\n'
    'if(!t)t=window.matchMedia&&window.matchMedia("(prefers-color-scheme: light)").matches?"light":"dark";\n'
    'document.documentElement.setAttribute("data-theme",t);}())</script>'
)

# ---------------------------------------------------------------- page tables

GUIDE = [
    ("01-hello.html", "Hello &amp; Basics",
     "Run your first script from C++, meet the values and literals, and learn print and format."),
    ("02-type-ladder.html", "The Type Ladder: typed, auto, var",
     "Three rungs of typing: int enforced, auto inferred then locked, var dynamic - plus the conversion rules typed targets apply."),
    ("03-value-semantics.html", "Value Semantics &amp; References",
     "JaiScript copies by default, everywhere. Here are the escape hatches - reference parameters, reference declarations, shared_ptr - and what each costs."),
    ("04-operators.html", "Operators &amp; Arithmetic",
     "The full C++ operator set with checked overflow, no undefined behavior, char promotion, and operator overloading on script classes."),
    ("05-control-flow.html", "Control Flow",
     "if, while, both for forms, a switch that breaks by default, and try/catch/throw that unwinds loops correctly."),
    ("06-functions.html", "Functions &amp; Lambdas",
     "Functions, lambdas with automatic captures, default arguments, variadics, and by-reference parameters."),
    ("07-classes.html", "Classes",
     "Fields in declaration order, constructor overloading and delegation, inheritance with super, statics, and hot reload with instance migration."),
    ("08-coroutines.html", "Coroutines",
     "Coroutines and yield: handles you resume, drivable by range-for, and the shapes that make multi-frame behavior easy to write."),
    ("09-containers-strings.html", "Containers &amp; Strings",
     "Arrays and maps that follow their declared type, the string methods that copy and the ones that mutate, and how to tell them apart."),
    ("10-modules.html", "Modules: include vs import",
     "include reruns a file; import caches it. Path resolution, host-controlled filesystem access, and what the checker sees across units."),
    ("11-kitchen-sink.html", "Enums, Namespaces, Destructuring &amp; Friends",
     "Enums, namespaces, destructuring, and the smaller conveniences that did not need a chapter of their own."),
    ("12-reflection.html", "Reflection: reflect::",
     "The reflect:: surface: ask a value what it is, walk fields and methods by name, and build tools on top of live objects."),
    ("13-strict-checking.html", "Types That Fail Early: strict checking",
     "What fails at load and what waits for run time, how to read a diagnostic, cast&lt;T&gt; as the explicit boundary, extern, and the [[warn_mode]] / [[strict_mode]] attributes."),
]

EMBEDDING = [
    ("01-embedding-quickstart.html", "Embedding Quickstart",
     "Two lines to a running engine, then the pieces you add: the standard library, globals, host functions, and where results come back."),
    ("02-dynamic-binder.html", "dynamic_binder: Binding C++ Classes",
     "One declaration makes a field reflected, script-visible and serialized - then the fluent binder chain, verb by verb."),
    ("03-backends.html", "Backends: Interpreter vs VM",
     "One parser, one AST, two interchangeable execution backends. What each is good at, and how to choose per engine."),
    ("04-jaibite.html", "jaibite: Parse Once, Ship Precompiled",
     "Parse once, ship precompiled: engine-bound script handles that skip straight to execution, and what the version stamp guarantees."),
    ("05-safety-rails.html", "Safety Rails",
     "Host-controlled filesystem and custom permissions, bounded script execution, and engine isolation limits."),
    ("06-static-checking.html", "Static Checking",
     "Compiler-style diagnostics before anything runs - strict by default, warn or off when you ask - and how var opts a value out."),
    ("07-hot-reload.html", "Hot Reload",
     "Redefining a class migrates every live instance in place. What survives a reload, what resets, and how to drive it from the host."),
    ("08-serialization.html", "Serialization",
     "Script values to JSON and back, plus a C++ archive framework that writes JSON or binary through one interface."),
    ("09-coroutine-hosting.html", "Coroutine Hosting",
     "Driving script coroutines from C++: resume, completion, and the shapes that fit per-entity behavior and cutscenes."),
    ("10-signals-properties.html", "Signals &amp; Observable Properties",
     "Firing and receiving signals across the boundary, the three routes a signal takes to become script-connectable, and values that announce their own changes."),
    ("11-performance.html", "Performance Notes",
     "What the VM does for you, what idiomatic-fast code looks like, the honest cost model, and real measured numbers."),
    ("12-debugging.html", "Debugging",
     "Breakpoints, stepping, and live variable reads and writes on both backends, plus a VS Code attach that costs nothing until it is used."),
    ("13-script-extensible.html", "Script-Extensible Objects: extend",
     "A script class can derive a bound C++ class, and the script decides which object becomes which class."),
    ("14-language-server.html", "Editor Support: the Language Server",
     "jaiscript_lsp answers the editor from the engine's own parser and strict checker: diagnostics as you type across a whole program, definitions, references, hover, completion, rename."),
]

SECTION_INDEX_DESC = {
    "tour/index.html":
        "Ten minutes, one small game: value semantics, the strict checker, a swarm, coroutines, hot reload and parallel_for, every line run on both backends.",
    "guide/index.html":
        "Thirteen chapters covering the whole core language - every sample executed on both backends before it was written down.",
    "embedding/index.html":
        "Fourteen chapters on hosting JaiScript in a C++ engine: binding classes, choosing a backend, safety rails, hot reload, shipping, and editor support.",
    "examples/index.html":
        "Complete, playable programs written in JaiScript - a raycast shooter, a dungeon crawler, a demoscene reel - and what each one proves.",
    "reference/index.html":
        "Quick-lookup tables distilled from the guide and embedding chapters: operators, types, conversions, the host API, and rules of thumb.",
    "index.html":
        "An embedded scripting language for C++ engines: C++-flavored syntax, script classes with hot reload, coroutines, and gradual typing.",
}

SECTION_TITLE = {"tour": "The Ten-Minute Tour", "guide": "The Language Guide", "embedding": "Embedding &amp; Engine Guide",
                 "examples": "The Example Games", "reference": "Reference"}

# ---------------------------------------------------------------- chrome parts


def attr(text):
    return text.replace('"', "&quot;")


def head(title, desc, root):
    return "\n".join([
        "<head>",
        '<meta charset="utf-8">',
        '<meta name="viewport" content="width=device-width, initial-scale=1">',
        "<title>%s</title>" % title,
        '<meta name="description" content="%s">' % attr(desc),
        '<meta name="theme-color" content="#0c0f14" media="(prefers-color-scheme: dark)">',
        '<meta name="theme-color" content="#f7f6f2" media="(prefers-color-scheme: light)">',
        '<meta property="og:type" content="website">',
        '<meta property="og:site_name" content="JaiScript docs">',
        '<meta property="og:title" content="%s">' % attr(title),
        '<meta property="og:description" content="%s">' % attr(desc),
        '<meta name="twitter:card" content="summary">',
        '<link rel="icon" href="%sassets/favicon.svg" type="image/svg+xml">' % root,
        '<link rel="stylesheet" href="%sassets/style.css">' % root,
        THEME_BOOTSTRAP,
        "</head>",
    ])


def header(section, root):
    def nav(key, label):
        mark = ' class="active" aria-current="page"' if key == section else ""
        return '<a href="%s%s/index.html"%s>%s</a>' % (root, key, mark, label)

    return "\n".join([
        '<a class="skip" href="#main">Skip to content</a>',
        '<header class="site-header">',
        '<a class="brand" href="%sindex.html"><span class="rainbow">JaiScript</span>'
        '<span class="brand-sub">docs</span></a>' % root,
        '<nav class="site-nav" aria-label="Sections">'
        + nav("tour", "Tour") + nav("guide", "Guide") + nav("embedding", "Embedding")
        + nav("examples", "Examples") + nav("reference", "Reference") + "</nav>",
        '<div class="header-tools">',
        '<div class="search" role="search"><input id="search-box" type="search" '
        'placeholder="search titles&hellip;  /" autocomplete="off" aria-label="Search docs" '
        'aria-controls="search-results" aria-expanded="false" aria-autocomplete="list">'
        '<div id="search-results" role="listbox" aria-label="Search results" hidden></div></div>',
        '<button id="theme-toggle" type="button" aria-label="Switch to light theme" '
        'title="Switch to light theme">&#9681;</button>',
        "</div>",
        "</header>",
    ])


def chapter_sidebar(section, chapters, current, root):
    label = "Guide" if section == "guide" else "Embedding"
    items = []
    for name, title, _ in chapters:
        href = "%s%s/%s" % (root, section, name)
        if name == current:
            items.append('<li class="current"><a href="%s" aria-current="page">%s</a>'
                         '<ol class="page-toc" data-page-toc></ol></li>' % (href, title))
        else:
            items.append("<li><a href=\"%s\">%s</a></li>" % (href, title))
    return ('<aside class="sidebar" aria-label="Chapters">'
            '<details class="sidebar-box" open><summary class="sidebar-title">%s</summary>'
            '<ol class="chapter-list">%s</ol></details></aside>' % (label, "".join(items)))


def page_sidebar(with_h3):
    flag = " data-toc-h3" if with_h3 else ""
    return ('<aside class="sidebar" aria-label="On this page">'
            '<details class="sidebar-box" open><summary class="sidebar-title">On this page</summary>'
            '<ol class="page-toc" data-page-toc%s></ol></details></aside>' % flag)


def footer(root):
    return "\n".join([
        '<footer class="site-footer">',
        '<p>JaiScript %s &middot; <a href="%s/blob/master/LICENSE">MIT license</a> &middot; '
        '<a href="%s">source</a> &middot; <a href="%s/issues">report a docs bug</a></p>'
        % (VERSION, REPO, REPO, REPO),
        '<p class="foot-note">every example on this site was run on both backends &middot; built %s</p>'
        % BUILD_DATE,
        "</footer>",
        '<script src="%sassets/search-index.js"></script>' % root,
        '<script src="%sassets/site.js"></script>' % root,
        "</body>",
        "</html>",
    ])


def prevnext(prev, nxt):
    """prev/nxt are (href, label, title) or None."""
    if not prev and not nxt:
        return ""
    parts = ['<nav class="prevnext" aria-label="Chapter navigation">']
    if prev:
        parts.append('<a class="prev" rel="prev" href="%s"><span>&larr; %s</span>%s</a>'
                     % (prev[0], prev[1], prev[2]))
    if nxt:
        parts.append('<a class="next" rel="next" href="%s"><span>%s &rarr;</span>%s</a>'
                     % (nxt[0], nxt[1], nxt[2]))
    parts.append("</nav>")
    return "".join(parts)


# ---------------------------------------------------------------- the chain

def build_pages():
    """Every page: (relative path, section, root prefix, description, sidebar, prevnext)."""
    pages = []

    pages.append(("index.html", None, "", SECTION_INDEX_DESC["index.html"], "", ""))

    pages.append(("tour/index.html", "tour", "../", SECTION_INDEX_DESC["tour/index.html"], page_sidebar(True),
                  prevnext(None, ("../guide/01-hello.html", "next &middot; the guide", GUIDE[0][1]))))

    guide_index = ("../guide/index.html", "guide", SECTION_TITLE["guide"])
    embed_index = ("../embedding/index.html", "embedding", SECTION_TITLE["embedding"])

    pages.append(("guide/index.html", "guide", "../", SECTION_INDEX_DESC["guide/index.html"],
                  chapter_sidebar("guide", GUIDE, None, "../"), ""))
    for i, (name, title, desc) in enumerate(GUIDE):
        prev = guide_index if i == 0 else \
            ("../guide/" + GUIDE[i - 1][0], "previous", GUIDE[i - 1][1])
        nxt = ("../embedding/" + EMBEDDING[0][0], "next &middot; embedding", EMBEDDING[0][1]) \
            if i == len(GUIDE) - 1 else ("../guide/" + GUIDE[i + 1][0], "next", GUIDE[i + 1][1])
        pages.append(("guide/" + name, "guide", "../", desc,
                      chapter_sidebar("guide", GUIDE, name, "../"), prevnext(prev, nxt)))

    pages.append(("embedding/index.html", "embedding", "../",
                  SECTION_INDEX_DESC["embedding/index.html"],
                  chapter_sidebar("embedding", EMBEDDING, None, "../"), ""))
    for i, (name, title, desc) in enumerate(EMBEDDING):
        prev = embed_index if i == 0 else \
            ("../embedding/" + EMBEDDING[i - 1][0], "previous", EMBEDDING[i - 1][1])
        nxt = ("../examples/index.html", "next &middot; examples", SECTION_TITLE["examples"]) \
            if i == len(EMBEDDING) - 1 else \
            ("../embedding/" + EMBEDDING[i + 1][0], "next", EMBEDDING[i + 1][1])
        pages.append(("embedding/" + name, "embedding", "../", desc,
                      chapter_sidebar("embedding", EMBEDDING, name, "../"), prevnext(prev, nxt)))

    pages.append(("examples/index.html", "examples", "../",
                  SECTION_INDEX_DESC["examples/index.html"], page_sidebar(False),
                  prevnext(("../embedding/" + EMBEDDING[-1][0], "previous &middot; embedding",
                            EMBEDDING[-1][1]),
                           ("../reference/index.html", "next &middot; reference",
                            SECTION_TITLE["reference"]))))

    pages.append(("reference/index.html", "reference", "../",
                  SECTION_INDEX_DESC["reference/index.html"], page_sidebar(True),
                  prevnext(("../examples/index.html", "previous &middot; examples",
                            SECTION_TITLE["examples"]), None)))
    return pages


# ---------------------------------------------------------------- rewriting

TITLE_RE = re.compile(r"<title>(.*?)</title>", re.S)
MAIN_OPEN_RE = re.compile(r"<main class=\"content\"[^>]*>")
PREVNEXT_RE = re.compile(r'<nav class="prevnext".*?</nav>', re.S)


def read_raw(path):
    """Read without newline translation so the file's own line endings survive."""
    with path.open("r", encoding="utf-8", newline="") as handle:
        return handle.read()


def rewrite(src, section, root, desc, sidebar, nav):
    title = TITLE_RE.search(src).group(1)
    prologue = src[:src.index("<head>")]

    open_tag = MAIN_OPEN_RE.search(src)
    body = src[open_tag.end():src.index("</main>")]

    if nav:
        if PREVNEXT_RE.search(body):
            body = PREVNEXT_RE.sub(lambda m: nav, body, count=1)
        else:
            cut = body.rindex("</article>")
            body = body[:cut] + nav + body[cut:]
    else:
        body = PREVNEXT_RE.sub("", body)

    out = (prologue + head(title, desc, root) + "\n<body>\n" + header(section, root)
           + '\n<div class="layout">\n' + sidebar
           + '\n<main class="content" id="main" tabindex="-1">' + body + "</main>\n</div>\n"
           + footer(root) + "\n")
    newline = "\r\n" if "\r\n" in src else "\n"
    return out.replace("\r\n", "\n").replace("\n", newline)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="report drift, write nothing")
    args = ap.parse_args()

    drifted = []
    for rel, section, root, desc, sidebar, nav in build_pages():
        path = SITE / rel
        if not path.exists():
            print("missing: %s" % rel)
            drifted.append(rel)
            continue
        src = read_raw(path)
        out = rewrite(src, section, root, desc, sidebar, nav)
        if out != src:
            drifted.append(rel)
            if not args.check:
                with path.open("w", encoding="utf-8", newline="") as handle:
                    handle.write(out)

    if args.check:
        print("\n".join(drifted) if drifted else "chrome up to date")
        return 1 if drifted else 0
    print("chrome applied: %d of 32 pages rewritten" % len(drifted))
    return 0


if __name__ == "__main__":
    sys.exit(main())
