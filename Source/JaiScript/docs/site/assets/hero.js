/* Landing-page hot-reload demo: the editor's spd value flips on each "save" while the gator keeps its slices. */
(function () {
  "use strict";
  var demo = document.getElementById("hero-demo");
  if (!demo) return;
  var spdEl = document.getElementById("demo-spd");
  var status = document.getElementById("demo-status");
  var label = document.getElementById("demo-label");
  var saveBtn = document.getElementById("demo-save");
  var cv = document.getElementById("demo-scene");
  var g = cv.getContext && cv.getContext("2d");
  if (!g) return;

  var reduce = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)");
  var still = !!(reduce && reduce.matches);
  var spd = 4, values = [4, 1], vi = 0;
  var x = 30, tick = 0, slices = 0, nom = 0, step = 0;
  var visible = true, running = false, statusTimer = 0, autoTimer = 0;
  var colors = {};
  var GATOR = "#3fae5a", GATOR_DARK = "#2a7a3d", BELLY = "#c9e7a1";

  function readColors() {
    var cs = getComputedStyle(document.documentElement);
    colors.ground = cs.getPropertyValue("--border").trim() || "#252b3d";
    colors.accent = cs.getPropertyValue("--accent").trim() || "#ffb43c";
  }

  function gator(px, py) {
    var leg = (step % 2) * 3;
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
    var flick = (tick >> 3) % 2, wdt = 44 - eaten;
    g.fillStyle = "#8b5a2b"; g.fillRect(cx + eaten, cy + 14, wdt, 16);
    g.fillStyle = "#f6d3e0"; g.fillRect(cx + eaten, cy + 6, wdt, 10);
    g.fillStyle = "#ffffff"; for (var d = eaten; d < 44; d += 8) g.fillRect(cx + d, cy + 15, 5, 4);
    g.fillStyle = "#e94b6a"; g.fillRect(cx + 30, cy - 8, 4, 14);
    g.fillStyle = flick ? "#ffd84a" : "#ff9f1a"; g.fillRect(cx + 29, cy - 15 - flick, 6, 7);
  }

  function setLabel() {
    label.textContent = "gator at spd " + spd + " · slices " + slices + (spd < 4 ? " (cake rush)" : "");
  }

  function draw() {
    var w = cv.width;
    g.clearRect(0, 0, w, cv.height);
    g.fillStyle = colors.ground;
    for (var i = 0; i < w; i += 32) g.fillRect(i, 118, 28, 6);
    cake(w - 96, 88, Math.min(18, slices * 6));
    gator(x, 92);
    if (nom > 0) {
      g.fillStyle = colors.accent;
      g.font = '600 15px "JetBrains Mono", Consolas, monospace';
      g.fillText("Nom.", x + 20, 72);
    }
  }

  function advance() {
    tick++;
    if (nom > 0) {
      nom--;
      if (nom === 0) { x = 30; if (slices >= 3) slices = 0; setLabel(); }
    } else if (tick % (spd * 5) === 0) {
      x += 5; step++;
      if (x >= cv.width - 150) { slices++; nom = 50; setLabel(); }
    }
  }

  function frame() {
    if (!running) return;
    advance();
    draw();
    requestAnimationFrame(frame);
  }

  function sync() {
    var want = !still && visible && !document.hidden;
    if (want && !running) { running = true; requestAnimationFrame(frame); }
    else if (!want) running = false;
    clearInterval(autoTimer);
    autoTimer = want ? setInterval(reload, 3600) : 0;
  }

  function reload() {
    vi = 1 - vi; spd = values[vi];
    spdEl.textContent = spd;
    spdEl.classList.add("flash");
    setTimeout(function () { spdEl.classList.remove("flash"); }, 900);
    status.classList.remove("idle");
    status.textContent = "saved · reloaded in 4 ms · slices kept";
    setLabel();
    clearTimeout(statusTimer);
    statusTimer = setTimeout(function () { status.textContent = "running"; status.classList.add("idle"); }, 1800);
    if (!running) draw();
  }

  readColors();
  setLabel();
  draw();
  if (saveBtn) saveBtn.addEventListener("click", reload);
  new MutationObserver(function () { readColors(); if (!running) draw(); })
    .observe(document.documentElement, { attributes: true, attributeFilter: ["data-theme"] });
  if (reduce && reduce.addEventListener) {
    reduce.addEventListener("change", function (e) { still = e.matches; sync(); });
  }
  document.addEventListener("visibilitychange", sync);
  if ("IntersectionObserver" in window) {
    new IntersectionObserver(function (entries) { visible = entries[0].isIntersecting; sync(); }).observe(demo);
  }
  sync();
}());
