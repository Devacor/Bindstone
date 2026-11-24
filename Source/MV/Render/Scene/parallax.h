#ifndef _MV_SCENE_PARALLAX_H_
#define _MV_SCENE_PARALLAX_H_

#include "camera.h"
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {
		// A layer at a depth of its own. At its rest view (a camera centre and magnification where the layers are
		// squared up) it sits at localOffset with the scale it was given; as the camera moves away from that centre it
		// follows a share of the way, and as it zooms it grows by a share of the zoom. The camera is the one the View
		// above it draws through; with none it rests.
		//
		// The layer is placed by writing its node's position (and scale, for a zoomRatio other than 1) before each
		// update and draw, so it is placed for its own View's camera only: a second View that draws it as a subject (a
		// minimap) sees it where the main camera put it, and an animation of the node's position is overridden.
		class Parallax : public jai::property_owner<Parallax, Component> {
			friend Node;
			friend jai::access;

		public:
			static void jai_auto_bind(jai::dynamic_binder<Parallax>& a_builder);

			ComponentDerivedAccessors(Parallax)

			// The share of the camera's movement the layer does not follow: 1 stays put in the scene, 0 moves with the camera.
			Point<> translateRatio() const { return ourTranslateRatio; }
			std::shared_ptr<Parallax> translateRatio(const Point<>& a_ratio);
			// Where the layer rests, in its parent's space.
			Point<> localOffset() const { return ourLocalOffset; }
			std::shared_ptr<Parallax> localOffset(const Point<>& a_localOffset);
			// The share of the zoom the layer takes, in log space: its size goes with magnification^zoomRatio, so 1
			// zooms with the scene, 0 never changes size on the screen, 0.5 halves every doubling.
			Point<> zoomRatio() const { return ourZoomRatio; }
			std::shared_ptr<Parallax> zoomRatio(const Point<>& a_zoomRatio);
			// The rest view: the camera's centre in the parent's space, and its magnification there (the camera's zoom
			// times the scale the parent has in the scene).
			Point<> restCenter() const { return ourRestCenter; }
			PointPrecision restZoom() const { return ourRestZoom; }
			std::shared_ptr<Parallax> restView(const Point<>& a_center, PointPrecision a_zoom);
			// Takes what a camera sees now as the rest view: at it the layer sits at localOffset.
			std::shared_ptr<Parallax> restViewFrom(const std::shared_ptr<Camera>& a_camera);

			// Where the layer goes, in its parent's space, and how much it grows, for a camera centre (in the parent's
			// space) and magnification.
			Point<> placement(const Point<>& a_center, PointPrecision a_zoom) const;
			Scale growth(PointPrecision a_zoom) const;

			bool enabled() const { return isEnabled; }
			std::shared_ptr<Parallax> enabled(bool a_enabled);
			std::shared_ptr<Parallax> disable() { return enabled(false); }
			std::shared_ptr<Parallax> enable() { return enabled(true); }

			// Authoring (an editor's mode, never saved): an enabled layer rests at localOffset with its authored scale
			// whatever the camera does, and a node moved while it rests carries localOffset with it.
			bool authoring() const { return isAuthoring; }
			std::shared_ptr<Parallax> authoring(bool a_authoring);

			// While authoring, the preview places the layer for this camera as a run would, and a node moved meanwhile
			// moves localOffset by what that move is at rest. Null puts the layer back at rest.
			std::shared_ptr<Camera> authoringPreview() const { return isAuthoring ? previewCamera.lock() : nullptr; }
			std::shared_ptr<Parallax> authoringPreview(const std::shared_ptr<Camera>& a_camera);

		protected:
			Parallax(const std::weak_ptr<Node> &a_owner);

			template<class Archive>
			void save(Archive & archive, std::uint32_t const) const {
				property_mgr.save(archive);
				Component::save(archive, 0);
			}

			template<class Archive>
			void load(Archive & archive, std::uint32_t const) {
				property_mgr.load(archive);
				Component::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<Parallax>& construct) {
				construct(std::shared_ptr<Node>());
				construct->load(ar, 0);
				construct->initialize();
			}

			std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) override {
				return cloneHelper(a_parent->attach<Parallax>().self());
			}

			void detachImplementation() override;
			void updateImplementation(double a_delta) override;
			void beforeNodeDraw() override;

		private:
			std::shared_ptr<Camera> placingCamera() const;
			PointPrecision magnification(const std::shared_ptr<Camera>& a_camera) const;
			void place();
			void rest();
			bool zooms() const { return ourZoomRatio->x != 1.0f || ourZoomRatio->y != 1.0f; }

			JAI_NAMED_PROPERTY((bool), "enabled", isEnabled, true);
			JAI_NAMED_PROPERTY((Point<>), "translateRatio", ourTranslateRatio);
			JAI_NAMED_PROPERTY((Point<>), "localOffset", ourLocalOffset);
			JAI_NAMED_PROPERTY((Point<>), "zoomRatio", ourZoomRatio, (Point<>(1.0f, 1.0f, 1.0f)));
			JAI_NAMED_PROPERTY((Point<>), "restCenter", ourRestCenter);
			JAI_NAMED_PROPERTY((PointPrecision), "restZoom", ourRestZoom, 1.0f);
			JAI_DELETED_PROPERTY((int32_t), space);

			Scale authoredScale;   // the node's own scale before zoom is applied on top of it
			bool authoredScaleKnown = false;
			bool placed = false;
			Point<> placedPosition;
			Scale placedGrowth;
			bool resting = false;
			bool isAuthoring = false;
			std::weak_ptr<Camera> previewCamera;
		};
	}
}

#endif
