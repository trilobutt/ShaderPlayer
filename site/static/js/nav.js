// nav.js -- the contents rail's mobile drawer toggle, and nothing else.
//
// On wide viewports the rail is a static sticky column and this script never
// changes anything: the button it drives is display:none there. Below the
// 1024px breakpoint the same markup becomes a drawer, and the open state is
// carried on the button's aria-expanded as well as the panel's class, so a
// screen reader hears what the animation shows.

(function () {
  var toggle = document.getElementById("nav-toggle");
  var sidebar = document.getElementById("site-nav");
  var scrim = document.getElementById("nav-scrim");
  if (!toggle || !sidebar) return;

  function isOpen() {
    return sidebar.classList.contains("is-open");
  }

  function setOpen(open) {
    sidebar.classList.toggle("is-open", open);
    toggle.setAttribute("aria-expanded", open ? "true" : "false");
    if (scrim) scrim.hidden = !open;
    if (open) {
      var first = sidebar.querySelector("a, summary");
      if (first) first.focus();
    }
  }

  toggle.addEventListener("click", function () {
    setOpen(!isOpen());
  });

  if (scrim) {
    scrim.addEventListener("click", function () {
      setOpen(false);
      toggle.focus();
    });
  }

  // Escape closes and returns the caret to the control that opened it.
  document.addEventListener("keydown", function (event) {
    if (event.key === "Escape" && isOpen()) {
      setOpen(false);
      toggle.focus();
    }
  });

  // Following a link inside the drawer navigates away; closing first means the
  // back button does not return to a page with the drawer still over it.
  sidebar.addEventListener("click", function (event) {
    if (event.target.closest("a") && isOpen()) setOpen(false);
  });

  // A drawer left open across a resize would become a stuck panel on the wide
  // layout, where the button that closes it is hidden.
  window.addEventListener("resize", function () {
    if (window.innerWidth > 1023 && isOpen()) setOpen(false);
  });
})();
