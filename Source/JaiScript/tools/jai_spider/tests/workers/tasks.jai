// The fixture for spider::every: a task that outlasts its period and records each run, so overlapping runs would show,
// and one that fails every time.
import "spider.jai";

Database db = open_database("tasks.db");
db.execute("CREATE TABLE IF NOT EXISTS runs (id INTEGER PRIMARY KEY, started INTEGER NOT NULL, ended INTEGER)");

spider::Router routes = spider::Router();
routes.get("/runs", (spider::Request req) => db.query("SELECT started, ended FROM runs ORDER BY id"));

spider::every(0.2, () => {
	db.execute("INSERT INTO runs (started) VALUES (?)", time::unix_ms());
	int id = db.last_insert_id();
	sleep(0.5);
	db.execute("UPDATE runs SET ended = ? WHERE id = ?", time::unix_ms(), id);
});
spider::every(1, () => { throw "thrown by a task"; });

spider::spin(routes);
