#pragma once
// Database: SQLite for scripts. One connection per open_database, its prepared statements cached by SQL text and
// finalized with it; parameters are always bound (?, ?NNN, :name), never spliced into the text; every failure raises
// a catchable script error carrying SQLite's own message.
//
//   Database db = open_database("app.db");          // relative to the app's folder; ":memory:" for a scratch one
//   db.execute(sql, a, b, ...) -> int               // rows changed; a text of several statements runs them all
//   db.query(sql, a, b, ...) -> array<map<string, var>>
//   db.query_one(sql, ...) -> map<string, var> or null
//   db.value(sql, ...) -> the first column of the first row, or null
//   db.transaction(fn)  db.begin()  db.commit()  db.rollback()  db.last_insert_id()  db.close()  db.is_open()
//
// One argument that is a map binds by name ({"id": 3} fills :id, @id or $id); one that is an array binds its elements
// in order. A row maps column names to int, float, string (TEXT, and BLOB as its bytes) or null.
#include "third_party/sqlite/sqlite3.h"
#include <jaiscript/jaiscript.hpp>

#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jaiweb {

class sqlite_connection {
public:
	explicit sqlite_connection(sqlite3* opened) : handle(opened) {}
	sqlite_connection(const sqlite_connection&) = delete;
	sqlite_connection& operator=(const sqlite_connection&) = delete;
	~sqlite_connection() { close(); }

	void close() {
		drop_statements();
		if (handle) {
			sqlite3_close_v2(handle);
			handle = nullptr;
		}
	}

	void drop_statements() {
		for (auto& [text, statement] : statements) { sqlite3_finalize(statement); }
		statements.clear();
	}

	sqlite3* handle = nullptr;
	std::unordered_map<std::string, sqlite3_stmt*> statements;
	int savepoint_depth = 0;
};

class database {
public:
	static constexpr size_t statement_cache_limit = 256;

	database() = default;
	database(std::shared_ptr<sqlite_connection> shared, jai::engine* owner) :
		connection_(std::move(shared)),
		eng_(owner) {
	}

	static database open(const std::string& path, jai::engine* owner) {
		sqlite3* handle = nullptr;
		const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI;
		if (sqlite3_open_v2(path.c_str(), &handle, flags, nullptr) != SQLITE_OK) {
			std::string message = handle ? sqlite3_errmsg(handle) : "out of memory";
			if (handle) { sqlite3_close_v2(handle); }
			throw std::runtime_error("SQLite error: cannot open '" + path + "': " + message);
		}
		sqlite3_busy_timeout(handle, 5000);
		sqlite3_extended_result_codes(handle, 1);
		// WAL: readers never wait for the writer, and a commit costs one sync instead of two
		sqlite3_exec(handle, "PRAGMA foreign_keys = ON; PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL", nullptr, nullptr, nullptr);
		return database(std::make_shared<sqlite_connection>(handle), owner);
	}

	bool is_open() const { return connection_ && connection_->handle; }

	void close() {
		if (connection_) { connection_->close(); }
	}

	jai::script_int execute(const std::string& sql, const std::vector<jai::script_value>& params) {
		sqlite3* handle = open_handle();
		const char* tail = nullptr;
		sqlite3_stmt* first = prepare_uncached(sql.c_str(), &tail);
		const bool several = tail && !only_whitespace(tail);
		if (!several) {
			sqlite3_finalize(first);
			statement_use use(*this, sql);
			bind_all(use.statement, params);
			step_to_end(use.statement);
			return static_cast<jai::script_int>(sqlite3_changes64(handle));
		}
		if (!params.empty()) {
			sqlite3_finalize(first);
			throw std::runtime_error("SQLite error: a text of several statements takes no parameters");
		}
		jai::script_int changed = 0;
		sqlite3_stmt* statement = first;
		for (;;) {
			if (statement) {
				try {
					step_to_end(statement);
				} catch (...) {
					sqlite3_finalize(statement);
					throw;
				}
				sqlite3_finalize(statement);
				changed += static_cast<jai::script_int>(sqlite3_changes64(handle));
			}
			if (!tail || only_whitespace(tail)) { break; }
			statement = prepare_uncached(tail, &tail);
		}
		return changed;
	}

