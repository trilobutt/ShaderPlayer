# ShaderPlayer marketing plan

Written 5 September 2026, against `docs/research/competitive-landscape.md`. That file holds
the prices, the capability gaps and the sources, all checked on the same day; nothing here
re-researches it. Where this plan departs from it, the departure is argued at the point of
departure.

The product is unreleased. There is no binary in anybody's hands, no installer, no code
signing certificate, no store listing, no support channel, no community, and no licence file
in the repository. Every recommendation below is shaped by that rather than around it.

## Positioning

The wedge is the recorder. Synesthesia is the nearest rival by a distance (Windows and macOS,
audio-reactive by design, a built-in shader editor over GLSL, Spout output, USD 199 to USD 399
lifetime) and it ships no recorder at all. Its own documentation tells the user to install OBS
and capture the Spout stream, which records whatever the live output managed on the night: a
scene too heavy for the display's refresh loses frames into the file permanently, and there is
no second pass. ShaderPlayer's file render is one decoded frame in, one encoded frame out,
with the source audio muxed, so a shader costing 40 ms a frame takes longer than real time and
writes an intact file.

Live shader editing over video is already free. The Vidvox ISF Editor does that loop on both
platforms at no cost, publishes to Spout and Syphon, and takes a camera or a movie as its test
feed. A pitch that leads with the in-app editor is therefore competing against zero, and the
editor's real job here is to make the recorder worth having rather than to sell anything on
its own. The keyframe system is the second half of the wedge for the same reason: per-parameter
timelines in absolute seconds with cubic bezier handles are what turn a shader into a shot with
a beginning and an end, and of the products in the comparison only TouchDesigner, Notch and Vuo
offer curve editing at all, at USD 600, USD 1,399 a year, and macOS respectively.

The sentence:

> **ShaderPlayer runs an HLSL pixel shader over a video, a camera, or nothing at all, and
> renders the result to an H.264 or ProRes file frame for frame, with every shader parameter
> animatable on a bezier timeline.**

Three claims the pitch must never make, because each one is refutable inside a single demo and
the refutation costs the channel it happens in:

- **That it performs.** No MIDI, no OSC, no DMX or Art-Net, no NDI, no Spout receive, no
  mapping, no clip grid. Verified by grep over `src/`. It feeds somebody else's show; it does
  not run one.
- **That it composites.** One shader, one texture bound at `t0`, one video blend mode against
  the source, thirty-two floats of parameters (`float4 custom[8]`). Anything patch-shaped is
  out of scope.
- **That it is ISF-compatible.** The parameter block borrows ISF's JSON vocabulary and the
  body underneath is HLSL for fxc. No `.fs` file from the ISF corpus will ever compile here.
  Claiming otherwise is found out on the first paste.

Say the third one out loud before anyone asks. A VJ arriving with a folder of ISF shaders is a
lost sale under every version of this plan, and admitting it early buys credibility with the
people who stay.

## Segments, ranked by reachability

Reachability means: can these people be reached this quarter, with no advertising budget, by
one person. Size is a separate axis and it is mostly inverted against this one.

**1. HLSL-literate developers and technical artists.** The most reachable segment by a wide
margin, because the reaching is already done: the repository is public, the reference site
carries 66 pages including the cbuffer contract, the packing rules, the `ShaderCommon.hlsli`
signatures and the documented fxc gotchas, and this audience self-serves from a source build
without needing an installer or a certificate. They are also the least monetisable segment in
the plan, which is a tension worth naming rather than smoothing: they convert into shaders,
issues, ports and word of mouth instead of into money. That is still the best return available
in the first month, since the product's credibility problem is larger than its revenue problem.

**2. Windows people already moving textures between applications.** TouchDesigner users, Spout
users, OBS shader-filter users. Spout is Windows-only and TouchDesigner's centre of gravity is
Windows, so the platform constraint that removes half the market elsewhere costs nothing here.
They already have somewhere to send a texture and they already understand why a separate fast
single-shader tool beside a patch is a reasonable thing to want. This is the highest-quality
channel per unit of effort in the whole plan.

