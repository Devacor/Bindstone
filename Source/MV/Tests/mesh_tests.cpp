// MV::Mesh: the operations a mesh editor runs on a Drawable's points and triangles.

#include <jaiscript/testing/foundry.hpp>
#include <cmath>
#include "MV/Render/mesh.h"

using namespace jai::foundry;
namespace Mesh = MV::Mesh;

namespace mv_tests {

	class mesh_suite : public suite {
	public:
		mesh_suite() : suite("MV Mesh") {}

		// Two triangles making a square 0..10, its texture laid over it, and a seam on the diagonal: the second
		// triangle has its own copies of the diagonal's corners, welded to the first's.
		static Mesh::Data square() {
			Mesh::Data result;
			auto add = [&](float a_x, float a_y) {
				result.points.push_back(MV::DrawPoint(MV::point(a_x, a_y), MV::Color(1.0f, 1.0f, 1.0f, 1.0f), MV::TexturePoint(a_x / 10.0f, a_y / 10.0f)));
			};
			add(0.0f, 0.0f);
			add(10.0f, 0.0f);
			add(10.0f, 10.0f);
			add(0.0f, 0.0f);
			add(10.0f, 10.0f);
			add(0.0f, 10.0f);
			result.indices = { 0, 1, 2, 3, 4, 5 };
			return result;
		}

		static bool near(float a_value, float a_wanted) {
			return std::abs(a_value - a_wanted) < 0.001f;
		}

		static float area(const Mesh::Data& a_mesh) {
			float result = 0.0f;
			for (size_t triangle = 0; triangle < a_mesh.triangleCount(); ++triangle) {
				const auto& a = a_mesh.points[a_mesh.indices[triangle * 3]];
				const auto& b = a_mesh.points[a_mesh.indices[triangle * 3 + 1]];
				const auto& c = a_mesh.points[a_mesh.indices[triangle * 3 + 2]];
				result += std::abs((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)) * 0.5f;
			}
			return result;
		}

		static bool sameWinding(const Mesh::Data& a_mesh) {
			int positive = 0;
			int negative = 0;
			for (size_t triangle = 0; triangle < a_mesh.triangleCount(); ++triangle) {
				const auto& a = a_mesh.points[a_mesh.indices[triangle * 3]];
				const auto& b = a_mesh.points[a_mesh.indices[triangle * 3 + 1]];
				const auto& c = a_mesh.points[a_mesh.indices[triangle * 3 + 2]];
				const auto signedArea = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
				(signedArea > 0.0f ? positive : negative) += 1;
			}
			return positive == 0 || negative == 0;
		}

