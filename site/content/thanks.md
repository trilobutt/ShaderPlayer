---
title: Thank you
nav_title: Thank you
---

<style>
/* The download panel's three states, and nothing else. Every other element on
   this page uses a class site.css already defines; only the state hue, the
   indeterminate bar and the panel's own hairline need rules here, and site.css
   belongs to the documentation surface rather than to this one page. */
#download:empty { display: none; }

#download {
  margin: var(--space-4) 0;
  padding: var(--space-3);
  border: 1px solid var(--panel-border);
  border-top: 2px solid var(--region);
  border-radius: var(--radius-panel);
  background: var(--panel-fill-raised);
  box-shadow: var(--edge-top), var(--lift-1);
}

/* State is carried by hue and by the words above it, so the panel still says
   which state it is in with colour off or unseen. */
#download.is-working { --region: var(--accent-secondary); }
#download.is-ready { --region: var(--accent-tertiary); }
#download.is-failed { --region: var(--state-error); }

.download__state {
  margin: 0 0 var(--space);
  color: var(--region);
  font-size: 1.0625rem;
  font-weight: 700;
}

/* The panel's paragraphs are not direct children of .page, so they take the
   reading measure here rather than from that rule. */
.download__note {
  margin: 0 0 var(--space);
  max-width: var(--measure);
}

.download__note:last-child { margin-bottom: 0; }

.download__actions {
  display: flex;
  flex-wrap: wrap;
  gap: var(--space-2);
  margin-top: var(--space-3);
}

/* An indeterminate sweep rather than a spinner: it shows the verification is
   running without claiming to know how far along it is. */
.download__bar {
  position: relative;
  height: 3px;
  margin-top: var(--space-2);
  overflow: hidden;
  border-radius: 2px;
  background: var(--input-fill);
}

.download__bar::after {
  content: "";
  position: absolute;
  inset: 0 auto 0 0;
  width: 32%;
  border-radius: 2px;
  background: var(--region);
  animation: download-sweep 1.1s var(--ease-standard) infinite;
}

@media (prefers-reduced-motion: reduce) {
  .download__bar::after {
    width: 100%;
    opacity: 0.45;
    animation: none;
  }
}

@keyframes download-sweep {
  from { transform: translateX(-100%); }
  to { transform: translateX(350%); }
}
</style>

# Thank you

Your payment went through and the download starts on its own. It is one installer
executable, about 92 MB, carrying the Visual C++ runtime, FFmpeg and Qt with it, so there
is nothing else to fetch.

<div id="download" data-endpoint="/api/download"></div>

<noscript>
<p class="empty-note">This page verifies the purchase with a small script and JavaScript
is switched off, so the download cannot start here. Email
<a href="mailto:marcsrour@gmail.com">marcsrour@gmail.com</a> with the full address of this
page and I will send the installer.</p>
</noscript>

## Keep this page's address

The address bar carries your Stripe checkout session, and that session is what the
download is verified against. Bookmark it: reloading this page starts the download again,
up to five times. Stripe's receipt email does not carry the session, so a tab closed
without a bookmark means writing to me for a fresh link.

## If the download does not start

The panel above holds a direct link the moment the session verifies, which covers a
browser that blocked the automatic start. A red panel means the session did not verify,
and the two usual reasons are a link truncated in a copy and paste, and a payment Stripe
has not finished settling. Wait a minute and reload. If it still refuses, email
[marcsrour@gmail.com](mailto:marcsrour@gmail.com) quoting the `cs_` session id printed in
the panel, and I will send the installer directly.

## Windows will stop the installer once

The installer is unsigned, so SmartScreen catches it on the first run with **Windows
protected your PC** and a line about an unrecognised publisher. That warning is about the
absence of a code-signing certificate rather than about anything found in the file. Click
**More info**, which reveals the publisher and file name along with a **Run anyway**
button, and the installation proceeds normally.

It installs per user, into `%LOCALAPPDATA%\Programs\ShaderPlayer`, with no administrator
rights and no UAC prompt. [Getting started](/docs/manual/getting-started/) covers the
first launch, including why that one takes 3.7 seconds longer than every launch after it.
