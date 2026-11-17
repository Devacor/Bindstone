// Grid layout: where a Grid puts its children.

#include <jaiscript/testing/foundry.hpp>
#include "mv_scene_host.hpp"
#include "MV/Render/Scene/grid.h"

using namespace jai::foundry;
using MV::Scene::Grid;
using MV::Scene::Node;

namespace mv_tests {

	class grid_suite : public suite {
	public:
		grid_suite() : suite("MV Grid") {}

		void forge_tests() override {
			test("a line steps down by the cell's height and the padding above and below it", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto grid = root->attach<Grid>()->columns(1)->cellSize(MV::size(50.0f, 20.0f));
				grid->padding(MV::point(10.0f, 2.0f), MV::point(7.0f, 3.0f));
				auto first = root->make("first");
				auto second = root->make("second");
				auto third = root->make("third");
				grid->layoutCells();
				check_near(25.0f, second->position().y - first->position().y, 1e-3f);
				check_near(25.0f, third->position().y - second->position().y, 1e-3f);
				check_near(first->position().x, second->position().x, 1e-3f);
			});

			test("cells along a line step across by the cell's width and the padding left and right of it", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto grid = root->attach<Grid>()->columns(2)->cellSize(MV::size(50.0f, 20.0f));
				grid->padding(MV::point(10.0f, 2.0f), MV::point(7.0f, 3.0f));
				auto first = root->make("first");
				auto second = root->make("second");
				auto third = root->make("third");
				grid->layoutCells();
				check_near(67.0f, second->position().x - first->position().x, 1e-3f);
				check_near(first->position().y, second->position().y, 1e-3f);
				check_near(25.0f, third->position().y - first->position().y, 1e-3f);
				check_near(first->position().x, third->position().x, 1e-3f);
			});

			test("even padding lays out as it always has", [] {
				SceneHost host;
				auto root = Node::make(host.renderer, "root");
				auto grid = root->attach<Grid>()->columns(1)->cellSize(MV::size(100.0f, 27.0f))->padding(MV::size(2.0f, 2.0f));
				auto first = root->make("first");
				auto second = root->make("second");
				grid->layoutCells();
				check_near(31.0f, second->position().y - first->position().y, 1e-3f);
			});
		}
	};

}

using grid_suite = mv_tests::grid_suite;
FOUNDRY_REGISTER(grid_suite)
