// The fixture jaiweb_test.py drives: one route per thing the host and lib/web.jai promise.
import "web.jai";
import "session.jai";
import "greeting.jai";

Database db = open_database("test.db");
db.execute("CREATE TABLE IF NOT EXISTS items (id INTEGER PRIMARY KEY, name TEXT NOT NULL, qty INTEGER, price REAL, note TEXT)");
SessionStore sessions = SessionStore(db);

int counter;

Router routes = Router();

routes.get("/", (Request req) => "<h1>home</h1>");
routes.get("/greeting", (Request req) => plain_text(greeting()));
routes.get("/count", (Request req) => {
	counter += 1;
	return plain_text(to_string(counter));
});
routes.get("/posts/:id", (Request req) => json({"id": req.param("id")}));
routes.get("/files/*rest", (Request req) => json({"rest": req.param("rest")}));
routes.post("/only-post", (Request req) => "posted");
routes.get("/echo", (Request req) => json({"method": req.method, "path": req.path, "query": req.query, "agent": req.header("X-TEST-Agent")}));
routes.any("/echo/body", (Request req) => json({"method": req.method, "body": req.body, "length": req.body.size()}));
routes.post("/echo/form", (Request req) => json(req.form()));
routes.post("/echo/json", (Request req) => json({"got": req.json()}));
routes.post("/echo/files", (Request req) => {
	map<string, var> out = {"fields": req.form()};
	map<string, var> files;
	for (auto [name, upload] : req.files()) {
		files[name] = {"filename": upload.filename, "size": upload.size(), "type": upload.content_type, "data": upload.data};
	}
	out["files"] = files;
	return json(out);
});
routes.get("/cookies", (Request req) => {
	Response r = json(req.cookies);
	r.set_cookie("flavor", "oat meal", {"max_age": 60});
	r.set_cookie("plain", "1", {"http_only": false, "same_site": ""});
	return r;
});
routes.get("/session", (Request req) => {
	Session s = sessions.load(req);
	int seen = s.get("seen", 0);
	s.set("seen", seen + 1);
	Response r = json({"seen": seen + 1, "new": s.is_new});
	sessions.save(s, r);
	return r;
});
routes.get("/download", (Request req) => file("public/style.css", "text/x-test"));
routes.get("/boom", (Request req) => 1 / 0);
routes.get("/throw", (Request req) => { throw "thrown from a handler"; });
routes.get("/spin", (Request req) => {
	int n = 0;
	while (true) { n += 1; }
	return "never";
});
routes.get("/status/:code", (Request req) => Response(req.param("code").to_int(), "custom status"));
routes.get("/redirect", (Request req) => redirect("/target"));
routes.get("/null", (Request req) => null);

routes.post("/items", (Request req) => {
	var body = req.json();
	db.execute("INSERT INTO items (name, qty, price, note) VALUES (?, ?, ?, ?)", body["name"], body["qty"], body["price"], body["note"]);
	return json({"id": db.last_insert_id()}, 201);
});
routes.get("/items", (Request req) => db.query("SELECT id, name, qty, price, note FROM items ORDER BY id"));
routes.get("/items/:id", (Request req) => {
	var row = db.query_one("SELECT id, name, qty, price, note FROM items WHERE id = :id", {"id": req.param("id").to_int()});
	return row == null ? not_found("no item") : json(row);
});
routes.put("/items/:id", (Request req) => {
	var body = req.json();
	int changed = db.execute("UPDATE items SET qty = ? WHERE id = ?", body["qty"], req.param("id").to_int());
	return json({"changed": changed});
});
routes.delete("/items/:id", (Request req) => json({"changed": db.execute("DELETE FROM items WHERE id = ?", req.param("id").to_int())}));
routes.get("/items-search", (Request req) => db.query("SELECT name FROM items WHERE name = ?", req.query.get("name", "")));
routes.post("/items/rollback", (Request req) => {
	try {
		db.transaction(() => {
			db.execute("INSERT INTO items (name, qty) VALUES (?, ?)", "doomed", 1);
			db.execute("INSERT INTO nowhere (x) VALUES (1)");
		});
	} catch (e) {
		return json({"error": e, "count": db.value("SELECT COUNT(*) FROM items WHERE name = ?", "doomed")});
	}
	return json({"error": null});
});
routes.post("/items/nested", (Request req) => {
	string thrown = "";
	db.transaction(() => {
		db.execute("INSERT INTO items (name, qty) VALUES (?, ?)", "outer", 1);
		try {
			db.transaction(() => {
				db.execute("INSERT INTO items (name, qty) VALUES (?, ?)", "inner", 1);
				throw "inner fails";
			});
		} catch (e) {
			thrown = e;
		}
	});
	return json({"thrown": thrown, "outer": db.value("SELECT COUNT(*) FROM items WHERE name = 'outer'"),
		"inner": db.value("SELECT COUNT(*) FROM items WHERE name = 'inner'"), "open": db.in_transaction()});
});
routes.get("/sql-error", (Request req) => db.query("SELECT * FROM missing_table"));
db.execute("CREATE TABLE IF NOT EXISTS kinds (v)");
routes.get("/sql/in", (Request req) => db.query("SELECT name FROM items WHERE qty IN (?) ORDER BY name", [101, 102]));
routes.get("/sql/in-empty", (Request req) => db.query("SELECT name FROM items WHERE qty IN (?)", []));
routes.get("/sql/sorted", (Request req) => db.query("SELECT name FROM items ORDER BY {}, id", sql_choose(req.query.get("by", "name"), ["name", "qty DESC", "price"])));
routes.get("/sql/injected", (Request req) => {
	var text = "SELECT name FROM items WHERE name = '" + req.query.get("n", "") + "'";
	[[warn_mode]] return json(db.query(text));
});
routes.get("/sql/reflected", (Request req) => {
	var text = "DELETE FROM items";
	return json(reflect::invoke(db, "execute", text));
});
routes.get("/sql/fragment-misuse", (Request req) => db.query("SELECT name FROM items WHERE name = ?", sql_choose("name", ["name"])));
routes.post("/sql/types", (Request req) => {
	db.execute("DELETE FROM kinds");
	for (var v : [1, 2.5, true, null, "it's \"q\" \xC3\xA9"]) { db.execute("INSERT INTO kinds (v) VALUES (?)", v); }
	return db.query("SELECT typeof(v) AS t, v FROM kinds ORDER BY rowid");
});

Response handle(Request req) { return routes.dispatch(req); }