	jai::script_value query(const std::string& sql, const std::vector<jai::script_value>& params) {
		statement_use use(*this, sql);
		bind_all(use.statement, params);
		jai::script_value rows = jai::script_value::make_array(nullptr, eng_);
		auto& out = rows.as_array();
		while (step_row(use.statement)) { out.push_back(row_of(use.statement)); }
		return rows;
	}

	jai::script_value query_one(const std::string& sql, const std::vector<jai::script_value>& params) {
		statement_use use(*this, sql);
		bind_all(use.statement, params);
		if (!step_row(use.statement)) { return jai::script_value(std::monostate{}, eng_); }
		return row_of(use.statement);
	}

	jai::script_value value(const std::string& sql, const std::vector<jai::script_value>& params) {
		statement_use use(*this, sql);
		bind_all(use.statement, params);
		if (!step_row(use.statement) || sqlite3_column_count(use.statement) == 0) { return jai::script_value(std::monostate{}, eng_); }
		return column_value(use.statement, 0);
	}

	jai::script_int last_insert_id() { return static_cast<jai::script_int>(sqlite3_last_insert_rowid(open_handle())); }
	jai::script_int changes() { return static_cast<jai::script_int>(sqlite3_changes64(open_handle())); }
	bool in_transaction() { return sqlite3_get_autocommit(open_handle()) == 0; }

	void begin() { run_plain("BEGIN"); }
	void commit() { run_plain("COMMIT"); }
	void rollback() { run_plain("ROLLBACK"); }

	// fn runs inside BEGIN/COMMIT (a SAVEPOINT when a transaction is open); its failure rolls back and raises again
	jai::script_value transaction(const jai::script_value& fn) {
		sqlite3* handle = open_handle();
		const bool nested = sqlite3_get_autocommit(handle) == 0;
		const std::string savepoint = "jaiweb_" + std::to_string(connection_->savepoint_depth + 1);
		auto body = jai::convert_script_function<jai::script_value()>(fn);   // the throwing flavor: a failure inside raises here
		run_plain(nested ? "SAVEPOINT " + savepoint : std::string("BEGIN"));
		++connection_->savepoint_depth;
		jai::script_value result(std::monostate{}, eng_);
		try {
			result = body();
		} catch (...) {
			--connection_->savepoint_depth;
			const std::string sql = nested ? "ROLLBACK TO " + savepoint + "; RELEASE " + savepoint : std::string("ROLLBACK");
			sqlite3_exec(handle, sql.c_str(), nullptr, nullptr, nullptr);
			throw;
		}
		--connection_->savepoint_depth;
		run_plain(nested ? "RELEASE " + savepoint : std::string("COMMIT"));
		return result;
	}

private:
	struct statement_use {
		statement_use(database& used, const std::string& sql) : statement(used.cached_statement(sql)) {}
		~statement_use() {
			sqlite3_reset(statement);
			sqlite3_clear_bindings(statement);
		}
		sqlite3_stmt* statement;
	};

	sqlite3* open_handle() {
		if (!is_open()) { throw std::runtime_error("SQLite error: the database is closed"); }
		return connection_->handle;
	}

	[[noreturn]] void fail(const char* what = nullptr) {
		throw std::runtime_error(std::string("SQLite error: ") + (what ? what : sqlite3_errmsg(connection_->handle)));
	}

	static bool only_whitespace(const char* text) {
		for (; *text; ++text) {
			if (*text != ' ' && *text != '\t' && *text != '\r' && *text != '\n' && *text != ';') { return false; }
		}
		return true;
	}

	sqlite3_stmt* prepare_uncached(const char* sql, const char** tail) {
		sqlite3_stmt* statement = nullptr;
		if (sqlite3_prepare_v2(open_handle(), sql, -1, &statement, tail) != SQLITE_OK) { fail(); }
		return statement;
	}

