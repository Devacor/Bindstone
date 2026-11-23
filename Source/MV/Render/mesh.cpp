#include "mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>

namespace MV {
	namespace Mesh {

		namespace {
			struct PlaceSets {
				std::vector<size_t> parent;

				explicit PlaceSets(size_t a_count) : parent(a_count) {
					std::iota(parent.begin(), parent.end(), size_t(0));
				}

				size_t find(size_t a_item) {
					while (parent[a_item] != a_item) {
						parent[a_item] = parent[parent[a_item]];
						a_item = parent[a_item];
					}
					return a_item;
				}

				void join(size_t a_first, size_t a_second) {
					auto first = find(a_first);
					auto second = find(a_second);
					if (first != second) {
						parent[std::max(first, second)] = std::min(first, second);
					}
				}
			};

			void joinWelded(const Data& a_mesh, PlaceSets& a_sets, PointPrecision a_weld) {
				std::vector<size_t> byX(a_mesh.points.size());
				std::iota(byX.begin(), byX.end(), size_t(0));
				std::sort(byX.begin(), byX.end(), [&](size_t a_first, size_t a_second) { return a_mesh.points[a_first].x < a_mesh.points[a_second].x; });
				for (size_t i = 0; i < byX.size(); ++i) {
					const auto& first = a_mesh.points[byX[i]];
					for (size_t j = i + 1; j < byX.size() && a_mesh.points[byX[j]].x - first.x < a_weld; ++j) {
						if (distance(first.point(), a_mesh.points[byX[j]].point()) < a_weld) {
							a_sets.join(byX[i], byX[j]);
						}
					}
				}
			}

			std::pair<size_t, size_t> placePair(const std::vector<size_t>& a_places, size_t a_first, size_t a_second) {
				return std::minmax(a_places[a_first], a_places[a_second]);
			}

			PointPrecision cross(const Point<>& a_origin, const Point<>& a_first, const Point<>& a_second) {
				return (a_first.x - a_origin.x) * (a_second.y - a_origin.y) - (a_first.y - a_origin.y) * (a_second.x - a_origin.x);
			}

			// The weights of a point against a triangle's corners; none for a triangle with no area.
			std::optional<std::array<PointPrecision, 3>> weightsIn(const Point<>& a_first, const Point<>& a_second, const Point<>& a_third, const Point<>& a_point) {
				const auto area = cross(a_first, a_second, a_third);
				if (std::abs(area) < 1e-6f) {
					return std::nullopt;
				}
				const auto second = cross(a_point, a_third, a_first) / area;
				const auto third = cross(a_point, a_first, a_second) / area;
				return std::array<PointPrecision, 3>{ 1.0f - second - third, second, third };
			}

			// A triangle's texture mapping carried to any point of its plane, else the fallback's coordinates.
			TexturePoint textureAt(const Data& a_mesh, size_t a_triangle, const Point<>& a_point, const TexturePoint& a_fallback) {
				const auto& first = a_mesh.points[a_mesh.indices[a_triangle * 3]];
				const auto& second = a_mesh.points[a_mesh.indices[a_triangle * 3 + 1]];
				const auto& third = a_mesh.points[a_mesh.indices[a_triangle * 3 + 2]];
				auto weights = weightsIn(first.point(), second.point(), third.point(), a_point);
				if (!weights) {
					return a_fallback;
				}
				return TexturePoint(
					first.textureX * (*weights)[0] + second.textureX * (*weights)[1] + third.textureX * (*weights)[2],
					first.textureY * (*weights)[0] + second.textureY * (*weights)[1] + third.textureY * (*weights)[2]);
			}

			DrawPoint between(const DrawPoint& a_first, const DrawPoint& a_second, PointPrecision a_along) {
				DrawPoint result;
				result.x = a_first.x + (a_second.x - a_first.x) * a_along;
				result.y = a_first.y + (a_second.y - a_first.y) * a_along;
				result.z = a_first.z + (a_second.z - a_first.z) * a_along;
				result.R = a_first.R + (a_second.R - a_first.R) * a_along;
				result.G = a_first.G + (a_second.G - a_first.G) * a_along;
				result.B = a_first.B + (a_second.B - a_first.B) * a_along;
				result.A = a_first.A + (a_second.A - a_first.A) * a_along;
				result.textureX = a_first.textureX + (a_second.textureX - a_first.textureX) * a_along;
				result.textureY = a_first.textureY + (a_second.textureY - a_first.textureY) * a_along;
				return result;
			}

