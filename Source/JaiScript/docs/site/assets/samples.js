/* Runnable samples: every block mark_runnable.py marked data-run gets Run and Reset beside Copy, and its code (and every
   support file a page's samples load, data-file) edits in place. Run sends the block and the page's support files, as they
   read now, to sample-worker.js, which runs them on the engine compiled to WebAssembly; the output fills the sample's
   output block until Reset puts the page's text back. */
(function () {
  "use strict";
  var WORKER = new URL("sample-worker.js", document.currentScript.src).href;
  var BUDGET = 20;
  var LIMIT = 200000;
  var SGR = new RegExp(String.fromCharCode(27) + "\\[([0-9;?]*)([A-Za-z])", "g");
  var worker = null, running = null;

  function codeOf(block) { return block.querySelector("pre > code"); }

  // An edit can add or drop lines: the gutter follows, when the block has one
  function renumber(code) {
    var gutter = code.parentNode.querySelector(".cb-gutter");
    if (!gutter) return;
    var count = code.textContent.replace(/\n$/, "").split("\n").length, nums = "";
    for (var n = 1; n <= count; n++) nums += n + "\n";
    gutter.textContent = nums;
  }

  function makeEditable(block, onRun) {
    var code = codeOf(block);
    if (!code) return null;
    var original = { html: code.innerHTML, text: code.textContent };
    code.setAttribute("contenteditable", "plaintext-only");
    if (code.contentEditable !== "plaintext-only") code.setAttribute("contenteditable", "true");
    code.setAttribute("spellcheck", "false");
    code.setAttribute("autocapitalize", "off");
    code.setAttribute("aria-label", "Sample code, editable");
    var painted = original.text;
    code.addEventListener("input", function () {
      renumber(code);
      block.classList.toggle("cb-edited", code.textContent !== original.text);
    });
    code.addEventListener("blur", function () {
      if (code.textContent === painted || !code.classList.contains("lang-jai") || !window.JaiDocs) return;
      painted = code.textContent;
      code.innerHTML = window.JaiDocs.highlight(painted);
    });
    code.addEventListener("keydown", function (event) {
      if (event.key === "Enter" && (event.ctrlKey || event.metaKey) && onRun) {
        event.preventDefault();
        onRun();
      }
    });
    return function reset() {
      code.innerHTML = original.html;
      painted = original.text;
      renumber(code);
      block.classList.remove("cb-edited");
    };
  }

  function button(name, text, label) {
    var result = document.createElement("button");
    result.type = "button";
    result.className = "cb-tool cb-" + name;
    result.textContent = text;
    result.setAttribute("aria-label", label);
    return result;
  }

  // The sample's own output block, or one made for it under the sample
  function outputFor(block) {
    var next = block.nextElementSibling;
    if (next && next.classList.contains("cb-output")) {
      var code = codeOf(next), label = next.querySelector(".cb-label");
      return { block: next, code: code, label: label, made: false,
        original: { html: code.innerHTML, label: label ? label.textContent : "" } };
    }
    var made = document.createElement("div");
    made.className = "codeblock cb-output";
    made.innerHTML = '<span class="cb-label">output</span><pre><code class="lang-output"></code></pre>';
    block.parentNode.insertBefore(made, block.nextSibling);
    return { block: made, code: codeOf(made), label: made.querySelector(".cb-label"), made: true };
  }

  // The engine's text: a red SGR (an error) shows as an error, every other escape sequence is dropped
  function paint(target, text) {
    target.textContent = "";
    var red = false, last = 0, found;
    function emit(piece) {
      if (!piece) return;
      if (red) {
        var span = document.createElement("span");
        span.className = "run-error";
        span.textContent = piece;
        target.appendChild(span);
      } else {
        target.appendChild(document.createTextNode(piece));
      }
    }
    SGR.lastIndex = 0;
    while ((found = SGR.exec(text))) {
      emit(text.slice(last, found.index));
      last = SGR.lastIndex;
      if (found[2] !== "m") continue;
      var codes = found[1].split(";");
      for (var i = 0; i < codes.length; i++) {
        if (codes[i] === "31") red = true;
        else if (codes[i] === "" || codes[i] === "0" || codes[i] === "39") red = false;
      }
    }
    emit(text.slice(last));
  }

  function stopRun(note) {
    if (!running) return;
    if (worker) { worker.terminate(); worker = null; }
    running.finish(note);
  }

  function startRun(block, run, out) {
    stopRun("stopped");
    if (!worker) worker = new Worker(WORKER);
    var files = {};
    var support = document.querySelectorAll(".codeblock[data-file]");
    for (var i = 0; i < support.length; i++) files[support[i].getAttribute("data-file")] = codeOf(support[i]).textContent;
    var main = block.getAttribute("data-run");
    files[main] = codeOf(block).textContent;
    var decoder = new TextDecoder(), text = "", started = performance.now();
    out.block.classList.add("cb-ran");
    if (out.label) out.label.textContent = "running in your browser…";
    out.code.textContent = "";
    run.textContent = "■ stop";
    run.setAttribute("aria-label", "Stop the sample");
    running = {
      finish: function (note) {
        running = null;
        run.textContent = "▶ run";
        run.setAttribute("aria-label", "Run the sample");
        text = text.replace(/\r/g, "").replace(/\n+$/, "");
        paint(out.code, text);
        if (out.label) out.label.textContent = "output · " + note;
      }
    };
    worker.onmessage = function (message) {
      var data = message.data;
      if (data.out && text.length < LIMIT) {
        text += decoder.decode(data.out, { stream: true });
        if (text.length >= LIMIT) text = text.slice(0, LIMIT) + "\n[output cut at " + LIMIT + " characters]";
        paint(out.code, text.replace(/\r/g, ""));
      }
      if (data.exit !== undefined) {
        var ms = Math.round(performance.now() - started);
        running.finish(data.exit === 0 ? "ran in your browser in " + ms + " ms" : "exit code " + data.exit);
      }
      if (data.failed !== undefined) {
        text += "\n" + data.failed;
        worker.terminate();
        worker = null;
        running.finish("the engine did not load");
      }
    };
    worker.postMessage({ files: files, main: main, budget: Number(block.getAttribute("data-budget")) || BUDGET });
  }

  var runnable = document.querySelectorAll(".codeblock[data-run]");
  for (var r = 0; r < runnable.length; r++) {
    (function (block) {
      var tools = document.createElement("div");
      tools.className = "cb-tools";
      var run = button("run", "▶ run", "Run the sample");
      var resetButton = button("reset", "reset", "Put the sample and its output back");
      var out = null;
      var trigger = function () {
        if (running && run.textContent.indexOf("stop") >= 0) { stopRun("stopped"); return; }
        if (!out) out = outputFor(block);
        block.classList.add("cb-touched");
        startRun(block, run, out);
      };
      var resetCode = makeEditable(block, trigger);
      resetButton.addEventListener("click", function () {
        if (running && run.textContent.indexOf("stop") >= 0) stopRun("stopped");
        if (resetCode) resetCode();
        if (out) {
          if (out.made) {
            out.block.parentNode.removeChild(out.block);
          } else {
            out.code.innerHTML = out.original.html;
            if (out.label) out.label.textContent = out.original.label;
            out.block.classList.remove("cb-ran");
          }
          out = null;
        }
        block.classList.remove("cb-touched");
      });
      run.addEventListener("click", trigger);
      var copy = block.querySelector(".cb-copy");
      tools.appendChild(resetButton);
      tools.appendChild(run);
      if (copy) tools.appendChild(copy);
      block.appendChild(tools);
    }(runnable[r]));
  }
  var files = document.querySelectorAll(".codeblock[data-file]");
  for (var f = 0; f < files.length; f++) makeEditable(files[f], null);
}());
