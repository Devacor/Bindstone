#ifndef _MV_SCENE_CAMERA_CONSTRAINT_H_
#define _MV_SCENE_CAMERA_CONSTRAINT_H_

#include "camera.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {

		// Keeps a camera's view of this node inside bounds drawn in this node's own space. While the view is held (a
		// drag or a pinch) it never shows past the hard box or zooms past the hard limits; let go, it eases back inside
		// the soft ones. On each axis a view wider than a box is centred on it, and one inside it that crosses an edge
		// is pushed back over that edge. Zooming out stops where the view fits a box, the tighter axis winning, unless
		// fitting is turned off for the plain minimum.
		class CameraConstraint : public jai::property_owner<CameraConstraint, Component> {
			friend Node;
			friend jai::access;

		public:
			static void jai_auto_bind(jai::dynamic_binder<CameraConstraint>& a_builder);

			ComponentDerivedAccessors(CameraConstraint)

			// The node whose Camera it constrains, by id, anywhere in the scene, as View::cameraNode names one.
			const std::string& cameraNode() const { return cameraNodeId; }
			std::shared_ptr<CameraConstraint> cameraNode(const std::string& a_id);
			std::shared_ptr<Camera> camera();

			BoxAABB<> hard() const { return hardBounds; }
			std::shared_ptr<CameraConstraint> hard(const BoxAABB<>& a_bounds);
			BoxAABB<> soft() const { return softBounds; }
			std::shared_ptr<CameraConstraint> soft(const BoxAABB<>& a_bounds);
			BoxAABB<> bounds(bool a_soft) const { return a_soft ? softBounds : hardBounds; }

			std::shared_ptr<CameraConstraint> hardZoom(PointPrecision a_minimum, PointPrecision a_maximum);
			std::shared_ptr<CameraConstraint> softZoom(PointPrecision a_minimum, PointPrecision a_maximum);
			bool fitsMinimumZoom() const { return fitMinimum; }
			std::shared_ptr<CameraConstraint> fitsMinimumZoom(bool a_fits);
			// The zoom range in force now: with fitting on, the minimum is where the view fits that box.
			PointPrecision minimumZoom(bool a_soft);
			PointPrecision maximumZoom(bool a_soft) const;
			// How fast a let-go view eases back: the share of the way left that it closes is 1 - e^(-easing * seconds).
			PointPrecision easing() const { return easingRate; }
			std::shared_ptr<CameraConstraint> easing(PointPrecision a_rate);

			// What the camera sees, in this node's space; the move, in this node's space, that brings it inside a box;
			// the zoom at which it fits a box.
			BoxAABB<> visible();
			Point<> correction(const BoxAABB<>& a_bounds);
			PointPrecision zoomToFit(const BoxAABB<>& a_bounds);

			// Held, it clamps to the hard limits; let go, it eases to the soft ones keeping the point of the view
			// (fractions of it, as Camera::sceneFromView takes) where the hold ended.
			std::shared_ptr<CameraConstraint> hold();
			std::shared_ptr<CameraConstraint> release(const Point<>& a_around = Point<>(0.5f, 0.5f));
			bool held() const { return isHeld; }
			// Puts the view inside the hard limits now, keeping a_around where it is on the view.
			std::shared_ptr<CameraConstraint> clamp(const Point<>& a_around = Point<>(0.5f, 0.5f));
			// Puts the view inside the soft limits now, as a finished ease would.
			std::shared_ptr<CameraConstraint> settle();
			bool settled();
			// Moves the camera by a distance in this node's space.
			std::shared_ptr<CameraConstraint> moveCamera(const Point<>& a_delta);

			// While authoring (an editor's mode, never saved) it leaves the camera to whoever is moving it.
			bool authoring() const { return isAuthoring; }
			std::shared_ptr<CameraConstraint> authoring(bool a_authoring);

		protected:
			CameraConstraint(const std::weak_ptr<Node>& a_owner);

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
			static void load_and_construct(Archive& ar, jai::serialization::construct<CameraConstraint>& construct) {
				construct(std::shared_ptr<Node>());
				construct->load(ar, 0);
				construct->initialize();
			}

			std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node>& a_parent) override {
				return cloneHelper(a_parent->attach<CameraConstraint>().self());
			}

			void updateImplementation(double a_delta) override;

		private:
			void ease(double a_delta);
			void within(bool a_soft, PointPrecision a_share, const Point<>& a_around);

			JAI_NAMED_PROPERTY((std::string), "camera", cameraNodeId);
			JAI_NAMED_PROPERTY((BoxAABB<>), "hard", hardBounds);
			JAI_NAMED_PROPERTY((BoxAABB<>), "soft", softBounds);
			JAI_NAMED_PROPERTY((PointPrecision), "hardZoomMinimum", hardZoomMinimum, 0.01f);
			JAI_NAMED_PROPERTY((PointPrecision), "hardZoomMaximum", hardZoomMaximum, 4.0f);
			JAI_NAMED_PROPERTY((PointPrecision), "softZoomMinimum", softZoomMinimum, 0.01f);
			JAI_NAMED_PROPERTY((PointPrecision), "softZoomMaximum", softZoomMaximum, 3.0f);
			JAI_NAMED_PROPERTY((bool), "fitMinimumZoom", fitMinimum, true);
			JAI_NAMED_PROPERTY((PointPrecision), "easing", easingRate, 12.0f);

			std::weak_ptr<Camera> resolvedCamera;
			Point<> releasedAround { 0.5f, 0.5f };
			bool isHeld = false;
			bool isAuthoring = false;
		};

	}
}

#endif
