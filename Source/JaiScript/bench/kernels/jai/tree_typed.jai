include "profile.jai";

// treap from completely-unscientific-benchmarks; the Lua reference's paired returns are out-params here
class Node {
	int x = 0;
	int y = 0;
	shared_ptr<Node> left = null;
	shared_ptr<Node> right = null;
}

void merge(shared_ptr<Node> lower, shared_ptr<Node> greater, shared_ptr<Node>& result) {
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

void split(shared_ptr<Node> orig, shared_ptr<Node>& lower, shared_ptr<Node>& greater_or_equal, int value) {
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

bool has_value(shared_ptr<Node>& root, int x) {
	shared_ptr<Node> lower = null;
	shared_ptr<Node> equal_greater = null;
	shared_ptr<Node> equal = null;
	shared_ptr<Node> greater = null;
	split(root, lower, equal_greater, x);
	split(equal_greater, equal, greater, x + 1);
	bool res = equal != null;
	shared_ptr<Node> tmp = null;
	merge(lower, equal, tmp);
	merge(tmp, greater, root);
	return res;
}

void insert(shared_ptr<Node>& root, int x) {
	shared_ptr<Node> lower = null;
	shared_ptr<Node> equal_greater = null;
	shared_ptr<Node> equal = null;
	shared_ptr<Node> greater = null;
	split(root, lower, equal_greater, x);
	split(equal_greater, equal, greater, x + 1);
	if (equal == null) {
		equal = new Node();
		equal.x = x;
		equal.y = math::random_int(0, 2147483647);
	}
	shared_ptr<Node> tmp = null;
	merge(lower, equal, tmp);
	merge(tmp, greater, root);
}

void erase(shared_ptr<Node>& root, int x) {
	shared_ptr<Node> lower = null;
	shared_ptr<Node> equal_greater = null;
	shared_ptr<Node> equal = null;
	shared_ptr<Node> greater = null;
	split(root, lower, equal_greater, x);
	split(equal_greater, equal, greater, x + 1);
	merge(lower, greater, root);
}

int tree_main() {
	shared_ptr<Node> root = null;
	int cur = 5;
	int res = 0;
	for (int i = 1; i <= 20000; ++i) {
		int a = i % 3;
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

int result = 0;
profile_it("tree", []() { result = tree_main(); });
print("checksum {}", result);
