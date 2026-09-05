# Competitive landscape

Research for the marketing plan. Every price below was checked against a vendor page or a
first-party document on **5 September 2026**, and the source is linked at the point of use.
Where a vendor blocks automated fetching and the figure comes from a third-party listing,
the table says so rather than presenting it as verified.

## What is being compared

ShaderPlayer runs one HLSL pixel shader over one picture on Windows 11 and shows the result
at frame rate while the shader source is being edited. The picture is an FFmpeg-decoded video
file, a DirectShow capture device, an RTSP or HTTP stream, or nothing at all (a generative or
audio-reactive shader draws its own image). Five capabilities carry the comparison, chosen
because they are what a buyer would otherwise assemble from two or three separate tools:

1. **Recording that does not stop.** H.264 MP4 or ProRes MOV written on an encoder thread
   while presets are switched, source is recompiled, and parameters are dragged. With a file
   open it is a deterministic render (one decoded frame in, one encoded frame out, source
   audio muxed) rather than a capture of the live output, so a shader too heavy for real time
   takes longer to render instead of dropping frames.
2. **Spout output**, plus a second borderless window on a chosen display.
3. **Live capture input**: webcam over DirectShow, or any URL FFmpeg can open.
4. **A keyframe system**: per-parameter timelines in absolute seconds, linear, ease-in-out or
   cubic bezier with an inline handle editor, markers on the transport scrubber, persisted to
   config.
5. **Hot-swappable HLSL with an in-app editor**: F5 compiles, a file watcher picks up external
   saves, a DXBC cache makes a warm start of forty-five shaders cost about fifteen
   milliseconds, and compile errors surface per preset in the library.

Forty-five shaders ship with it, of which seven are broadcast and post-production scopes
(waveform, vectorscope, RGB parade, false colour, focus peaking, zebra, safe areas). That
subset is what makes the film and post channel plausible at all, and it is the only part of
the shipped set that no competitor here treats as its business.

## Pricing

