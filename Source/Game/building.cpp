#include "building.h"

bool Creature::draw() {
	if (ai) {
		ai->draw();
	}
	return true;
}
