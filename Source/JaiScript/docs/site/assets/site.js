/* JaiScript docs: theme toggle, hand-rolled highlighter, client-side search. No dependencies. */
(function () {
  "use strict";

  /* ---------------- theme toggle ---------------- */
  var root = document.documentElement;
  var toggle = document.getElementById("theme-toggle");
  if (toggle) {
    toggle.addEventListener("click", function () {
      var next = root.getAttribute("data-theme") === "dark" ? "light" : "dark";
      root.setAttribute("data-theme", next);
      try { localStorage.setItem("jai-theme", next); } catch (e) {}
    });
  }

  /* ---------------- tiny tokenizing highlighter ---------------- */
  var JAI_KW = {};
  ("var auto int double float string bool char class function coroutine yield if else while for " +
   "switch case default fallthrough break continue try catch throw return include import namespace " +
   "enum new null true false void this super override operator static private public array map " +
   "shared_ptr weak_ptr skip_newline skip_flush").split(" ").forEach(function (k) { JAI_KW[k] = 1; });
  var CPP_KW = {};
  ("auto int double float bool char class struct const constexpr void return if else while for switch " +
   "case break continue try catch throw new delete nullptr true false namespace using template typename " +
   "static inline virtual override public private protected friend operator enum union sizeof this " +
   "std jai uint8_t int8_t int16_t int32_t int64_t uint32_t uint64_t size_t include define").split(" ")
    .forEach(function (k) { CPP_KW[k] = 1; });
  var JAI_TYPES = {};
  "int double float string bool char void array map shared_ptr weak_ptr auto var".split(" ")
    .forEach(function (k) { JAI_TYPES[k] = 1; });

  function isDigit(c) { return c >= "0" && c <= "9"; }
  function isIdent(c) {
    return (c >= "a" && c <= "z") || (c >= "A" && c <= "Z") || isDigit(c) || c === "_";
  }
  function esc(s) {
    return s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  }

  function highlight(src, kw, types) {
    var out = [], i = 0, n = src.length;
    function push(cls, text) {
      out.push(cls ? '<span class="' + cls + '">' + esc(text) + "</span>" : esc(text));
    }
    while (i < n) {
      var c = src[i];
      // line comment
      if (c === "/" && src[i + 1] === "/") {
        var j = src.indexOf("\n", i); if (j < 0) j = n;
        push("tk-com", src.slice(i, j)); i = j; continue;
      }
      // block comment
      if (c === "/" && src[i + 1] === "*") {
        var j2 = src.indexOf("*/", i + 2); j2 = j2 < 0 ? n : j2 + 2;
        push("tk-com", src.slice(i, j2)); i = j2; continue;
      }
      // string
      if (c === '"') {
        var k = i + 1;
        while (k < n && src[k] !== '"') { if (src[k] === "\\") k++; k++; }
        k = Math.min(k + 1, n);
        push("tk-str", src.slice(i, k)); i = k; continue;
      }
      // char literal
      if (c === "'") {
        var k2 = i + 1;
        while (k2 < n && src[k2] !== "'") { if (src[k2] === "\\") k2++; k2++; }
        k2 = Math.min(k2 + 1, n);
        push("tk-str", src.slice(i, k2)); i = k2; continue;
      }
      // number (hex, binary, decimal w/ exponent)
      if (isDigit(c)) {
        var k3 = i;
        if (c === "0" && (src[i + 1] === "x" || src[i + 1] === "X" || src[i + 1] === "b" || src[i + 1] === "B")) {
          k3 = i + 2;
          while (k3 < n && isIdent(src[k3])) k3++;
        } else {
          while (k3 < n && (isDigit(src[k3]) || src[k3] === ".")) k3++;
          if (src[k3] === "e" || src[k3] === "E") {
            k3++;
            if (src[k3] === "+" || src[k3] === "-") k3++;
            while (k3 < n && isDigit(src[k3])) k3++;
          }
        }
        push("tk-num", src.slice(i, k3)); i = k3; continue;
      }
      // identifier / keyword
      if (isIdent(c) && !isDigit(c)) {
        var k4 = i;
        while (k4 < n && isIdent(src[k4])) k4++;
        var word = src.slice(i, k4);
        if (kw[word]) push(types[word] ? "tk-ty" : "tk-kw", word);
        else if (src[k4] === "(") push("tk-fn", word);
        else push("", word);
        i = k4; continue;
      }
      // everything else: pass through one char
      push("", c); i++;
    }
    return out.join("");
  }

  var blocks = document.querySelectorAll("code.lang-jai, code.lang-cpp");
  for (var b = 0; b < blocks.length; b++) {
    var el = blocks[b];
    var isJai = el.className.indexOf("lang-jai") >= 0;
    el.innerHTML = highlight(el.textContent, isJai ? JAI_KW : CPP_KW, isJai ? JAI_TYPES : {});
  }

  /* ---------------- line-number gutters ---------------- */
  // Every code snippet gets a line gutter; the numbers live outside the selection
  // (user-select: none) so copied code stays clean. Output/capture frames are
  // program results, not snippets — no numbers there. One-liners stay bare.
  var numbered = document.querySelectorAll(
    ".codeblock:not(.cb-output):not(.cb-capture) pre > code");
  for (var n = 0; n < numbered.length; n++) {
    var codeEl = numbered[n];
    var text = codeEl.textContent;
    if (text.charAt(text.length - 1) === "\n") { text = text.slice(0, -1); }
    var count = text.split("\n").length;
    if (count < 2) { continue; }
    var gutter = document.createElement("span");
    gutter.className = "cb-gutter";
    gutter.setAttribute("aria-hidden", "true");
    var nums = "";
    for (var ln = 1; ln <= count; ln++) { nums += ln + "\n"; }
    gutter.textContent = nums;
    var pre = codeEl.parentNode;
    pre.classList.add("cb-numbered");
    pre.insertBefore(gutter, codeEl);
  }

  /* ---------------- search ---------------- */
  var box = document.getElementById("search-box");
  var results = document.getElementById("search-results");
  var index = window.SEARCH_INDEX || [];
  // Compute path prefix back to site root from the stylesheet href.
  var cssHref = (document.querySelector('link[rel="stylesheet"]') || {}).getAttribute
    ? document.querySelector('link[rel="stylesheet"]').getAttribute("href") : "assets/style.css";
  var rootPrefix = cssHref.replace(/assets\/style\.css$/, "");
  var sel = -1;

  function render(items) {
    if (!items.length) {
      results.innerHTML = '<div class="sr-none">no matches</div>';
      results.hidden = false; sel = -1; return;
    }
    var htmlOut = "";
    for (var i = 0; i < items.length; i++) {
      var it = items[i];
      htmlOut += '<a href="' + rootPrefix + it.u + (it.a ? "#" + it.a : "") + '">' +
        '<div class="sr-title">' + esc(it.label) + "</div>" +
        '<div class="sr-ctx">' + esc(it.ctx) + "</div></a>";
    }
    results.innerHTML = htmlOut; results.hidden = false; sel = -1;
  }

  function search(q) {
    q = q.toLowerCase().trim();
    if (!q) { results.hidden = true; return; }
    var hits = [];
    for (var i = 0; i < index.length && hits.length < 12; i++) {
      var e = index[i];
      if (e.t.toLowerCase().indexOf(q) >= 0) {
        hits.push({ u: e.u, label: e.t, ctx: e.s });
        continue;
      }
      for (var h = 0; h < e.h.length; h++) {
        if (e.h[h].toLowerCase().indexOf(q) >= 0) {
          hits.push({ u: e.u, label: e.h[h], ctx: e.s + " › " + e.t,
                      a: e.h[h].toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "") });
          break;
        }
      }
    }
    render(hits);
  }

  if (box && results) {
    box.addEventListener("input", function () { search(box.value); });
    box.addEventListener("keydown", function (ev) {
      var links = results.querySelectorAll("a");
      if (ev.key === "ArrowDown" || ev.key === "ArrowUp") {
        ev.preventDefault();
        if (!links.length) return;
        sel = ev.key === "ArrowDown" ? Math.min(sel + 1, links.length - 1) : Math.max(sel - 1, 0);
        for (var i = 0; i < links.length; i++) links[i].className = i === sel ? "sel" : "";
        links[sel].scrollIntoView({ block: "nearest" });
      } else if (ev.key === "Enter") {
        if (links.length) { window.location.href = links[Math.max(sel, 0)].getAttribute("href"); }
      } else if (ev.key === "Escape") {
        results.hidden = true; box.blur();
      }
    });
    document.addEventListener("click", function (ev) {
      if (!results.contains(ev.target) && ev.target !== box) results.hidden = true;
    });
  }
}());
