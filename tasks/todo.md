# ShaderPlayer: active plans

base: 0f1b8696ddbc8a2d67b6e45c671bdc47174fbab9

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

### B0: Server app, site and DNS  [DONE]

- **Anchor**: Opalstack API, token in `OPALSTACK_API` in `~/.abl/credentials.env` (outside
  the repo; never copy the value into any file here, and never echo it).
- **Change**: create the Opalstack app, site and DNS record for
  `shaderplayer.marcsplained.com` (confirmed by the user during the run, over the shorter
  `shaders.`). Copies the existing `science.`/`movies.` static-site pattern exactly, read
  from the API rather than guessed: app type `APA`, osuser `ablfilms`, server `opal11`,
  site routing `/` to the app, `generate_le` true, ip4 `178.162.201.225`. DNS for
  `marcsplained.com` is delegated to Opalstack nameservers, so creating the domain creates
  the A record.
- **skill**: `none`
- **model**: `sonnet`
- **done-when**: `ssh opal ls -d ~/apps/marcsplained_shaderplayer` succeeds, and
  `nslookup shaderplayer.marcsplained.com` resolves to `178.162.201.225`.
- **Created** (record for B6, all read back from the API after creation):
  domain `c13d62d5-2062-478f-9431-cce654cea3b4`, app
  `2721e7c0-4f91-4e39-95a0-e38070267c54` (`~/apps/marcsplained_shaderplayer`), site
  `21ecc2b3-3a8a-4cb9-9632-6c9192eaa03b`. HTTP serves 403 from the empty directory, which
  is the expected pre-deploy state. The Let's Encrypt certificate was still issuing when
  the phase closed; B6 must confirm HTTPS rather than assume it.

### B0a: Shader descriptions  [DONE]

Corrective phase, added during the run. `CLAUDE.md` claims every shipped shader's ISF block
carries its own description; it does not. Only 5 of 45 files have a `DESCRIPTION` key, and
`ShaderManager.cpp` never reads one. What the files carry instead is a `//` header comment
beneath the ISF block, which is unusable as a description for nine of them because it opens
with an implementation note ("ISF packing: eight scalars at offsets 0..7").

- **Anchor**: all 45 `default_shaders/*.hlsl`, each one's leading `/*{ ... }*/` ISF block.
  `CLAUDE.md` § "ISF JSON Block Parsing" and § "Shader System".
- **Change**: add a `"DESCRIPTION"` key to every shader's ISF block, placed immediately
  after `"SHADER_TYPE"` (or first, where that key is absent) and before `"INPUTS"`. One or
  two sentences of plain technical description saying what the shader does and what the
  reader would see, written from the shader body and its header comment rather than from the
  filename. It must be a single-line JSON string: JSON forbids a literal newline inside a
  string, and `ParseISFParams` runs the block through `nlohmann::json`, so a multi-line
  value silently empties the entire parameter list and drops the shader. The five files that
  already carry a `DESCRIPTION` keep it unless it is thin. Nothing outside the ISF block
  changes: no header comment is edited, no HLSL body is touched.
- **Intent**: the site's 45 reference pages are generated from this field, so it is the one
  place a description can live without drifting from the shipped shader. It also makes
  `CLAUDE.md`'s existing claim true.
- **skill**: `none`
- **model**: `opus` (raised outside the mechanical rule: the `done-when` is a judgement on
  45 pieces of technical writing about non-obvious algorithms, not an exit code)
- **done-when**: `python tools/validate_shaders.py` exits 0 with no failures, and
  `python -c "import json,re,pathlib;fs=sorted(pathlib.Path('default_shaders').glob('*.hlsl'));d=[json.loads(f.read_text(encoding='utf-8')[f.read_text(encoding='utf-8').find('/*{')+2:f.read_text(encoding='utf-8').find('}*/')+1]) for f in fs];print(len(d),all(j.get('DESCRIPTION','').strip() for j in d))"`
  prints `45 True`.

### B1: Content inventory  [DONE]

