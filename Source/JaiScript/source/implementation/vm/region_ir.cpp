#include <jaiscript/vm/region_ir.hpp>
#include <algorithm>

namespace jai::vm {

region_cfg control_flow(const region_ir& r) {
	const std::vector<ir_op>& ops = r.ops;
	region_cfg g;
	const size_t n_ops = ops.size();
	g.back_node = n_ops; g.exit_node = n_ops + 1;
	auto node_of = [&](uint32_t target) -> size_t { return target > r.back || target < r.first ? g.exit_node : static_cast<size_t>(target - r.first); };
	g.succ.assign(n_ops + 1, std::array<size_t, 2>{ g.exit_node, g.exit_node });
	for (size_t k = 0; k < n_ops; ++k) {
		const ir_op& op = ops[k];
		switch (op.kind) {
		case ir::exit: case ir::bail: break;   // no successor: the block ends (a bail: the general form takes the iteration from here)
		case ir::jump: g.succ[k][0] = node_of(op.target); break;
		case ir::range:
		case ir::cmp: g.succ[k][0] = k + 1; g.succ[k][1] = op.head ? g.exit_node : node_of(op.target); break;
		default: g.succ[k][0] = k + 1; break;
		}
	}
	g.succ[g.back_node][0] = 0;
	if (r.span) {   // a span: whatever would continue to the back node leaves the region
		g.succ[g.back_node] = std::array<size_t, 2>{ g.exit_node, g.exit_node };
		for (size_t k = 0; k < n_ops; ++k) { for (size_t& s : g.succ[k]) { if (s == g.back_node) { s = g.exit_node; } } }
	}
	g.preds.assign(n_ops + 1, {});
	for (size_t k = 0; k <= n_ops; ++k) { for (size_t s : g.succ[k]) { if (s <= n_ops && s != 0) { g.preds[s].push_back(k); } } }
	return g;
}

// the must-define dataflow over the graph is the rule (a definition in an untaken branch does not reach a read after the
// join), and it is the rule every pass over the region must keep
region_verdict verify(const region_ir& r, const std::vector<chunk::cell_loop::cell>& cells) {
	const std::vector<ir_op>& ops = r.ops;
	const size_t n_ops = ops.size();
	const region_cfg g = control_flow(r);
	std::vector<uint32_t> tracked;   // the slots undefined at entry
	for (const auto& cl : cells) { if (cl.env_operand == k_invalid_u32 && (cl.temp || cl.first_decl) && std::find(tracked.begin(), tracked.end(), cl.slot) == tracked.end()) { tracked.push_back(cl.slot); } }
	auto tracked_at = [&](uint32_t slot) -> int { for (size_t t = 0; t < tracked.size(); ++t) { if (tracked[t] == slot) return static_cast<int>(t); } return -1; };
	std::vector<std::vector<bool>> in(n_ops + 1, std::vector<bool>(tracked.size(), true));   // the meet's identity; the entry defines nothing tracked
	std::fill(in[0].begin(), in[0].end(), false);
	for (bool changed = true; changed;) {
		changed = false;
		for (size_t k = 1; k <= n_ops; ++k) {
			if (g.preds[k].empty()) continue;
			for (size_t t = 0; t < tracked.size(); ++t) {
				bool all = true;
				for (size_t p : g.preds[k]) { if (!(in[p][t] || (p < n_ops && ops[p].writes == tracked[t]))) { all = false; break; } }
				if (in[k][t] != all) { in[k][t] = all; changed = true; }
			}
		}
	}
	std::vector<std::pair<uint32_t, uint32_t>> snapshots;   // slot, the read's ip (the guard is open until it)
	for (size_t k = 0; k < n_ops; ++k) {
		const uint32_t ip = r.first + static_cast<uint32_t>(k);
		const ir_op& op = ops[k];
		for (uint8_t x = 0; x < op.nreads; ++x) {
			const int t = tracked_at(op.reads[x]);
			if (t >= 0 && !in[k][static_cast<size_t>(t)]) { return region_verdict{ chunk::cell_refusal_malformed, ip }; }
		}
		if (op.writes != k_invalid_u32) {
			for (const auto& s : snapshots) { if (s.first == op.writes && ip < s.second) { return region_verdict{ chunk::cell_refusal_malformed, ip }; } }
		}
		if (op.kind == ir::guard) {
			for (const auto& s : snapshots) { if (s.first == op.a && ip < s.second) { return region_verdict{ chunk::cell_refusal_malformed, ip }; } }
			if (op.guarded == 0xFFFFFFu) { return region_verdict{ chunk::cell_refusal_malformed, ip }; }
			snapshots.emplace_back(op.a, r.head + op.guarded);
		}
	}
	for (const auto& op : ops) {
		if (op.container == k_invalid_u32) continue;
		for (const auto& w : ops) {
			if (w.writes != op.container) continue;
			if (w.kind == ir::rebind && &w < &op) continue;   // an alias slot: its sites follow its declaration, and the must-define dataflow above holds every read to a defined alias
			return region_verdict{ chunk::cell_refusal_effect, r.head };
		}   // a container binding rewritten inside the body
	}
	return region_verdict{};
}

std::string listing(const region_ir& r) {
	std::string text;
	for (size_t k = 0; k < r.ops.size(); ++k) {
		const ir_op& op = r.ops[k];
		text += std::to_string(k) + ":" + ir_name(op.kind);
		if (op.kind == ir::cmp || op.kind == ir::jump || op.kind == ir::exit || op.kind == ir::bail || op.kind == ir::range) {
			text += op.kind == ir::exit || op.kind == ir::bail || op.head || op.target > r.back || op.target < r.first ? "->exit" : op.target == r.back ? (r.span ? "->exit" : "->back") : "->" + std::to_string(op.target - r.first);
		}
		if (op.writes != k_invalid_u32) { text += " w" + std::to_string(op.writes); }
		for (uint8_t x = 0; x < op.nreads; ++x) { text += (x ? "," : " r") + std::to_string(op.reads[x]); }
		text += ";";
	}
	return text;
}

std::vector<chunk::cell_mask> live_temps(const region_ir& r, const std::vector<chunk::cell_loop::cell>& cells,
                                         const std::vector<uint32_t>& slot_to_cell, const std::vector<std::pair<uint32_t, uint32_t>>& defs,
                                         const std::vector<uint32_t>& copy_at, uint32_t copy_head, size_t copy_count) {
	std::vector<chunk::cell_mask> live(copy_count, chunk::cell_mask{});
	std::vector<uint32_t> cur = slot_to_cell;
	std::vector<size_t> def_end(cells.size(), 0);   // per compiler value: the copy offset after its defining instruction(s)
	size_t nd = 0;
	for (size_t k = 0; k < r.ops.size(); ++k) {
		const ir_op& op = r.ops[k];
		const size_t ip = r.first + k;
		const size_t here_end = copy_at[ip + 1 - r.head] - copy_head;   // the copy offset after this operation's instructions
		for (uint8_t x = 0; x < op.nreads; ++x) {
			const uint32_t cl = op.reads[x] < cur.size() ? cur[op.reads[x]] : k_invalid_u32;
			if (cl == k_invalid_u32 || !cells[cl].temp) continue;
			for (size_t m = def_end[cl]; m < here_end && m < live.size(); ++m) { live[m].set(cl); }
		}
		while (nd < defs.size() && defs[nd].first == ip) { cur[cells[defs[nd].second].slot] = defs[nd].second; def_end[defs[nd].second] = here_end; ++nd; }
	}
	return live;
}

} // namespace jai::vm
