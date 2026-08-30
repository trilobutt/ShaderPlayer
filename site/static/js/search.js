// search.js -- client-side search over /search-index.json, loaded only on
// the search page (see templates/search.html). Each entry has the shape
// {title, url, text, section}; see shader_page_context and collect_sources
// in build.py for how the index is produced.
//
// The header search box on every other page is a plain GET form pointing
// at /search/, so a query typed there arrives here in the URL and this
// script runs it immediately on load.

function normalize(str) {
  return str.toLowerCase();
}

function score(entry, terms) {
  var title = normalize(entry.title);
  var section = normalize(entry.section);
  var text = normalize(entry.text);
  var total = 0;

  for (var i = 0; i < terms.length; i++) {
    var term = terms[i];
    var matched = false;
    if (title.indexOf(term) !== -1) {
      total += 5;
      matched = true;
    }
    if (section.indexOf(term) !== -1) {
      total += 2;
      matched = true;
    }
    if (text.indexOf(term) !== -1) {
      total += 1;
      matched = true;
    }
    if (!matched) return 0; // multi-term queries require every term to hit somewhere
  }
  return total;
}

function slug(str) {
  return str.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "");
}

function buildResult(entry) {
  var item = document.createElement("li");
  item.className = "search-result search-result--" + slug(entry.section);

  var link = document.createElement("a");
  link.href = entry.url;
  link.textContent = entry.title;
  item.appendChild(link);

  var section = document.createElement("span");
  section.className = "search-result__section";
  section.textContent = entry.section;
  item.appendChild(section);

  return item;
}

function renderResults(resultsEl, countEl, entries, query) {
  resultsEl.textContent = "";

  if (entries.length === 0) {
    countEl.textContent = "no results";

    var empty = document.createElement("div");
    empty.className = "search__empty";

    var mark = document.createElement("span");
    mark.className = "search__empty-mark";
    mark.setAttribute("aria-hidden", "true");
    empty.appendChild(mark);

    var line = document.createElement("p");
    line.className = "search__empty-line";
    line.textContent = "Nothing here matches “" + query + "”.";
    empty.appendChild(line);

    var hint = document.createElement("p");
    hint.className = "search__empty-hint";
    hint.appendChild(document.createTextNode("Try fewer words, or a shader's name. "));
    var back = document.createElement("a");
    back.href = "/";
    back.textContent = "Browse everything instead";
    hint.appendChild(back);
    hint.appendChild(document.createTextNode("."));
    empty.appendChild(hint);

    resultsEl.appendChild(empty);
    return;
  }

  countEl.textContent = entries.length + (entries.length === 1 ? " result" : " results");
  var list = document.createElement("ul");
  list.className = "search-results-list";
  entries.forEach(function (entry) {
    list.appendChild(buildResult(entry));
  });
  resultsEl.appendChild(list);
}

function init() {
  var input = document.getElementById("site-search-input");
  var countEl = document.getElementById("search-count");
  var resultsEl = document.getElementById("search-results");
  if (!input || !countEl || !resultsEl) return;

  var initialQuery = new URLSearchParams(window.location.search).get("q");
  if (initialQuery && !input.value) {
    input.value = initialQuery;
  }

  var indexPromise = null;
  function loadIndex() {
    if (!indexPromise) {
      indexPromise = fetch("/search-index.json").then(function (response) {
        return response.json();
      });
    }
    return indexPromise;
  }

  var debounceTimer = null;

  function runSearch() {
    var query = input.value.trim();
    if (query === "") {
      resultsEl.textContent = "";
      countEl.textContent = "";
      return;
    }

    var terms = normalize(query).split(/\s+/).filter(Boolean);

    loadIndex().then(function (entries) {
      var scored = entries
        .map(function (entry) {
          return { entry: entry, points: score(entry, terms) };
        })
        .filter(function (result) {
          return result.points > 0;
        })
        .sort(function (a, b) {
          return b.points - a.points;
        })
        .map(function (result) {
          return result.entry;
        });

      renderResults(resultsEl, countEl, scored, query);
    });
  }

  input.addEventListener("input", function () {
    window.clearTimeout(debounceTimer);
    debounceTimer = window.setTimeout(runSearch, 120);
  });

  var form = input.closest("form");
  if (form) {
    form.addEventListener("submit", function (event) {
      event.preventDefault();
      runSearch();
    });
  }

  if (input.value.trim() !== "") {
    runSearch();
  }
}

if (typeof document !== "undefined") {
  init();
}
