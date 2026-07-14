#ifndef _WORKBENCH_GIZMOS_H_
#define _WORKBENCH_GIZMOS_H_

#include <functional>
#include <memory>
#include "MV/Render/package.h"
#include "MV/Interface/tapDevice.h"
#include "commands.h"
#include "theme.h"

namespace Workbench {

	// Viewport manipulation gizmos (the old editor's Editable*/ResizeHandles, ported). Handles
	// live in an unscaled overlay sibling of the scene root: constant pixel size at any zoom,
	// stencil-clipped to the viewport, never serialized. A per-frame dirty-check rebuilds them
	// when the target moves underneath (pan/zoom/inspector/undo); each drag gesture records
	// ONE undo command at release.
	class GizmoLayer {
	public:
		GizmoLayer(const std::shared_ptr<MV::Scene::Node>& a_overlay, MV::TapDevice& a_mouse, CommandHistory& a_history, const Theme& a_theme);
		~GizmoLayer();

		void select(const std::shared_ptr<MV::Scene::Node>& a_node);
		void editComponent(const std::shared_ptr<MV::Scene::Component>& a_component);
		std::shared_ptr<MV::Scene::Component> editedComponent() const { return componentTarget; }
		static bool supports(const std::shared_ptr<MV::Scene::Component>& a_component);

		void update();

		std::function<void()> onEdited;
		// Fires at handle press, inside the same TapDevice update pass that armed the viewport
		// pan (raw receivers run before exclusive Clickable resolution) — App cancels the pan here.
		std::function<void()> onHandlePressed;

	private:
		struct Gizmo;
		struct NodeGizmo;
		struct BoundsGizmo;
		struct PointsGizmo;
		struct HighlightGizmo;

		void pressed();
		void edited();
		std::unique_ptr<Gizmo> makeComponentGizmo(const std::shared_ptr<MV::Scene::Component>& a_component);

		std::shared_ptr<MV::Scene::Node> overlay;
		MV::TapDevice& mouse;
		CommandHistory& history;
		const Theme& theme;

		std::shared_ptr<MV::Scene::Node> nodeTarget;
		std::shared_ptr<MV::Scene::Component> componentTarget;
		std::weak_ptr<MV::Scene::Node> componentOwner;
		std::unique_ptr<Gizmo> nodeGizmo;
		std::unique_ptr<Gizmo> componentGizmo;
	};

}

#endif