**3. Music producers who need a video for a track.** Magic Music Visuals proves the segment
exists and prices it at roughly USD 45 to USD 140. The requirement ends in a file, which is
exactly the thing Synesthesia cannot produce without OBS, and the audio-reactive set (8 of the
45 shipped shaders, plus AudioBand inputs on many of the generative ones) is built for it.
Reachability is real but gated: this segment will not install Visual Studio and Qt, so it is
unreachable until a packaged download exists, which is why the packaging work sits on the
critical path below.

**4. Camera department and post: the scopes.** Seven of the shipped shaders are broadcast and
grading overlays (waveform, vectorscope, RGB parade, false colour, focus peaking, zebra, safe
areas), running over a webcam, an RTSP camera feed, or a file. No product in the comparison
treats that as its business, which reads as an opening. It is the segment I would defer, for
two reasons. The competition there was never researched (Scopebox, Nobe OmniScope, and the
scopes inside Resolve are the real field, and none of them appears in the C1 table), so the
plan would be committing to a channel with no map. And the trust bar is different in kind: a
scope nobody has checked against a reference signal is worse than no scope, and an unsigned
executable does not go on a DIT cart. Treat it as a probe rather than a channel, and let one
page decide whether it earns the research pass.

## Pricing

**One perpetual tier at USD 49, with a free tier that records, and no subscription.** The
reasoning runs in three parts.

Against the comparison set, USD 49 is the honest number. Synesthesia at USD 199 is a mature
cross-platform product with a community, a shader library and years of shipping behind it;
ShaderPlayer is one shader over one picture, on Windows only, from nobody. Magic Music Visuals
brackets the price-sensitive end of the same audience at USD 44.95 to USD 139.95. Pricing near
Synesthesia would invite a feature comparison ShaderPlayer loses on every row except recording
and keyframes, while pricing at USD 19 would say the recorder is a utility rather than the
point. USD 49 sits above tip-jar noise, below the threshold where a purchase needs justifying
to anybody, and it does not pretend to a track record that does not exist.

Subscription is the wrong instrument, and the competition makes the argument. There is no
server, no hosted service and no ongoing marginal cost to recover, so a subscription would be
charging rent on a local executable. Notch has just removed its perpetual option entirely and
now offers no commercial tier under USD 1,399 a year; Resolume's model (perpetual licence,
optional paid year of updates, last paid build runs unwatermarked forever) is the one to copy
in spirit. Copy it without the renewal, at least at first: renewals create a support obligation
that a single developer with no support channel cannot honour, and administering them costs
more than they would return at this volume.

The free tier runs everything, edits everything, animates everything, sends Spout, and records,
with the output file capped at 1920x1080 and 60 seconds. That cap is chosen so an evaluator can
verify the one claim the product is sold on (open the render in a player, count the frames,
confirm nothing dropped) and a professional cannot deliver work with it. No watermark,
deliberately: a watermark on the render obstructs the exact inspection the free tier exists to
permit. Implementation is two checks, in `VideoEncoder::StartRecording` (refuse a geometry
above the cap) and against the frame counter in `SubmitFrame`, which keeps the licensed surface
small enough to audit.

The cap is a courtesy line rather than a defence, and the plan should not spend a day of
engineering pretending otherwise. The repository is public, so anyone able to read C++ can
delete the check, and `shaderfx` writes raw frames to stdout with no encoder in the path at
all, so piping it into ffmpeg bypasses the cap without editing anything. The people who would
do either are the developers in segment 1, who were never going to pay, and the producer in
segment 3 will not build from source to save USD 49.

Which answers the question of whether the public repository is an asset or a pricing problem:
it is an asset, and it costs approximately nothing in revenue. The repository is also the proof
of the central claim, since "one decoded frame in, one encoded frame out" is checkable in
`VideoEncoder.cpp` by anyone who doubts it, and that is worth more than any amount of copy
saying the same thing. It does force one decision that is currently unmade.

