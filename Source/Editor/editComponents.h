#ifndef __MV_EDITOR_SELECTION_H__
#define __MV_EDITOR_SELECTION_H__

#include <memory>
#include <map>
#include "Render/package.h"
#include "editorDefines.h"
class EditorPanel;

class EditableNode {
public:
	EditableNode(std::shared_ptr<MV::Scene::Node> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::MouseState *a_mouse);

	~EditableNode();

	void removeHandles();

	void resetHandles();

	void position(MV::Point<> a_newPosition);
	MV::Point<> position() const;

	std::function<void(EditableNode*)> onChange;

	std::shared_ptr<MV::Scene::Node> elementToEdit;
	std::shared_ptr<MV::Scene::Node> controlContainer;
private:
	MV::MouseState *mouse;

	MV::Scene::SafeComponent<MV::Scene::Clickable> positionHandle;
};

class EditableGrid {
public:
	EditableGrid(MV::Scene::SafeComponent<MV::Scene::Grid> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::MouseState *a_mouse);

	~EditableGrid() {
		controlContainer->removeFromParent();
	}

	void removeHandles();

	void resetHandles();

	std::function<void(EditableGrid*)> onChange;

	MV::Scene::SafeComponent<MV::Scene::Grid> elementToEdit;
private:

	MV::Scene::Node::BasicSharedSignalType nodeMoved;

	MV::MouseState *mouse;

	MV::Scene::SafeComponent<MV::Scene::Sprite> positionHandle;

	std::shared_ptr<MV::Scene::Node> controlContainer;
};

class EditableRectangle {
public:
	EditableRectangle(MV::Scene::SafeComponent<MV::Scene::Sprite> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_controlContainer, MV::MouseState *a_mouse);

	~EditableRectangle(){
		controlContainer->removeFromParent();
	}

	void removeHandles();

	void resetHandles();

	void texture(const std::shared_ptr<MV::TextureHandle> a_handle);
	std::shared_ptr<MV::TextureHandle> texture() const;
	void position(MV::Point<> a_newPosition);
	MV::Point<> position() const;

	void size(MV::Size<> a_newSize);
	MV::Size<> size();

	void aspect(MV::Size<> a_newAspect);

	void repositionHandles(bool a_fireOnChange = true, bool a_repositionElement = true, bool a_resizeElement = true);

	std::function<void(EditableRectangle*)> onChange;

	MV::Scene::SafeComponent<MV::Scene::Sprite> elementToEdit;
private:

	MV::Scene::Node::BasicSharedSignalType nodeMoved;

	MV::MouseState *mouse;

	MV::Size<> aspectSize;

	MV::Scene::SafeComponent<MV::Scene::Clickable> topLeftSizeHandle;
	MV::Scene::SafeComponent<MV::Scene::Clickable> topRightSizeHandle;
	MV::Scene::SafeComponent<MV::Scene::Clickable> bottomLeftSizeHandle;
	MV::Scene::SafeComponent<MV::Scene::Clickable> bottomRightSizeHandle;

	MV::Scene::SafeComponent<MV::Scene::Clickable> positionHandle;

	std::shared_ptr<MV::Scene::Node> controlContainer;
};

class Selection {
public:
	Selection(std::shared_ptr<MV::Scene::Node> a_scene, MV::MouseState &a_mouse);

	void callback(std::function<void(const MV::BoxAABB<int> &)> a_callback);
	void enable(std::function<void(const MV::BoxAABB<int> &)> a_callback);
	void enable();
	void disable();
	void exitSelection();

private:
	std::shared_ptr<MV::Scene::Node> scene;

	MV::Scene::SafeComponent<MV::Scene::Sprite> visibleSelection;
	MV::MouseState &mouse;
	MV::BoxAABB<int> selection;
	std::function<void(const MV::BoxAABB<int> &)> selectedCallback;

	MV::MouseState::SignalType onMouseDownHandle;
	MV::MouseState::SignalType onMouseUpHandle;

	MV::MouseState::SignalType onMouseMoveHandle;

	bool inSelection = false;

	static long gid;
	long id;
};

class EditableEmitter {
public:
	EditableEmitter(MV::Scene::SafeComponent<MV::Scene::Emitter> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::MouseState *a_mouse);

	~EditableEmitter(){
		controlContainer->removeFromParent();
		removeHandles();
	}

	void removeHandles();

	void resetHandles();

	void position(MV::Point<> a_newPosition);
	MV::Point<> position() const;

	void size(MV::Size<> a_newSize);
	MV::Size<> size();

	void texture(const std::shared_ptr<MV::TextureHandle> a_handle);

	void repositionHandles(bool a_fireOnChange = true, bool a_repositionElement = true, bool a_resizeElement = true);

	std::function<void(EditableEmitter*)> onChange;
	MV::Scene::SafeComponent<MV::Scene::Emitter> elementToEdit;
private:

	MV::Scene::Node::BasicSharedSignalType nodeMoved;

	MV::MouseState *mouse;

	MV::Scene::SafeComponent<MV::Scene::Clickable> topLeftSizeHandle;
	MV::Scene::SafeComponent<MV::Scene::Clickable> topRightSizeHandle;
	MV::Scene::SafeComponent<MV::Scene::Clickable> bottomLeftSizeHandle;
	MV::Scene::SafeComponent<MV::Scene::Clickable> bottomRightSizeHandle;

	MV::Scene::SafeComponent<MV::Scene::Clickable> positionHandle;

	std::shared_ptr<MV::Scene::Node> controlContainer;
};

#endif
