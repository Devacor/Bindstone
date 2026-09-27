// The runner's host functions, captured before crawler.jai shadows their names. An import runs once per
// engine, so loading crawler.jai again in the same engine (the REPL) keeps the originals instead of
// capturing the shim's own read_key/sleep_ms and recursing.
var HOST_READ_KEY_RAW = read_key;   // non-blocking edge poll: "" when no key is waiting
var HOST_SLEEP_MS = sleep_ms;       // real sleep (the shim's sleep_ms no-ops when headless)