			struct OutsideEdge {
				size_t from;
				size_t to;
				size_t triangle;
			};

			std::vector<OutsideEdge> outsideEdgesWithTriangles(const Data& a_mesh, const std::vector<size_t>& a_places) {
				std::map<std::pair<size_t, size_t>, int> uses;
				for (size_t triangle = 0; triangle < a_mesh.triangleCount(); ++triangle) {
					for (size_t corner = 0; corner < 3; ++corner) {
						++uses[placePair(a_places, a_mesh.indices[triangle * 3 + corner], a_mesh.indices[triangle * 3 + (corner + 1) % 3])];
					}
				}
				std::vector<OutsideEdge> result;
				for (size_t triangle = 0; triangle < a_mesh.triangleCount(); ++triangle) {
					for (size_t corner = 0; corner < 3; ++corner) {
						size_t from = a_mesh.indices[triangle * 3 + corner];
						size_t to = a_mesh.indices[triangle * 3 + (corner + 1) % 3];
						if (a_places[from] != a_places[to] && uses[placePair(a_places, from, to)] == 1) {
							result.push_back({ from, to, triangle });
						}
					}
				}
				return result;
			}

			// The triangle's corners turned so the one at a_first comes first.
			std::array<size_t, 3> turnedTo(const std::array<size_t, 3>& a_corners, size_t a_first) {
				return { a_corners[a_first], a_corners[(a_first + 1) % 3], a_corners[(a_first + 2) % 3] };
			}
		}

		bool Data::operator==(const Data& a_other) const {
			if (points.size() != a_other.points.size() || indices != a_other.indices) {
				return false;
			}
			for (size_t i = 0; i < points.size(); ++i) {
				if (!(points[i].point() == a_other.points[i].point()) || !(points[i].color() == a_other.points[i].color()) || !(points[i].texturePoint() == a_other.points[i].texturePoint())) {
					return false;
				}
			}
			return true;
		}

		std::vector<size_t> places(const Data& a_mesh, PointPrecision a_weld) {
			PlaceSets sets(a_mesh.points.size());
			joinWelded(a_mesh, sets, a_weld);
			std::vector<size_t> result(a_mesh.points.size());
			for (size_t i = 0; i < result.size(); ++i) {
				result[i] = sets.find(i);
			}
			return result;
		}

		Selection withWelded(const Data& a_mesh, const Selection& a_selection, PointPrecision a_weld) {
			auto placeOf = places(a_mesh, a_weld);
			std::set<size_t> chosen;
			for (auto vertex : a_selection) {
				if (vertex < placeOf.size()) {
					chosen.insert(placeOf[vertex]);
				}
			}
			Selection result;
			for (size_t i = 0; i < placeOf.size(); ++i) {
				if (chosen.contains(placeOf[i])) {
					result.insert(i);
				}
			}
			return result;
		}

		Selection island(const Data& a_mesh, size_t a_vertex, PointPrecision a_weld) {
			PlaceSets sets(a_mesh.points.size());
			joinWelded(a_mesh, sets, a_weld);
			for (size_t triangle = 0; triangle < a_mesh.triangleCount(); ++triangle) {
				sets.join(a_mesh.indices[triangle * 3], a_mesh.indices[triangle * 3 + 1]);
				sets.join(a_mesh.indices[triangle * 3], a_mesh.indices[triangle * 3 + 2]);
			}
			Selection result;
			if (a_vertex >= a_mesh.points.size()) {
				return result;
			}
			const auto root = sets.find(a_vertex);
			for (size_t i = 0; i < a_mesh.points.size(); ++i) {
				if (sets.find(i) == root) {
					result.insert(i);
				}
			}
			return result;
		}

		Selection all(const Data& a_mesh) {
			Selection result;
			for (size_t i = 0; i < a_mesh.points.size(); ++i) {
				result.insert(i);
			}
			return result;
		}

		Selection inside(const Data& a_mesh, const BoxAABB<>& a_box) {
			Selection result;
			const auto low = MV::point(std::min(a_box.minPoint.x, a_box.maxPoint.x), std::min(a_box.minPoint.y, a_box.maxPoint.y));
			const auto high = MV::point(std::max(a_box.minPoint.x, a_box.maxPoint.x), std::max(a_box.minPoint.y, a_box.maxPoint.y));
			for (size_t i = 0; i < a_mesh.points.size(); ++i) {
				const auto& point = a_mesh.points[i];
				if (point.x >= low.x && point.x <= high.x && point.y >= low.y && point.y <= high.y) {
					result.insert(i);
				}
			}
			return result;
		}

