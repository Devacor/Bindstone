#include "profile.as"

// treap from completely-unscientific-benchmarks; the Lua reference's paired returns are out-parameters here
class Tree {
	Node@ root;
}

class Node {
	int x;
	int y;
	Node@ left;
	Node@ right;
	Node(int a_x, int a_y) {
		x = a_x;
		y = a_y;
	}
}

// AngelScript ships no random generator: a 32-bit LCG gives the node priorities (the checksum is independent of them)
uint prioritySeed = 1;

int next_priority() {
	prioritySeed = prioritySeed * 1664525 + 1013904223;
	return int(prioritySeed >> 1);
}

void merge(Node@ lower, Node@ greater, Node@ &out result) {
	if (lower is null) {
		@result = greater;
	} else if (greater is null) {
		@result = lower;
	} else if (lower.y < greater.y) {
		@result = lower;
		merge(lower.right, greater, lower.right);
	} else {
		@result = greater;
		merge(lower, greater.left, greater.left);
	}
}

void split(Node@ orig, Node@ &out lower, Node@ &out greaterOrEqual, int value) {
	if (orig is null) {
		@lower = null;
		@greaterOrEqual = null;
	} else if (orig.x < value) {
		@lower = orig;
		split(orig.right, orig.right, greaterOrEqual, value);
	} else {
		@greaterOrEqual = orig;
		split(orig.left, lower, orig.left, value);
	}
}

bool has_value(Tree@ tree, int x) {
	Node@ lower;
	Node@ equalGreater;
	Node@ equal;
	Node@ greater;
	split(tree.root, lower, equalGreater, x);
	split(equalGreater, equal, greater, x + 1);
	bool result = equal !is null;
	Node@ tmp;
	merge(lower, equal, tmp);
	merge(tmp, greater, tree.root);
	return result;
}

void insert(Tree@ tree, int x) {
	Node@ lower;
	Node@ equalGreater;
	Node@ equal;
	Node@ greater;
	split(tree.root, lower, equalGreater, x);
	split(equalGreater, equal, greater, x + 1);
	if (equal is null) {
		@equal = Node(x, next_priority());
	}
	Node@ tmp;
	merge(lower, equal, tmp);
	merge(tmp, greater, tree.root);
}

void erase(Tree@ tree, int x) {
	Node@ lower;
	Node@ equalGreater;
	Node@ equal;
	Node@ greater;
	split(tree.root, lower, equalGreater, x);
	split(equalGreater, equal, greater, x + 1);
	merge(lower, greater, tree.root);
}

int tree_main() {
	Tree tree;
	int cur = 5;
	int res = 0;
	for (int i = 1; i <= 20000; ++i) {
		int a = i % 3;
		cur = (cur * 57 + 43) % 10007;
		if (a == 0) {
			insert(tree, cur);
		} else if (a == 1) {
			erase(tree, cur);
		} else if (has_value(tree, cur)) {
			res = res + 1;
		}
	}
	return res;
}

int result = 0;

void main() {
	profile_it("tree", function() { result = tree_main(); });
	print("checksum " + result);
}