| Product | Price (checked 2026-09-05) | Model | Platform | Source |
| --- | --- | --- | --- | --- |
| Resolume Avenue | EUR 299, 1 computer | Perpetual, includes 12 months of updates; EUR 79 per further year | Win, macOS | [resolume.com](https://resolume.com/software/avenue-arena), [renewals](https://resolume.com/shop/upgrade), [update policy](https://resolume.com/support/en/updates) |
| Resolume Arena | EUR 799, 1 computer | As above; EUR 219 per further year | Win, macOS | [resolume.com](https://resolume.com/software/avenue-arena) |
| Resolume Wire | EUR 399, 1 computer (bundled as a trial with Arena and Avenue) | Perpetual | Win, macOS | [resolume.com/software/wire](https://resolume.com/software/wire) |
| VDMX6 | USD 199 | Perpetual, education and upgrade discounts | macOS 13+ only | [vidvox.net](https://vidvox.net/) |
| VDMX6 Plus | USD 349 | Perpetual; adds TouchDesigner and Vuo composition hosting | macOS 13+ only | [vidvox.net](https://vidvox.net/), [announcement](https://vdmx.vidvox.net/blog/vdmx6) |
| TouchDesigner Non-Commercial | Free | Free tier, capped at 1280x1280 output, some operators withheld | Win, macOS | [derivative.ca](https://derivative.ca/UserGuide/TouchDesigner_Products) |
| TouchDesigner Educational | USD 300 | Per licence, non-commercial use only, approval required | Win, macOS | [derivative.ca](https://derivative.ca/UserGuide/TouchDesigner_Products) |
| TouchDesigner Commercial | USD 600 | Per licence | Win, macOS | [derivative.ca](https://derivative.ca/UserGuide/TouchDesigner_Products) |
| TouchDesigner Pro | USD 2,200 | Per licence, includes 6 hours of direct support | Win, macOS | [derivative.ca](https://derivative.ca/UserGuide/TouchDesigner_Products) |
| Magic Music Visuals (entry) | From USD 44.95 one-time (third-party listing; the vendor site returns 403 to automated fetch, so treat as **unverified**) | Perpetual, per-computer | Win, macOS | [SourceForge listing](https://sourceforge.net/software/product/Magic-Music-Visuals/) |
| Magic Music Visuals Performer | USD 139.95 (search result against [magicmusicvisuals.com/purchase](https://magicmusicvisuals.com/purchase); the page is not directly fetchable, so **partially verified**) | Perpetual, per-computer | Win, macOS | [magicmusicvisuals.com](https://magicmusicvisuals.com/purchase) |
| Magic Music Visuals Studio | **Could not verify.** The middle tier's current price is not readable from any source that responded. | Perpetual, per-computer | Win, macOS | n/a |
| Synesthesia Standard | USD 199 | Lifetime, 1 device, 1080p ceiling, 1 year of updates then USD 60/year (currently waived) | Win, macOS | [synesthesia.live/pricing](https://synesthesia.live/pricing), [resolution note](https://app.synesthesia.live/docs/faq/) |
| Synesthesia Pro | USD 399 | Lifetime, 2 devices, unlimited resolution, Syphon/Spout/NDI, OSC | Win, macOS | [synesthesia.live/pricing](https://synesthesia.live/pricing) |
| Notch Indie | USD 279/year | Subscription, non-commercial only, 1080p export ceiling, no Blocks | Win | [notch.one/pricing](https://www.notch.one/pricing) |
| Notch VFX | USD 1,399/year annual, or USD 139/month | Subscription, up to 16K export, NDI and Spout, no Blocks | Win | [notch.one/pricing](https://www.notch.one/pricing) |
| Notch RFX | USD 2,589/year annual, or USD 315/month | Subscription, dongle, Notch Blocks for media servers | Win | [notch.one/pricing](https://www.notch.one/pricing) |
| Vuo Pro | USD 299 | Perpetual; Community Edition free and open source for personal use and small organisations | macOS only | [vuo.org](https://vuo.org/), [CDM on 2.0](https://cdm.link/vuo-2-effects-free-edition/) |
| ISF Editor (Vidvox) | Free | Free download, Mac stable and Windows beta | Win (beta), macOS | [isf.vidvox.net/desktop-editor](https://isf.vidvox.net/desktop-editor/) |
| CoGe VJ | **Could not verify.** The product page 404s and the last published demo targets macOS 10.9 to 10.14, so it is best treated as dormant. | n/a | macOS | [imimot.com/cogevj](https://imimot.com/cogevj/) |
| Shadertoy | Free | Web service, WebGL/GLSL, no licence purchase | Browser | [shadertoy.com](https://www.shadertoy.com/) |
| OBS Studio + obs-shaderfilter | Free | GPLv2, plugin applies user-written shaders in OBS's HLSL dialect | Win, macOS, Linux | [obs-shaderfilter](https://github.com/exeldro/obs-shaderfilter) |
| ShaderPlayer | Unpriced (pre-launch) | n/a | Windows 11 | this repository |

Two of those numbers deserve a note. Resolume's licence is perpetual in the sense that
matters: the last build released inside the paid window keeps running unwatermarked forever,
and the renewal buys new versions rather than continued use ([Resolume support on
updates](https://resolume.com/support/en/updates)). Notch has moved the other way, and as of
the 2026.2 plans there is no perpetual option and no free commercial tier at all, only an
Indie subscription that forbids commercial work and caps export at 1080p.

## The capability gap, competitor by competitor

Read this table as "does the product have the thing ShaderPlayer has", not as a scoreboard.
Several of these have it and a great deal more.

| Product | Records to file | Spout out | Live capture in | Keyframes on parameters | In-app shader editor with hot reload |
| --- | --- | --- | --- | --- | --- |
| Resolume Avenue / Arena | Yes, DXV3 or ProRes, live output ([docs](https://resolume.com/support/en/recording)) | Yes | Yes | Clip and layer automation, no per-shader-parameter curve editor | No built-in code editor; ISF files hot reload from disk, and writing them in-app needs Wire (EUR 399) or a third-party add-on |
| VDMX6 | Yes, Movie Recorder plugin, H.264/HEVC/ProRes/HAP ([docs](https://docs.vidvox.net/vdmx/vdmx_plugins)) | Syphon (macOS), no Spout | Yes | Timeline and LFO automation, not a bezier keyframe editor per shader parameter | Yes, ISF editing in-app, GLSL |
| TouchDesigner | Yes, including non-real-time render | Yes | Yes | Yes, animation COMP with curve editing | Yes, GLSL TOP with in-app editing |
| Magic Music Visuals | Yes, exports to movie files | Yes (Spout module on Windows) | Yes, live cameras and movie files | Limited, mostly modulation rather than curves | Yes, GLSL shader modules; also loads ISF |
| Synesthesia | **No built-in recorder.** The documented route is Spout into OBS ([docs](https://app.synesthesia.live/docs/tutorials/livestream.html)) | Yes (Pro) | Media files yes; NDI and Spout input on Pro; no documented DirectShow camera path | No | Yes, built-in SSF scene editor over GLSL |
| Notch | Yes, up to 16K on VFX | Yes | Yes | Yes, full node and keyframe timeline | The primary surface is a node graph rather than a text shader editor |
| Vuo | Yes | macOS Syphon | Yes | Yes, via patches | Yes, GLSL and ISF shaders inside patches |
| ISF Editor | No | Yes, publishes to Syphon/Spout | Yes, movies, images, Syphon/Spout and camera as test feeds | No | Yes, this is its entire purpose, GLSL |
| Shadertoy | No | No | Webcam and video as channels, in-browser only | No | Yes, browser editor, GLSL |
| OBS + obs-shaderfilter | Yes, OBS is a recorder first | Yes (Spout2 plugin) | Yes | No | Yes, a shader text box per filter, OBS's HLSL dialect |
| ShaderPlayer | Yes, and as a deterministic render rather than a capture | Yes | Yes | Yes, per-parameter bezier | Yes, HLSL |

The row that matters most is Synesthesia's. It is the nearest competitor by a distance:
Windows and macOS, audio-reactive by design, a built-in shader editor over GLSL, Spout output,
a community shader library, and USD 199 to USD 399 lifetime. Its documented answer to "how do
I get a video file out of this" is to install OBS and the Spout2 source plugin, which means
the recording runs at whatever the live output managed rather than at the shader's own rate,
and a heavy scene loses frames into the file permanently. That is the clearest hole in the
nearest thing to a direct rival, and it is precisely the hole ShaderPlayer's offline render
fills.

The second useful reading is the free tier. The free ISF Editor already does the loop that
feels most distinctive about ShaderPlayer (edit a shader, watch it run over a video or a
camera feed, publish the result over Spout), on both platforms, at no cost. It cannot record,
it has no keyframes, and it is a shader development tool rather than a player, though anybody
evaluating ShaderPlayer on "live shader editing over video" alone has a free answer already
installed. Positioning has to lead with the recording and the animation, since those are where
the free tier stops.

Resolume, VDMX, Magic and Notch buy different audiences. Resolume sells to working VJs and to
production companies who need projection mapping, DMX fixtures, and multi-output show
infrastructure; the Arena price is justified by the show, not by the effects. VDMX sells to
macOS live-visuals people who want a modular instrument, and its move to Metal in VDMX6 has
kept that audience current. Magic sells to musicians and small labels making music videos and
stream visuals, which is the price-sensitive end and the closest audience to ShaderPlayer's
audio-reactive shaders. Notch sells to broadcast, touring, and virtual production, where the
per-year cost is a line item on a show budget and the deciding feature is Blocks running on
disguise or Pixera media servers.

TouchDesigner is the awkward one. It does everything on the table and its non-commercial tier
is free, so on a feature checklist it wins outright. What it charges instead is time: a node
graph, a scripting layer, and a set of conventions that take weeks to become productive in.
The free tier's 1280x1280 output cap ([Derivative
docs](https://derivative.ca/UserGuide/TouchDesigner_Products)) is the practical limit for
anyone trying to deliver 1080p work without paying USD 600.

## Where ShaderPlayer loses

Being honest about this matters more than the wins, because these are the objections the
launch will actually meet.

**It has no show infrastructure.** No MIDI, no OSC, no DMX or Art-Net, no NDI, no Spout
receive, no projection mapping, no multi-screen output mapping, and no clip library or
launching grid. Verified by grep over `src/`: none of those strings appear anywhere. A VJ
performing a set cannot use this as their main application, and any pitch that implies
otherwise will be found out within one demo. It sends a texture into somebody else's show
rather than running one.

**One shader over one picture.** There is no layer stack, no compositing beyond a single video
blend mode against the source, and one texture input bound at `t0`. Resolume, VDMX and
TouchDesigner all composite arbitrarily many layers. The parameter budget is thirty-two floats
per shader (`float4 custom[8]`), which is generous for a single effect and immediately
limiting for anything patch-shaped.

**Windows 11 only**, because the renderer is D3D11. That removes VDMX's, Vuo's, and a large
share of Synesthesia's and the ISF Editor's audience from consideration entirely. The Mac half
of the live-visuals world is unaddressable at any price.

**No GLSL, therefore no shader library.** Covered in full below. The practical form of the
objection is that a Synesthesia or ISF user arrives with hundreds of shaders they already own
and none of them run.

**Notch and TouchDesigner do things this cannot approach**: 3D scenes, particle systems,
deferred rendering, physics, camera tracking, and in Notch's case a real-time toolset built for
touring shows with Blocks that drop into media servers. Where the brief is "make me a
generative 3D world reacting to audio", ShaderPlayer is out of the running.

**It is unknown, unsupported, and unproven.** No track record, no support contract, no
community, no shader marketplace, no case studies. Against Resolume's twenty-plus years and
Derivative's install base, that is a real and rational reason for a professional to decline.

## The ISF question

ShaderPlayer's shaders carry a JSON block in the ISF shape (an `INPUTS` array with `TYPE`,
`NAME`, `DEFAULT`, `MIN`, `MAX`, `VALUES` and `LABELS`), and the parser reads it with the same
vocabulary ISF uses. The shader body underneath is HLSL compiled with fxc to shader model 5.0
for D3D11. ISF proper is a **GLSL** format ([ISF docs](https://docs.isf.video/)), so nothing
written for ISF will compile here and nothing written here will run in an ISF host.

That ecosystem is large enough for the incompatibility to be a genuine cost. ISF originated in
VDMX in 2013 and is now hosted by CoGe, EboSuite, the ISF Editor, MadMapper, Magic Music
Visuals, Millumin, Modulo Player, Motion and Final Cut Pro via a plugin, Resolume Avenue and
Arena (natively since 7.8), Smode, Synesthesia, VDMX and Videosync, among others
([isf.video/integrations](https://isf.video/integrations/)). Thousands of shaders exist for it,
and Synesthesia additionally imports from Shadertoy, which is a far larger corpus again.

Whether that costs a sale depends entirely on which buyer is asked, and the two answers are
opposite.

For an existing VJ with a shader collection, the incompatibility is disqualifying. They hold a
folder of `.fs` files that work in four applications they already own, and an application that
cannot open any of them fails at the first attempt. No amount of documentation changes that
arithmetic, and the launch should not try.

For a shader author, a video engineer, or a colourist, it is close to irrelevant, and for a
Windows D3D11 developer it is an advantage. HLSL is the language of Direct3D, Unreal, Unity
(HLSL-flavoured), and most Windows graphics material written in the last decade; fxc's error
messages, the `custom[]` packing rules, and the shipped `ShaderCommon.hlsli` are all documented
on the site. Somebody writing a shader from scratch on Windows is more likely to know HLSL
already than GLSL. The forty-five shipped shaders exist precisely so that a new user starts
from a populated folder.

The honest conclusion is that the ISF-shaped block buys familiarity of concept and buys no
portability, and it should be described that way. Claiming ISF compatibility would be found out
on the first paste. A GLSL-to-HLSL import path is the obvious future feature, and the ISF
Editor already ships a Shadertoy converter, so the direction is established and the work is
tractable.

## Where the buyers are

Four channels, ranked by whether the people there are reachable without a budget.

**The VJ and live-visuals communities** are the largest concentration and the hardest fit,
because of the show-infrastructure gap above. VJ Union, the Resolume forum, r/vjing, and the
Synesthesia and Magic user groups are where these people talk. The angle that works is a
companion tool rather than a replacement: ShaderPlayer sends over Spout into the Resolume or
Magic rig they already run, and it renders finished files for their content library. The angle
that fails is any suggestion that it replaces a mixer.

**The Spout and TouchDesigner ecosystems** are the highest-quality channel per unit of effort.
These are Windows users by definition (Spout is Windows-only, and TouchDesigner's centre of
gravity is Windows), they already think in textures moving between applications, and they
already have somewhere to send a Spout stream. The TouchDesigner community forum, the
Derivative Discord, and the r/TouchDesigner audience contain exactly the people who would use a
fast single-shader tool alongside a patch rather than instead of one. A worked example of a
ShaderPlayer sender feeding a TouchDesigner network is the concrete artefact this channel
wants.

**Film and post production** is the channel nobody else in this table is serving, on the
strength of the scopes. Waveform, vectorscope, RGB parade, false colour, focus peaking, zebra,
and safe areas are the standard on-set and grading overlays, and here they run over a webcam,
an RTSP feed from a camera, or a file, on any Windows machine, for nothing. That reaches camera
assistants, DITs, and colourists through liftgammagain, the cinematography subreddits, and the
DIT community, none of whom have heard of Resolume and all of whom know what a parade is. The
competition in that specific niche is absent from this table entirely (it is Scopebox, Nobe
OmniScope, and the scopes built into Resolve), which is worth its own research pass before the
plan commits to the channel.

**Music production** is the segment Magic Music Visuals proves exists and prices at roughly USD
45 to USD 140. Producers wanting a video for a track need a file at the end, which is exactly
what the deterministic render produces and exactly what Synesthesia lacks. They are reachable
through r/WeAreTheMusicMakers, r/edmproduction, KVR, and the Ableton and FL Studio communities.
The pitch there is short: point it at your track, pick a shader, get an MP4 that did not drop a
frame.

## Sources

All checked 5 September 2026.

- Resolume pricing and features: <https://resolume.com/software/avenue-arena>
- Resolume renewals: <https://resolume.com/shop/upgrade>
- Resolume update and expiry policy: <https://resolume.com/support/en/updates>
- Resolume recording: <https://resolume.com/support/en/recording>
- Resolume ISF support: <https://resolume.com/support/en/isf>
- Resolume Wire: <https://resolume.com/software/wire>
- VDMX6 pricing: <https://vidvox.net/>
- VDMX6 announcement: <https://vdmx.vidvox.net/blog/vdmx6>
- VDMX plugins, including Movie Recorder: <https://docs.vidvox.net/vdmx/vdmx_plugins>
- TouchDesigner products and pricing: <https://derivative.ca/UserGuide/TouchDesigner_Products>
- Magic Music Visuals purchase page: <https://magicmusicvisuals.com/purchase>
- Magic Music Visuals third-party listing: <https://sourceforge.net/software/product/Magic-Music-Visuals/>
- Synesthesia pricing: <https://synesthesia.live/pricing>
- Synesthesia FAQ and resolution tiers: <https://app.synesthesia.live/docs/faq/>
- Synesthesia livestream and recording route: <https://app.synesthesia.live/docs/tutorials/livestream.html>
- Notch pricing: <https://www.notch.one/pricing>
- Vuo: <https://vuo.org/> and <https://cdm.link/vuo-2-effects-free-edition/>
- ISF specification and documentation: <https://docs.isf.video/>
- ISF host applications: <https://isf.video/integrations/>
- ISF Editor: <https://isf.vidvox.net/desktop-editor/>
- CoGe VJ: <https://imimot.com/cogevj/>
- Shadertoy: <https://www.shadertoy.com/>
- obs-shaderfilter: <https://github.com/exeldro/obs-shaderfilter>