		Point<> centre(const Data& a_mesh, const Selection& a_selection) {
			auto placeOf = places(a_mesh);
			std::set<size_t> counted;
			Point<> result;
			for (auto vertex : a_selection) {
				if (vertex < a_mesh.points.size() && counted.insert(placeOf[vertex]).second) {
					result += a_mesh.points[vertex].point();
				}
			}
			return counted.empty() ? result : result / static_cast<PointPrecision>(counted.size());
		}

		std::optional<size_t> vertexNear(const Data& a_mesh, const Point<>& a_point, PointPrecision a_within) {
			std::optional<size_t> result;
			PointPrecision nearest = a_within;
			for (size_t i = 0; i < a_mesh.points.size(); ++i) {
				const auto away = distance(a_mesh.points[i].point(), MV::point(a_point.x, a_point.y, a_mesh.points[i].z));
				if (away <= nearest) {
					nearest = away;
					result = i;
				}
			}
			return result;
		}

		std::optional<size_t> triangleAt(const Data& a_mesh, const Point<>& a_point) {
			// the last drawn is the one on top
			for (size_t triangle = a_mesh.triangleCount(); triangle-- > 0;) {
				auto weights = weightsIn(a_mesh.points[a_mesh.indices[triangle * 3]].point(), a_mesh.points[a_mesh.indices[triangle * 3 + 1]].point(), a_mesh.points[a_mesh.indices[triangle * 3 + 2]].point(), a_point);
				if (weights && (*weights)[0] >= 0.0f && (*weights)[1] >= 0.0f && (*weights)[2] >= 0.0f) {
					return triangle;
				}
			}
			return std::nullopt;
		}

		std::optional<EdgeHit> edgeNear(const Data& a_mesh, const Point<>& a_point, PointPrecision a_within) {
			std::optional<EdgeHit> result;
			PointPrecision nearest = a_within;
			for (size_t triangle = 0; triangle < a_mesh.triangleCount(); ++triangle) {
				for (size_t corner = 0; corner < 3; ++corner) {
					size_t from = a_mesh.indices[triangle * 3 + corner];
					size_t to = a_mesh.indices[triangle * 3 + (corner + 1) % 3];
					const auto start = a_mesh.points[from].point();
					const auto end = a_mesh.points[to].point();
					const auto lengthSquared = (end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y);
					if (lengthSquared < 1e-8f) {
						continue;
					}
					auto along = ((a_point.x - start.x) * (end.x - start.x) + (a_point.y - start.y) * (end.y - start.y)) / lengthSquared;
					along = std::clamp(along, 0.0f, 1.0f);
					const auto onEdge = MV::point(start.x + (end.x - start.x) * along, start.y + (end.y - start.y) * along);
					const auto away = distance(onEdge, MV::point(a_point.x, a_point.y));
					if (away <= nearest) {
						nearest = away;
						result = EdgeHit{ from, to, along };
					}
				}
			}
			return result;
		}

		std::vector<std::pair<size_t, size_t>> outsideEdges(const Data& a_mesh, PointPrecision a_weld) {
			std::vector<std::pair<size_t, size_t>> result;
			for (auto&& edge : outsideEdgesWithTriangles(a_mesh, places(a_mesh, a_weld))) {
				result.emplace_back(edge.from, edge.to);
			}
			return result;
		}

		void setColor(Data& a_mesh, const Selection& a_selection, const Color& a_color) {
			for (auto vertex : a_selection) {
				if (vertex < a_mesh.points.size()) {
					a_mesh.points[vertex] = a_color;
				}
			}
		}

		void paint(Data& a_mesh, const Point<>& a_centre, PointPrecision a_radius, const Color& a_color, PointPrecision a_strength) {
			if (a_radius <= 0.0f) {
				return;
			}
			for (auto&& point : a_mesh.points) {
				const auto away = distance(MV::point(point.x, point.y), MV::point(a_centre.x, a_centre.y)) / a_radius;
				if (away >= 1.0f) {
					continue;
				}
				const auto amount = std::clamp(a_strength, 0.0f, 1.0f) * (1.0f - away * away * (3.0f - 2.0f * away));
				point.R += (a_color.R - point.R) * amount;
				point.G += (a_color.G - point.G) * amount;
				point.B += (a_color.B - point.B) * amount;
				point.A += (a_color.A - point.A) * amount;
			}
		}

