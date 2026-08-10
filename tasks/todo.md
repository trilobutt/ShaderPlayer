# ShaderPlayer: active plans

Two projects remain, and B runs first: C's marketing plan is written against the site
B builds, and its launch date is the moment B's `--noindex` default is turned off.

---

## Project B: manual and marketing site on Opalstack

done-when: the manual is reachable and correct at the new `marcsplained.com` subdomain
over HTTPS.

Scope: **the subdomain only**. The ABL Films site is not a target and nothing in this
project touches `~/apps/abl2019`. No analytics.

**Known server facts** (verified during planning, read-only):

- SSH alias `opal` resolves to `opal11.opalstack.com`, user `ablfilms`, key
  `~/.ssh/ablfilms_opal`, already in `~/.ssh/config`. Never copy key material into any
  file in this repo.
- `marcsplained.com` subdomains follow a static-site pattern: `~/apps/marcsplained_anime`,
  `_movies`, `_science`, `_books`, each a plain directory of generated HTML with
  `sitemap.xml`, `feed.xml` and `search-index.json`. The new app follows it.

**One input is blocked on the user and cannot be done from this machine:**

- **B0**: create the Opalstack app, site and DNS record for the chosen
  `marcsplained.com` subdomain (`shaders.` is the obvious name, matching the existing
  `science.`/`movies.` pattern). This needs the Opalstack dashboard or an API token, and no
  token exists on this PC. B6 cannot deploy until the app directory exists; everything
  before it is unblocked and can proceed immediately.

### B1: Content inventory

- **Anchor**: `CLAUDE.md` (architecture, shader system, parameter system),
  `docs/shader-parameter-guide.md` (the author-facing reference),
  `default_shaders/*.hlsl` (44 files, each carrying its own ISF description and
  `SHADER_TYPE`), `src/ShaderCommon.hlsli` (the helper library).
- **Change**: produce `site/content/outline.md`: the page tree, and for each page its
  source material and one line on what a reader should be able to do after reading it.
  Extract the shader inventory programmatically into `site/content/shaders.json`
  (name, type, description, parameter list with types, ranges and defaults) with a script at
  `site/tools/extract_shaders.py` that parses the ISF block of each file, so the reference
  pages cannot drift from the shipped shaders.
- **skill**: `coding-standards:python`
- **model**: `sonnet`
- **done-when**: `python site/tools/extract_shaders.py` writes `shaders.json` containing 44
  entries and exits 0; every entry has a non-empty description.

### B2: Static site generator

- **Anchor**: new `site/build.py`, `site/templates/`, `site/content/`.
- **Change**: a single-file generator: Markdown in `site/content/`, Jinja2 templates,
  output to `site/dist/`. Produces per-page HTML, a nav tree, `sitemap.xml`, `robots.txt`
  and a client-side search index in the same shape the existing marcsplained sites use
  (`search-index.json`). New Python dependencies (`markdown`, `jinja2`, `pygments`) live in
  `site/requirements.txt` and a local venv; they are build-time only and nothing is shipped
  to the server but static output. Pygments provides HLSL highlighting for code samples.
  One `--noindex` flag adds `<meta name="robots" content="noindex,nofollow">` to every page
  and writes a `robots.txt` disallowing everything. It defaults **on** until the launch in
  Project C, so the manual can be finished and reviewed at a real URL without being indexed
  early. Unindexed is not private: the URL is reachable by anyone who has it, and the plan
  should not pretend otherwise.
- **skill**: `coding-standards:python`
- **model**: `sonnet`
- **done-when**: `python site/build.py` exits 0, `site/dist/index.html` exists and contains
  the noindex tag, and `python site/build.py --no-noindex` produces the same page without it.

### B3: Visual design

- **Anchor**: new `site/templates/base.html`, `site/static/site.css`.
- **Change**: carry the product's identity rather than inventing a second one: same canvas
  `#0B0B0B`, same `anchor` gradient, same accent set and the same region-colour idea applied
  to documentation sections. Tokens as CSS custom properties. Glass is native on the web
  (`backdrop-filter`, `rgba` fills, `border-radius`, `box-shadow`), so the panels can be
  fuller glass than Qt allows. Server-rendered HTML, no framework, and the only JavaScript
  is the search box and the mobile nav toggle. Hero uses display type over the gradient with
  a wide accent glow, and shader screenshots or short loops as the visual payload.
  Honour `prefers-reduced-motion` and `prefers-color-scheme` is not needed (this design is
  deliberately dark-only, matching the product).