**There is no LICENSE file.** Under default copyright that means every reader of the public
repository has no right to use, build or modify what they are being invited to read, which is
precisely backwards for the most reachable segment in the plan. Add one in week one. Aseprite's
arrangement is the working precedent: source public and readable, modification and personal
building permitted, redistribution of binaries reserved. That keeps segment 1 legitimate while
leaving the packaged download as the thing being sold.

**Two dependencies before any binary ships, free or paid.** First, the default encoder is
`libx264` (`Common.h:170`) and x264 is GPL, so distributing a binary against the gyan.dev full
build makes the whole work GPL or makes the distribution a violation. FFmpeg itself is LGPL,
ProRes is native to it, and Windows has `h264_mf` plus the vendor hardware encoders, so the
clean route is an LGPL FFmpeg build with the H.264 path moved off libx264. The codec is
selected by name in `RecordingSettings::codec`, so the change is small; the decision is not,
and it wants a look from someone who does this for a living before the first release. Second,
selling to individuals across borders means VAT registration, which a merchant of record
(Paddle, Gumroad, Lemon Squeezy) absorbs for a percentage. Compare those percentages at the
point of decision in December, not now.

**Do not charge during the 90 days.** Ship the first binary free and unrestricted as a public
beta, publish the price and the tier structure on the site from the day the download appears so
nobody meets a surprise later, and give every beta installer a v1.0 licence when the price
starts. The scarce resource this quarter is evidence that anyone wants the thing, and charging
on day one would put payments, VAT and code signing on the critical path of a launch that has
neither an installer nor a user. The price decision belongs at the end of the window, taken
against the download numbers, and it is dated accordingly.

## Launch sequence, with the site as the hub

The site is live at `https://shaderplayer.marcsplained.com`, serving 66 pages over HTTPS with
`robots.txt` set to `Disallow: /` and a `noindex` meta on every page. Going live is one
mechanical act: `./site/deploy.sh --app marcsplained_shaderplayer --live` by hand, or a manual
`workflow_dispatch` run of `.github/workflows/deploy-site.yml` with `live=true`. Every ordinary
push keeps the noindex, so nothing can publish it early by accident.

The order below is a dependency chain rather than a preference. Each step is blocked by the one
above it.

1. **LICENSE file, and the x264 decision resolved.** Both gate distribution of any binary.
2. **A packaged download.** The build already runs `windeployqt` and copies the FFmpeg DLLs, so
   the beta package is a zip of the deployed build plus `default_shaders/`, with the Debug
   preset kept well away from it. An Inno Setup installer belongs at v1.0; a zip that works
   beats an installer that is late.
3. **A tested build on a machine that is not the developer's.** Unsigned executables meet
   SmartScreen, Qt deployment misses things that only appear on a clean machine, and the FFmpeg
   DLL step has a failure mode that produces a linking executable which dies at load. Five
   people, five machines, before anything is announced.
4. **A download page on the site**, pointing at GitHub Releases, carrying the system
   requirements from the installation page and the published price for v1.0. GitHub Releases is
   the distribution mechanism: it costs nothing, it is where the source already is, and its
   download counter is the only metric in this plan that can currently be collected at all.
5. **Turn off noindex.** This is the launch moment, and it comes after the download exists. A
   documentation site indexed while there is nothing to download spends its first crawl, and
   its first impression, on software nobody can obtain.
6. **Channel posts**, in the priority order below, starting the same day the site becomes
   indexable so that inbound traffic and the first crawl arrive together.

Code signing sits alongside this rather than inside it. An unsigned binary triggers SmartScreen
on every first run, which costs some fraction of downloads at the worst possible moment. Azure
Trusted Signing is the cheapest route I am aware of and accepts individual developers subject
to a verifiable trading history, at a monthly figure an order of magnitude below a
hardware-token OV certificate; both the eligibility rule and the price need checking against a
vendor page before they are budgeted, since I have not verified either today. A free beta aimed
at developers can ship unsigned. Segment 3 will lose installs to SmartScreen, and segment 4
will decline an unsigned executable outright.

