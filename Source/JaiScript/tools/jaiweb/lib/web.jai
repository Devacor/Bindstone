// jaiweb's script surface: Request, Response, the response helpers and Router. The host imports this file before
// the app and calls jaiweb_serve once per request; the app defines Response handle(Request req).
//
// The host functions it stands on: parse_cookies, parse_multipart, random_token, http_date, read_file and write_file;
// web::parse_query and web::url_encode are the web library's.

extern function handle;

class Upload {
	string name;            // the form field
	string filename;        // as the browser sent it: a name to show, never a path to trust
	string content_type;
	string data;

	int size() { return data.size(); }
	bool save(string path) { return write_file(path, data); }
}

class Request {
	string method;
	string target;          // as it arrived: path and query, percent-encoded
	string path;            // decoded, without the query
	string query_string;
	string version;
	string body;
	string remote_addr;
	map<string, string> query;
	map<string, string> headers;      // names lowercased
	map<string, string> cookies;
	map<string, string> params;       // the route's :name and *name captures

	Request() {}
	Request(map<string, var> raw) {
		method = raw["method"];
		target = raw["target"];
		path = raw["path"];
		query_string = raw["query_string"];
		version = raw["version"];
		body = raw["body"];
		remote_addr = raw["remote_addr"];
		headers = raw["headers"];
		query = web::parse_query(query_string);
		cookies = parse_cookies(headers.get("cookie", ""));
	}

	// any spelling of the name: header("Content-Type") reads content-type
	string header(string name) { return headers.get(string::to_lower(name), ""); }
	string cookie(string name) { return cookies.get(name, ""); }
	string param(string name) { return params.get(name, ""); }
	string content_type() { return string::to_lower(header("content-type")); }
	bool is_multipart() { return content_type().starts_with("multipart/form-data"); }

	// the fields of an urlencoded or multipart form body; a file field is in files(), not here
	map<string, string> form() {
		if (!is_multipart()) { return web::parse_query(body); }
		map<string, string> fields;
		array<map<string, var>> parts = parse_multipart(body, header("content-type"));
		for (auto part : parts) {
			if (!part["is_file"]) { fields[part["name"]] = part["data"]; }
		}
		return fields;
	}

	map<string, Upload> files() {
		map<string, Upload> uploads;
		if (!is_multipart()) { return uploads; }
		array<map<string, var>> parts = parse_multipart(body, header("content-type"));
		for (auto part : parts) {
			if (part["is_file"]) {
				Upload u;
				u.name = part["name"];
				u.filename = part["filename"];
				u.content_type = part["content_type"];
				u.data = part["data"];
				uploads[u.name] = u;
			}
		}
		return uploads;
	}

	var json() { return from_json(body); }
}

class Response {
	int status = 200;
	map<string, string> headers;
	array<string> cookies;            // Set-Cookie values, one per cookie
	string body;
	string file_path;                 // set by file(path): the host streams the file

	Response() {}
	Response(int code) { status = code; }
	Response(int code, string text) {
		status = code;
		body = text;
	}

	Response header(string name, string value) {
		headers[name] = value;
		return this;
	}

	// options: max_age (seconds), expires (unix seconds), path ("/"), domain, secure (false), http_only (true),
	// same_site ("Lax")
	void set_cookie(string name, string value, map<string, var> options = {}) {
		string line = name + "=" + web::url_encode(value);
		line += "; Path=" + options.get("path", "/");
		if (options.contains("domain")) { line += "; Domain=" + options["domain"]; }
		if (options.contains("max_age")) { line += "; Max-Age=" + to_string(options["max_age"]); }
		if (options.contains("expires")) { line += "; Expires=" + http_date(options["expires"]); }
		if (options.get("secure", false)) { line += "; Secure"; }
		if (options.get("http_only", true)) { line += "; HttpOnly"; }
		string same_site = options.get("same_site", "Lax");
		if (same_site != "") { line += "; SameSite=" + same_site; }
		cookies.push(line);
	}

	void clear_cookie(string name, map<string, var> options = {}) {
		map<string, var> expired = options;
		expired["max_age"] = 0;
		set_cookie(name, "", expired);
	}