- **Anchor**: `CLAUDE.md` (architecture, shader system, parameter system),
  `docs/shader-parameter-guide.md` (the author-facing reference),
  `default_shaders/*.hlsl` (45 files, each carrying its own ISF `DESCRIPTION` and
  `SHADER_TYPE` as of B0a), `src/ShaderCommon.hlsli` (the helper library),
  `src/ShaderManager.cpp` (`ParseISFParams`, which the extractor must agree with).
- **Change**: produce `site/content/outline.md`: the page tree, and for each page its
  source material and one line on what a reader should be able to do after reading it.
  Extract the shader inventory programmatically into `site/content/shaders.json`
  (name, type, description, parameter list with types, ranges and defaults) with a script at
  `site/tools/extract_shaders.py` that parses the ISF block of each file, so the reference
  pages cannot drift from the shipped shaders.
- **skill**: `coding-standards:python`
- **model**: `sonnet`
- **done-when**: `python site/tools/extract_shaders.py` writes `shaders.json` containing 45
  entries and exits 0; every entry has a non-empty description.
- **Repaired during verification** (the done-when passed but the output was wrong in ways
  it could not see, and all 45 reference pages are generated from it):
  - `long`, `bool` and `event` params were reporting `min: 0.0, max: 1.0, step: 0.01`,
    the C++ `ShaderParam` struct defaults, for every one of the 45 `long` params in the
    set. None declares `MIN`/`MAX` in its ISF block, and none shows a numeric range in the
    UI (dropdown, checkbox, button), so the docs would have claimed a range of 0 to 1
    beside a dropdown of `[240, 480, 576, 720]`. They now report `null`. `float`,
    `point2d` and `color` keep the struct default, which genuinely is the slider range
    the app applies.
  - A `long`'s `default` was emitted as a float (`9.0`); it is one of the ints in
    `VALUES`, and is now an int.
  - Titles: `Game of Life` (was `Game Of Life`), plus a hand-checked override list for
    the hyphenated names (`Hele-Shaw Fingering`, `Diffusion-Limited Aggregation`,
    `Non-Euclidean Lens`).
  - 17 em-dashes removed from `outline.md`.
  `custom_floats` and `param_count` were verified against `tools/validate_shaders.py`
  for all 45 shaders, before and after: zero mismatches.

### B2: Static site generator  [DONE]

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
- **Built**: 47 pages, 45 of them generated from `shaders.json`. A page is any `.md` under
  `site/content/` opening with `---` frontmatter carrying a `title`; `outline.md` has none
  and so is skipped as a specification rather than published. The venv is `site/.venv`;
  `site/.venv/` and `site/dist/` are gitignored.
- **The existing marcsplained search-index shape was not reusable**: `science.` serves an
  empty array, and its consuming `search.js` expects blog fields (publish date, accent
  colour, category badge, `/articles/<slug>/` URLs). This site uses a flat array of
  `{title, url, text, section}` instead.
- **Repaired during verification**:
  - `DEFAULT_BASE_URL` was the placeholder `https://docs.shaderplayer.invalid`, which was
    reaching every `<loc>` in `sitemap.xml`. Now the real domain from B0.
  - Null bounds were rendering as the words "app default" in the Min/Max/Step columns of
    all 45 `long` params, which restates in prose exactly the false claim B1's fix removed.
    A `long` is a dropdown, a `bool` a checkbox, an `event` a button: the table now says
    which, in a single spanning cell, and names the values list for a `long`.
  - Audio-band params were showing "app default" in the Default column. A band has no
    stored value, so that row now reads "read live from the audio analyser, band: bass".
  - Verified afterwards that every parameter row on all 45 pages totals 7 columns.

### B3: Visual design  [DONE]

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
- **Verified in the driving session by looking at the rendered pages**, not from the CSS:
  homepage, a Generative shader page, a Video Effects shader page, and the search
  no-results state. Group hues carry correctly (mint, amber, magenta) through badge,
  sidebar marker and head tint. At 360px, `scrollWidth` 345 against `innerWidth` 360 on
  every page, and 316/320 at 320px, with `body` `overflow-x: visible`, so the pass is real
  rather than a clipped overflow. The 7-column parameter table scrolls inside its own
  container as intended.