## Content strategy

The scarce asset is the documentation, and it already exists: 45 shader pages, each generated
from the shipped `.hlsl` file with a written description and a full seven-column parameter
table, plus eight hand-written reference pages covering the cbuffer contract, the ISF block and
every parameter type, audio bands and the spectrum texture, the noise texture, the helper
library, sampling and derivative rules, and the gotchas. Nothing in the competitive set
publishes an equivalent. Synesthesia's library lists names, Shadertoy publishes code with no
parameter model at all, and Resolume documents its host rather than its shaders. Derivative and
Vidvox are the only comparable documentation efforts, and both are documenting node graphs.

Three pieces of work, in order of return.

**A rendered loop on every shader page.** Six seconds, 960x540, generated by `shaderfx` itself
so the artefact doubles as a demonstration of the render path, encoded as web video at a few
hundred kilobytes each. The 45 pages currently rank for nothing and convince nobody, because a
parameter table is not a picture, and a page showing what `physarum_slime_mould` actually looks
like is the cheapest improvement available: the pages, the descriptions and the renderer are
all built already. One script and one afternoon.

**A GLSL to HLSL porting guide.** The longest new page and the most valuable one, because it
answers the disqualifying objection head on. Somebody holding a Shadertoy shader and a Windows
machine currently has no good page to read; this one covers the coordinate and texture
conventions, `mainImage` against `main(PS_INPUT)`, `iTime` and `iResolution` against the b0
cbuffer, `texture()` against `Sample`, and the parameter block that ISF hosts give free and fxc
does not. It captures search traffic no competitor is serving, it makes the eventual importer
credible when it arrives, and it converts the ISF incompatibility from a closed door into an
hour of work.

**Output files as the channel artefact.** The product makes video, so every post to every
channel is a video the product made, with the shader source and the parameter values alongside
it. Nobody in any of these communities wants a screenshot of a Qt window.

Recommended against, with reasons: a blog on a schedule (there is nothing to say weekly, and a
blog whose last post is two months old dates the product on every page it appears); a Discord
before roughly a hundred users (a server with four people in it is worse than no server, and
GitHub Discussions costs nothing, needs no moderation, and is indexed by Google); and a mailing
list before there is a release to announce.

## Channels, in priority order

**1. The repository and the documentation site.** Owned, free, already built. First action:
commit the LICENSE file, enable GitHub Discussions, and rewrite `README.md`'s opening paragraph
to lead with the render. That README is the front door for segment 1 and it currently sells the
editor.

**2. TouchDesigner and Spout.** First action: build the worked example, a ShaderPlayer Spout
sender feeding a TouchDesigner network, published as a `.toe` file plus a page on the site, and
post it to the Derivative forum's showcase area rather than to its help area. The artefact is
the post; the tool is a detail inside it.

**3. Music production.** First action: render one complete music video end to end, on a track
whose author has agreed to it, and post the finished video to r/edmproduction with the shader,
the parameter curves and the render time in the comments. Post the output, not the application.

**4. VJ and live-visuals communities.** One post, in VJ Union, framed as a Spout source and an
offline renderer that sits beside Resolume or Magic, stating in its second sentence that it has
no MIDI and is no substitute for a mixer. This channel is the largest audience and the worst
fit, so it gets one honest post and no campaign. Any pitch here implying a replacement is
refuted in one demo, and the refutation is public and permanent.

**Show HN and r/GraphicsProgramming** get one shot each, spent on the documentation site rather
than on the product, at the point where the download exists and the site is indexable. Both
audiences reward an artefact and punish a pitch, and the artefact here is 66 pages of shader
documentation with a public repository underneath it.

