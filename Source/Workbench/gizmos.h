#ifndef _WORKBENCH_GIZMOS_H_
#define _WORKBENCH_GIZMOS_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "MV/Render/package.h"
#include "MV/Interface/tapDevice.h"
#include "commands.h"
#include "theme.h"

namespace Workbench {

	enum class Tool { SELECT, MOVE, ROTATE, SCALE };
	enum class GizmoAxis { X, Y, BOTH };

	// What a gesture snaps its result to while Shift or Ctrl is held: parent-space units on each axis, degrees, scale units.
	struct GizmoSnapSteps {
		float moveX = 10.0f;
		float moveY = 10.0f;
		float rotate = 5.0f;
		float scale = 0.1f;
	};

	// Viewport manipulation gizmos (the old editor's Editable*/ResizeHandles, ported). Handles
	// live in an unscaled overlay sibling of the scene root: constant pixel size at any zoom,
	// stencil-clipped to the viewport, never serialized. A per-frame dirty-check rebuilds them
	// when the target moves underneath (pan/zoom/inspector/undo); each drag gesture records
	// ONE undo command at release.
	class GizmoLayer {
	public:
		GizmoLayer(const std::shared_ptr<MV::Scene::Node>& a_overlay, MV::TapDevice& a_mouse, CommandHistory& a_history, const Theme& a_theme, MV::TextLibrary& a_textLibrary);
		~GizmoLayer();

		void select(const std::shared_ptr<MV::Scene::Node>& a_node);
		void editComponent(const std::shared_ptr<MV::Scene::Component>& a_component);
		std::shared_ptr<MV::Scene::Component> editedComponent() const { return componentTarget; }
		static bool supports(const std::shared_ptr<MV::Scene::Component>& a_component);

		// The transform handles drawn on the selected node; ignored while one of them is being dragged.
		// A component gizmo that takes input hides them until it is switched off, so the two never overlap.
		void tool(Tool a_tool);
		Tool tool() const { return activeTool; }

		GizmoSnapSteps snapSteps;
		// The transform handles stay off while another editor works on the selection (the mesh editor).
		void handlesHidden(bool a_hidden) { hideHandles = a_hidden; }
		// Snapping on without Ctrl (the toolbar's magnet); Ctrl then turns it off for a gesture.
		bool snapAlways = false;

		// One whole gesture on the selected node, as a handle drag applies it: the same begin, apply and end,
		// so one undo command. The move is in the node's parent space; false when nothing is selected or a drag is running.
		bool moveSelected(GizmoAxis a_axis, const MV::Point<>& a_parentDelta, bool a_snap = false);
		bool rotateSelected(float a_degrees, bool a_snap = false);
		bool scaleSelected(GizmoAxis a_axis, float a_factor, bool a_snap = false);

		// Where a transform handle can be grabbed: moveX, moveY, moveFree, rotate, scaleX, scaleY or scaleUniform.
		std::optional<MV::Point<int>> handleScreenPoint(const std::string& a_handle) const;
		// A corner handle of the box gizmo on the edited component (a_corner 0 top left, 1 top right, 2 bottom right,
		// 3 bottom left); a CameraConstraint's are box 0 the hard bounds and box 1 the soft.
		std::optional<MV::Point<int>> componentCornerScreenPoint(size_t a_box, size_t a_corner) const;

		// The rotate sweep on screen while its ring is dragged, in degrees turning the way screen angles grow
		// (clockwise, as y runs down); zero when no sweep shows.
		float sweepShown() const;

		void update();

		// Whether a screen point is over open scene. Handles outrank ordinary controls, so while the pointer is over
		// a toolbar, a floating panel or anything else that covers the scene they take no presses at all.
		std::function<bool(const MV::Point<int>&)> pointerOverScene;

		std::function<void()> onEdited;
		// Fires for each change a drag applies, before the gesture's onEdited.
		std::function<void()> onEditing;
		// Fires at handle press, inside the same TapDevice update pass that armed the viewport
		// pan (raw receivers run before exclusive Clickable resolution) — App cancels the pan here.
		std::function<void()> onHandlePressed;

	private:
		struct Gizmo;
		struct NodeGizmo;
		struct BoundsGizmo;
		struct BoundsPairGizmo;
		struct HighlightGizmo;

		Tool shownTool() const;
		void pressed();
		void edited();
		void editing();
		std::unique_ptr<Gizmo> makeComponentGizmo(const std::shared_ptr<MV::Scene::Component>& a_component);

		std::shared_ptr<MV::Scene::Node> overlay;
		MV::TapDevice& mouse;
		CommandHistory& history;
		const Theme& theme;
		MV::TextLibrary& textLibrary;
		Tool activeTool = Tool::MOVE;
		bool hideHandles = false;

		std::shared_ptr<MV::Scene::Node> nodeTarget;
		std::shared_ptr<MV::Scene::Component> componentTarget;
		std::weak_ptr<MV::Scene::Node> componentOwner;
		std::unique_ptr<NodeGizmo> nodeGizmo;
		std::unique_ptr<Gizmo> componentGizmo;

		void gateInput();
		std::vector<std::pair<std::weak_ptr<MV::Scene::Clickable>, MV::Scene::Clickable::BoundsType>> gatedHitAreas;
	};

}

#endif
