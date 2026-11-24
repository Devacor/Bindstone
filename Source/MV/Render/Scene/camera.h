#ifndef _MV_SCENE_CAMERA_H_
#define _MV_SCENE_CAMERA_H_

#include "node.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {

		// The eye a View draws through. The eye is this node: where it is and how it is turned, so a camera moves,
		// follows (as a child of what it follows) and animates like any node. It draws nothing itself. The node's scale,
		// and its parents', is not the eye's: how much it sees is its zoom alone.
		class Camera : public jai::property_owner<Camera, Component> {
			friend Node;
			friend jai::access;

		public:
			enum class Projection { ORTHOGRAPHIC, PERSPECTIVE };
			// What an orthographic unit is. PIXELS: a unit of the window as the renderer lays it out (its world size).
			// SCALED_PIXELS: one grown by the OS's scale for the window, so content keeps its physical size (tools).
			// REFERENCE: a unit of a reference screen (`width` by `height`) stretched over the view, matching its width,
			// its height or a blend of the two (`match`, 0 to 1), so a layout looks alike on every screen (games).
			enum class Units { PIXELS, SCALED_PIXELS, REFERENCE };
			// Where the eye's position falls in an orthographic view: its top left, as a screen is laid out, or its middle.
			enum class Origin { TOP_LEFT, CENTER };

			static void jai_auto_bind(jai::dynamic_binder<Camera>& a_builder);

			ComponentDerivedAccessors(Camera)

			Projection projection() const { return projectionKind; }
			std::shared_ptr<Camera> projection(Projection a_projection);
			Units units() const { return unitKind; }
			std::shared_ptr<Camera> units(Units a_units);
			Origin origin() const { return originKind; }
			std::shared_ptr<Camera> origin(Origin a_origin);
			PointPrecision width() const { return referenceWidth; }
			std::shared_ptr<Camera> width(PointPrecision a_width);
			PointPrecision height() const { return fixedHeight; }
			std::shared_ptr<Camera> height(PointPrecision a_height);
			// REFERENCE only: 0 fits the reference width to the view, 1 its height, between blends the two.
			PointPrecision match() const { return widthHeightMatch; }
			std::shared_ptr<Camera> match(PointPrecision a_match);
			// Whether the user's own interface scale (Window::userScale) grows what it shows: on for a game's interface.
			bool userScaled() const { return followsUserScale; }
			std::shared_ptr<Camera> userScaled(bool a_userScaled);
			PointPrecision zoom() const { return zoomLevel; }
			std::shared_ptr<Camera> zoom(PointPrecision a_zoom);
			PointPrecision fieldOfView() const { return verticalFieldOfView; }
			std::shared_ptr<Camera> fieldOfView(PointPrecision a_degrees);
			PointPrecision nearPlane() const { return nearDistance; }
			PointPrecision farPlane() const { return farDistance; }
			std::shared_ptr<Camera> planes(PointPrecision a_near, PointPrecision a_far);
			// The groups (Node::group, 0 to 31) this camera draws, one bit each; all of them to start with.
			uint32_t sees() const { return groupMask; }
			std::shared_ptr<Camera> sees(uint32_t a_groups);
			bool seesGroup(uint32_t a_group) const { return a_group >= 32 || (groupMask.get() & (1u << a_group)) != 0; }

			// The projection for a view of a_viewSize window units: those units to clip space. a_contentScale is the OS's
			// scale for the window, a_userScale the user's own.
			TransformMatrix projectionFor(const Size<>& a_viewSize, PointPrecision a_contentScale, PointPrecision a_userScale) const;
			// From the scene's world to the eye's own space.
			TransformMatrix eye();

			// The view it draws into, in window units, and the OS's and the user's scales: its View sets them as it
			// draws, a host looking through it another way sets them itself. Until then it is the renderer's whole world.
			Size<> viewSize() const;
			std::shared_ptr<Camera> viewSize(const Size<>& a_size, PointPrecision a_contentScale, PointPrecision a_userScale);
			// How much of the scene it sees at its zoom, in scene units: exact for an orthographic camera, and for a
			// perspective one at the plane z = 0 straight ahead.
			Size<> seen() const;
			// From a point of its view, as fractions (0,0 its top left, 1,1 its bottom right), to the scene.
			Point<> sceneFromView(const Point<>& a_fraction);
			// The middle of what it sees, in the scene, and what it sees as a box in a node's own space.
			Point<> center();
			BoxAABB<> visibleIn(const std::shared_ptr<Node>& a_node);
			// Moves its node by a distance in the scene, whatever the node's parents do to it.
			std::shared_ptr<Camera> sceneTranslate(const Point<>& a_delta);
			// Zooms keeping the scene point under a point of its view where it is on the view.
			std::shared_ptr<Camera> zoomAround(PointPrecision a_zoom, const Point<>& a_fraction);

		protected:
			Camera(const std::weak_ptr<Node>& a_owner);

			template<class Archive>
			void save(Archive& archive, std::uint32_t const) const {
				property_mgr.save(archive);
				Component::save(archive, 0);
			}

			template<class Archive>
			void load(Archive& archive, std::uint32_t const) {
				property_mgr.load(archive);
				Component::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Camera>& construct) {
				construct(std::shared_ptr<Node>());
				construct->load(ar, 0);
				construct->initialize();
			}

			std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node>& a_parent) override {
				return cloneHelper(a_parent->attach<Camera>().self());
			}

		private:
			// Its node's place and turn in the scene, without scale.
			TransformMatrix lens();
			Size<> orthographicSeen(const Size<>& a_viewSize, PointPrecision a_contentScale, PointPrecision a_userScale) const;

			JAI_NAMED_PROPERTY((Projection), "projection", projectionKind, Projection::ORTHOGRAPHIC);
			JAI_NAMED_PROPERTY((Units), "units", unitKind, Units::PIXELS);
			JAI_NAMED_PROPERTY((Origin), "origin", originKind, Origin::TOP_LEFT);
			JAI_NAMED_PROPERTY((PointPrecision), "width", referenceWidth, 1920.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "height", fixedHeight, 1080.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "match", widthHeightMatch, 1.0f);
			JAI_NAMED_PROPERTY((bool), "userScaled", followsUserScale, false);
			JAI_NAMED_PROPERTY((PointPrecision), "zoom", zoomLevel, 1.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "fieldOfView", verticalFieldOfView, 60.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "near", nearDistance, -10000.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "far", farDistance, 10000.0f);
			JAI_NAMED_PROPERTY((uint32_t), "sees", groupMask, 0xFFFFFFFFu);
			Size<> viewedSize;
			PointPrecision viewedContentScale = 1.0f;
			PointPrecision viewedUserScale = 1.0f;
		};

	}
}

#endif