- **skill**: `coding-standards:aesthetic`
- **model**: `opus`
- **done-when**: `site/dist/index.html` opened in a browser shows the styled page; every
  text-on-background pairing checked against the Legibility floor; page scores no horizontal
  scroll at 360px width.

### B4: Manual content

- **Anchor**: `site/content/` per the B1 outline.
- **Change**: write the manual: installation, the interface tour panel by panel, opening
  video and live capture, applying and editing shaders, the parameter system, keyframe
  animation, recording, Spout and the video output window, workspaces and keybindings,
  troubleshooting. Screenshots come from the finished Qt UI (Project A must be complete).
- **skill**: `write`
- **model**: `opus`
- **done-when**: every page in the outline exists with no placeholder text; build succeeds;
  a reader following the installation page from a clean machine reaches a running app.

### B5: Shader language reference

- **Anchor**: `site/content/shaders/`, generated in part from `shaders.json`.
- **Change**: the authoring reference: the cbuffer contract, the ISF block and every
  parameter type with its packing rules, audio bands and the spectrum texture, the noise
  texture, the `ShaderCommon.hlsli` helpers with signatures, the sampling and derivative
  rules, the documented gotchas (intrinsic shadowing, `atanh`, `long` needing `VALUES`,
  array-form `MIN`/`MAX` being ignored), and a per-shader page for all 44 shipped shaders
  generated from `shaders.json`. `docs/shader-parameter-guide.md` is the starting point and
  stays the in-repo source of truth.
- **skill**: `write`
- **model**: `opus`
- **done-when**: every parameter type documented with a working code sample; all 44 shader
  pages generated; a sample copied from any page compiles under
  `python tools/validate_shaders.py`.

### B6: Deploy

- **Anchor**: new `site/deploy.sh`; server app directory from B0.
- **Change**: build, then `rsync -avz --delete site/dist/ opal:~/apps/<app>/`. The app
  directory is a required argument with no default, and the script refuses to run if it is
  absent or if it does not already exist on the server: `--delete` against a mistyped path
  would erase a live site. Nothing in this script may reference `abl2019`.
- **skill**: `none`
- **model**: `sonnet`
- **done-when**: `curl -sI https://<subdomain>.marcsplained.com` returns 200 over HTTPS,
  `curl -s https://<subdomain>.marcsplained.com/robots.txt` shows the disallow, and a
  spot-checked deep page loads with styling and working search.

---

## Project C: marketing plan

done-when: `docs/marketing-plan.md` exists and is specific enough to act on this week.

### C1: Market research

- **Anchor**: new `docs/research/competitive-landscape.md`.
- **Change**: research and write up the actual landscape: Resolume Arena and Avenue, VDMX,
  TouchDesigner, Magic Music Visuals, Synesthesia, ISF-based tools, Notch, and the free tier
  represented by Shadertoy and OBS shader plugins. For each: what it costs, who buys it,
  which of ShaderPlayer's capabilities it lacks (uninterrupted recording, Spout output, live
  capture input, the keyframe system, hot-swappable HLSL with an in-app editor). Then the
  channels where those buyers actually are (VJ and live-visuals communities, the Spout and
  TouchDesigner ecosystems, film and post production for the scopes and safe-areas shaders,
  music production).
- **skill**: `none`
- **model**: `opus`
- **done-when**: file exists with a per-competitor pricing and gap table, sources linked.

### C2: The plan

- **Anchor**: new `docs/marketing-plan.md`, informed by C1 and by the site from Project B.
- **Change**: positioning and the single sentence that says what this is; target segments
  ranked by reachability rather than size; pricing (including whether there is a free tier
  and what it withholds); the launch sequence with the site as the hub; content strategy
  built on the thing that is actually scarce, which is 44 documented shaders and a
  documented shader language; distribution channels in priority order with the first
  concrete action for each; a 90 day calendar (including the point at which the site's
  `--noindex` default from B2 is turned off, which is the actual launch moment); and the
  three or four numbers worth measuring, noting that nothing currently measures them since
  no analytics were installed. No fluff, no "leverage synergies", and every recommendation carries its
  reasoning.
- **skill**: `write`
- **model**: `opus`
- **done-when**: file exists, every section above present, and the 90 day calendar names
  specific actions on specific dates rather than themes.

---

---

## Risks worth stating before starting

- **Project B's deploy phase is gated on B0**, which only the user can unblock. B1 to
  B5 run without it.
- **B4's screenshots come from the finished Qt UI.** Capture them at a window size
  where no panel is cramped: the shader editor in particular is unreadably narrow in
  the factory layout's right-hand column and should be floated or widened first.
