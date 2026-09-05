// download.js -- fills #download on /thanks/ with the state of the buyer's
// download, loaded only on that page (see templates/page.html).
//
// The endpoint (site/api/download.py) verifies the Stripe checkout session and
// answers 200 with {"url": "/api/download/file?session_id=..."} or 403 with a
// deliberately generic body. Two rules follow from that:
//
//   * The session id is validated here against the same pattern the server
//     uses, so a mangled address renders the failure state without spending a
//     request or handing the API something to reject.
//   * The url in the response is only ever assigned to location.href after it
//     resolves to the endpoint's own origin. It is our own API today, and an
//     unchecked redirect target read out of a response body stops being safe
//     the moment anything upstream of it is wrong.
//
// Every state names what happened in words. The bar is decoration on top of a
// sentence, never the only thing saying the page is working.

(function () {
  var SESSION_ID_RE = /^cs_[A-Za-z0-9_]{10,200}$/;
  var SUPPORT_EMAIL = "marcsrour@gmail.com";

  var host = document.getElementById("download");
  if (!host) return;

  var endpoint = host.getAttribute("data-endpoint") || "/api/download";

  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text) node.textContent = text;
    return node;
  }

  function render(state, heading, nodes) {
    host.className = "is-" + state;
    host.textContent = "";
    host.appendChild(el("p", "download__state", heading));
    nodes.forEach(function (node) {
      host.appendChild(node);
    });
  }

  function note() {
    var paragraph = el("p", "download__note");
    for (var i = 0; i < arguments.length; i++) {
      var part = arguments[i];
      paragraph.appendChild(
        typeof part === "string" ? document.createTextNode(part) : part
      );
    }
    return paragraph;
  }

  function link(href, className, text) {
    var anchor = el("a", className, text);
    anchor.href = href;
    return anchor;
  }

  function working() {
    render("working", "Checking your purchase", [
      note("This takes a second. The download starts by itself once Stripe confirms the payment."),
      el("div", "download__bar")
    ]);
  }

  function ready(target) {
    var actions = el("div", "download__actions");
    var button = link(target, "button button--primary", "Download the installer");
    button.setAttribute("download", "");
    actions.appendChild(button);
    actions.appendChild(link("/docs/manual/getting-started/", "button", "Read Getting Started"));

    render("ready", "Your download is starting", [
      note("Nothing to do. If the browser blocked it, the button below fetches the same file."),
      actions
    ]);
  }

  // retryable is false for the two failures a reload cannot change: an address
  // with no session on it, and one whose session is malformed. A button that
  // reruns a check with the same inputs is a button that lies.
  function failed(sessionId, reason, retryable) {
    var body = [note(reason)];

    if (sessionId) {
      body.push(
        note(
          "Email ",
          link("mailto:" + SUPPORT_EMAIL, null, SUPPORT_EMAIL),
          " quoting checkout session ",
          el("code", null, sessionId),
          " and I will send the installer directly."
        )
      );
    } else {
      body.push(
        note(
          "Email ",
          link("mailto:" + SUPPORT_EMAIL, null, SUPPORT_EMAIL),
          " with your Stripe receipt and I will send the installer directly."
        )
      );
    }

    if (retryable) {
      var actions = el("div", "download__actions");
      var retry = link(window.location.href, "button", "Try again");
      retry.addEventListener("click", function (event) {
        event.preventDefault();
        window.location.reload();
      });
      actions.appendChild(retry);
      body.push(actions);
    }

    render("failed", "The download could not be verified", body);
  }

  // Resolve the response's url against the endpoint rather than against the
  // page, so an API served from its own subdomain still works, and refuse
  // anything that lands on a different origin than the endpoint itself.
  function sameOriginUrl(raw) {
    if (typeof raw !== "string" || raw === "") return null;
    try {
      var base = new URL(endpoint, window.location.href);
      var target = new URL(raw, base);
      return target.origin === base.origin ? target.href : null;
    } catch (err) {
      return null;
    }
  }

  var sessionId = new URLSearchParams(window.location.search).get("session_id");

  if (!sessionId) {
    failed(
      null,
      "This page's address carries no checkout session, so there is nothing to verify. Open the link Stripe returned after payment.",
      false
    );
    return;
  }

  if (!SESSION_ID_RE.test(sessionId)) {
    failed(
      null,
      "The checkout session in this page's address is malformed, which usually means the link was truncated in a copy and paste. Open it again from Stripe's confirmation.",
      false
    );
    return;
  }

  working();

  fetch(endpoint + "?session_id=" + encodeURIComponent(sessionId))
    .then(function (response) {
      if (!response.ok) throw new Error("HTTP " + response.status);
      return response.json();
    })
    .then(function (payload) {
      var target = sameOriginUrl(payload && payload.url);
      if (!target) throw new Error("no usable url in the response");
      // Paint the manual link first: the response is an attachment, so the
      // page stays put and the buyer keeps a link they can press again.
      ready(target);
      window.location.href = target;
    })
    .catch(function () {
      failed(
        sessionId,
        "Stripe has not confirmed this session, or the download limit on it has been reached. Waiting a minute and reloading clears the first of those.",
        true
      );
    });
})();