		void forge_tests() override {
			test("welded vertices are one place, and an island is what triangles join", [] {
				auto mesh = square();
				auto placeOf = Mesh::places(mesh);
				check_true(placeOf[0] == placeOf[3] && placeOf[2] == placeOf[4] && placeOf[0] != placeOf[1], "the seam's copies share a place");
				check_eq(size_t(2), Mesh::withWelded(mesh, { 0 }).size(), "picking a corner picks its copy");
				auto apart = Mesh::append(mesh, square());
				for (auto vertex : apart) {
					mesh.points[vertex].x += 100.0f;
				}
				check_eq(size_t(6), Mesh::island(mesh, 1).size(), "an island is the piece a vertex is part of");
				check_true(Mesh::island(mesh, 7) == apart, "and the other piece is another island");
			});

			test("the outside edges go round the square and not across its seam", [] {
				auto mesh = square();
				auto edges = Mesh::outsideEdges(mesh);
				check_eq(size_t(4), edges.size());
				auto placeOf = Mesh::places(mesh);
				for (auto&& [from, to] : edges) {
					check_false((placeOf[from] == placeOf[0] && placeOf[to] == placeOf[2]) || (placeOf[from] == placeOf[2] && placeOf[to] == placeOf[0]), "the diagonal is inside");
				}
			});

			test("extruding an edge adds a quad beyond it, and the texture runs on across it", [] {
				auto mesh = square();
				// the bottom edge: (10,10) and (0,10)
				auto made = Mesh::extrude(mesh, Mesh::withWelded(mesh, { 4, 5 }), MV::point(0.0f, 5.0f));
				check_eq(size_t(2), made.size(), "two new corners");
				check_eq(size_t(4), mesh.triangleCount(), "two new triangles");
				check_true(near(area(mesh), 150.0f), "the square grew by 10 by 5: " + std::to_string(area(mesh)));
				check_true(sameWinding(mesh), "wound as the rest");
				for (auto vertex : made) {
					check_true(near(mesh.points[vertex].y, 15.0f), "the new corners are 5 below");
					check_true(near(mesh.points[vertex].textureY, 1.5f) && near(mesh.points[vertex].textureX, mesh.points[vertex].x / 10.0f), "and their texture continues: " + std::to_string(mesh.points[vertex].textureY));
				}
				auto again = Mesh::extrude(mesh, made, MV::point(0.0f, 5.0f));
				check_true(again.size() == 2 && near(area(mesh), 200.0f), "the new edge is outside, so it extrudes again");
				check_true(Mesh::extrude(mesh, Mesh::withWelded(mesh, { 0, 2 }), MV::point(5.0f, 0.0f)).empty(), "an edge inside the mesh does not extrude");
			});

			test("subdividing splits selected triangles in four and splits a neighbour so no edge has a corner in its middle", [] {
				auto mesh = square();
				auto made = Mesh::subdivide(mesh, { 0, 1, 2 });
				check_true(near(area(mesh), 100.0f), "the area is unchanged");
				check_eq(size_t(4 + 2), mesh.triangleCount(), "four from the first, two from its neighbour across the diagonal");
				check_eq(size_t(3 + 1), made.size(), "a middle for each edge, and one on the neighbour's side of the seam");
				auto all = Mesh::subdivide(mesh, Mesh::all(mesh));
				check_true(near(area(mesh), 100.0f) && sameWinding(mesh) && !all.empty(), "subdividing everything keeps the area and the winding");
			});

			test("splitting an edge puts a corner on it in both triangles, each with its own texture side", [] {
				auto mesh = square();
				auto made = Mesh::splitEdge(mesh, 0, 2, 0.5f);
				check_eq(size_t(2), made.size(), "one per side of the seam");
				check_eq(size_t(4), mesh.triangleCount());
				for (auto vertex : made) {
					check_true(near(mesh.points[vertex].x, 5.0f) && near(mesh.points[vertex].y, 5.0f) && near(mesh.points[vertex].textureX, 0.5f), "in the middle of the diagonal");
				}
				check_true(near(area(mesh), 100.0f) && sameWinding(mesh));
				auto holder = Mesh::triangleAt(mesh, MV::point(4.0f, 2.0f));
				check_true(holder.has_value(), "the point is in a triangle");
				auto inside = Mesh::splitTriangle(mesh, holder.value_or(0), MV::point(4.0f, 2.0f));
				check_true(near(mesh.points[inside].textureX, 0.4f) && near(mesh.points[inside].textureY, 0.2f) && mesh.triangleCount() == 6 && near(area(mesh), 100.0f) && sameWinding(mesh), "a corner inside a triangle makes three of it");
			});

			test("deleting vertices takes their triangles, deleting triangles keeps shared corners", [] {
				auto mesh = square();
				Mesh::removeVertices(mesh, { 1 });
				check_true(mesh.points.size() == 5 && mesh.triangleCount() == 1, "one corner goes with its triangle");
				auto other = square();
				Mesh::removeTriangles(other, { 0, 1, 2 });
				check_true(other.triangleCount() == 1 && other.points.size() == 3, "a whole triangle goes with the corners no other triangle used");
				auto loose = square();
				loose.points.push_back(MV::DrawPoint(MV::point(50.0f, 50.0f)));
				Mesh::removeUnused(loose);
				check_eq(size_t(6), loose.points.size(), "a corner no triangle uses goes");
			});

			test("filling makes triangles over the selected corners", [] {
				Mesh::Data mesh;
				for (auto&& corner : { MV::point(0.0f, 0.0f), MV::point(10.0f, 0.0f), MV::point(10.0f, 10.0f), MV::point(0.0f, 10.0f) }) {
					mesh.points.push_back(MV::DrawPoint(corner));
				}
				check_false(Mesh::fill(mesh, { 0, 1 }), "two corners make nothing");
				check_true(Mesh::fill(mesh, Mesh::all(mesh)) && mesh.triangleCount() == 2 && near(area(mesh), 100.0f), "four make a square of two");
			});

			test("a concave outline triangulates to its own area", [] {
				// an L: 10 by 10 less its top right quarter
				std::vector<MV::Point<>> outline { MV::point(0.0f, 0.0f), MV::point(5.0f, 0.0f), MV::point(5.0f, 5.0f), MV::point(10.0f, 5.0f), MV::point(10.0f, 10.0f), MV::point(0.0f, 10.0f) };
				auto mesh = Mesh::polygon(outline);
				check_true(mesh.triangleCount() == 4 && near(area(mesh), 75.0f), "four triangles, 75 square: " + std::to_string(area(mesh)));
				std::reverse(outline.begin(), outline.end());
				check_true(near(area(Mesh::polygon(outline)), 75.0f), "either winding");
				check_true(near(mesh.points[4].textureX, 1.0f) && near(mesh.points[4].textureY, 1.0f), "the texture is laid over its bounds");
			});

			test("a ribbon follows its path with the texture across and along it", [] {
				auto mesh = Mesh::ribbon({ MV::point(0.0f, 0.0f), MV::point(0.0f, 10.0f), MV::point(0.0f, 20.0f) }, 4.0f, 3, 10.0f, MV::Color(1.0f, 1.0f, 1.0f, 1.0f), true);
				check_true(mesh.points.size() == 9 && mesh.triangleCount() == 8, "three rows three across");
				check_true(near(area(mesh), 80.0f) && sameWinding(mesh), "4 wide 20 long");
				check_true(near(mesh.points[0].x, 2.0f) || near(mesh.points[0].x, -2.0f), "the edges sit half the width out");
				check_true(near(mesh.points[1].textureX, 0.5f) && near(mesh.points[7].textureY, 2.0f), "across 0 to 1, along one repeat per 10");
				check_true(near(mesh.points[0].A, 0.0f) && near(mesh.points[4].A, 1.0f) && near(mesh.points[8].A, 0.0f), "the ends fade");
			});

			test("painting blends toward a colour by distance, and setting colours the selection", [] {
				auto mesh = square();
				Mesh::paint(mesh, MV::point(0.0f, 0.0f), 10.0f, MV::Color(1.0f, 0.0f, 0.0f, 0.0f), 1.0f);
				check_true(near(mesh.points[0].A, 0.0f) && near(mesh.points[3].A, 0.0f), "fully at the centre, welded copies alike");
				check_true(near(mesh.points[1].A, 1.0f) && near(mesh.points[2].A, 1.0f), "not at all at the radius and past it");
				auto half = square();
				Mesh::paint(half, MV::point(0.0f, 0.0f), 20.0f, MV::Color(1.0f, 1.0f, 1.0f, 0.0f), 0.5f);
				check_true(half.points[1].A < 1.0f && half.points[1].A > 0.5f, "part way between: " + std::to_string(half.points[1].A));
				Mesh::setColor(half, { 2 }, MV::Color(0.0f, 0.0f, 1.0f, 0.5f));
				check_true(near(half.points[2].B, 1.0f) && near(half.points[2].A, 0.5f) && near(half.points[2].textureX, 1.0f), "colour only");
			});

			test("duplicate, merge and add a corner", [] {
				auto mesh = square();
				auto copies = Mesh::duplicate(mesh, { 0, 1, 2 }, MV::point(20.0f, 0.0f));
				check_true(copies.size() == 3 && mesh.triangleCount() == 3 && near(area(mesh), 150.0f), "the whole triangle is copied over");
				auto merged = square();
				Mesh::merge(merged, Mesh::withWelded(merged, { 1, 2 }));
				check_true(merged.triangleCount() == 1 && near(area(merged), 50.0f), "two corners merged leave the triangle that had both with no area, and it goes");
				auto grown = square();
				auto corner = Mesh::addVertex(grown, Mesh::withWelded(grown, { 1, 2 }), MV::DrawPoint(MV::point(15.0f, 5.0f)));
				check_true(grown.triangleCount() == 3 && near(area(grown), 125.0f) && sameWinding(grown), "a corner by a selected outside edge makes a triangle with it");
				check_true(near(grown.points[corner].textureX, 1.5f), "its texture continues");
				auto alone = square();
				Mesh::addVertex(alone, {}, MV::DrawPoint(MV::point(50.0f, 50.0f)));
				check_true(alone.points.size() == 7 && alone.triangleCount() == 2, "with nothing selected the corner is alone");
			});

			test("picking a corner, an edge and a triangle", [] {
				auto mesh = square();
				check_true(Mesh::vertexNear(mesh, MV::point(9.5f, 0.5f), 1.0f) == std::optional<size_t>(1));
				check_false(Mesh::vertexNear(mesh, MV::point(5.0f, 5.0f), 1.0f).has_value());
				auto edge = Mesh::edgeNear(mesh, MV::point(5.0f, 0.3f), 1.0f);
				check_true(edge && near(edge->along, 0.5f) && ((edge->from == 0 && edge->to == 1) || (edge->from == 1 && edge->to == 0)));
				check_true(Mesh::triangleAt(mesh, MV::point(8.0f, 2.0f)) == std::optional<size_t>(0) && Mesh::triangleAt(mesh, MV::point(2.0f, 8.0f)) == std::optional<size_t>(1));
				check_true(Mesh::inside(mesh, MV::BoxAABB<>(MV::point(-1.0f, -1.0f), MV::point(1.0f, 11.0f))).size() == 3, "a box takes the corners in it");
			});
		}
	};

}

using mv_tests_mesh_suite = mv_tests::mesh_suite;
FOUNDRY_REGISTER(mv_tests_mesh_suite)
