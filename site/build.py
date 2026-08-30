#!/usr/bin/env python3
"""Static site generator for the ShaderPlayer documentation site.

Reads Markdown pages from ``site/content/``, Jinja2 templates from
``site/templates/``, and static assets from ``site/static/``, and writes a
complete site into ``site/dist/``: per-page HTML, a nav tree threaded through
every page, ``sitemap.xml``, ``robots.txt``, and a client-side search index
at ``search-index.json``.

Content versus specification
-----------------------------
``site/content/`` holds three different kinds of file, and the generator
tells them apart structurally rather than by a filename denylist:

* A *page* is a ``.md`` file that opens with a ``---``-delimited frontmatter
  block carrying at least a ``title`` key (see ``parse_frontmatter``). It is
  rendered and published.
* A *specification* is a ``.md`` file with no such frontmatter — the only
  one today is ``outline.md``, the page plan this generator is built
  against. It is skipped, with a note printed to stdout, rather than
  published as a page nobody meant to read.
* *Data* is anything that is not Markdown at all, chiefly ``shaders.json``.
  It is never matched by the page glob (``**/*.md``) and is consumed
  directly by the shader reference generator instead.

Shader reference pages
-----------------------
``site/tools/extract_shaders.py`` is re-run as the first build step (never
hand-edited output) and the build fails outright if it exits non-zero, so
the generated shader pages can never drift from what actually ships in
``default_shaders/``. ``site/content/shaders.json`` is then re-validated
before use, independent of that script's own exit code, so a shape change on
either side is caught here with a clear message rather than surfacing as a
``KeyError`` deep in template rendering.
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import markdown as markdown_lib
from jinja2 import Environment, FileSystemLoader, select_autoescape
from pygments.formatters import HtmlFormatter
from pygments.lexers import get_lexer_by_name
from pygments.util import ClassNotFound

SITE_ROOT = Path(__file__).resolve().parent
REPO_ROOT = SITE_ROOT.parent
CONTENT_DIR = SITE_ROOT / "content"
TEMPLATES_DIR = SITE_ROOT / "templates"
STATIC_DIR = SITE_ROOT / "static"
DIST_DIR = SITE_ROOT / "dist"
SHADERS_JSON = CONTENT_DIR / "shaders.json"
EXTRACT_SCRIPT = SITE_ROOT / "tools" / "extract_shaders.py"

SITE_TITLE = "ShaderPlayer Documentation"
# The live site, created in phase B0. Overridable with --base-url for a local
# or staging build; it only ever appears in sitemap.xml and robots.txt.
DEFAULT_BASE_URL = "https://shaderplayer.marcsplained.com"

# Directory name under content/ -> nav section label. A top-level directory
# with no entry here still gets a page (falls back to a title-cased name);
# this dict only controls display, not what is published.
NAV_SECTION_NAMES = {"manual": "Manual", "reference": "Reference"}

# ShaderManager's SHADER_TYPE -> the Shader Library grouping the outline
# says to mirror (see outline.md, "reference/index.md").
SHADER_GROUPS = {"audio": "Audio Reactive", "generative": "Generative", "video": "Video Effects"}
SHADER_GROUP_SLUGS = {"audio": "audio-reactive", "generative": "generative", "video": "video-effects"}
SHADER_GROUP_ORDER = ["Audio Reactive", "Generative", "Video Effects"]

REQUIRED_SHADER_KEYS = {
    "name", "file", "title", "type", "description", "param_count", "custom_floats", "params",
}

_TAG_RE = re.compile(r"<[^>]+>")
_WHITESPACE_RE = re.compile(r"\s+")


@dataclass
class NavItem:
    """One entry in the nav tree. A group has no url and only children."""

    title: str
    url: str | None = None
    children: list["NavItem"] = field(default_factory=list)


@dataclass
class PageSource:
    """Everything needed to render one page and place it in the nav/index."""

    url: str
    output_path: Path
    title: str
    nav_label: str
    nav_path: list[str]
    in_nav: bool
    search_section: str
    search_text: str
    template_name: str
    template_context: dict[str, Any]
    # A section's own index page leads its nav group rather than sorting into
    # the middle of it alphabetically ("Overview" between "Noise Texture" and
    # "Sampling and Derivatives" reads as a sibling topic, not the way in).
    is_section_index: bool = False


class BuildError(Exception):
    """A build-time failure with a message meant to be read, not a traceback."""


def parse_frontmatter(text: str) -> tuple[dict[str, str], str] | None:
    """Split a content file into its frontmatter dict and Markdown body.

    A page opens with a line that is exactly ``---``, one ``key: value`` pair
    per line up to a closing ``---`` line, then the body. A file that does
    not open with ``---`` is not a page at all and this returns ``None`` --
    the mechanism that lets ``outline.md`` sit in ``site/content/`` without
    being published (see the module docstring).
    """
    lines = text.splitlines()
    if not lines or lines[0].strip() != "---":
        return None

    meta: dict[str, str] = {}
    i = 1
    while i < len(lines) and lines[i].strip() != "---":
        line = lines[i]
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            if ":" not in line:
                raise BuildError(f"malformed frontmatter line (no ':'): {line!r}")
            key, _, value = line.partition(":")
            meta[key.strip()] = value.strip()
        i += 1

    if i >= len(lines):
        raise BuildError("frontmatter opened with '---' but was never closed")

    body = "\n".join(lines[i + 1 :])
    return meta, body


def discover_pages(content_dir: Path) -> list[tuple[Path, dict[str, str], str]]:
    """Find every publishable Markdown page under ``content_dir``.

    Returns ``(relative_path, frontmatter, body)`` triples. A ``.md`` file
    with no frontmatter is a specification, not a page, and is skipped with
    a note; a page missing the required ``title`` key is a build error.
    """
    pages: list[tuple[Path, dict[str, str], str]] = []
    for path in sorted(content_dir.rglob("*.md")):
        rel = path.relative_to(content_dir)
        text = path.read_text(encoding="utf-8")
        parsed = parse_frontmatter(text)
        if parsed is None:
            print(f"skipping specification file (no frontmatter): {rel}")
            continue
        meta, body = parsed
        if "title" not in meta or not meta["title"]:
            raise BuildError(f"{rel}: frontmatter is missing the required 'title' key")
        pages.append((rel, meta, body))
    return pages


def load_shaders(path: Path) -> list[dict[str, Any]]:
    """Load and validate shaders.json's shape before anything renders from it."""
    if not path.is_file():
        raise BuildError(f"{path} does not exist; extract_shaders.py should have written it")

    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise BuildError(f"{path} is not valid JSON: {exc}") from exc

    if not isinstance(document, dict) or "shaders" not in document:
        raise BuildError(f"{path} does not have the expected shape: a JSON object with a 'shaders' key")

    shaders = document["shaders"]
    if not isinstance(shaders, list):
        raise BuildError(f"{path}: 'shaders' is not a list")

    for entry in shaders:
        if not isinstance(entry, dict):
            raise BuildError(f"{path}: a 'shaders' entry is not an object")
        missing = REQUIRED_SHADER_KEYS - entry.keys()
        if missing:
            name = entry.get("name", "?")
            raise BuildError(f"{path}: shader entry {name!r} is missing keys: {sorted(missing)}")

    return shaders