	sqlite3_stmt* cached_statement(const std::string& sql) {
		sqlite3* handle = open_handle();
		auto& statements = connection_->statements;
		if (auto it = statements.find(sql); it != statements.end()) { return it->second; }
		sqlite3_stmt* statement = nullptr;
		const char* tail = nullptr;
		if (sqlite3_prepare_v3(handle, sql.c_str(), static_cast<int>(sql.size()), SQLITE_PREPARE_PERSISTENT, &statement, &tail) != SQLITE_OK) { fail(); }
		if (!statement) { fail("the text holds no statement"); }
		if (tail && !only_whitespace(tail)) {
			sqlite3_finalize(statement);
			fail("query takes one statement (execute runs a text of several)");
		}
		if (statements.size() >= statement_cache_limit) { connection_->drop_statements(); }
		statements.emplace(sql, statement);
		return statement;
	}

	void run_plain(const std::string& sql) {
		char* message = nullptr;
		if (sqlite3_exec(open_handle(), sql.c_str(), nullptr, nullptr, &message) != SQLITE_OK) {
			std::string text = message ? message : sqlite3_errmsg(connection_->handle);
			sqlite3_free(message);
			fail(text.c_str());
		}
	}

	void step_to_end(sqlite3_stmt* statement) {
		while (step_row(statement)) {}
	}

	bool step_row(sqlite3_stmt* statement) {
		const int code = sqlite3_step(statement);
		if (code == SQLITE_ROW) { return true; }
		if (code == SQLITE_DONE) { return false; }
		fail();
	}

	void bind_all(sqlite3_stmt* statement, const std::vector<jai::script_value>& params) {
		const int wanted = sqlite3_bind_parameter_count(statement);
		if (params.size() == 1 && params[0].is_map()) {
			for (const auto& [key, item] : params[0].as_map()) {
				const std::string* name = key.text_bytes();
				if (!name) { fail("a named parameter's key is a string"); }
				int index = 0;
				if (!name->empty() && (name->front() == ':' || name->front() == '@' || name->front() == '$')) {
					index = sqlite3_bind_parameter_index(statement, name->c_str());
				} else {
					for (const char* prefix : {":", "@", "$"}) {
						if ((index = sqlite3_bind_parameter_index(statement, (prefix + *name).c_str())) != 0) { break; }
					}
				}
				if (index == 0) { fail(("the statement has no parameter named '" + *name + "'").c_str()); }
				bind_one(statement, index, item);
			}
			return;
		}
		const std::vector<jai::script_value>* positional = &params;
		std::vector<jai::script_value> unpacked;
		if (params.size() == 1 && params[0].is_array() && wanted != 1) {
			params[0].unchecked_array_node()->for_each(eng_, [&](const jai::script_value& element) { unpacked.push_back(element); });
			positional = &unpacked;
		}
		if (static_cast<int>(positional->size()) != wanted) {
			fail(("the statement takes " + std::to_string(wanted) + " parameter(s), got " + std::to_string(positional->size())).c_str());
		}
		for (int i = 0; i < wanted; ++i) { bind_one(statement, i + 1, (*positional)[static_cast<size_t>(i)]); }
	}

	void bind_one(sqlite3_stmt* statement, int index, const jai::script_value& item) {
		int code = SQLITE_OK;
		if (item.is_null()) {
			code = sqlite3_bind_null(statement, index);
		} else if (item.is_bool()) {
			code = sqlite3_bind_int64(statement, index, item.as_bool() ? 1 : 0);
		} else if (item.is_int()) {
			code = sqlite3_bind_int64(statement, index, item.as_int());
		} else if (item.is_float()) {
			code = sqlite3_bind_double(statement, index, item.as_float());
		} else if (const std::string* text = item.text_bytes()) {
			code = sqlite3_bind_text64(statement, index, text->data(), text->size(), SQLITE_TRANSIENT, SQLITE_UTF8);
		} else if (item.is_string()) {
			const std::string& bound = item.as_string();
			code = sqlite3_bind_text64(statement, index, bound.data(), bound.size(), SQLITE_TRANSIENT, SQLITE_UTF8);
		} else {
			fail(("parameter " + std::to_string(index) + " is not an int, float, bool, string or null").c_str());
		}
		if (code != SQLITE_OK) { fail(); }
	}

