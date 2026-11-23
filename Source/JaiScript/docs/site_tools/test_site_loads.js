#!/usr/bin/env node
/* Runs assets/site.js in a stub page and checks the scripts it loads later land beside it: samples.js on a page with a
   runnable sample, search.js when the search box takes focus.

       node site_tools/test_site_loads.js

   Each page is tried as the tree has it and as a deploy serves it, every asset address ending in a version query, which
   a selector matching the end of an address no longer finds. Exit code 1 when a script is missing or lands elsewhere. */
"use strict";

var fs = require("fs");
var path = require("path");
var vm = require("vm");

var SITE_JS = path.join(__dirname, "..", "site", "assets", "site.js");
var PREFIXES = ["", "../", "/"];
var QUERIES = ["", "?v=0123456789"];

// The page's tags a script might look itself up by, matched as a browser matches [attr="x"], [attr$="x"] and [attr*="x"]
function findTag(tags, selector) {
  var tests = [], pattern = /\[(\w+)([$*^]?)="([^"]*)"\]/g, found;
  while ((found = pattern.exec(selector))) tests.push(found.slice(1));
  var name = /^\w+/.exec(selector);
  for (var t = 0; t < tags.length; t++) {
    var tag = tags[t], matches = tests.length > 0 && (!name || name[0] === tag.name);
    for (var i = 0; matches && i < tests.length; i++) {
      var value = tag[tests[i][0]], how = tests[i][1], want = tests[i][2];
      matches = value !== undefined && (how === "$" ? value.endsWith(want) : how === "*" ? value.indexOf(want) >= 0
        : how === "^" ? value.startsWith(want) : value === want);
    }
    if (matches) return element(tag);
  }
  return null;
}

function element(attributes) {
  var listeners = {};
  return {
    listeners: listeners, hidden: true, value: "", src: attributes.src,
    getAttribute: function (name) { return attributes[name] === undefined ? null : attributes[name]; },
    setAttribute: function () {}, removeAttribute: function () {}, appendChild: function () {},
    addEventListener: function (name, handler) { listeners[name] = handler; }
  };
}

// What site.js loads on a page that carries a runnable sample, once its search box has taken focus
function loadedBy(prefix, query) {
  var loaded = [];
  var tags = [
    { name: "link", rel: "stylesheet", href: prefix + "assets/style.css" + query },
    { name: "script", src: prefix + "assets/search-index.js" + query },
    { name: "script", src: prefix + "assets/site.js" + query }
  ];
  var box = element({}), results = element({});
  var document = {
    currentScript: element(tags[2]),
    documentElement: element({}),
    head: { appendChild: function (node) { loaded.push(node.src); } },
    body: { appendChild: function (node) { loaded.push(node.src); } },
    getElementById: function (id) { return id === "search-box" ? box : id === "search-results" ? results : null; },
    querySelector: function (selector) { return selector === ".codeblock[data-run]" ? element({}) : findTag(tags, selector); },
    querySelectorAll: function () { return []; },
    createElement: function () { return element({}); },
    addEventListener: function () {}
  };
  var window = { addEventListener: function () {} };
  vm.runInNewContext(fs.readFileSync(SITE_JS, "utf8"), { document: document, window: window, location: { hash: "" } },
    { filename: SITE_JS });
  box.listeners.focus();
  return loaded;
}

function main() {
  var failed = 0;
  PREFIXES.forEach(function (prefix) {
    QUERIES.forEach(function (query) {
      var page = "a page loading " + prefix + "assets/site.js" + query;
      var loaded = loadedBy(prefix, query);
      ["samples.js", "search.js"].forEach(function (name) {
        if (loaded.indexOf(prefix + "assets/" + name) >= 0) return;
        failed += 1;
        console.log("FAIL " + page + " does not load " + prefix + "assets/" + name + " (it loads " + JSON.stringify(loaded) + ")");
      });
    });
  });
  console.log(failed ? failed + " scripts not loaded" : "site.js loads samples.js and search.js beside itself, with and without a version query");
  return failed ? 1 : 0;
}

process.exit(main());