		void projectTexture(Data& a_mesh, const Selection& a_selection, const BoxAABB<>& a_area) {
			const auto width = a_area.maxPoint.x - a_area.minPoint.x;
			const auto height = a_area.maxPoint.y - a_area.minPoint.y;
			if (std::abs(width) < 1e-6f || std::abs(height) < 1e-6f) {
				return;
			}
			for (auto vertex : a_selection) {
				if (vertex < a_mesh.points.size()) {
					auto& point = a_mesh.points[vertex];
					point.textureX = (point.x - a_area.minPoint.x) / width;
					point.textureY = (point.y - a_area.minPoint.y) / height;
				}
			}
		}

		std::vector<std::optional<size_t>> removeVertices(Data& a_mesh, const Selection& a_selection) {
			std::vector<std::optional<size_t>> result(a_mesh.points.size());
			std::vector<DrawPoint> kept;
			for (size_t i = 0; i < a_mesh.points.size(); ++i) {
				if (!a_selection.contains(i)) {
					result[i] = kept.size();
					kept.push_back(a_mesh.points[i]);
				}
			}
			std::vector<std::uint32_t> keptIndices;
			for (size_t triangle = 0; triangle < a_mesh.triangleCount(); ++triangle) {
				const auto first = result[a_mesh.indices[triangle * 3]];
				const auto second = result[a_mesh.indices[triangle * 3 + 1]];
				const auto third = result[a_mesh.indices[triangle * 3 + 2]];
				if (first && second && third) {
					keptIndices.insert(keptIndices.end(), { static_cast<std::uint32_t>(*first), static_cast<std::uint32_t>(*second), static_cast<std::uint32_t>(*third) });
				}
			}
			a_mesh.points = std::move(kept);
			a_mesh.indices = std::move(keptIndices);
			return result;
		}

		void removeTriangles(Data& a_mesh, const Selection& a_selection) {
			std::vector<std::uint32_t> keptIndices;
			for (size_t triangle = 0; triangle < a_mesh.triangleCount(); ++triangle) {
				bool whole = true;
				for (size_t corner = 0; corner < 3; ++corner) {
					whole = whole && a_selection.contains(a_mesh.indices[triangle * 3 + corner]);
				}
				if (!whole) {
					keptIndices.insert(keptIndices.end(), a_mesh.indices.begin() + static_cast<std::ptrdiff_t>(triangle * 3), a_mesh.indices.begin() + static_cast<std::ptrdiff_t>(triangle * 3 + 3));
				}
			}
			a_mesh.indices = std::move(keptIndices);
			std::set<size_t> used(a_mesh.indices.begin(), a_mesh.indices.end());
			Selection unusedSelected;
			for (auto vertex : a_selection) {
				if (!used.contains(vertex)) {
					unusedSelected.insert(vertex);
				}
			}
			removeVertices(a_mesh, unusedSelected);
		}

		void removeUnused(Data& a_mesh) {
			std::set<size_t> used(a_mesh.indices.begin(), a_mesh.indices.end());
			Selection unused;
			for (size_t i = 0; i < a_mesh.points.size(); ++i) {
				if (!used.contains(i)) {
					unused.insert(i);
				}
			}
			removeVertices(a_mesh, unused);
		}

		Selection extrude(Data& a_mesh, const Selection& a_selection, const Point<>& a_offset, PointPrecision a_weld) {
			auto placeOf = places(a_mesh, a_weld);
			Selection result;
			std::map<size_t, size_t> copyOf;
			auto copyFor = [&](size_t a_source, size_t a_triangle) {
				if (auto found = copyOf.find(a_source); found != copyOf.end()) {
					return found->second;
				}
				auto copy = a_mesh.points[a_source];
				const auto moved = MV::point(copy.x + a_offset.x, copy.y + a_offset.y, copy.z);
				copy = textureAt(a_mesh, a_triangle, moved, copy.texturePoint());
				copy.x = moved.x;
				copy.y = moved.y;
				const auto index = a_mesh.points.size();
				a_mesh.points.push_back(copy);
				copyOf[a_source] = index;
				result.insert(index);
				return index;
			};
			for (auto&& edge : outsideEdgesWithTriangles(a_mesh, placeOf)) {
				if (!a_selection.contains(edge.from) || !a_selection.contains(edge.to)) {
					continue;
				}
				const auto from = copyFor(edge.from, edge.triangle);
				const auto to = copyFor(edge.to, edge.triangle);
				// across the edge from its triangle, wound the same way
				a_mesh.indices.insert(a_mesh.indices.end(), {
					static_cast<std::uint32_t>(edge.to), static_cast<std::uint32_t>(edge.from), static_cast<std::uint32_t>(from),
					static_cast<std::uint32_t>(edge.to), static_cast<std::uint32_t>(from), static_cast<std::uint32_t>(to) });
			}
			return result;
		}

