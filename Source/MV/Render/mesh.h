#ifndef _MV_RENDER_MESH_H_
#define _MV_RENDER_MESH_H_

#include <cstdint>
#include <optional>
#include <set>
#include <vector>
#include "points.h"
#include "boxaabb.h"

namespace MV {
	namespace Mesh {

		// A Drawable's triangles as plain data: its points and three indices per triangle. Every operation here
		// works on this and on a selection of vertex indices, so an editor edits a copy and writes it back whole.
		struct Data {
			std::vector<DrawPoint> points;
			std::vector<std::uint32_t> indices;

			size_t triangleCount() const { return indices.size() / 3; }
			bool operator==(const Data& a_other) const;
		};

		using Selection = std::set<size_t>;

		// Vertices closer than this are one place: a seam keeps two vertices where the texture or colour changes,
		// and they are picked, moved, painted and counted as one corner together.
		constexpr PointPrecision DEFAULT_WELD = 0.75f;

		// The place each vertex belongs to: equal numbers are vertices within a_weld of each other, transitively.
		std::vector<size_t> places(const Data& a_mesh, PointPrecision a_weld = DEFAULT_WELD);
		// A selection grown to every vertex sharing a place with one in it.
		Selection withWelded(const Data& a_mesh, const Selection& a_selection, PointPrecision a_weld = DEFAULT_WELD);
		// Every vertex joined to a_vertex through triangles: the piece it is part of.
		Selection island(const Data& a_mesh, size_t a_vertex, PointPrecision a_weld = DEFAULT_WELD);
		Selection all(const Data& a_mesh);
		// The selected vertices inside a box.
		Selection inside(const Data& a_mesh, const BoxAABB<>& a_box);
		Point<> centre(const Data& a_mesh, const Selection& a_selection);

		// The vertex nearest a point within a distance, the triangle a point is in, and the edge nearest a point within
		// a distance (its two vertices and how far along it the point lies, 0 at the first).
		std::optional<size_t> vertexNear(const Data& a_mesh, const Point<>& a_point, PointPrecision a_within);
		std::optional<size_t> triangleAt(const Data& a_mesh, const Point<>& a_point);
		struct EdgeHit {
			size_t from;
			size_t to;
			PointPrecision along;
		};
		std::optional<EdgeHit> edgeNear(const Data& a_mesh, const Point<>& a_point, PointPrecision a_within);

		// Edges on the outside of the mesh: used by one triangle only, counted by place. Each is listed in the winding
		// of its triangle.
		std::vector<std::pair<size_t, size_t>> outsideEdges(const Data& a_mesh, PointPrecision a_weld = DEFAULT_WELD);

		void setColor(Data& a_mesh, const Selection& a_selection, const Color& a_color);
		// Blends each vertex toward a colour by how near it is to a_centre: all the way at the centre times
		// a_strength, none at a_radius, smoothly between.
		void paint(Data& a_mesh, const Point<>& a_centre, PointPrecision a_radius, const Color& a_color, PointPrecision a_strength);
		// Texture coordinates for the selection laid flat over a box: its corners take the texture's corners.
		void projectTexture(Data& a_mesh, const Selection& a_selection, const BoxAABB<>& a_area);

		// Takes the selected vertices out with every triangle that used one; the vertices left are renumbered.
		// Returns the new index of each vertex that stays, or none for one that went.
		std::vector<std::optional<size_t>> removeVertices(Data& a_mesh, const Selection& a_selection);
		// Takes out the triangles whose three vertices are all selected, then the selected vertices no triangle
		// uses any more.
		void removeTriangles(Data& a_mesh, const Selection& a_selection);
		// Takes out the vertices no triangle uses.
		void removeUnused(Data& a_mesh);

		// Grows the mesh outward from the selected outside edges by a_offset. Each selected outside edge becomes the
		// inner side of a new quad; the new vertices carry their source's colour and continue its triangle's texture
		// mapping, so the texture runs on across the new quad rather than stretching. Returns the new vertices.
		Selection extrude(Data& a_mesh, const Selection& a_selection, const Point<>& a_offset, PointPrecision a_weld = DEFAULT_WELD);
		// Splits each triangle with all three vertices selected into four at its edge midpoints. A neighbour that
		// shares a split edge is split too, so no corner is left in the middle of an edge. Returns the new vertices.
		Selection subdivide(Data& a_mesh, const Selection& a_selection, PointPrecision a_weld = DEFAULT_WELD);
		// Puts a vertex on an edge, a_along of the way from its first vertex, in every triangle that has that edge.
		// Returns the new vertices (one per triangle side, so seams stay).
		Selection splitEdge(Data& a_mesh, size_t a_from, size_t a_to, PointPrecision a_along, PointPrecision a_weld = DEFAULT_WELD);
		// Puts a vertex inside a triangle at a point, making three triangles of it. Returns the new vertex.
		size_t splitTriangle(Data& a_mesh, size_t a_triangle, const Point<>& a_at);
		// Makes triangles over the selected places: one for three, a fan of ear-clipped triangles around their
		// middle for more. Uses one vertex per place. Returns whether anything was made.
		bool fill(Data& a_mesh, const Selection& a_selection, PointPrecision a_weld = DEFAULT_WELD);
		// Copies the selected vertices, and the triangles all of whose vertices are selected, offset by a_offset.
		// Returns the copies.
		Selection duplicate(Data& a_mesh, const Selection& a_selection, const Point<>& a_offset);
		// Moves the selected vertices to their middle, then takes out the triangles that have no area left.
		void merge(Data& a_mesh, const Selection& a_selection);
		// A vertex joined to two selected vertices that share an outside edge, or alone with none: Ctrl+press on
		// open space. Returns the new vertex.
		size_t addVertex(Data& a_mesh, const Selection& a_selection, const DrawPoint& a_vertex, PointPrecision a_weld = DEFAULT_WELD);

		// Triangles over a simple polygon, as indices into its outline, by ear clipping. Either winding.
		std::vector<std::uint32_t> triangulate(const std::vector<Point<>>& a_outline);
		// A filled polygon with the texture laid over its bounds.
		Data polygon(const std::vector<Point<>>& a_outline, const Color& a_color = Color());
		// A strip a_width wide along a path, a_across vertices wide (2 or more). The texture runs across it (0 to 1)
		// and along it, one repeat per a_repeatLength units (the whole strip when 0 or less). With a_fadeEnds the
		// first and last rows are clear. The waterfall's streams are this shape.
		Data ribbon(const std::vector<Point<>>& a_path, PointPrecision a_width, size_t a_across = 2, PointPrecision a_repeatLength = 0.0f, const Color& a_color = Color(), bool a_fadeEnds = false);
		// Adds another mesh's vertices and triangles; returns the vertices it added.
		Selection append(Data& a_mesh, const Data& a_more);

	}
}

#endif
