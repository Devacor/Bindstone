include "profile.jai";

// member and index chains read through: n.left.value, n.right.left.value, w.entities[k].value, w.grid[k][k].value
class Leaf {
	var value = 1;
}
class Node {
	var left = null;
	var right = null;
	var value = 0;
}
class World {
	var entities = [];
	var grid = [];
}

var leaf = Leaf();
var mid = Node();
mid.left = leaf;
var root = Node();
root.left = leaf;
root.right = mid;
var world = World();
for (var i = 0; i < 8; ++i) {
	world.entities.push(Leaf());
	var row = [];
	for (var j = 0; j < 8; ++j) { row.push(Leaf()); }
	world.grid.push(row);
}

function chains(var n, var w) {
	var total = 0;
	for (var i = 0; i < 1000; ++i) {
		var k = i % 8;
		total += n.left.value + n.right.left.value + w.entities[k].value + w.grid[k][k].value;
	}
	return total;
}

var result = 0;
profile_it("chain_reads", function() { result = chains(root, world); });
print("checksum {}", result);
