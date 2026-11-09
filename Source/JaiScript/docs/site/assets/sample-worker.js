/* Runs the site's samples (samples.js) on the engine as WebAssembly: the plain build, which needs no shared memory, so any
   page can start it. Each run gets a folder of its own holding the sample and the page's support files, and a fresh
   engine; {out} carries output as it is printed and {exit} the exit code. A sleep() spins this worker, never the page. */
"use strict";

var ENGINE = new URL("../play/engine/plain/", self.location.href).href;
importScripts(ENGINE + "jaiscript_web_worker.js");

var ready = createJaiScript({
  locateFile: function (path) { return ENGINE + path; },
  jaiwebWrite: function (bytes) { self.postMessage({ out: bytes }, [bytes.buffer]); }
});
var runs = 0;

self.onmessage = function (message) {
  ready.then(function (jai) {
    var data = message.data, folder = "/run" + (++runs);
    jai.FS.mkdir(folder);
    Object.keys(data.files).forEach(function (name) {
      var full = folder + "/" + name;
      jai.FS.mkdirTree(full.slice(0, full.lastIndexOf("/")));
      jai.FS.writeFile(full, data.files[name]);
    });
    jai.FS.chdir(folder);
    self.postMessage({ exit: jai.ccall("jaiweb_run_file", "number", ["string", "string", "number", "number"], [data.main, "", data.budget, 1]) });
  }).catch(function (error) {
    self.postMessage({ failed: String(error && error.message || error) });
  });
};