**Deliberately not doing:** paid advertising of any kind (no budget, and a USD 49 product with
no conversion data cannot support a cost per acquisition anybody could calculate); YouTube
tutorials as a primary channel (the production cost per view is not recoverable at this
audience size, though a 90-second silent screen recording on the download page is worth the
afternoon); and any approach to Resolume, Vidvox or Derivative for a partnership before there
are users to bring to it.

## The 90 days

Starting Monday 7 September 2026, the first working day after this plan, and running to Sunday
6 December 2026. The first fortnight is legal and packaging work that gates everything else,
which is why the calendar opens on tasks with no marketing content in them at all.

**September: make it distributable.**

| Date | Action |
| --- | --- |
| Mon 7 Sep | Write `LICENSE` (source-available, redistribution reserved). Commit and push. |
| Tue 8 Sep | Resolve the x264 question: move the H.264 path to `h264_mf` with the vendor hardware encoders where present, or take advice and accept GPL. Whichever, decide, and record the decision in `CLAUDE.md`. |
| Wed 9 to Fri 11 Sep | Implement the encoder change and re-verify a file render frame for frame against the source. |
| Mon 14 Sep | Rewrite the `README.md` opening to lead with the render. Enable GitHub Discussions. |
| Tue 15 to Wed 16 Sep | Package: a zip of the deployed build plus `default_shaders/`, built into a clean output directory, with a `README-first-run.txt` naming the Scan Folder step. |
| Thu 17 Sep | Send the package to five named people on five machines that are not the developer's. Ask each for one thing only: did it launch, and did Scan Folder work. |
| Mon 21 to Wed 23 Sep | Fix what those five hit. Assume SmartScreen, a missing DLL, and one Qt deployment surprise. |
| Thu 24 Sep | Write the `shaderfx` loop-rendering script; render all 45 six-second loops. |
| Fri 25 Sep | Add the loops to the 45 shader pages, rebuild, deploy (noindex still on). |
| Mon 28 to Tue 29 Sep | Write the download page: requirements, the zip link, the published v1.0 price, and the beta bargain. |
| Wed 30 Sep | Write the GLSL to HLSL porting guide. |
| Thu 1 Oct | Cut `v0.9.0` on GitHub Releases with the zip attached. Deploy the site with the download page (noindex still on). |
| Fri 2 Oct | Install `v0.9.0` from the public release link on a clean machine, as a stranger would. Fix anything that breaks; re-cut if needed. |

**October: launch, and the first two channels.**

| Date | Action |
| --- | --- |
| **Tue 6 Oct, morning** | **Launch. Run `deploy-site.yml` manually with `live=true` (or `./site/deploy.sh --app marcsplained_shaderplayer --live`). Confirm `robots.txt` no longer disallows and the `noindex` meta is gone from `/reference/shaders/plasma/`.** |
| Tue 6 Oct, afternoon | Post the TouchDesigner worked example to the Derivative forum showcase. |
| Wed 7 Oct | Submit the sitemap to Google Search Console and Bing Webmaster Tools. This doubles as a free source of query and impression data, the one analytics substitute available without installing anything. |
| Thu 8 Oct | Post to r/TouchDesigner with the same artefact, worded differently. |
| Fri 9 Oct | Read the server access logs for the first time and establish the baseline. |
| Mon 12 to Tue 13 Oct | Build the `.toe` example into a proper page on the site, with its loop video. |
| Wed 14 Oct | Post the porting guide to r/GraphicsProgramming. |
| Thu 15 to Fri 16 Oct | Answer everything that came back. Cut `v0.9.1` with the fixes. |
| Mon 19 to Fri 23 Oct | Produce the music video artefact: one track, one shader, keyframed, rendered, with the render time recorded. |
| Mon 26 Oct | Post the music video to r/edmproduction. Link the download in a comment, not in the post body. |
| Tue 27 Oct | Post the same to KVR's audio-visual area. |
| Wed 28 to Fri 30 Oct | Write the scopes probe page: the seven scope shaders, what each measures, over a webcam and over a file. |