- **The site must be served from a root.** `build.py` emits absolute asset paths
  (`/static/site.css`), so opening `site/dist/index.html` over `file://` renders unstyled.
  This is correct for deployment and is only a constraint on local review; serve `dist/`
  over a local HTTP server to look at it.

### B4: Manual content  [PROSE DONE, screenshots outstanding]

- **Anchor**: `site/content/` per the B1 outline.
- **Change**: write the manual: installation, the interface tour panel by panel, opening
  video and live capture, applying and editing shaders, the parameter system, keyframe
  animation, recording, Spout and the video output window, workspaces and keybindings,
  troubleshooting. Screenshots come from the finished Qt UI (Project A must be complete).
- **skill**: `coding-standards:prose`
- **model**: `opus`
- **done-when**: every page in the outline exists with no placeholder text; build succeeds;
  a reader following the installation page from a clean machine reaches a running app.
- **Prose complete**: 11 pages (the outline's ten plus `manual/index.md` as the section
  landing page), taking the site from 55 to 66. 0 broken internal links, no placeholder
  text, no em-dashes anywhere in `site/content/`. Every documented keybinding was verified
  against its dispatch line in `src/Application.cpp`.
- **Outstanding: screenshots.** Written so images slot between sections without
  restructuring; no image is referenced yet. Capture needs the app driven to particular
  states at a window size where no panel is cramped, and CLAUDE.md's Qt note means a
  `PrintWindow` or backing-store capture shows stale pixels in the viewport region, so
  these must be genuine screen captures.
- **Corrected in B5's output**: `reference/isf-block-and-parameters.md` claimed `point2d`
  "renders a draggable 2D pad". `ui/ParamsPanel.cpp:726` builds two `QDoubleSpinBox`
  controls prefixed `X ` and `Y `. B4 found it and correctly declined to edit another
  phase's page; fixed in the driving session.

### B5: Shader language reference  [DONE]

- **Anchor**: `site/content/shaders/`, generated in part from `shaders.json`.
- **Change**: the authoring reference: the cbuffer contract, the ISF block and every
  parameter type with its packing rules, audio bands and the spectrum texture, the noise
  texture, the `ShaderCommon.hlsli` helpers with signatures, the sampling and derivative
  rules, the documented gotchas (intrinsic shadowing, `atanh`, `long` needing `VALUES`,
  array-form `MIN`/`MAX` being ignored), and a per-shader page for all 45 shipped shaders
  generated from `shaders.json`. `docs/shader-parameter-guide.md` is the starting point and
  stays the in-repo source of truth.
- **skill**: `coding-standards:prose`
- **model**: `opus`
- **done-when**: every parameter type documented with a working code sample; all 45 shader
  pages generated; a sample copied from any page compiles under
  `python tools/validate_shaders.py`.
- **Built**: 8 hand-written pages, taking the site from 47 to 55. Every complete code
  sample was extracted mechanically from the page text (not retyped) into a scratch shader
  and compiled: 8/8 clean. Two documented failure modes were confirmed by deliberately
  breaking a copy, so the quoted error codes (`X3003`, `X3000`) are real.
- **Repaired during verification**:
  - `collect_sources` rendered `reference/index.md` to `/reference/index/`, so the 45
    generated pages' "Back to <group>" breadcrumbs, which point at `/reference/#<group>`,
    all 404'd. A directory index now renders to `dist/<dir>/index.html` at `/<dir>/`.
    Swept afterwards: 0 broken internal links across all 55 pages.
  - The section index sorted alphabetically into the middle of its own nav group
    ("Overview" between "Noise Texture" and "Sampling and Derivatives"). It now leads the
    group.
