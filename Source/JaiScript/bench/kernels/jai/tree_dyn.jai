include "profile.jai";

// treap from completely-unscientific-benchmarks; the Lua reference's paired returns are out-params here
class Node {
	var x = 0;
	var y = 0;
	var left = null;
	var right = null;
}

function merge(var lower, var greater, var& result) {
	if (lower == null) {
		result = greater;
	} else if (greater == null) {
		result = lower;
	} else if (lower.y < greater.y) {
		result = lower;
		merge(lower.right, greater, lower.right);
	} else {
		result = greater;
		merge(lower, greater.left, greater.left);
	}
}

function split(var orig, var& lower, var& greater_or_equal, var value) {
	if (orig == null) {
		lower = null;
		greater_or_equal = null;
	} else if (orig.x < value) {
		lower = orig;
		split(orig.right, orig.right, greater_or_equal, value);
	} else {
		greater_or_equal = orig;
		split(orig.left, lower, orig.left, value);
	}
}

function has_value(var& root, var x) {
	var lower = null;
	var equal_greater = null;
	var equal = null;
	var greater = null;
	split(root, lower, equal_greater, x);
	split(equal_greater, equal, greater, x + 1);
	var res = equal != null;
	var tmp = null;
	merge(lower, equal, tmp);
	merge(tmp, greater, root);
	return res;
}

function insert(var& root, var x) {
	var lower = null;
	var equal_greater = null;
	var equal = null;
	var greater = null;
	split(root, lower, equal_greater, x);
	split(equal_greater, equal, greater, x + 1);
	if (equal == null) {
		equal = new Node();
		equal.x = x;
		equal.y = random_int(0, 2147483647);
	}
	var tmp = null;
	merge(lower, equal, tmp);
	merge(tmp, greater, root);
}

function erase(var& root, var x) {
	var lower = null;
	var equal_greater = null;
	var equal = null;
	var greater = null;
	split(root, lower, equal_greater, x);
	split(equal_greater, equal, greater, x + 1);
	merge(lower, greater, root);
}

function tree_main() {
	var root = null;
	var cur = 5;
	var res = 0;
	for (var i = 1; i <= 20000; ++i) {
		var a = i % 3;
		cur = (cur * 57 + 43) % 10007;
		if (a == 0) {
			insert(root, cur);
		} else if (a == 1) {
			erase(root, cur);
		} else if (has_value(root, cur)) {
			res = res + 1;
		}
	}
	return res;
}

var result = 0;
profile_it("tree", []() { result = tree_main(); });
print("checksum {}", result);
