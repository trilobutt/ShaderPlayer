# ShaderPlayer download API

A small standard-library-only WSGI app that gates the installer download
behind a paid Stripe Checkout session. It is a **second, separate**
Opalstack app from the static site: `site/deploy.sh` knows nothing about it
and must not be changed to.

Files:

- `download.py` — the application. All configuration is read from the
  environment once, at import time.
- `wsgi.py` — the uWSGI entry point (`wsgi:application`); keep it to its one
  import line.
- `test_download.py` — `unittest`, no network.

## Creating the Stripe Payment Link

The Buy buttons point at a Stripe Payment Link, never at this app. Create one in the
Stripe dashboard for a one-off $20 purchase of ShaderPlayer and set its success URL to:

```
https://shaderplayer.marcsplained.com/thanks/?session_id={CHECKOUT_SESSION_ID}
```

The `{CHECKOUT_SESSION_ID}` placeholder is Stripe's own and is substituted on redirect;
it is what `/thanks/` reads out of the address and hands to this endpoint. The link's URL
is never committed: it reaches the templates as `site/build.py --payment-link`, defaulting
to `$SHADERPLAYER_PAYMENT_LINK`, and an indexable build (`--no-noindex`) refuses to carry
the placeholder. The `price_...` id that link sells is the `SHADERPLAYER_PRICE_ID` below;
the endpoint requires a line item matching it, so a session bought through some other link
is refused.

## Creating the Opalstack app

1. In the Opalstack control panel, create a new **app** of type "Uwsgi
   app" (Python), separate from the static site app. Opalstack provisions
   it under its own directory, typically `~/apps/<app_name>/`, with a venv
   and a generated `uwsgi.ini`.
2. Confirm the exact layout Opalstack has provisioned before proceeding —
   the directory name and the generated `uwsgi.ini`'s default `wsgi-file`
   value vary by Opalstack's current app template, and the steps below
   assume the common one.
3. Copy `download.py` and `wsgi.py` from this directory into the app's
   directory (there is no source control on the server; this repo is the
   source of truth, the server just runs a copy).
4. Edit the app's `uwsgi.ini` so it loads our entry point:

   ```ini
   wsgi-file = wsgi.py
   callable = application
   ```

5. In the Opalstack control panel, under Sites, attach this app to
   `shaderplayer.marcsplained.com` at the URI path `/api/`, leaving the static
   site app attached at `/`. Both apps then serve the one domain, which is what
   lets the `/thanks/` page reach the endpoint with a same-origin `fetch` and
   no CORS. A separate subdomain would work too, but only after
   `data-endpoint` in `site/content/thanks.md` is repointed at its absolute
   URL and the app is given CORS headers, so the path mount is the shape the
   site is written for.

## Environment variables

`download.py` requires `STRIPE_SECRET_KEY`, `SHADERPLAYER_PRICE_ID`,
`SHADERPLAYER_RELEASE_FILE` and `SHADERPLAYER_DOWNLOAD_DB`, and reads
`SHADERPLAYER_MAX_DOWNLOADS` (default `5`). Every one of these is read once
at process start, so a missing or malformed value fails the app at startup
with a message naming the variable, rather than on the first request.

Opalstack's uWSGI apps take environment variables as `env = KEY=value`
lines directly in the app's own `uwsgi.ini` — a file that lives on the
server, outside this repository, and is therefore never committed. Populate
it by hand:

1. Open `~/.abl/credentials.env` on your own machine and copy the relevant
   values across by hand (`STRIPE_SECRET_KEY`, and the Stripe price id
   already used to build the Checkout Session on the product page). **Never
   `cat`, print, or paste the whole credentials file anywhere** — copy only
   the two values this app needs.
2. Add them to the app's `uwsgi.ini` on the server, for example:

   ```ini
   env = STRIPE_SECRET_KEY=sk_live_...
   env = SHADERPLAYER_PRICE_ID=price_...
   env = SHADERPLAYER_RELEASE_FILE=/home/<opalstack-user>/releases/ShaderPlayer-1.0.0-setup.exe
   env = SHADERPLAYER_DOWNLOAD_DB=/home/<opalstack-user>/apps/<app_name>/downloads.sqlite3
   env = SHADERPLAYER_MAX_DOWNLOADS=5
   ```

3. Restart the app for the new environment to take effect (Opalstack's
   uWSGI apps reload on `touch ~/apps/<app_name>/tmp/restart.txt`; if that
   path does not exist for the provisioned app, use the restart action in
   the control panel instead).

`SHADERPLAYER_RELEASE_FILE` must be an absolute path **outside any web
root** — nothing under a directory a web server would serve directly — so
the installer is reachable only through the verified endpoint below, never
by guessing its URL. `SHADERPLAYER_DOWNLOAD_DB` must likewise be an
absolute path; anywhere writable by the app's own user is fine, since the
SQLite file holds nothing beyond a session id, a first-seen timestamp and a
count.

## Placing the installer

Upload the built installer (see the release packaging project) to the path
named by `SHADERPLAYER_RELEASE_FILE`, e.g.:

```
scp dist/ShaderPlayer-1.0.0-setup.exe <opalstack-user>@<host>:releases/
```

The basename of `SHADERPLAYER_RELEASE_FILE` is what the buyer's browser saves
the file as (it becomes the `Content-Disposition` filename), so upload it under
the name it was built with rather than renaming it on the way.

Replacing the file in place (uploading a new one to the same path) takes
effect on the next request; the app does not cache it.

A new installer also changes what the site claims a buyer gets, and nothing
checks that for you. Two files state it and both need the same pass as the
upload: the version and size line in the pricing section of
`site/content/index.md`, and the size in the lede of `site/content/thanks.md`.
The version comes from `project(ShaderPlayer VERSION ...)` in `CMakeLists.txt`,
and the size is `[math]::Round((Get-Item
dist/ShaderPlayer-<version>-setup.exe).Length / 1MB)` MB, rounded the same way
the pages state it. Then rebuild and deploy the site, or they keep advertising
the previous release.

## Exercising the endpoint

Create a Stripe **test-mode** Checkout Session for the configured price
(e.g. via the Stripe dashboard's test mode, or `curl` against
`https://api.stripe.com/v1/checkout/sessions` with your test secret key),
complete it with a Stripe test card, then take the resulting session id
(`cs_test_...`) and call the API directly:

```
curl -i "https://shaderplayer.marcsplained.com/api/download?session_id=cs_test_XXXXXXXXXXXXXXXXXXXX"
```

A verified, paid session returns `200` with a JSON body:

```json
{"url": "/api/download/file?session_id=cs_test_XXXXXXXXXXXXXXXXXXXX"}
```

Follow that with:

```
curl -i "https://shaderplayer.marcsplained.com/api/download/file?session_id=cs_test_XXXXXXXXXXXXXXXXXXXX" \
  -O -J
```

which streams the installer and increments that session's download count.
Repeating it past `SHADERPLAYER_MAX_DOWNLOADS` returns `403`. An
unrecognised, incomplete, unpaid, or wrong-price session also returns `403`
with a generic body; the real reason is written to the app's stderr log
(the session id and a short reason only — never the secret key, never
Stripe's own error body).