	jai::script_value column_value(sqlite3_stmt* statement, int column) {
		switch (sqlite3_column_type(statement, column)) {
			case SQLITE_INTEGER: return jai::script_value(static_cast<jai::script_int>(sqlite3_column_int64(statement, column)), eng_);
			case SQLITE_FLOAT: return jai::script_value(sqlite3_column_double(statement, column), eng_);
			case SQLITE_TEXT: {
				const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, column));
				return jai::script_value(std::string(text ? text : "", static_cast<size_t>(sqlite3_column_bytes(statement, column))), eng_);
			}
			case SQLITE_BLOB: {
				const auto* bytes = static_cast<const char*>(sqlite3_column_blob(statement, column));
				return jai::script_value(std::string(bytes ? bytes : "", static_cast<size_t>(sqlite3_column_bytes(statement, column))), eng_);
			}
			default: return jai::script_value(std::monostate{}, eng_);
		}
	}

	jai::script_value row_of(sqlite3_stmt* statement) {
		jai::script_value row = jai::script_value::make_map(nullptr, nullptr, eng_);
		auto& fields = row.as_map();
		const int columns = sqlite3_column_count(statement);
		for (int column = 0; column < columns; ++column) {
			const char* name = sqlite3_column_name(statement, column);
			fields[jai::script_value(std::string(name ? name : ""), eng_)] = column_value(statement, column);
		}
		return row;
	}

	std::shared_ptr<sqlite_connection> connection_;
	jai::engine* eng_ = nullptr;
};

template<size_t>
using database_argument = const jai::script_value&;

template<typename Body, size_t... I>
auto database_method(Body body, std::index_sequence<I...>) {
	return [body](database& self, const std::string& sql, database_argument<I>... params) {
		return body(self, sql, std::vector<jai::script_value>{params...});
	};
}

// one overload per argument count, 0 to 16 parameters after the SQL text
template<typename Binder, typename Body, size_t... Counts>
void bind_database_method(Binder& binder, const char* name, Body body, std::index_sequence<Counts...>) {
	(binder.method(name, database_method(body, std::make_index_sequence<Counts>{})), ...);
}

inline void register_database(jai::engine& eng, const std::filesystem::path& app_dir) {
	auto binder = jai::dynamic_binder<database>(eng, "Database");
	binder.constructor<>();
	constexpr auto counts = std::make_index_sequence<17>{};
	bind_database_method(binder, "execute", [](database& self, const std::string& sql, const std::vector<jai::script_value>& params) {
		return self.execute(sql, params);
	}, counts);
	bind_database_method(binder, "query", [](database& self, const std::string& sql, const std::vector<jai::script_value>& params) {
		return self.query(sql, params);
	}, counts);
	bind_database_method(binder, "query_one", [](database& self, const std::string& sql, const std::vector<jai::script_value>& params) {
		return self.query_one(sql, params);
	}, counts);
	bind_database_method(binder, "value", [](database& self, const std::string& sql, const std::vector<jai::script_value>& params) {
		return self.value(sql, params);
	}, counts);
	binder.method("transaction", [](database& self, const jai::script_value& fn) { return self.transaction(fn); });
	binder.method("begin", [](database& self) { self.begin(); });
	binder.method("commit", [](database& self) { self.commit(); });
	binder.method("rollback", [](database& self) { self.rollback(); });
	binder.method("last_insert_id", [](database& self) { return self.last_insert_id(); });
	binder.method("changes", [](database& self) { return self.changes(); });
	binder.method("in_transaction", [](database& self) { return self.in_transaction(); });
	binder.method("is_open", [](const database& self) { return self.is_open(); });
	binder.method("close", [](database& self) { self.close(); });
	binder.build();
	jai::engine* engine_pointer = &eng;
	eng.add_function("open_database", [engine_pointer, app_dir](const std::string& path) {
		std::string resolved = path;
		if (path != ":memory:" && path.rfind("file:", 0) != 0 && std::filesystem::path(path).is_relative()) {
			resolved = (app_dir / path).lexically_normal().string();
		}
		return std::make_shared<database>(database::open(resolved, engine_pointer));
	}, jai::filesystem);
}

} // namespace jaiweb