- **Three CLAUDE.md claims were found false against the code** and documented as the code
  actually behaves. All three belong in the fold-in:
  - "no UI currently surfaces this field" (of `ShaderPreset::compileError`) is stale.
    `ui/LibraryPanel.cpp:320` gives every failed preset an error dot with the message as
    its tooltip.
  - Budget overflow is not "skipped with a warning appended to `compileError`".
    `ShaderManager.cpp:568` `break`s silently, dropping that parameter and every one after
    it; the shader then fails on an undeclared identifier with no indication why.
  - The noise default is 512, not the "1024² default" in § "Global Noise Texture".
    `Common.h:187`.

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
  built on the thing that is actually scarce, which is 45 documented shaders and a
  documented shader language; distribution channels in priority order with the first
  concrete action for each; a 90 day calendar (including the point at which the site's
  `--noindex` default from B2 is turned off, which is the actual launch moment); and the
  three or four numbers worth measuring, noting that nothing currently measures them since
  no analytics were installed. No fluff, no "leverage synergies", and every recommendation carries its
  reasoning.
- **skill**: `coding-standards:prose`
- **model**: `opus`
- **done-when**: file exists, every section above present, and the 90 day calendar names
  specific actions on specific dates rather than themes.

---

---

## CLAUDE.md corrections owed at fold-in

Nine claims in `CLAUDE.md` were found false against the code during this run. None was
edited by a phase; all belong in the review session's fold-in. The first three are
documentation errors, the rest are product defects the documentation had papered over.

1. § "Shader System": shipped shaders each carry an ISF description. Was false for 40 of
   45 until B0a; true now.
2. § "Shader Parameter System": `compileError` has "no UI currently surfaces this field".
   `ui/LibraryPanel.cpp:320` gives a failed preset an error dot with the message as tooltip.
3. § "Global Noise Texture": the "1024² default" is 512 (`Common.h:187`).
4. § "Shader System": the `resolution` cbuffer field is called "output resolution". It is
   the viewport panel's client size, which differs from the surface the shader fills.
5. § "Cbuffer Packing Rules": over-budget parameters are "skipped with a warning appended
   to `ShaderPreset::compileError`". `ShaderManager.cpp:568` `break`s silently, dropping
   that parameter and every one after it. The shader then fails on an undeclared
   identifier with nothing on screen explaining why.
6. § "Qt Notes": "`Application::HandleKeyboardShortcuts` owns every key in the product".
   Ctrl+N is reserved by `FindBindingConflict` (`Application.cpp:1045`) and displayed in
   the F6 reference, but the dispatch `switch` has no `case 'N'`. It is unbindable and
   does nothing.
7. F9 ignores the Recording panel: it builds a fresh `RecordingSettings` with
   `outputPath = "output.mp4"` (`Application.cpp:276`), while the menu route uses the
   panel's settings. The panel's hint that "F9 starts and stops from anywhere" is true for
   stopping only.
8. Reserved keys swallow every modifier combination, but the binding dialog refuses only
   the unmodified form, so Shift+F1, Ctrl+F5 and a plain `O` or `S` all pass
   `FindBindingConflict` and then never fire.
9. A file reload resets parameter values and drops keyframes, while an F5 recompile
   preserves values by name and keeps keyframes (`ShaderManager.cpp:276` against `:107`).
   Nothing calls `RefreshParameters` after `CheckForChanges`, so the panel shows stale
   widgets after an external edit.

Unmeasured and flagged rather than asserted: generative recording declares 60 fps
unconditionally (`Application.cpp:913`) while submitting one frame per render tick, so on a
display above 60 Hz the file should run longer than real time. No recording was made to
confirm it.

## Risks worth stating before starting

- **B0 is unblocked**: an Opalstack API token now exists at `OPALSTACK_API` in
  `~/.abl/credentials.env`. It is outside the repo and its value must never be written into
  one.
- **B4's screenshots come from the finished Qt UI.** Capture them at a window size
  where no panel is cramped: the shader editor in particular is unreadably narrow in
  the factory layout's right-hand column and should be floated or widened first.
