// The fixture spider_test.py's worker checks drive: requests that take their time, a global a request changes, and a
// marker file a request writes when it starts, so a check can wait for it rather than for a clock.
import "spider.jai";

int touched;

spider::Router routes = spider::Router();

routes.get("/touch", (spider::Request req) => {
	touched += 1;
	return spider::plain_text(to_string(touched));
});
routes.get("/sleep/:ms", (spider::Request req) => {
	int started = time::unix_ms();
	write_file("started-" + req.param("ms") + ".txt", to_string(started));
	sleep(req.param("ms").to_int() / 1000.0);
	return spider::json({"ms": req.param("ms"), "started": started, "ended": time::unix_ms()});
});

spider::spin(routes);