		Selection subdivide(Data& a_mesh, const Selection& a_selection, PointPrecision a_weld) {
			auto placeOf = places(a_mesh, a_weld);
			std::set<std::pair<size_t, size_t>> splitPlaces;
			for (size_t triangle = 0; triangle < a_mesh.triangleCount(); ++triangle) {
				bool whole = true;
				for (size_t corner = 0; corner < 3; ++corner) {
					whole = whole && a_selection.contains(a_mesh.indices[triangle * 3 + corner]);
				}
				if (whole) {
					for (size_t corner = 0; corner < 3; ++corner) {
						splitPlaces.insert(placePair(placeOf, a_mesh.indices[triangle * 3 + corner], a_mesh.indices[triangle * 3 + (corner + 1) % 3]));
					}
				}
			}
			Selection result;
			std::map<std::pair<size_t, size_t>, size_t> middles;
			auto middleOf = [&](size_t a_first, size_t a_second) {
				auto key = std::minmax(a_first, a_second);
				if (auto found = middles.find(key); found != middles.end()) {
					return found->second;
				}
				const auto index = a_mesh.points.size();
				a_mesh.points.push_back(between(a_mesh.points[a_first], a_mesh.points[a_second], 0.5f));
				middles[key] = index;
				result.insert(index);
				return index;
			};
			std::vector<std::uint32_t> made;
			auto add = [&](size_t a_first, size_t a_second, size_t a_third) {
				made.insert(made.end(), { static_cast<std::uint32_t>(a_first), static_cast<std::uint32_t>(a_second), static_cast<std::uint32_t>(a_third) });
			};
			const auto triangles = a_mesh.triangleCount();
			for (size_t triangle = 0; triangle < triangles; ++triangle) {
				std::array<size_t, 3> corners { a_mesh.indices[triangle * 3], a_mesh.indices[triangle * 3 + 1], a_mesh.indices[triangle * 3 + 2] };
				std::array<bool, 3> split {};
				int splitCount = 0;
				for (size_t corner = 0; corner < 3; ++corner) {
					split[corner] = splitPlaces.contains(placePair(placeOf, corners[corner], corners[(corner + 1) % 3]));
					splitCount += split[corner] ? 1 : 0;
				}
				if (splitCount == 0) {
					add(corners[0], corners[1], corners[2]);
				} else if (splitCount == 3) {
					const auto first = middleOf(corners[0], corners[1]);
					const auto second = middleOf(corners[1], corners[2]);
					const auto third = middleOf(corners[2], corners[0]);
					add(corners[0], first, third);
					add(first, corners[1], second);
					add(third, second, corners[2]);
					add(first, second, third);
				} else if (splitCount == 1) {
					size_t at = split[0] ? 0 : split[1] ? 1 : 2;
					auto turned = turnedTo(corners, at);
					const auto middle = middleOf(turned[0], turned[1]);
					add(turned[0], middle, turned[2]);
					add(middle, turned[1], turned[2]);
				} else {
					// the edge left whole goes last: turned[2] to turned[0]
					size_t at = !split[0] ? 1 : !split[1] ? 2 : 0;
					auto turned = turnedTo(corners, at);
					const auto first = middleOf(turned[0], turned[1]);
					const auto second = middleOf(turned[1], turned[2]);
					add(first, turned[1], second);
					add(turned[0], first, second);
					add(turned[0], second, turned[2]);
				}
			}
			a_mesh.indices = std::move(made);
			return result;
		}

