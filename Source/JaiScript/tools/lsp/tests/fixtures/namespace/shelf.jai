import "shop.jai";

namespace shop {
	int shelf_total() {
		Item stocked;
		return tax(stocked.price) + base;
	}
	Kind shelf_kind() { return Kind::fancy; }
}

print(shop::shelf_total());