def run_extract() -> None:
    """Re-run extract_shaders.py so shaders.json can never drift from disk.

    Uses an argument list (never shell=True) and fails the build outright on
    a non-zero exit, per the constraint that the generated reference pages
    must never describe a shader differently than the app itself would.
    """
    result = subprocess.run(
        [sys.executable, str(EXTRACT_SCRIPT), "--out", str(SHADERS_JSON)],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    if result.returncode != 0:
        raise BuildError(
            f"extract_shaders.py exited {result.returncode}; refusing to build "
            "against a stale or partial shaders.json"
        )


def resolve_hlsl_alias() -> str:
    """Confirm Pygments can highlight HLSL, or fall back with a warning.

    Returns the Pygments lexer alias to use for ```hlsl fenced code blocks:
    'hlsl' when that lexer loads, otherwise a named fallback ('cpp', the
    closest C-family lexer) with a warning printed rather than silently
    emitting unhighlighted code.
    """
    try:
        get_lexer_by_name("hlsl")
        return "hlsl"
    except ClassNotFound:
        fallback = "cpp"
        print(
            f"warning: pygments has no 'hlsl' lexer in this environment; "
            f"falling back to '{fallback}' for shader code samples",
            file=sys.stderr,
        )
        return fallback


def render_markdown(body: str, hlsl_alias: str) -> str:
    """Render a page body to HTML, highlighting fenced code with Pygments."""
    if hlsl_alias != "hlsl":
        body = body.replace("```hlsl", f"```{hlsl_alias}")
    md = markdown_lib.Markdown(
        extensions=["fenced_code", "codehilite", "tables", "toc"],
        extension_configs={"codehilite": {"guess_lang": False, "css_class": "highlight"}},
    )
    return md.convert(body)


def html_to_text(html: str) -> str:
    """Strip tags for the search index's full-text field."""
    return _WHITESPACE_RE.sub(" ", _TAG_RE.sub(" ", html)).strip()


def format_number(value: float) -> str:
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    return str(value)


# Controls that expose no numeric range in the UI, and so have no meaningful
# min/max/step to publish: a long is a dropdown over its VALUES, a bool a
# checkbox, an event a button. extract_shaders.py reports null bounds for all
# three; the table says why rather than printing a bound the app never applies.
UNRANGED_NOTE = {
    "long": "not applicable, chosen from the values below",
    "bool": "not applicable, on or off",
    "event": "not applicable, a trigger button",
}


def format_bound(value: Any) -> str:
    """Render a min/max/step value. Null reaches the unranged branch instead."""
    if value is None:
        return ""
    return format_number(value)


def format_default(value: Any) -> str:
    # Only an audio param carries a null default, and the template gives those
    # their own cell rather than calling this.
    if value is None:
        return ""
    if isinstance(value, list):
        return ", ".join(format_number(v) if isinstance(v, (int, float)) else str(v) for v in value)
    if isinstance(value, bool):
        return "on" if value else "off"
    if isinstance(value, (int, float)):
        return format_number(value)
    return str(value)


def shader_page_context(shader: dict[str, Any]) -> dict[str, Any]:
    """Build the template context for one generated shader reference page."""
    group_key = shader["type"] if shader["type"] in SHADER_GROUPS else "video"
    params: list[dict[str, Any]] = []
    for p in shader["params"]:
        entry: dict[str, Any] = {
            "label": p["label"],
            "name": p["name"],
            "type": p["type"],
            "is_audio": p["type"] == "audio",
            "unranged_note": UNRANGED_NOTE.get(p["type"]),
            "band": p.get("band"),
            "default": format_default(p.get("default")),
            "min": format_bound(p.get("min")),
            "max": format_bound(p.get("max")),
            "step": format_bound(p.get("step")),
            "value_labels": None,
        }
        values = p.get("values")
        if values is not None:
            labels = p.get("labels") or [str(v) for v in values]
            entry["value_labels"] = [f"{v}: {label}" for v, label in zip(values, labels)]
        params.append(entry)

    return {
        "shader": shader,
        "params": params,
        "group_title": SHADER_GROUPS[group_key],
        "group_slug": SHADER_GROUP_SLUGS[group_key],
        "budget_line": f"{shader['custom_floats']}/32 floats, {shader['param_count']} params",
    }


def collect_sources(shaders: list[dict[str, Any]], hlsl_alias: str) -> list[PageSource]:
    """Build the full page list: content pages, generated shader pages, search."""
    sources: list[PageSource] = []

    for rel, meta, body in discover_pages(CONTENT_DIR):
        title = meta["title"]
        nav_label = meta.get("nav_title", title)
        html_body = render_markdown(body, hlsl_alias)

        is_section_index = False
        if rel == Path("index.md"):
            url = "/"
            output_path = DIST_DIR / "index.html"
            nav_path: list[str] = []
            section = "Home"
        else:
            slug = rel.with_suffix("")
            # A directory index (reference/index.md) is the page at that
            # directory's own URL, not a child called "index": it renders to
            # dist/reference/index.html and is served at /reference/. The
            # generated shader pages' "Back to <group>" breadcrumbs point at
            # /reference/#<group>, so without this they land on a 404.
            is_section_index = slug.name == "index"
            if is_section_index:
                slug = slug.parent
            url = "/" + slug.as_posix() + "/"
            output_path = DIST_DIR / slug / "index.html"
            top = rel.parts[0]
            section = NAV_SECTION_NAMES.get(top, top.replace("-", " ").replace("_", " ").title())
            nav_path = [section]

        sources.append(
            PageSource(
                url=url,
                output_path=output_path,
                title=title,
                nav_label=nav_label,
                nav_path=nav_path,
                is_section_index=is_section_index,
                in_nav=True,
                search_section=section,
                search_text=html_to_text(html_body),
                template_name="page.html",
                template_context={"body": html_body},
            )
        )

    for shader in shaders:
        ctx = shader_page_context(shader)
        group_title = ctx["group_title"]
        name = shader["name"]
        text = shader["description"] + " " + " ".join(p["label"] for p in shader["params"])
        sources.append(
            PageSource(
                url=f"/reference/shaders/{name}/",
                output_path=DIST_DIR / "reference" / "shaders" / name / "index.html",
                title=shader["title"],
                nav_label=shader["title"],
                nav_path=["Reference", group_title],
                in_nav=True,
                search_section=group_title,
                search_text=text,
                template_name="shader.html",
                template_context=ctx,
            )
        )

    sources.append(
        PageSource(
            url="/search/",
            output_path=DIST_DIR / "search" / "index.html",
            title="Search",
            nav_label="Search",
            nav_path=[],
            in_nav=False,
            search_section="Search",
            search_text="",
            template_name="search.html",
            template_context={},
        )
    )

    return sources


def build_nav(sources: list[PageSource]) -> list[NavItem]:
    """Turn the flat page list into a two-level nav tree, Home pinned first."""
    home_item: NavItem | None = None
    top_order: list[str] = []
    top_groups: dict[str, dict[str, Any]] = {}

    for src in sources:
        if not src.in_nav:
            continue
        if not src.nav_path:
            home_item = NavItem(title=src.nav_label, url=src.url)
            continue

        top_title = src.nav_path[0]
        if top_title not in top_groups:
            top_groups[top_title] = {"pages": [], "sub_order": [], "subgroups": {}}
            top_order.append(top_title)
        group = top_groups[top_title]

        if len(src.nav_path) == 1:
            group["pages"].append(src)
        else:
            sub_title = src.nav_path[1]
            if sub_title not in group["subgroups"]:
                group["subgroups"][sub_title] = []
                group["sub_order"].append(sub_title)
            group["subgroups"][sub_title].append(src)

    nav: list[NavItem] = []
    if home_item is not None:
        nav.append(home_item)

    for top_title in top_order:
        group = top_groups[top_title]
        item = NavItem(title=top_title)
        item.children = [
            NavItem(title=s.nav_label, url=s.url)
            for s in sorted(
                group["pages"], key=lambda s: (not s.is_section_index, s.nav_label)
            )
        ]

        def sub_key(t: str) -> tuple[int, str]:
            try:
                return (SHADER_GROUP_ORDER.index(t), t)
            except ValueError:
                return (len(SHADER_GROUP_ORDER), t)

        for sub_title in sorted(group["sub_order"], key=sub_key):
            sub_item = NavItem(title=sub_title)
            sub_item.children = [
                NavItem(title=s.nav_label, url=s.url)
                for s in sorted(group["subgroups"][sub_title], key=lambda s: s.nav_label)
            ]
            item.children.append(sub_item)

        nav.append(item)

    return nav


def render_sitemap(sources: list[PageSource], base_url: str) -> str:
    urls = "\n".join(f"  <url><loc>{base_url}{s.url}</loc></url>" for s in sources)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">\n'
        f"{urls}\n"
        "</urlset>\n"
    )