		Selection splitEdge(Data& a_mesh, size_t a_from, size_t a_to, PointPrecision a_along, PointPrecision a_weld) {
			auto placeOf = places(a_mesh, a_weld);
			Selection result;
			if (a_from >= placeOf.size() || a_to >= placeOf.size()) {
				return result;
			}
			const auto key = placePair(placeOf, a_from, a_to);
			std::map<std::pair<size_t, size_t>, size_t> made;
			std::vector<std::uint32_t> indices;
			for (size_t triangle = 0; triangle < a_mesh.triangleCount(); ++triangle) {
				std::array<size_t, 3> corners { a_mesh.indices[triangle * 3], a_mesh.indices[triangle * 3 + 1], a_mesh.indices[triangle * 3 + 2] };
				std::optional<size_t> at;
				for (size_t corner = 0; corner < 3 && !at; ++corner) {
					if (placePair(placeOf, corners[corner], corners[(corner + 1) % 3]) == key) {
						at = corner;
					}
				}
				if (!at) {
					indices.insert(indices.end(), { static_cast<std::uint32_t>(corners[0]), static_cast<std::uint32_t>(corners[1]), static_cast<std::uint32_t>(corners[2]) });
					continue;
				}
				auto turned = turnedTo(corners, *at);
				auto pairKey = std::minmax(turned[0], turned[1]);
				size_t middle;
				if (auto found = made.find(pairKey); found != made.end()) {
					middle = found->second;
				} else {
					const auto along = placeOf[turned[0]] == placeOf[a_from] ? a_along : 1.0f - a_along;
					middle = a_mesh.points.size();
					a_mesh.points.push_back(between(a_mesh.points[turned[0]], a_mesh.points[turned[1]], along));
					made[pairKey] = middle;
					result.insert(middle);
				}
				indices.insert(indices.end(), {
					static_cast<std::uint32_t>(turned[0]), static_cast<std::uint32_t>(middle), static_cast<std::uint32_t>(turned[2]),
					static_cast<std::uint32_t>(middle), static_cast<std::uint32_t>(turned[1]), static_cast<std::uint32_t>(turned[2]) });
			}
			a_mesh.indices = std::move(indices);
			return result;
		}

		size_t splitTriangle(Data& a_mesh, size_t a_triangle, const Point<>& a_at) {
			const auto first = a_mesh.indices[a_triangle * 3];
			const auto second = a_mesh.indices[a_triangle * 3 + 1];
			const auto third = a_mesh.indices[a_triangle * 3 + 2];
			const auto& a = a_mesh.points[first];
			const auto& b = a_mesh.points[second];
			const auto& c = a_mesh.points[third];
			auto weights = weightsIn(a.point(), b.point(), c.point(), a_at).value_or(std::array<PointPrecision, 3>{ 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f });
			DrawPoint made;
			made.x = a_at.x;
			made.y = a_at.y;
			made.z = a.z * weights[0] + b.z * weights[1] + c.z * weights[2];
			made.R = a.R * weights[0] + b.R * weights[1] + c.R * weights[2];
			made.G = a.G * weights[0] + b.G * weights[1] + c.G * weights[2];
			made.B = a.B * weights[0] + b.B * weights[1] + c.B * weights[2];
			made.A = a.A * weights[0] + b.A * weights[1] + c.A * weights[2];
			made.textureX = a.textureX * weights[0] + b.textureX * weights[1] + c.textureX * weights[2];
			made.textureY = a.textureY * weights[0] + b.textureY * weights[1] + c.textureY * weights[2];
			const auto index = static_cast<std::uint32_t>(a_mesh.points.size());
			a_mesh.points.push_back(made);
			a_mesh.indices[a_triangle * 3 + 2] = index;
			a_mesh.indices.insert(a_mesh.indices.end(), { second, third, index, third, first, index });
			return index;
		}

		bool fill(Data& a_mesh, const Selection& a_selection, PointPrecision a_weld) {
			auto placeOf = places(a_mesh, a_weld);
			std::map<size_t, size_t> onePerPlace;
			for (auto vertex : a_selection) {
				if (vertex < placeOf.size()) {
					onePerPlace.emplace(placeOf[vertex], vertex);
				}
			}
			if (onePerPlace.size() < 3) {
				return false;
			}
			std::vector<size_t> corners;
			Point<> middle;
			for (auto&& [place, vertex] : onePerPlace) {
				corners.push_back(vertex);
				middle += a_mesh.points[vertex].point();
			}
			middle /= static_cast<PointPrecision>(corners.size());
			std::sort(corners.begin(), corners.end(), [&](size_t a_first, size_t a_second) {
				return std::atan2(a_mesh.points[a_first].y - middle.y, a_mesh.points[a_first].x - middle.x) < std::atan2(a_mesh.points[a_second].y - middle.y, a_mesh.points[a_second].x - middle.x);
			});
			std::vector<Point<>> outline;
			for (auto corner : corners) {
				outline.push_back(a_mesh.points[corner].point());
			}
			auto triangles = triangulate(outline);
			for (auto corner : triangles) {
				a_mesh.indices.push_back(static_cast<std::uint32_t>(corners[corner]));
			}
			return !triangles.empty();
		}

