/* Landing-page hot-reload demo: an edit to gator.jai re-runs it once JaiScript's lexer and checker rules accept it (the
   error texts are the engine's own), and the file's reload hook copies the new spd onto the live gator. */
(function () {
  "use strict";
  var demo = document.getElementById("hero-demo");
  if (!demo) return;
  var code = demo.querySelector("pre");
  var spdIn = document.getElementById("demo-spd");
  var sayIn = document.getElementById("demo-say");
  var hookEl = document.getElementById("demo-hook");
  var status = document.getElementById("demo-status");
  var label = document.getElementById("demo-label");
  var checkEl = document.getElementById("demo-check");
  var live = document.getElementById("demo-live");
  var cv = document.getElementById("demo-scene");
  var g = cv.getContext && cv.getContext("2d");
  if (!g || !code || !spdIn || !sayIn) return;

  var reduce = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)");
  var still = !!(reduce && reduce.matches);
  var TICK = 1000 / 60, START = 30, DEBOUNCE = 400;
  var gator = { spd: 1, slices: 0, x: START, says: "" };
  var applied = { spd: spdIn.value, say: sayIn.value };
  var frameNo = 0, nom = 0, lastTs = 0, acc = 0;
  var visible = true, running = false, statusTimer = 0, editTimer = 0, editsItself = true;
  var colors = {};
  var GATOR = "#3fae5a", GATOR_DARK = "#2a7a3d", BELLY = "#c9e7a1";

  // Names the shown program, the standard library or the keywords declare: what one would do here is not the demo's to say.
  var DECLARED = ("spd slices gator cake Gator Mob tick say step_toward beside hot_reload_migrate print sleep to_string " +
    "format read_file write_file math random time reflect crypto binary class const continue else float for function if " +
    "int map array namespace nullptr override private public return static string this void while auto var let super " +
    "weak_ptr shared_ptr try catch throw switch case default fallthrough include import coroutine yield enum new " +
    "protected parallel_for nocopy cast extern inline true false null").split(" ");
  var NAMESPACES = ["math", "time", "reflect", "crypto", "binary", "string"];
  var ESCAPES = { n: "\n", r: "\r", t: "\t", "\\": "\\", '"': '"', "'": "'" };

  function isDigit(c) { return c >= "0" && c <= "9"; }
  function hexValue(c) { return c ? "0123456789abcdef".indexOf(c.toLowerCase()) : -1; }
  function isWordStart(c) { return !!c && /[A-Za-z_]/.test(c); }
  function startsToken(c) { return !!c && /[\w"'`]/.test(c); }
  function skipSpace(t, i) { while (i < t.length && /\s/.test(t[i])) i++; return i; }

  // The lexer's scan_string, run over the rest of the line: an unclosed quote runs on to the line's end.
  function lexString(t, i) {
    var value = "";
    for (var j = i + 1; j < t.length; j++) {
      var c = t[j];
      if (c === '"') return { end: j + 1, value: value };
      if (c !== "\\") { value += c; continue; }
      if (++j >= t.length) return { err: "unterminated string literal" };
      var e = t[j];
      if (ESCAPES.hasOwnProperty(e)) { value += ESCAPES[e]; continue; }
      if (e !== "x") return { err: "invalid escape sequence" };
      if (j + 1 >= t.length) return { err: "unterminated string literal" };
      var hi = hexValue(t[j + 1]), lo = hexValue(t[j + 2]);
      if (hi < 0 || lo < 0) return { err: "invalid \\x escape: expected two hex digits" };
      value += String.fromCharCode(hi * 16 + lo);
      j += 2;
    }
    return { err: "unterminated string literal" };
  }

  // The lexer's scan_number: 0x and 0b prefixes, C octal, C++'s fraction (1.5, 1., 1.e5; a '.' before a name stays a member
  // access), an exponent that needs digits.
  function lexNumber(t, i) {
    var j = i, isFloat = false, isHex = false, isBinary = false;
    if (t[j] === "0") {
      j++;
      if (t[j] === "x" || t[j] === "X") { isHex = true; j++; while (hexValue(t[j]) >= 0) j++; }
      else if (t[j] === "b" || t[j] === "B") { isBinary = true; j++; while (t[j] === "0" || t[j] === "1") j++; }
      else while (isDigit(t[j])) j++;
    } else {
      while (isDigit(t[j])) j++;
    }
    if (!isHex && !isBinary) {
      if (t[j] === "." && t[j + 1] !== "." && (!isWordStart(t[j + 1]) || t[j + 1] === "e" || t[j + 1] === "E")) {
        isFloat = true; j++; while (isDigit(t[j])) j++;
      }
      if (t[j] === "e" || t[j] === "E") {
        isFloat = true; j++;
        if (t[j] === "+" || t[j] === "-") j++;
        if (!isDigit(t[j])) return { err: "invalid number literal: exponent has no digits in '" + t.slice(i, j) + "'" };
        while (isDigit(t[j])) j++;
      }
    }
    var lexeme = t.slice(i, j), value;
    if (isFloat) {
      value = Number(lexeme);
      if (!isFinite(value) || (value === 0 && /[1-9]/.test(lexeme.split(/[eE]/)[0]))) {
        return { err: "number literal out of range: '" + lexeme + "'" };
      }
    } else if (isHex || isBinary) {
      if (lexeme.length < 3) return { err: "invalid number literal: '" + lexeme + "'" };
      value = parseInt(lexeme.slice(2), isHex ? 16 : 2);
    } else if (lexeme.length > 1 && lexeme[0] === "0") {
      if (/[89]/.test(lexeme)) return { err: "invalid octal literal (digits 8/9 not allowed): '" + lexeme + "'" };
      value = parseInt(lexeme, 8);
    } else {
      value = parseInt(lexeme, 10);
    }
    return { end: j, value: value, isFloat: isFloat };
  }

  // Where an input sits in the shown file: its 1-based line and column, and the text of that line around it.
  function locate(input) {
    var before = "", after = "", found = false;
    (function walk(node) {
      for (var n = node.firstChild; n; n = n.nextSibling) {
        if (n === input) { found = true; continue; }
        var text = n.nodeType === 3 ? n.nodeValue : n.tagName === "INPUT" ? n.value : null;
        if (text === null) { walk(n); continue; }
        if (found) after += text; else before += text;
      }
    }(code));
    var lines = before.split("\n"), prefix = lines[lines.length - 1];
    return { input: input, line: lines.length, col: prefix.length + 1, prefix: prefix, suffix: after.split("\n")[0] };
  }

  function diag(stage, at, offset, message) { return { stage: stage, at: at, col: at.col + offset, message: message }; }

  // int spd = <text>;  the int it initializes, or the first diagnostic the engine gives.
  function checkSpd() {
    var t = spdIn.value, at = locate(spdIn), field = at.prefix.indexOf("spd") + 1 - at.col;
    var demoNote = diag("demo", at, 0, "this demo applies a literal here: a number, true or false");
    var i = skipSpace(t, 0), sign = 1, token, word;
    if (i === t.length) return diag("parse", at, t.length, "expected expression");
    if (t[i] === "+" || t[i] === "-") {
      if (t[i + 1] === t[i]) return demoNote;
      sign = t[i] === "-" ? -1 : 1;
      var op = i;
      i = skipSpace(t, i + 1);
      var operand = /^\w+/.exec(t.slice(i));
      if (operand && (operand[0] === "true" || operand[0] === "false") && skipSpace(t, i + operand[0].length) === t.length) {
        return diag("check", at, op, sign < 0 ? "Unary minus requires numeric operand" : "Unary plus requires numeric operand");
      }
      if (!isDigit(t[i])) return demoNote;
    }
    var start = i;
    if (isDigit(t[i])) {
      token = lexNumber(t, i);
    } else if (t[i] === '"') {
      token = lexString(t + at.suffix, i);
      token.check = "cannot initialize field 'spd' declared 'int' with 'string'";
    } else if (/^'[^'\\]'/.test(t.slice(i))) {
      token = { end: i + 3, check: "cannot initialize field 'spd' declared 'int' with 'char'" };
    } else if (isWordStart(t[i])) {
      word = /^\w+/.exec(t.slice(i))[0];
      token = { end: i + word.length, value: word === "true" ? 1 : 0 };
      if (word === "null") token.check = "cannot initialize field 'spd' declared 'int' with 'null'";
      else if (word === "random") token.check = "cannot initialize field 'spd' declared 'int' with 'class random'";
      else if (NAMESPACES.indexOf(word) >= 0) token.atStart = "'" + word + "' is a namespace, not a value; name one of its members (" + word + "::name)";
      else if (word === "spd" || word === "slices") {
        token.atStart = "field initializer of 'spd' reads '" + word + "' before its initialization: fields initialize in declaration order" +
          (word === "spd" ? "" : "; declare 'slices' above 'spd'");
      } else if (word !== "true" && word !== "false") token[DECLARED.indexOf(word) >= 0 ? "demo" : "undeclared"] = word;
    } else {
      return demoNote;
    }
    if (token.err) return diag("parse", at, i, token.err);
    var j = skipSpace(t, token.end);
    if (j < t.length) {
      if (startsToken(t[j])) return diag("parse", at, j, "expected ';' after field declaration");
      if (t[j] !== "." || !isDigit(t[start])) return demoNote;
      var k = skipSpace(t, j + 1);
      return isWordStart(t[k]) ? demoNote : diag("parse", at, k, "expected member name");
    }
    if (token.demo) return demoNote;
    if (token.undeclared) return diag("check", at, start, "use of undeclared identifier '" + token.undeclared + "'");
    if (token.atStart) return diag("check", at, start, token.atStart);
    if (token.check) return diag("check", at, field, token.check);
    var value = sign * token.value;
    if (token.isFloat && !(Math.abs(value) < 9223372036854775808)) {
      return diag("check", at, field, "cannot initialize field 'spd' declared 'int' with 'float': float literal is outside the int range");
    }
    return { value: Math.trunc(value) || 0 };
  }

  // say(<text>);  the string the gator says, or the first diagnostic.
  function checkSay() {
    var t = sayIn.value, at = locate(sayIn);
    var demoNote = diag("demo", at, 0, 'this demo applies a string literal here: keep the "quotes"');
    var i = skipSpace(t, 0), token, word;
    if (t[i] === '"') {
      token = lexString(t + at.suffix, i);
    } else if (isDigit(t[i])) {
      token = lexNumber(t, i);
      token.demo = true;
    } else if (isWordStart(t[i])) {
      word = /^\w+/.exec(t.slice(i))[0];
      token = { end: i + word.length };
      token[DECLARED.indexOf(word) >= 0 ? "demo" : "undeclared"] = word;
    } else {
      return demoNote;
    }
    if (token.err) return diag("parse", at, i, token.err);
    var j = skipSpace(t, token.end);
    if (j < t.length) return startsToken(t[j]) ? diag("parse", at, j, "expected ')' after arguments") : demoNote;
    if (token.demo) return demoNote;
    if (token.undeclared) return diag("check", at, i, "use of undeclared identifier '" + token.undeclared + "'");
    return { value: token.value };
  }

  // A parse error reports before any check error, and the demo's own notes come last.
  function firstProblem(results) {
    var stages = ["parse", "check", "demo"];
    for (var s = 0; s < stages.length; s++) {
      for (var r = 0; r < results.length; r++) {
        if (results[r].stage === stages[s]) return results[r];
      }
    }
    return null;
  }

  function problemText(problem) {
    if (problem.stage === "demo") return problem.message;
    return "gator.jai:" + problem.at.line + ":" + problem.col + ": error: " + problem.message;
  }

  function showProblem(problem) {
    spdIn.removeAttribute("aria-invalid");
    sayIn.removeAttribute("aria-invalid");
    checkEl.textContent = "";
    checkEl.className = "demo-check";
    checkEl.hidden = !problem;
    if (!problem) return;
    var msg = document.createElement("div");
    msg.textContent = problemText(problem);
    checkEl.appendChild(msg);
    if (problem.stage === "demo") {
      checkEl.className += " note";
      return;
    }
    problem.at.input.setAttribute("aria-invalid", "true");
    var src = document.createElement("div");
    src.className = "src";
    src.setAttribute("aria-hidden", "true");
    var line = "    " + problem.at.prefix + problem.at.input.value + problem.at.suffix;
    src.textContent = line + "\n" + new Array(problem.col + 4).join(" ") + "^";
    checkEl.appendChild(src);
    src.scrollLeft = (problem.col + 3) / line.length * src.scrollWidth - src.clientWidth / 2;
  }

  function readColors() {
    var cs = getComputedStyle(document.documentElement);
    function token(name, fallback) { return cs.getPropertyValue(name).trim() || fallback; }
    colors.ground = token("--border", "#252b3d");
    colors.bubble = token("--panel", "#141826");
    colors.bubbleEdge = token("--border-strong", "#3d4560");
    colors.words = token("--head", "#f1f3f8");
  }

  function goal() { return cv.width - 150; }

  function drawGator(px, py) {
    var leg = (((px - START) / 20 | 0) % 2) * 3;
    g.fillStyle = GATOR_DARK;
    g.fillRect(px - 34, py + 10, 14, 6); g.fillRect(px - 44, py + 12, 12, 4); g.fillRect(px - 52, py + 13, 10, 3);
    g.fillStyle = GATOR; g.fillRect(px - 22, py, 44, 18);
    g.fillStyle = BELLY; g.fillRect(px - 18, py + 13, 36, 5);
    g.fillStyle = GATOR_DARK; for (var b = 0; b < 4; b++) g.fillRect(px - 16 + b * 10, py - 4, 6, 4);
    g.fillStyle = GATOR; g.fillRect(px + 18, py + 2, 30, 10);
    g.fillStyle = GATOR_DARK; g.fillRect(px + 18, py + 10, 30, 3);
    g.fillStyle = "#ffffff"; for (var t = 0; t < 4; t++) g.fillRect(px + 22 + t * 7, py + 9, 3, 3);
    g.fillStyle = GATOR; g.fillRect(px + 14, py - 6, 10, 8);
    g.fillStyle = "#f5f2e8"; g.fillRect(px + 18, py - 4, 4, 4);
    g.fillStyle = "#111111"; g.fillRect(px + 20, py - 3, 2, 2);
    g.fillStyle = GATOR_DARK;
    g.fillRect(px - 16, py + 18, 6, 6 - leg); g.fillRect(px + 8, py + 18, 6, 3 + leg);
  }

  function cake(cx, cy, eaten) {
    var flick = (frameNo >> 3) % 2, wdt = 44 - eaten;
    g.fillStyle = "#8b5a2b"; g.fillRect(cx + eaten, cy + 14, wdt, 16);
    g.fillStyle = "#f6d3e0"; g.fillRect(cx + eaten, cy + 6, wdt, 10);
    g.fillStyle = "#ffffff"; for (var d = eaten; d < 44; d += 8) g.fillRect(cx + d, cy + 15, 5, 4);
    g.fillStyle = "#e94b6a"; g.fillRect(cx + 30, cy - 8, 4, 14);
    g.fillStyle = flick ? "#ffd84a" : "#ff9f1a"; g.fillRect(cx + 29, cy - 15 - flick, 6, 7);
  }

  function bubble(headX, headY, words) {
    var text = words.replace(/[\x00-\x1f\x7f]+/g, " ").trim();
    if (!text) return;
    g.font = '600 15px "JetBrains Mono", Consolas, monospace';
    var w = Math.ceil(g.measureText(text).width) + 16, h = 24;
    var x = Math.max(4, Math.min(cv.width - w - 4, headX - 12)), y = headY - h - 12;
    var tail = Math.max(x + 6, Math.min(x + w - 14, headX - 4));
    g.fillStyle = colors.bubble; g.strokeStyle = colors.bubbleEdge; g.lineWidth = 2;
    g.beginPath(); g.moveTo(tail, y + h); g.lineTo(tail + 8, y + h); g.lineTo(tail + 2, y + h + 8); g.closePath();
    g.fill(); g.stroke();
    g.fillRect(x, y, w, h); g.strokeRect(x, y, w, h);
    g.fillRect(tail + 1, y + h - 2, 6, 3);
    g.fillStyle = colors.words; g.textBaseline = "middle";
    g.fillText(text, x + 8, y + h / 2 + 1);
  }

  function setLabel() {
    var mood = gator.spd <= 0 ? " (standing still)" : gator.spd > 1 ? " (cake rush)" : "";
    label.textContent = "gator at spd " + gator.spd + " · slices " + gator.slices + mood;
  }

  // A still frame (reduced motion) shows the gator at the cake, saying its line.
  function draw() {
    var w = cv.width, x = still ? goal() : gator.x;
    g.clearRect(0, 0, w, cv.height);
    g.fillStyle = colors.ground;
    for (var i = 0; i < w; i += 32) g.fillRect(i, 118, 28, 6);
    cake(w - 96, 88, Math.min(18, gator.slices * 6));
    drawGator(x, 92);
    if (nom > 0 || still) bubble(x + 18, 86, gator.says);
  }

  function tick() {
    frameNo++;
    if (nom > 0) {
      if (--nom === 0) {
        gator.x = START;
        if (gator.slices >= 3) gator.slices = 0;
        setLabel();
        if (editsItself) editByItself();
      }
      return;
    }
    gator.x = Math.max(START, Math.min(goal(), gator.x + gator.spd));
    if (gator.x >= goal()) { gator.slices++; nom = 50; setLabel(); }
  }

  function frame(ts) {
    if (!running) { lastTs = 0; return; }
    acc += lastTs ? Math.min(ts - lastTs, 250) : TICK;
    lastTs = ts;
    for (; acc >= TICK; acc -= TICK) tick();
    draw();
    requestAnimationFrame(frame);
  }

  function sync() {
    var want = !still && visible && !document.hidden;
    if (want && !running) { running = true; requestAnimationFrame(frame); }
    else if (!want) running = false;
  }

  function flash(el) {
    if (!el) return;
    el.classList.add("flash");
    setTimeout(function () { el.classList.remove("flash"); }, 900);
  }

  function fit(input) { input.style.width = Math.max(1, input.value.length) + "ch"; }

  function setStatus(text, state) {
    clearTimeout(statusTimer);
    status.textContent = text;
    status.classList.toggle("idle", state === "idle");
    status.classList.toggle("bad", state === "bad");
    if (state === "ok") statusTimer = setTimeout(function () { setStatus("running", "idle"); }, 1800);
  }

  // Check the shown file and, when it passes, re-run it: the class takes the new say, the file's hook the new spd.
  function reload(fromVisitor, force) {
    clearTimeout(editTimer);
    var spd = checkSpd(), say = checkSay(), problem = firstProblem([spd, say]);
    showProblem(problem);
    if (problem) {
      var verdict = { parse: "parse failed", check: "static check failed", demo: "not applied" }[problem.stage];
      setStatus(verdict + " · old code running", "bad");
      if (fromVisitor) live.textContent = verdict + ", the old code keeps running: " + problemText(problem);
      return;
    }
    if (!force && spdIn.value === applied.spd && sayIn.value === applied.say) {
      if (status.classList.contains("bad")) setStatus("running", "idle");
      return;
    }
    applied.spd = spdIn.value;
    applied.say = sayIn.value;
    gator.spd = spd.value;
    gator.says = say.value;
    flash(hookEl);
    setStatus("reloaded · slices kept", "ok");
    setLabel();
    if (fromVisitor) live.textContent = "Reloaded: spd " + gator.spd + ", slices kept.";
    if (!running) draw();
  }

  // Until a visitor edits, the demo edits itself once per slice, as a save in an editor would.
  function editByItself() {
    spdIn.value = gator.spd === 1 ? "4" : "1";
    fit(spdIn);
    flash(spdIn);
    reload(false, true);
  }

  [spdIn, sayIn].forEach(function (input) {
    fit(input);
    input.addEventListener("focus", function () { editsItself = false; });
    input.addEventListener("input", function () {
      editsItself = false;
      fit(input);
      clearTimeout(editTimer);
      editTimer = setTimeout(function () { reload(true, false); }, DEBOUNCE);
    });
    input.addEventListener("keydown", function (e) {
      var save = (e.ctrlKey || e.metaKey) && (e.key === "s" || e.key === "S");
      if (e.key === "Enter" || save) {
        e.preventDefault();
        reload(true, true);
      } else if (e.key === "Escape") {
        e.preventDefault();
        input.value = input === spdIn ? applied.spd : applied.say;
        fit(input);
        reload(true, false);
      }
    });
  });

  readColors();
  gator.spd = checkSpd().value;
  gator.says = checkSay().value;
  setLabel();
  draw();
  new MutationObserver(function () { readColors(); if (!running) draw(); })
    .observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
  if (reduce && reduce.addEventListener) {
    reduce.addEventListener("change", function (e) { still = e.matches; sync(); if (!running) draw(); });
  }
  document.addEventListener("visibilitychange", sync);
  if ("IntersectionObserver" in window) {
    new IntersectionObserver(function (entries) { visible = entries[0].isIntersecting; sync(); }).observe(demo);
  }
  sync();
}());
