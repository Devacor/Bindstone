#ifndef __MV_EDITOR_SELECTION_H__
#define __MV_EDITOR_SELECTION_H__

#include <memory>
#include <map>
#include "Render/package.h"
#include "editorDefines.h"
class EditorPanel;

class EditableRectangle {
public:
	EditableRectangle(std::shared_ptr<MV::Scene::Rectangle> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_controlContainer, MV::MouseState *a_mouse);

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

	void repositionHandles(bool a_fireOnChange = true);

	std::function<void(EditableRectangle*)> onChange;
private:

	MV::MouseState *mouse;

	MV::Size<> aspectSize;

	std::shared_ptr<MV::Scene::Rectangle> elementToEdit;

	std::shared_ptr<MV::Scene::Clickable> topLeftSizeHandle;
	std::shared_ptr<MV::Scene::Clickable> topRightSizeHandle;
	std::shared_ptr<MV::Scene::Clickable> bottomLeftSizeHandle;
	std::shared_ptr<MV::Scene::Clickable> bottomRightSizeHandle;

	std::shared_ptr<MV::Scene::Clickable> positionHandle;

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

	std::shared_ptr<MV::Scene::Rectangle> visibleSelection;
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
	EditableEmitter(std::shared_ptr<MV::Scene::Emitter> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::MouseState *a_mouse);

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

	void repositionHandles(bool a_fireOnChange = true);

	std::function<void(EditableEmitter*)> onChange;
	std::shared_ptr<MV::Scene::Emitter> elementToEdit;
private:

	MV::MouseState *mouse;

	std::shared_ptr<MV::Scene::Clickable> topLeftSizeHandle;
	std::shared_ptr<MV::Scene::Clickable> topRightSizeHandle;
	std::shared_ptr<MV::Scene::Clickable> bottomLeftSizeHandle;
	std::shared_ptr<MV::Scene::Clickable> bottomRightSizeHandle;

	std::shared_ptr<MV::Scene::Clickable> positionHandle;

	std::shared_ptr<MV::Scene::Node> controlContainer;
};

#endif