		Selection duplicate(Data& a_mesh, const Selection& a_selection, const Point<>& a_offset) {
			std::map<size_t, size_t> copyOf;
			Selection result;
			for (auto vertex : a_selection) {
				if (vertex >= a_mesh.points.size()) {
					continue;
				}
				auto copy = a_mesh.points[vertex];
				copy.x += a_offset.x;
				copy.y += a_offset.y;
				copyOf[vertex] = a_mesh.points.size();
				result.insert(a_mesh.points.size());
				a_mesh.points.push_back(copy);
			}
			const auto triangles = a_mesh.triangleCount();
			for (size_t triangle = 0; triangle < triangles; ++triangle) {
				bool whole = true;
				for (size_t corner = 0; corner < 3; ++corner) {
					whole = whole && copyOf.contains(a_mesh.indices[triangle * 3 + corner]);
				}
				if (whole) {
					for (size_t corner = 0; corner < 3; ++corner) {
						a_mesh.indices.push_back(static_cast<std::uint32_t>(copyOf[a_mesh.indices[triangle * 3 + corner]]));
					}
				}
			}
			return result;
		}

		void merge(Data& a_mesh, const Selection& a_selection) {
			if (a_selection.empty()) {
				return;
			}
			const auto middle = centre(a_mesh, a_selection);
			for (auto vertex : a_selection) {
				if (vertex < a_mesh.points.size()) {
					a_mesh.points[vertex].x = middle.x;
					a_mesh.points[vertex].y = middle.y;
				}
			}
			std::vector<std::uint32_t> kept;
			for (size_t triangle = 0; triangle < a_mesh.triangleCount(); ++triangle) {
				const auto& a = a_mesh.points[a_mesh.indices[triangle * 3]];
				const auto& b = a_mesh.points[a_mesh.indices[triangle * 3 + 1]];
				const auto& c = a_mesh.points[a_mesh.indices[triangle * 3 + 2]];
				if (std::abs(cross(a.point(), b.point(), c.point())) > 1e-6f) {
					kept.insert(kept.end(), a_mesh.indices.begin() + static_cast<std::ptrdiff_t>(triangle * 3), a_mesh.indices.begin() + static_cast<std::ptrdiff_t>(triangle * 3 + 3));
				}
			}
			a_mesh.indices = std::move(kept);
		}

		size_t addVertex(Data& a_mesh, const Selection& a_selection, const DrawPoint& a_vertex, PointPrecision a_weld) {
			const auto index = a_mesh.points.size();
			auto made = a_vertex;
			for (auto&& edge : outsideEdgesWithTriangles(a_mesh, places(a_mesh, a_weld))) {
				if (a_selection.contains(edge.from) && a_selection.contains(edge.to)) {
					made = textureAt(a_mesh, edge.triangle, a_vertex.point(), a_vertex.texturePoint());
					made = between(a_mesh.points[edge.from], a_mesh.points[edge.to], 0.5f).color();
					a_mesh.points.push_back(made);
					a_mesh.indices.insert(a_mesh.indices.end(), { static_cast<std::uint32_t>(edge.to), static_cast<std::uint32_t>(edge.from), static_cast<std::uint32_t>(index) });
					return index;
				}
			}
			a_mesh.points.push_back(made);
			return index;
		}

		std::vector<std::uint32_t> triangulate(const std::vector<Point<>>& a_outline) {
			std::vector<std::uint32_t> result;
			const auto count = a_outline.size();
			if (count < 3) {
				return result;
			}
			PointPrecision area = 0.0f;
			for (size_t i = 0; i < count; ++i) {
				const auto& here = a_outline[i];
				const auto& next = a_outline[(i + 1) % count];
				area += here.x * next.y - next.x * here.y;
			}
			std::vector<size_t> remaining(count);
			std::iota(remaining.begin(), remaining.end(), size_t(0));
			if (area < 0.0f) {
				std::reverse(remaining.begin(), remaining.end());
			}
			auto inTriangle = [](const Point<>& a_point, const Point<>& a_first, const Point<>& a_second, const Point<>& a_third) {
				return cross(a_first, a_second, a_point) >= 0.0f && cross(a_second, a_third, a_point) >= 0.0f && cross(a_third, a_first, a_point) >= 0.0f;
			};
			size_t sinceEar = 0;
			size_t at = 0;
			while (remaining.size() > 3 && sinceEar < remaining.size()) {
				const auto size = remaining.size();
				const auto previous = remaining[(at + size - 1) % size];
				const auto here = remaining[at % size];
				const auto next = remaining[(at + 1) % size];
				bool ear = cross(a_outline[previous], a_outline[here], a_outline[next]) > 1e-9f;
				for (size_t other = 0; ear && other < size; ++other) {
					const auto candidate = remaining[other];
					if (candidate != previous && candidate != here && candidate != next && inTriangle(a_outline[candidate], a_outline[previous], a_outline[here], a_outline[next])) {
						ear = false;
					}
				}
				if (ear) {
					result.insert(result.end(), { static_cast<std::uint32_t>(previous), static_cast<std::uint32_t>(here), static_cast<std::uint32_t>(next) });
					remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(at % size));
					sinceEar = 0;
					at = at % remaining.size();
				} else {
					++sinceEar;
					at = (at + 1) % size;
				}
			}
			if (remaining.size() == 3) {
				result.insert(result.end(), { static_cast<std::uint32_t>(remaining[0]), static_cast<std::uint32_t>(remaining[1]), static_cast<std::uint32_t>(remaining[2]) });
			} else {
				// a self-crossing outline has no ears left: what remains is fanned
				for (size_t i = 1; i + 1 < remaining.size(); ++i) {
					result.insert(result.end(), { static_cast<std::uint32_t>(remaining[0]), static_cast<std::uint32_t>(remaining[i]), static_cast<std::uint32_t>(remaining[i + 1]) });
				}
			}
			return result;
		}