	map<string, var> parts() {
		return {"status": status, "headers": headers, "cookies": cookies, "body": body, "file": file_path};
	}
}

Response html(string text, int status = 200) {
	Response r = Response(status, text);
	r.header("Content-Type", "text/html; charset=utf-8");
	return r;
}

Response plain_text(string text, int status = 200) {
	Response r = Response(status, text);
	r.header("Content-Type", "text/plain; charset=utf-8");
	return r;
}

Response json(var value, int status = 200) {
	Response r = Response(status, to_json(value));
	r.header("Content-Type", "application/json");
	return r;
}

// 303 sends a browser back with GET: the answer to a form POST
Response redirect(string url, int status = 303) {
	Response r = Response(status);
	r.header("Location", url);
	return r;
}

// a file from disk, relative to the app's folder; its Content-Type follows the extension unless one is given
Response file(string path, string content_type = "") {
	Response r = Response(200);
	r.file_path = path;
	if (content_type != "") { r.header("Content-Type", content_type); }
	return r;
}

Response not_found(string text = "Not Found") { return plain_text(text, 404); }

// a handler may answer a Response, a string (an HTML page), null (204) or any other value (JSON)
Response to_response(var result) {
	if (result == null) { return Response(204); }
	string kind = type_of(result);
	if (kind == "Response") { return result; }
	if (kind == "string" || kind == "utf8_string") { return html(result); }
	return json(result);
}

class Route {
	string method;
	array<string> parts;
	function handler;
}

// Routes match in the order they were added: "/posts/:id" captures one segment as params["id"], "/files/*rest" the
// rest of the path. A HEAD request takes a GET route; a path that matches only other methods answers 405.
class Router {
	array<Route> routes;
	array<function> filters;

	void add(string method, string pattern, function handler) {
		Route r;
		r.method = method;
		r.parts = split_path(pattern);
		r.handler = handler;
		routes.push(r);
	}
	void get(string pattern, function handler) { add("GET", pattern, handler); }
	void post(string pattern, function handler) { add("POST", pattern, handler); }
	void put(string pattern, function handler) { add("PUT", pattern, handler); }
	void patch(string pattern, function handler) { add("PATCH", pattern, handler); }
	void delete(string pattern, function handler) { add("DELETE", pattern, handler); }
	void any(string pattern, function handler) { add("*", pattern, handler); }

	// a filter runs before every route: null lets the request through, anything else is the answer
	void before(function filter) { filters.push(filter); }

	Response dispatch(Request& req) {
		for (auto& filter : filters) {
			var early = filter(req);
			if (early != null) { return to_response(early); }
		}
		array<string> path_parts = split_path(req.path);
		array<string> allowed;
		for (auto& r : routes) {
			map<string, string> captures;
			if (!match_parts(r.parts, path_parts, captures)) { continue; }
			if (r.method == req.method || r.method == "*" || (req.method == "HEAD" && r.method == "GET")) {
				req.params = captures;
				return to_response(r.handler(req));
			}
			allowed.push(r.method);
		}
		if (allowed.size() > 0) {
			Response r = plain_text("Method Not Allowed", 405);
			r.header("Allow", allowed.join(", "));
			return r;
		}
		return not_found();
	}
}

array<string> split_path(string path) {
	array<string> parts;
	for (auto piece : path.split("/")) {
		if (piece != "") { parts.push(piece); }
	}
	return parts;
}

bool match_parts(array<string> pattern, array<string> parts, map<string, string>& captures) {
	for (int i = 0; i < pattern.size(); i++) {
		string want = pattern[i];
		if (want.starts_with("*")) {
			array<string> rest;
			for (int j = i; j < parts.size(); j++) { rest.push(parts[j]); }
			captures[want.substr(1)] = rest.join("/");
			return true;
		}
		if (i >= parts.size()) { return false; }
		if (want.starts_with(":")) {
			captures[want.substr(1)] = parts[i];
		} else if (want != parts[i]) {
			return false;
		}
	}
	return pattern.size() == parts.size();
}

// the host's entry: one request in, its response's parts out
map<string, var> jaiweb_serve(map<string, var> raw) {
	Request req = Request(raw);
	Response resp = to_response(handle(req));
	return resp.parts();
}