def render_robots(base_url: str, noindex: bool) -> str:
    if noindex:
        return "User-agent: *\nDisallow: /\n"
    return f"User-agent: *\nAllow: /\n\nSitemap: {base_url}/sitemap.xml\n"


def write_file(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def copy_static() -> None:
    dest = DIST_DIR / "static"
    if STATIC_DIR.exists():
        shutil.copytree(STATIC_DIR, dest, dirs_exist_ok=True)
    else:
        dest.mkdir(parents=True, exist_ok=True)


def build(*, base_url: str, noindex: bool) -> tuple[int, int]:
    """Build the whole site into DIST_DIR. Returns (total_pages, shader_pages)."""
    if DIST_DIR.exists():
        shutil.rmtree(DIST_DIR)
    DIST_DIR.mkdir(parents=True)

    print("running site/tools/extract_shaders.py ...")
    run_extract()

    shaders = load_shaders(SHADERS_JSON)
    hlsl_alias = resolve_hlsl_alias()

    sources = collect_sources(shaders, hlsl_alias)
    nav = build_nav(sources)

    env = Environment(
        loader=FileSystemLoader(str(TEMPLATES_DIR)),
        autoescape=select_autoescape(["html"]),
        trim_blocks=True,
        lstrip_blocks=True,
    )

    search_entries: list[dict[str, str]] = []
    shader_page_count = 0

    for src in sources:
        template = env.get_template(src.template_name)
        context = {
            "nav": nav,
            "base_url": base_url,
            "noindex": noindex,
            "url": src.url,
            "site_title": SITE_TITLE,
            "static_prefix": "/static/",
            "title": src.title,
            **src.template_context,
        }
        html = template.render(**context)
        write_file(src.output_path, html)

        if src.template_name == "shader.html":
            shader_page_count += 1
        if src.search_text:
            search_entries.append(
                {"title": src.title, "url": src.url, "text": src.search_text, "section": src.search_section}
            )

    write_file(DIST_DIR / "sitemap.xml", render_sitemap(sources, base_url))
    write_file(DIST_DIR / "robots.txt", render_robots(base_url, noindex))
    write_file(DIST_DIR / "search-index.json", json.dumps(search_entries, indent=2) + "\n")

    copy_static()
    pygments_css = HtmlFormatter(style="default").get_style_defs(".highlight") + "\n"
    write_file(DIST_DIR / "static" / "pygments.css", pygments_css)

    return len(sources), shader_page_count


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Build the ShaderPlayer documentation site into site/dist/.")
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help=f"absolute site origin for canonical links, the sitemap and robots.txt (default: {DEFAULT_BASE_URL})",
    )
    parser.add_argument(
        "--noindex",
        action=argparse.BooleanOptionalAction,
        default=True,
        help=(
            "add <meta name=robots content=noindex,nofollow> to every page and write a "
            "disallow-all robots.txt (default: on; pass --no-noindex at the Project C launch)"
        ),
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        total, shader_count = build(base_url=args.base_url.rstrip("/"), noindex=args.noindex)
    except BuildError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"built {total} pages ({shader_count} from shaders.json) into {DIST_DIR}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