		Data polygon(const std::vector<Point<>>& a_outline, const Color& a_color) {
			Data result;
			for (auto&& corner : a_outline) {
				result.points.push_back(DrawPoint(corner, a_color));
			}
			result.indices = triangulate(a_outline);
			if (!a_outline.empty()) {
				BoxAABB<> area(a_outline.front(), a_outline.front());
				for (auto&& corner : a_outline) {
					area.expandWith(corner);
				}
				projectTexture(result, all(result), area);
			}
			return result;
		}

		Data ribbon(const std::vector<Point<>>& a_path, PointPrecision a_width, size_t a_across, PointPrecision a_repeatLength, const Color& a_color, bool a_fadeEnds) {
			Data result;
			std::vector<Point<>> path;
			for (auto&& point : a_path) {
				if (path.empty() || distance(path.back(), point) > 1e-4f) {
					path.push_back(point);
				}
			}
			a_across = std::max<size_t>(a_across, 2);
			if (path.size() < 2) {
				return result;
			}
			std::vector<PointPrecision> lengthTo { 0.0f };
			for (size_t i = 1; i < path.size(); ++i) {
				lengthTo.push_back(lengthTo.back() + distance(path[i - 1], path[i]));
			}
			const auto repeat = a_repeatLength > 0.0f ? a_repeatLength : lengthTo.back();
			for (size_t row = 0; row < path.size(); ++row) {
				const auto& before = path[row == 0 ? 0 : row - 1];
				const auto& after = path[row + 1 < path.size() ? row + 1 : row];
				auto direction = MV::point(after.x - before.x, after.y - before.y);
				const auto length = std::max(distance(MV::point(0.0f, 0.0f), direction), 1e-6f);
				const auto across = MV::point(-direction.y / length, direction.x / length);
				auto color = a_color;
				if (a_fadeEnds && (row == 0 || row + 1 == path.size())) {
					color.A = 0.0f;
				}
				for (size_t column = 0; column < a_across; ++column) {
					const auto share = static_cast<PointPrecision>(column) / static_cast<PointPrecision>(a_across - 1);
					const auto offset = (share - 0.5f) * a_width;
					result.points.push_back(DrawPoint(MV::point(path[row].x + across.x * offset, path[row].y + across.y * offset, path[row].z), color, TexturePoint(share, lengthTo[row] / repeat)));
				}
			}
			for (size_t row = 0; row + 1 < path.size(); ++row) {
				for (size_t column = 0; column + 1 < a_across; ++column) {
					const auto here = static_cast<std::uint32_t>(row * a_across + column);
					const auto below = static_cast<std::uint32_t>((row + 1) * a_across + column);
					result.indices.insert(result.indices.end(), { here, here + 1, below + 1, here, below + 1, below });
				}
			}
			return result;
		}

		Selection append(Data& a_mesh, const Data& a_more) {
			Selection result;
			const auto offset = static_cast<std::uint32_t>(a_mesh.points.size());
			for (size_t i = 0; i < a_more.points.size(); ++i) {
				result.insert(offset + i);
			}
			a_mesh.points.insert(a_mesh.points.end(), a_more.points.begin(), a_more.points.end());
			for (auto index : a_more.indices) {
				a_mesh.indices.push_back(index + offset);
			}
			return result;
		}

	}
}
