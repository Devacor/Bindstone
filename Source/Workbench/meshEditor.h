#ifndef _WORKBENCH_MESHEDITOR_H_
#define _WORKBENCH_MESHEDITOR_H_

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "MV/Render/package.h"
#include "MV/Render/mesh.h"
#include "MV/Interface/tapDevice.h"
#include "commands.h"
#include "dock.h"
#include "focus.h"
#include "keyBindings.h"
#include "theme.h"

namespace Workbench {

	// Edit mode for a Drawable's own triangles. Vertices are picked by place (welded corners go together), and
	// every operation below is one undo step that writes the Drawable's points and indices whole.
	//
	// The Select tool: a press on a corner picks it, on a triangle picks its corners, on open scene starts a box
	// (Shift adds). A drag moves what is picked. Ctrl+press puts a corner on an edge, inside a triangle, or out in
	// the open joined to the selected outside edge.
	// Keys (standard bindings, each changeable in Settings, KeyBindings): G move, R rotate, S scale (X or Y keeps a
	// move on one axis; a press or Enter keeps, Esc or a right press undoes), E extrude, F fill, X or Delete remove, M
	// merge, Shift+D duplicate, A all or none, L island under the pointer, C colour the selection, 1 to 4 the tools.
	// Esc, Enter and Backspace are fixed: Esc leaves.
	// Polygon: presses lay an outline, Enter or a press on its first corner fills it. Ribbon: presses lay a path, Enter
	// makes a strip along it. Paint: drag to blend the corners under the brush toward the colour.
	class MeshEditor {
	public:
		enum class Tool { SELECT, POLYGON, RIBBON, PAINT };

		MeshEditor(const std::shared_ptr<MV::Scene::Node>& a_overlay, const std::shared_ptr<MV::Scene::Node>& a_barHost, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, CommandHistory& a_history, MV::TapDevice& a_mouse, const KeyBindings& a_keys);
		~MeshEditor();

		void begin(const std::shared_ptr<MV::Scene::Drawable>& a_target);
		void end();
		bool active() const { return static_cast<bool>(target); }
		std::shared_ptr<MV::Scene::Drawable> editing() const { return target; }

		void update();
		// Places the tool bar along the bottom of the view.
		void layout(const DockRect& a_view);
		// A key while the mode is on; true when the mode took it.
		bool key(const SDL_Event& a_event);
		bool barContains(const MV::Point<>& a_worldPoint) const;
		std::optional<MV::Point<int>> barButtonScreenPoint(const std::string& a_label) const;

		Tool tool() const { return activeTool; }
		void tool(Tool a_tool);
		const MV::Mesh::Selection& selection() const { return selected; }
		void select(const MV::Mesh::Selection& a_selection);

		// The operations the keys and the bar run.
		bool fill();
		bool subdivide();
		bool removeSelected();
		bool removeTriangles();
		bool merge();
		bool duplicate();
		bool colorSelection();
		bool projectTexture();
		bool removeUnused();
		bool startModal(char a_kind);

		struct Settings {
			float weld = MV::Mesh::DEFAULT_WELD;
			float ribbonWidth = 40.0f;
			int ribbonAcross = 2;
			float ribbonRepeat = 0.0f;
			bool ribbonFade = true;
			float brushRadius = 40.0f;
			float brushStrength = 0.35f;
			MV::Color color { 1.0f, 1.0f, 1.0f, 1.0f };
		};
		Settings settings;

		std::function<bool(const MV::Point<int>&)> pointerOverScene;
		// After each change is kept (one undo step).
		std::function<void()> onEdited;
		std::function<void()> onEnded;

	private:
		enum class Modal { NONE, DRAG, GRAB, ROTATE, SCALE, EXTRUDE, BOX, PAINT };

		MV::Mesh::Data read() const;
		void write(const MV::Mesh::Data& a_mesh);
		void keep(const std::string& a_label, const MV::Mesh::Data& a_before);
		bool operate(const std::string& a_label, const std::function<bool(MV::Mesh::Data&)>& a_change);

		MV::Point<> local(const MV::Point<int>& a_screen) const;
		float localPerPixel() const;
		std::optional<size_t> cornerUnder(const MV::Point<int>& a_screen) const;

		void pressed(MV::TapDevice& a_mouse);
		void released(MV::TapDevice& a_mouse);
		void moved(MV::TapDevice& a_mouse);
		void beginModal(Modal a_modal);
		void applyModal();
		void confirmModal();
		void cancelModal();
		void finishOutline();

		void buildBar();
		void refreshBar();
		void draw();

		MV::Services& services;
		const Theme& theme;
		FocusRouter& focus;
		CommandHistory& history;
		MV::TapDevice& mouse;
		const KeyBindings& keys;
		std::shared_ptr<MV::Scene::Node> overlay;
		std::shared_ptr<MV::Scene::Node> drawing;
		std::shared_ptr<MV::Scene::Node> barHost;
		std::shared_ptr<MV::Scene::Node> bar;
		std::vector<std::pair<std::string, std::weak_ptr<MV::Scene::Node>>> barButtons;
		std::vector<MV::TapDevice::SignalType> receivers;

		std::shared_ptr<MV::Scene::Drawable> target;
		Tool activeTool = Tool::SELECT;
		MV::Mesh::Selection selected;

		Modal modal = Modal::NONE;
		MV::Mesh::Data modalStart;
		MV::Mesh::Selection modalSelection;
		MV::Point<int> modalPointer;
		bool modalMoved = false;
		bool modalAdding = false;
		int axisLock = 0;

		std::vector<MV::Point<>> outline;
	};

}

#endif
