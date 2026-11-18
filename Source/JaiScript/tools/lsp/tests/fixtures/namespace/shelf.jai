import "shop.jai";

namespace shop {
	int shelf_total() {
		Item stocked;
		return tax(stocked.price) + base;
	}
}

print(shop::shelf_total());
