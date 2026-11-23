#include "meshEditor.h"
#include "widgets.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Workbench {

	namespace Mesh = MV::Mesh;

	namespace {
		const MV::Color edgeColor(1.0f, 1.0f, 1.0f, 0.35f);
		const MV::Color outsideColor(0.35f, 0.75f, 1.0f, 0.95f);
		const MV::Color cornerColor(1.0f, 1.0f, 1.0f, 0.95f);
		const MV::Color cornerBack(0.0f, 0.0f, 0.0f, 0.7f);
		const MV::Color selectedColor(1.0f, 0.6f, 0.1f, 1.0f);
		const MV::Color faceColor(1.0f, 0.6f, 0.1f, 0.2f);
		const MV::Color toolColor(0.4f, 1.0f, 0.5f, 0.95f);

		// Flat shapes in the overlay's space, all in one Drawable.
		struct Shapes {
			std::vector<MV::DrawPoint> points;
			std::vector<GLuint> indices;

			void quad(const MV::Point<>& a_first, const MV::Point<>& a_second, const MV::Point<>& a_third, const MV::Point<>& a_fourth, const MV::Color& a_color) {
				const auto base = static_cast<GLuint>(points.size());
				for (auto&& corner : { a_first, a_second, a_third, a_fourth }) {
					points.push_back(MV::DrawPoint(corner, a_color));
				}
				indices.insert(indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
			}

			void triangle(const MV::Point<>& a_first, const MV::Point<>& a_second, const MV::Point<>& a_third, const MV::Color& a_color) {
				const auto base = static_cast<GLuint>(points.size());
				for (auto&& corner : { a_first, a_second, a_third }) {
					points.push_back(MV::DrawPoint(corner, a_color));
				}
				indices.insert(indices.end(), { base, base + 1, base + 2 });
			}

			void line(const MV::Point<>& a_from, const MV::Point<>& a_to, float a_width, const MV::Color& a_color) {
				const auto length = MV::distance(MV::point(a_from.x, a_from.y), MV::point(a_to.x, a_to.y));
				if (length < 0.01f) {
					return;
				}
				const auto across = MV::point(-(a_to.y - a_from.y) / length * a_width * 0.5f, (a_to.x - a_from.x) / length * a_width * 0.5f);
				quad(MV::point(a_from.x + across.x, a_from.y + across.y), MV::point(a_to.x + across.x, a_to.y + across.y), MV::point(a_to.x - across.x, a_to.y - across.y), MV::point(a_from.x - across.x, a_from.y - across.y), a_color);
			}

			void square(const MV::Point<>& a_at, float a_size, const MV::Color& a_color) {
				const auto half = a_size * 0.5f;
				quad(MV::point(a_at.x - half, a_at.y - half), MV::point(a_at.x + half, a_at.y - half), MV::point(a_at.x + half, a_at.y + half), MV::point(a_at.x - half, a_at.y + half), a_color);
			}

			void circle(const MV::Point<>& a_at, float a_radius, float a_width, const MV::Color& a_color) {
				const int steps = 32;
				for (int step = 0; step < steps; ++step) {
					const auto from = static_cast<float>(step) / steps * 6.2831853f;
					const auto to = static_cast<float>(step + 1) / steps * 6.2831853f;
					line(MV::point(a_at.x + std::cos(from) * a_radius, a_at.y + std::sin(from) * a_radius), MV::point(a_at.x + std::cos(to) * a_radius, a_at.y + std::sin(to) * a_radius), a_width, a_color);
				}
			}
		};

		bool holdsAll(const Mesh::Selection& a_holder, const Mesh::Selection& a_items) {
			return std::all_of(a_items.begin(), a_items.end(), [&](size_t a_item) { return a_holder.contains(a_item); });
		}

		std::optional<float> numberIn(const std::string& a_text) {
			char* end = nullptr;
			const float result = std::strtof(a_text.c_str(), &end);
			if (end == a_text.c_str()) {
				return std::nullopt;
			}
			return result;
		}

		MV::Point<> rotated(const MV::Point<>& a_point, const MV::Point<>& a_centre, float a_angle) {
			const auto cosine = std::cos(a_angle);
			const auto sine = std::sin(a_angle);
			const auto x = a_point.x - a_centre.x;
			const auto y = a_point.y - a_centre.y;
			return MV::point(a_centre.x + x * cosine - y * sine, a_centre.y + x * sine + y * cosine, a_point.z);
		}
	}

	MeshEditor::MeshEditor(const std::shared_ptr<MV::Scene::Node>& a_overlay, const std::shared_ptr<MV::Scene::Node>& a_barHost, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, CommandHistory& a_history, MV::TapDevice& a_mouse) :
		services(a_services),
		theme(a_theme),
		focus(a_focus),
		history(a_history),
		mouse(a_mouse),
		overlay(a_overlay->make("meshEditor")),
		barHost(a_barHost) {

		drawing = overlay->make("drawing");
		barHost->hide();
		receivers.push_back(mouse.onLeftMouseDown.connect([this](MV::TapDevice& a_mouse) { pressed(a_mouse); }));
		receivers.push_back(mouse.onLeftMouseUp.connect([this](MV::TapDevice& a_mouse) { released(a_mouse); }));
		receivers.push_back(mouse.onMove.connect([this](MV::TapDevice& a_mouse) { moved(a_mouse); }));
		receivers.push_back(mouse.onRightMouseDown.connect([this](MV::TapDevice&) {
			if (!active()) {
				return;
			}
			if (modal != Modal::NONE) {
				cancelModal();
			} else {
				outline.clear();
			}
		}));
	}

	MeshEditor::~MeshEditor() {
		receivers.clear();
		if (overlay) {
			overlay->removeFromParent();
		}
	}

	void MeshEditor::begin(const std::shared_ptr<MV::Scene::Drawable>& a_target) {
		if (target) {
			end();
		}
		if (!a_target) {
			return;
		}
		target = a_target;
		selected.clear();
		outline.clear();
		activeTool = Tool::SELECT;
		buildBar();
		barHost->show();
	}

	void MeshEditor::end() {
		if (!target) {
			return;
		}
		cancelModal();
		target.reset();
		selected.clear();
		outline.clear();
		drawing->clear();
		if (auto canvas = drawing->component<MV::Scene::Drawable>(false, false)) {
			canvas->setPoints({}, {});
		}
		barHost->clear();
		bar.reset();
		barButtons.clear();
		barHost->hide();
		if (onEnded) {
			onEnded();
		}
	}

	MV::Mesh::Data MeshEditor::read() const {
		Mesh::Data result;
		result.points = target->getPoints();
		const auto& indices = target->pointIndices();
		result.indices.assign(indices.begin(), indices.end());
		return result;
	}

	void MeshEditor::write(const MV::Mesh::Data& a_mesh) {
		target->setPoints(a_mesh.points, std::vector<GLuint>(a_mesh.indices.begin(), a_mesh.indices.end()));
	}

	void MeshEditor::keep(const std::string& a_label, const MV::Mesh::Data& a_before) {
		auto after = read();
		if (after == a_before) {
			return;
		}
		std::weak_ptr<MV::Scene::Drawable> weakTarget = target;
		auto apply = [weakTarget](const Mesh::Data& a_mesh) {
			if (auto locked = weakTarget.lock()) {
				locked->setPoints(a_mesh.points, std::vector<GLuint>(a_mesh.indices.begin(), a_mesh.indices.end()));
			}
		};
		history.record("mesh " + a_label, [apply, after]() { apply(after); }, [apply, a_before]() { apply(a_before); });
		if (onEdited) {
			onEdited();
		}
	}

	bool MeshEditor::operate(const std::string& a_label, const std::function<bool(MV::Mesh::Data&)>& a_change) {
		if (!target || modal != Modal::NONE) {
			return false;
		}
		auto before = read();
		auto changed = before;
		if (!a_change(changed)) {
			return false;
		}
		write(changed);
		keep(a_label, before);
		return true;
	}

	MV::Point<> MeshEditor::local(const MV::Point<int>& a_screen) const {
		return target->owner()->localFromScreen(a_screen);
	}

	float MeshEditor::localPerPixel() const {
		const auto at = mouse.position();
		return MV::distance(local(at), local(at + MV::Point<int>(10, 0))) / 10.0f;
	}

	std::optional<size_t> MeshEditor::cornerUnder(const MV::Point<int>& a_screen) const {
		return Mesh::vertexNear(read(), local(a_screen), theme.px(7.0f) * localPerPixel());
	}

	void MeshEditor::tool(Tool a_tool) {
		if (modal != Modal::NONE) {
			cancelModal();
		}
		const bool changed = activeTool != a_tool;
		activeTool = a_tool;
		outline.clear();
		if (changed && bar) {
			// the settings row follows the tool
			buildBar();
		}
	}

	void MeshEditor::select(const MV::Mesh::Selection& a_selection) {
		MV::Mesh::Selection result;
		if (target) {
			for (auto vertex : a_selection) {
				if (vertex < target->pointSize()) {
					result.insert(vertex);
				}
			}
		}
		selected = std::move(result);
	}

	void MeshEditor::pressed(MV::TapDevice& a_mouse) {
		if (!active() || !pointerOverScene || !pointerOverScene(a_mouse.position())) {
			return;
		}
		const auto at = a_mouse.position();
		if (modal == Modal::GRAB || modal == Modal::ROTATE || modal == Modal::SCALE || modal == Modal::EXTRUDE) {
			confirmModal();
			return;
		}
		const auto modifiers = SDL_GetModState();
		const bool shift = (modifiers & KMOD_SHIFT) != 0;
		const bool control = (modifiers & KMOD_CTRL) != 0;
		switch (activeTool) {
			case Tool::SELECT: {
				if (control) {
					const auto point = local(at);
					const auto reach = theme.px(6.0f) * localPerPixel();
					const auto weld = settings.weld;
					const auto color = settings.color;
					operate("add corner", [&](Mesh::Data& a_mesh) {
						if (auto edge = Mesh::edgeNear(a_mesh, point, reach)) {
							selected = Mesh::splitEdge(a_mesh, edge->from, edge->to, edge->along, weld);
						} else if (auto triangle = Mesh::triangleAt(a_mesh, point)) {
							selected = { Mesh::splitTriangle(a_mesh, *triangle, point) };
						} else {
							selected = { Mesh::addVertex(a_mesh, selected, MV::DrawPoint(point, color), weld) };
						}
						return true;
					});
					return;
				}
				const auto mesh = read();
				const auto corner = cornerUnder(at);
				Mesh::Selection picked;
				if (corner) {
					picked = Mesh::withWelded(mesh, { *corner }, settings.weld);
				} else if (auto triangle = Mesh::triangleAt(mesh, local(at))) {
					picked = Mesh::withWelded(mesh, { mesh.indices[*triangle * 3], mesh.indices[*triangle * 3 + 1], mesh.indices[*triangle * 3 + 2] }, settings.weld);
				}
				if (!picked.empty()) {
					const bool already = holdsAll(selected, picked);
					if (shift && already) {
						for (auto vertex : picked) {
							selected.erase(vertex);
						}
						return;
					}
					if (shift) {
						selected.insert(picked.begin(), picked.end());
					} else if (!already) {
						selected = picked;
					}
					beginModal(Modal::DRAG);
					return;
				}
				modalAdding = shift;
				beginModal(Modal::BOX);
				return;
			}
			case Tool::POLYGON:
			case Tool::RIBBON: {
				if (activeTool == Tool::POLYGON && outline.size() >= 3) {
					const auto first = target->owner()->screenFromLocal(outline.front());
					if (std::abs(first.x - at.x) + std::abs(first.y - at.y) <= theme.px(8.0f)) {
						finishOutline();
						return;
					}
				}
				outline.push_back(local(at));
				return;
			}
			case Tool::PAINT:
				beginModal(Modal::PAINT);
				applyModal();
				return;
		}
	}

	void MeshEditor::released(MV::TapDevice& a_mouse) {
		if (!active()) {
			return;
		}
		switch (modal) {
			case Modal::DRAG:
				modal = Modal::NONE;
				if (modalMoved) {
					keep("move", modalStart);
				}
			break;
			case Modal::BOX: {
				modal = Modal::NONE;
				const auto at = a_mouse.position();
				if (!modalMoved) {
					if (!modalAdding) {
						selected.clear();
					}
					break;
				}
				const auto low = MV::Point<int>(std::min(at.x, modalPointer.x), std::min(at.y, modalPointer.y));
				const auto high = MV::Point<int>(std::max(at.x, modalPointer.x), std::max(at.y, modalPointer.y));
				const auto mesh = read();
				Mesh::Selection boxed;
				for (size_t i = 0; i < mesh.points.size(); ++i) {
					const auto onScreen = target->owner()->screenFromLocal(mesh.points[i].point());
					if (onScreen.x >= low.x && onScreen.x <= high.x && onScreen.y >= low.y && onScreen.y <= high.y) {
						boxed.insert(i);
					}
				}
				boxed = Mesh::withWelded(mesh, boxed, settings.weld);
				if (!modalAdding) {
					selected.clear();
				}
				selected.insert(boxed.begin(), boxed.end());
			}
			break;
			case Modal::PAINT:
				modal = Modal::NONE;
				keep("paint", modalStart);
			break;
			default:
			break;
		}
	}

	void MeshEditor::moved(MV::TapDevice& a_mouse) {
		if (!active() || modal == Modal::NONE) {
			return;
		}
		const auto at = a_mouse.position();
		if (std::abs(at.x - modalPointer.x) + std::abs(at.y - modalPointer.y) > 3) {
			modalMoved = true;
		}
		if (modal == Modal::BOX || (modal == Modal::DRAG && !modalMoved)) {
			return;
		}
		applyModal();
	}

	void MeshEditor::beginModal(Modal a_modal) {
		modal = a_modal;
		modalStart = read();
		modalSelection = selected;
		modalPointer = mouse.position();
		modalMoved = false;
		axisLock = 0;
	}

	void MeshEditor::applyModal() {
		const auto now = local(mouse.position());
		const auto from = local(modalPointer);
		auto offset = MV::point(now.x - from.x, now.y - from.y);
		if (axisLock == 1) {
			offset.y = 0.0f;
		} else if (axisLock == 2) {
			offset.x = 0.0f;
		}
		auto mesh = modalStart;
		switch (modal) {
			case Modal::DRAG:
			case Modal::GRAB:
				for (auto vertex : modalSelection) {
					mesh.points[vertex].x += offset.x;
					mesh.points[vertex].y += offset.y;
				}
			break;
			case Modal::ROTATE: {
				const auto centre = Mesh::centre(modalStart, modalSelection);
				const auto angle = std::atan2(now.y - centre.y, now.x - centre.x) - std::atan2(from.y - centre.y, from.x - centre.x);
				for (auto vertex : modalSelection) {
					auto turned = rotated(mesh.points[vertex].point(), centre, angle);
					mesh.points[vertex].x = turned.x;
					mesh.points[vertex].y = turned.y;
				}
			}
			break;
			case Modal::SCALE: {
				const auto centre = Mesh::centre(modalStart, modalSelection);
				const auto start = MV::distance(MV::point(from.x, from.y), MV::point(centre.x, centre.y));
				const auto factor = start > 1e-4f ? MV::distance(MV::point(now.x, now.y), MV::point(centre.x, centre.y)) / start : 1.0f;
				for (auto vertex : modalSelection) {
					if (axisLock != 2) {
						mesh.points[vertex].x = centre.x + (mesh.points[vertex].x - centre.x) * factor;
					}
					if (axisLock != 1) {
						mesh.points[vertex].y = centre.y + (mesh.points[vertex].y - centre.y) * factor;
					}
				}
			}
			break;
			case Modal::EXTRUDE:
				selected = Mesh::extrude(mesh, modalSelection, offset, settings.weld);
			break;
			case Modal::PAINT:
				mesh = read();
				Mesh::paint(mesh, now, settings.brushRadius * localPerPixel(), settings.color, settings.brushStrength);
			break;
			default:
				return;
		}
		write(mesh);
	}

	void MeshEditor::confirmModal() {
		static const std::pair<Modal, const char*> labels[] {
			{ Modal::GRAB, "move" }, { Modal::DRAG, "move" }, { Modal::ROTATE, "rotate" }, { Modal::SCALE, "scale" }, { Modal::EXTRUDE, "extrude" }, { Modal::PAINT, "paint" } };
		const auto kind = modal;
		modal = Modal::NONE;
		for (auto&& [candidate, label] : labels) {
			if (candidate == kind) {
				keep(label, modalStart);
			}
		}
	}

	void MeshEditor::cancelModal() {
		const auto kind = modal;
		modal = Modal::NONE;
		if (kind == Modal::NONE || kind == Modal::BOX || !target) {
			return;
		}
		write(modalStart);
		selected = modalSelection;
	}

	bool MeshEditor::startModal(char a_kind) {
		if (!target || modal != Modal::NONE || selected.empty()) {
			return false;
		}
		Modal kind = Modal::NONE;
		switch (a_kind) {
			case 'g':
				kind = Modal::GRAB;
			break;
			case 'r':
				kind = Modal::ROTATE;
			break;
			case 's':
				kind = Modal::SCALE;
			break;
			case 'e': {
				auto trial = read();
				if (Mesh::extrude(trial, selected, MV::point(0.0f, 0.0f), settings.weld).empty()) {
					return false;
				}
				kind = Modal::EXTRUDE;
			}
			break;
			default:
				return false;
		}
		beginModal(kind);
		applyModal();
		return true;
	}

	void MeshEditor::finishOutline() {
		auto path = outline;
		outline.clear();
		if (activeTool == Tool::POLYGON && path.size() >= 3) {
			operate("polygon", [&](Mesh::Data& a_mesh) {
				selected = Mesh::append(a_mesh, Mesh::polygon(path, settings.color));
				return true;
			});
		} else if (activeTool == Tool::RIBBON && path.size() >= 2) {
			operate("ribbon", [&](Mesh::Data& a_mesh) {
				selected = Mesh::append(a_mesh, Mesh::ribbon(path, settings.ribbonWidth, static_cast<size_t>(std::max(2, settings.ribbonAcross)), settings.ribbonRepeat, settings.color, settings.ribbonFade));
				return !selected.empty();
			});
		}
	}

	bool MeshEditor::fill() {
		return operate("fill", [&](Mesh::Data& a_mesh) { return Mesh::fill(a_mesh, selected, settings.weld); });
	}

	bool MeshEditor::subdivide() {
		return operate("subdivide", [&](Mesh::Data& a_mesh) {
			auto made = Mesh::subdivide(a_mesh, selected, settings.weld);
			selected.insert(made.begin(), made.end());
			return !made.empty();
		});
	}

	bool MeshEditor::removeSelected() {
		return operate("remove", [&](Mesh::Data& a_mesh) {
			if (selected.empty()) {
				return false;
			}
			Mesh::removeVertices(a_mesh, selected);
			selected.clear();
			return true;
		});
	}

	bool MeshEditor::removeTriangles() {
		return operate("remove triangles", [&](Mesh::Data& a_mesh) {
			if (selected.empty()) {
				return false;
			}
			Mesh::removeTriangles(a_mesh, selected);
			selected.clear();
			return true;
		});
	}

	bool MeshEditor::merge() {
		return operate("merge", [&](Mesh::Data& a_mesh) {
			if (selected.size() < 2) {
				return false;
			}
			Mesh::merge(a_mesh, selected);
			return true;
		});
	}

	bool MeshEditor::duplicate() {
		const bool made = operate("duplicate", [&](Mesh::Data& a_mesh) {
			selected = Mesh::duplicate(a_mesh, selected, MV::point(0.0f, 0.0f));
			return !selected.empty();
		});
		return made && startModal('g');
	}

	bool MeshEditor::colorSelection() {
		return operate("colour", [&](Mesh::Data& a_mesh) {
			if (selected.empty()) {
				return false;
			}
			Mesh::setColor(a_mesh, selected, settings.color);
			return true;
		});
	}

	bool MeshEditor::projectTexture() {
		return operate("lay texture", [&](Mesh::Data& a_mesh) {
			auto chosen = selected.empty() ? Mesh::all(a_mesh) : selected;
			if (chosen.empty()) {
				return false;
			}
			auto first = a_mesh.points[*chosen.begin()].point();
			MV::BoxAABB<> area(first, first);
			for (auto vertex : chosen) {
				area.expandWith(a_mesh.points[vertex].point());
			}
			Mesh::projectTexture(a_mesh, chosen, area);
			return true;
		});
	}

	bool MeshEditor::removeUnused() {
		return operate("tidy", [&](Mesh::Data& a_mesh) {
			const auto before = a_mesh.points.size();
			Mesh::removeUnused(a_mesh);
			selected.clear();
			return a_mesh.points.size() != before;
		});
	}

	bool MeshEditor::key(const SDL_Event& a_event) {
		if (!active() || a_event.type != SDL_KEYDOWN) {
			return false;
		}
		const auto modifiers = SDL_GetModState();
		if (modifiers & (KMOD_CTRL | KMOD_ALT | KMOD_GUI)) {
			return false;
		}
		const auto symbol = a_event.key.keysym.sym;
		if (modal == Modal::GRAB || modal == Modal::ROTATE || modal == Modal::SCALE || modal == Modal::EXTRUDE) {
			if (symbol == SDLK_ESCAPE) {
				cancelModal();
			} else if (symbol == SDLK_RETURN || symbol == SDLK_KP_ENTER) {
				confirmModal();
			} else if (symbol == SDLK_x || symbol == SDLK_y) {
				const int wanted = symbol == SDLK_x ? 1 : 2;
				axisLock = axisLock == wanted ? 0 : wanted;
				applyModal();
			}
			return true;
		}
		if (a_event.key.repeat) {
			return true;
		}
		switch (symbol) {
			case SDLK_ESCAPE:
				if (!outline.empty()) {
					outline.clear();
				} else {
					end();
				}
			break;
			case SDLK_RETURN:
			case SDLK_KP_ENTER:
				finishOutline();
			break;
			case SDLK_BACKSPACE:
				if (!outline.empty()) {
					outline.pop_back();
				}
			break;
			case SDLK_1:
				tool(Tool::SELECT);
			break;
			case SDLK_2:
				tool(Tool::POLYGON);
			break;
			case SDLK_3:
				tool(Tool::RIBBON);
			break;
			case SDLK_4:
				tool(Tool::PAINT);
			break;
			case SDLK_g:
				startModal('g');
			break;
			case SDLK_r:
				startModal('r');
			break;
			case SDLK_s:
				startModal('s');
			break;
			case SDLK_e:
				startModal('e');
			break;
			case SDLK_f:
				fill();
			break;
			case SDLK_x:
			case SDLK_DELETE:
				removeSelected();
			break;
			case SDLK_m:
				merge();
			break;
			case SDLK_c:
				colorSelection();
			break;
			case SDLK_d:
				if (modifiers & KMOD_SHIFT) {
					duplicate();
				}
			break;
			case SDLK_a:
				if (selected.size() == target->pointSize()) {
					selected.clear();
				} else {
					selected = Mesh::all(read());
				}
			break;
			case SDLK_l:
				if (auto corner = cornerUnder(mouse.position())) {
					auto piece = Mesh::island(read(), *corner, settings.weld);
					selected.insert(piece.begin(), piece.end());
				}
			break;
			default:
				return false;
		}
		return true;
	}

	void MeshEditor::buildBar() {
		barHost->clear();
		barButtons.clear();
		bar = barHost->make("meshBar");
		const float buttonWidth = theme.px(72.0f);
		const float height = theme.rowHeight;
		const float gap = theme.px(3.0f);
		struct Entry {
			std::string label;
			std::function<void()> action;
		};
		// the tools and leaving, what to do to the picked corners, and what the tool in use is set to
		std::vector<std::vector<Entry>> rows {
			{ { "Select", [this]() { tool(Tool::SELECT); } },
				{ "Polygon", [this]() { tool(Tool::POLYGON); } },
				{ "Ribbon", [this]() { tool(Tool::RIBBON); } },
				{ "Paint", [this]() { tool(Tool::PAINT); } },
				{ "Done", [this]() { end(); } } },
			{ { "Extrude", [this]() { startModal('e'); } },
				{ "Fill", [this]() { fill(); } },
				{ "Subdivide", [this]() { subdivide(); } },
				{ "Delete", [this]() { removeSelected(); } },
				{ "Merge", [this]() { merge(); } },
				{ "Colour", [this]() { colorSelection(); } },
				{ "Lay UV", [this]() { projectTexture(); } },
				{ "Tidy", [this]() { removeUnused(); } } } };
		float width = 0.0f;
		float y = gap;
		for (auto&& row : rows) {
			float x = gap;
			for (auto&& entry : row) {
				if (entry.label == "Done") {
					x += gap * 4.0f;
				}
				auto button = makeButton(bar, services, theme, "mesh_" + entry.label, MV::size(buttonWidth, height), entry.label, entry.action);
				button->owner()->position(MV::point(x, y));
				barButtons.emplace_back(entry.label, button->owner());
				x += buttonWidth + gap;
			}
			width = std::max(width, x);
			y += height + gap;
		}
		auto mark = bar->make("toolMark");
		mark->attach<MV::Scene::Sprite>()->bounds(MV::size(buttonWidth, theme.px(2.0f)))->color(theme.accent());

		struct Field {
			std::string label;
			std::function<float()> read;
			std::function<void(float)> write;
		};
		std::vector<Field> fields { { "Weld", [this]() { return settings.weld; }, [this](float a_value) { settings.weld = std::max(0.0f, a_value); } } };
		if (activeTool == Tool::RIBBON) {
			fields.push_back({ "Width", [this]() { return settings.ribbonWidth; }, [this](float a_value) { settings.ribbonWidth = std::max(0.01f, a_value); } });
			fields.push_back({ "Across", [this]() { return static_cast<float>(settings.ribbonAcross); }, [this](float a_value) { settings.ribbonAcross = std::max(2, static_cast<int>(a_value)); } });
			fields.push_back({ "Repeat", [this]() { return settings.ribbonRepeat; }, [this](float a_value) { settings.ribbonRepeat = std::max(0.0f, a_value); } });
			fields.push_back({ "Fade", [this]() { return settings.ribbonFade ? 1.0f : 0.0f; }, [this](float a_value) { settings.ribbonFade = a_value != 0.0f; } });
		} else if (activeTool == Tool::PAINT) {
			fields.push_back({ "Brush", [this]() { return settings.brushRadius; }, [this](float a_value) { settings.brushRadius = std::max(1.0f, a_value); } });
			fields.push_back({ "Strength", [this]() { return settings.brushStrength; }, [this](float a_value) { settings.brushStrength = std::clamp(a_value, 0.0f, 1.0f); } });
		}
		fields.push_back({ "R", [this]() { return settings.color.R; }, [this](float a_value) { settings.color.R = std::clamp(a_value, 0.0f, 1.0f); } });
		fields.push_back({ "G", [this]() { return settings.color.G; }, [this](float a_value) { settings.color.G = std::clamp(a_value, 0.0f, 1.0f); } });
		fields.push_back({ "B", [this]() { return settings.color.B; }, [this](float a_value) { settings.color.B = std::clamp(a_value, 0.0f, 1.0f); } });
		fields.push_back({ "A", [this]() { return settings.color.A; }, [this](float a_value) { settings.color.A = std::clamp(a_value, 0.0f, 1.0f); } });
		float x = gap;
		const float fieldWidth = theme.px(42.0f);
		for (auto&& field : fields) {
			const float labelWidth = labelTextWidth(services, theme, field.label) + theme.px(4.0f);
			auto label = makeLabel(bar, services, theme, "label_" + field.label, MV::size(labelWidth, height), field.label);
			label->owner()->position(MV::point(x, y));
			x += labelWidth;
			auto write = field.write;
			auto text = makeTextField(bar, services, theme, focus, "field_" + field.label, MV::size(fieldWidth, height), formatNumberCompact(field.read()), [write](const std::string& a_text) {
				if (auto value = numberIn(a_text)) {
					write(*value);
				}
			});
			text->owner()->parent()->position(MV::point(x, y));
			x += fieldWidth + gap * 2.0f;
		}
		width = std::max(width, x);
		y += height + gap;
		auto back = bar->make("back");
		back->depth(-1.0f);
		attachOutlinedRect(back, services, theme, MV::size(width + gap, y), theme.header(), theme.windowBg());
		bar->normalizeDepth();
		refreshBar();
	}

	void MeshEditor::refreshBar() {
		if (!bar) {
			return;
		}
		static const char* toolLabels[] { "Select", "Polygon", "Ribbon", "Paint" };
		for (auto&& [label, weakButton] : barButtons) {
			auto button = weakButton.lock();
			if (button && label == toolLabels[static_cast<int>(activeTool)]) {
				if (auto mark = bar->get("toolMark", false)) {
					mark->position(MV::point(button->position().x, button->position().y + theme.rowHeight - theme.px(2.0f)));
				}
			}
		}
	}

	void MeshEditor::layout(const DockRect& a_view) {
		if (!bar) {
			return;
		}
		const auto size = bar->bounds(true).size();
		bar->position(MV::point(std::round(a_view.position.x + theme.pad), std::round(a_view.position.y + a_view.size.height - size.height - theme.pad)));
	}

	bool MeshEditor::barContains(const MV::Point<>& a_worldPoint) const {
		if (!bar || !active()) {
			return false;
		}
		auto box = bar->worldBounds(true);
		return a_worldPoint.x >= box.minPoint.x && a_worldPoint.x <= box.maxPoint.x && a_worldPoint.y >= box.minPoint.y && a_worldPoint.y <= box.maxPoint.y;
	}

	std::optional<MV::Point<int>> MeshEditor::barButtonScreenPoint(const std::string& a_label) const {
		for (auto&& [label, weakButton] : barButtons) {
			if (label == a_label) {
				if (auto button = weakButton.lock()) {
					return button->screenBounds().centerPoint();
				}
			}
		}
		return std::nullopt;
	}

	void MeshEditor::update() {
		if (!target) {
			return;
		}
		if (!target->ownerIsAlive() || !target->owner()->parent()) {
			end();
			return;
		}
		if (modal == Modal::NONE) {
			select(selected);
		}
		draw();
	}

	void MeshEditor::draw() {
		auto canvas = drawing->component<MV::Scene::Drawable>(false, false);
		if (!canvas) {
			canvas = drawing->attach<MV::Scene::Drawable>();
		}
		const auto node = target->owner();
		auto toOverlay = [&](const MV::Point<>& a_local) {
			return drawing->localFromWorld(node->worldFromLocal(a_local));
		};
		auto screenToOverlay = [&](const MV::Point<int>& a_screen) {
			return drawing->localFromScreen(a_screen);
		};
		const auto mesh = read();
		std::vector<MV::Point<>> placed;
		placed.reserve(mesh.points.size());
		for (auto&& point : mesh.points) {
			placed.push_back(toOverlay(point.point()));
		}
		const float thin = theme.px(1.0f);
		Shapes shapes;
		for (size_t triangle = 0; triangle < mesh.triangleCount(); ++triangle) {
			const auto a = mesh.indices[triangle * 3];
			const auto b = mesh.indices[triangle * 3 + 1];
			const auto c = mesh.indices[triangle * 3 + 2];
			if (selected.contains(a) && selected.contains(b) && selected.contains(c)) {
				shapes.triangle(placed[a], placed[b], placed[c], faceColor);
			}
		}
		std::set<std::pair<size_t, size_t>> drawn;
		for (size_t triangle = 0; triangle < mesh.triangleCount(); ++triangle) {
			for (size_t corner = 0; corner < 3; ++corner) {
				const size_t from = mesh.indices[triangle * 3 + corner];
				const size_t to = mesh.indices[triangle * 3 + (corner + 1) % 3];
				if (drawn.insert(std::minmax(from, to)).second) {
					shapes.line(placed[from], placed[to], thin, edgeColor);
				}
			}
		}
		for (auto&& [from, to] : Mesh::outsideEdges(mesh, settings.weld)) {
			shapes.line(placed[from], placed[to], thin * 2.0f, selected.contains(from) && selected.contains(to) ? selectedColor : outsideColor);
		}
		const auto placeOf = Mesh::places(mesh, settings.weld);
		std::set<size_t> placesDrawn;
		std::set<size_t> placesSelected;
		for (auto vertex : selected) {
			if (vertex < placeOf.size()) {
				placesSelected.insert(placeOf[vertex]);
			}
		}
		const auto hovered = activeTool == Tool::SELECT && modal == Modal::NONE ? cornerUnder(mouse.position()) : std::nullopt;
		for (size_t i = 0; i < mesh.points.size(); ++i) {
			if (!placesDrawn.insert(placeOf[i]).second) {
				continue;
			}
			const bool chosen = placesSelected.contains(placeOf[i]);
			const bool under = hovered && placeOf[*hovered] == placeOf[i];
			const float size = theme.px(chosen || under ? 7.0f : 5.0f);
			shapes.square(placed[i], size + thin * 2.0f, cornerBack);
			shapes.square(placed[i], size, chosen ? selectedColor : cornerColor);
		}
		const auto pointer = mouse.position();
		if (modal == Modal::BOX && modalMoved) {
			const auto from = screenToOverlay(modalPointer);
			const auto to = screenToOverlay(pointer);
			const auto corner = MV::point(to.x, from.y);
			const auto other = MV::point(from.x, to.y);
			shapes.line(from, corner, thin, toolColor);
			shapes.line(corner, to, thin, toolColor);
			shapes.line(to, other, thin, toolColor);
			shapes.line(other, from, thin, toolColor);
		}
		if (!outline.empty()) {
			for (size_t i = 0; i < outline.size(); ++i) {
				const auto here = toOverlay(outline[i]);
				shapes.square(here, theme.px(6.0f), toolColor);
				const auto next = i + 1 < outline.size() ? toOverlay(outline[i + 1]) : screenToOverlay(pointer);
				shapes.line(here, next, thin * 2.0f, toolColor);
			}
			if (activeTool == Tool::POLYGON && outline.size() >= 2) {
				shapes.line(screenToOverlay(pointer), toOverlay(outline.front()), thin, edgeColor);
			}
		}
		if (activeTool == Tool::PAINT && pointerOverScene && pointerOverScene(pointer)) {
			const auto centre = screenToOverlay(pointer);
			const auto edge = screenToOverlay(pointer + MV::Point<int>(static_cast<int>(settings.brushRadius), 0));
			shapes.circle(centre, MV::distance(MV::point(centre.x, centre.y), MV::point(edge.x, edge.y)), thin * 1.5f, settings.color.A > 0.1f ? MV::Color(settings.color.R, settings.color.G, settings.color.B, 0.9f) : toolColor);
		}
		canvas->setPoints(shapes.points, shapes.indices);
	}

}
