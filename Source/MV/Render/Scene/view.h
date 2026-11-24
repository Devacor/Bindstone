#ifndef _MV_SCENE_VIEW_H_
#define _MV_SCENE_VIEW_H_

#include "sprite.h"
#include "camera.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/serialization/archive.hpp>

namespace MV {
	namespace Scene {

		// A layer: its node and everything under it drawn through one camera into one rectangle of the window. A scene
		// with no View draws as one full-window, pixel, orthographic view, as ever. What a game calls its interface is a
		// View with a pixel camera drawn after the world's View; a picture-in-picture is a View with a smaller viewport.
		// Its box is the area its camera sees, in its node's space, so anchors lay a layer out against its View.
		class View : public jai::property_owner<View, Sprite> {
			friend Node;
			friend jai::access;

		public:
			enum class Clear { NOTHING, DEPTH, COLOR };
			enum class Depth { OFF, PER_OBJECT, SHARED };

			static void jai_auto_bind(jai::dynamic_binder<View>& a_builder);

			DrawableDerivedAccessors(View)

			// Where in the window it draws, as fractions of the window: 0,0 to 1,1 is all of it.
			BoxAABB<> viewport() const { return viewportBox; }
			std::shared_ptr<View> viewport(const BoxAABB<>& a_viewport);
			Clear clear() const { return clearKind; }
			std::shared_ptr<View> clear(Clear a_clear);
			Depth depthMode() const { return depthKind; }
			std::shared_ptr<View> depthMode(Depth a_depth);
			// The node under this one whose Camera it looks through, by id; empty takes the first Camera under it, and
			// with none it draws in pixels with no eye, as a scene with no View does.
			const std::string& cameraNode() const { return cameraNodeId; }
			std::shared_ptr<View> cameraNode(const std::string& a_id);
			// A node anywhere in the scene, by id, drawn through this View's camera before what is under this View: a
			// second look at another layer (a minimap). Empty draws only what is under it.
			const std::string& subject() const { return subjectId; }
			std::shared_ptr<View> subject(const std::string& a_id);

			std::shared_ptr<Camera> camera() const;
			// At run time only, never saved: look through this Camera, and draw this node, wherever they are (an editor's
			// preview of a camera in the scene it edits). Null goes back to `camera` and `subject`.
			std::shared_ptr<View> lookThrough(const std::shared_ptr<Camera>& a_camera);
			std::shared_ptr<View> show(const std::shared_ptr<Node>& a_subject);
			// The slot its subtree draws and converts through; -1 while it has none.
			int32_t slot() const { return viewSlot; }
			// The whole matrix from the scene's world to the window's clip space.
			TransformMatrix viewProjection() const;
			// Where it draws, in the window's pixels: its viewport's part of the renderer's target.
			BoxAABB<int> pixelRect() const;
			// Its size in window units, what its camera's projection is made for.
			Size<> viewSize() const;
			// A point of the window, in its pixels, as a fraction of this View's rectangle: what Camera::sceneFromView takes.
			Point<> viewFromScreen(const Point<int>& a_screen) const;

			// While authoring, a View takes no part: what is under it draws through the editor's own view.
			bool authoring() const { return isAuthoring; }
			std::shared_ptr<View> authoring(bool a_authoring);

		protected:
			View(const std::weak_ptr<Node>& a_owner);

			template<class Archive>
			void save(Archive& archive, std::uint32_t const) const {
				property_mgr.save(archive);
				Sprite::save(archive, 0);
			}

			template<class Archive>
			void load(Archive& archive, std::uint32_t const) {
				property_mgr.load(archive);
				Sprite::load(archive, 0);
			}

			template<typename Archive>
			static void load_and_construct(Archive& ar, jai::serialization::construct<View>& construct) {
				construct(std::shared_ptr<Node>());
				construct->load(ar, 0);
				construct->initialize();
			}

			std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node>& a_parent) override {
				return cloneHelper(a_parent->attach<View>().self());
			}

			void initialize() override;
			void postLoadInitialize() override;
			void reattachImplementation() override;
			void detachImplementation() override;
			void onOwnerDestroyed() override;
			void updateImplementation(double a_delta) override;
			void defaultDrawImplementation() override;
			void beforeNodeDraw() override;
			void afterNodeDraw() override;
			bool ownsCamera() const override { return !isAuthoring && viewSlot >= 0; }

		private:
			void claim();
			void release();
			void stepAside();
			// Its rectangle as fractions of the whole window: left, top, width and height.
			BoxAABB<> windowFraction() const;
			void refresh();
			std::shared_ptr<Node> subjectNode() const;

			JAI_NAMED_PROPERTY((BoxAABB<>), "viewport", viewportBox, BoxAABB<>(MV::point(0.0f, 0.0f), MV::point(1.0f, 1.0f)));
			JAI_NAMED_PROPERTY((Clear), "clear", clearKind, Clear::NOTHING);
			JAI_NAMED_PROPERTY((Depth), "depth", depthKind, Depth::OFF);
			JAI_NAMED_PROPERTY((std::string), "camera", cameraNodeId);
			JAI_NAMED_PROPERTY((std::string), "subject", subjectId);

			std::weak_ptr<Camera> cameraOverride;
			std::weak_ptr<Node> subjectOverride;
			int32_t viewSlot = -1;
			bool isAuthoring = false;
			bool scissored = false;
			bool pushedGroups = false;
			bool drawingColor = false;
		};

	}
}

#endif