**November and early December: the remaining channels, then the numbers.**

| Date | Action |
| --- | --- |
| Mon 2 Nov | Deploy the scopes page. Post it to r/cinematography with no product pitch in the post body. |
| Tue 3 Nov | One post to VJ Union, companion framing, the "no MIDI, not a mixer" sentence in second position. |
| Thu 5 Nov | Show HN, pointing at the documentation site. Be at the keyboard for the six hours after posting. |
| Fri 6 Nov | Read the logs and the release counters. Write the week down somewhere durable. |
| Mon 9 to Fri 13 Nov | Fix and ship whatever the three posts surfaced. Cut `v0.9.2`. |
| Mon 16 to Fri 20 Nov | Build the Inno Setup installer, and get Azure Trusted Signing eligibility confirmed or ruled out. |
| Mon 23 to Wed 25 Nov | Second content pass on the five shader pages with the most traffic: better loops, worked examples, a paragraph on what each is for. |
| Thu 26 to Fri 27 Nov | Whichever channel produced the most traffic gets a second artefact. The others get nothing further. |
| Mon 30 Nov | Compare merchant-of-record fees (Paddle, Gumroad, Lemon Squeezy) against the actual download numbers. |
| Tue 1 Dec | Price decision, taken against those numbers: hold at USD 49, adjust, or stay free and say so plainly. |
| Wed 2 to Fri 4 Dec | If the decision was to charge, implement the free-tier caps in `VideoEncoder::StartRecording` and `SubmitFrame` behind the licence check. |
| Sun 6 Dec | Ninety days. Read everything, and write the v1.0 plan against evidence rather than against this document. |

## What to measure, and what can actually be measured

Nothing on the site measures anything today. No analytics package was installed, deliberately,
and the site is 66 static pages of server-rendered HTML whose only JavaScript is the search box
and the mobile nav toggle. Every number below therefore arrives with the means of collecting
it, or with an admission that it cannot be collected.

**1. Downloads per release.** Collectable today, free, with no code change:
`GET https://api.github.com/repos/trilobutt/ShaderPlayer/releases` returns
`assets[].download_count` per asset. This is the only metric in the plan available right now,
and it is the closest thing to a demand signal the product has. Record it weekly, by hand, on
the dates in the calendar; a fortnightly cron job appending to a text file would be better and
costs an hour.

**2. Unique readers of the documentation site, and which pages they read.** Not currently
collectable. The fix is the server's own access logs rather than a client-side script:
Opalstack writes web server logs per account under `~/logs/`, and GoAccess over those files
gives sessions, top pages and referrers with no JavaScript, no consent banner, no third party,
and no change to the site build. Confirm the log path exists for a static app before relying on
it, since I have not checked that from the server. The reason this metric earns its place is
narrow: which of the 45 shader pages people read tells you which shader to build the next one
like, and which three to put on the homepage.

**3. Referrals by source.** Same collection route, from the `Referer` field in those logs, and
the only way to tell which of the four channels did anything. Without it the plan cannot
distinguish a channel that worked from a week that happened to be busy, and the November
decision to drop three channels and concentrate on one depends entirely on this number. Search
Console covers the organic half separately, from the day the sitemap is submitted.

**4. Whether anybody used it twice.** Not collectable, and it will stay that way. Measuring it
means telemetry in a product whose trust argument is that its source is readable, and that
trade is bad at this scale. The available proxy is replies, issues and Discussions posts per
hundred downloads, counted by hand. It is a weak proxy and should be treated as one: a rate
near zero after two hundred downloads says something real, while five per hundred says little
beyond that some people are talking.

Three numbers that look tempting and should be ignored: GitHub stars (they measure whether a
post did well, which metric 3 already covers, and they arrive from people who will never run a
Windows executable), page views without sessions (a documentation site's view count is
dominated by crawlers), and time on page (unmeasurable from access logs, and meaningless on a
reference page somebody is reading beside an editor).
