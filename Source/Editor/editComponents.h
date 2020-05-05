#ifndef __MV_EDITOR_SELECTION_H__
#define __MV_EDITOR_SELECTION_H__

#include <memory>
#include <map>
#include "editorDefines.h"

class EditorPanel;

class EditableNode {
public:
	EditableNode(std::shared_ptr<MV::Scene::Node> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse);

	
	~EditableNode();

	void removeHandles();

	void resetHandles();

	void position(const MV::Point<> &a_newPosition);
	MV::Point<> position() const;

	void rotation(const MV::AxisAngles &a_rotate);
	MV::AxisAngles rotation() const;

	std::function<void(EditableNode*)> onChange;

	std::shared_ptr<MV::Scene::Node> elementToEdit;
	std::shared_ptr<MV::Scene::Node> controlContainer;
private:
	MV::TapDevice *mouse;

	MV::Scene::SafeComponent<MV::Scene::Clickable> positionHandle;
	MV::Scene::SafeComponent<MV::Scene::Clickable> rotationHandle;
};

class ResizeHandles {
public:
	ResizeHandles(MV::Scene::SafeComponent<MV::Scene::Component> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse);
	virtual ~ResizeHandles() {
		controlContainer->removeFromParent();
	}

	virtual void removeHandles();

	virtual void resetHandles();

	virtual void repositionHandles(bool a_fireOnChange = true, bool a_repositionElement = true, bool a_resizeElement = true);

	void position(MV::Point<> a_newPosition);
	MV::Point<> position() const;

	void size(MV::Size<> a_newSize);
	MV::Size<> size();

	std::function<void(ResizeHandles*)> onChange;

	MV::Scene::SafeComponent<MV::Scene::Component> elementToEditBase;
protected:
	virtual void removeHandlesImplementation() {}

	MV::Scene::Node::BasicReceiverType nodeMoved;
	MV::TapDevice *mouse;

	MV::Size<> aspectSize;

	MV::Scene::SafeComponent<MV::Scene::Clickable> topLeftSizeHandle;
	MV::Scene::SafeComponent<MV::Scene::Clickable> topRightSizeHandle;
	MV::Scene::SafeComponent<MV::Scene::Clickable> bottomLeftSizeHandle;
	MV::Scene::SafeComponent<MV::Scene::Clickable> bottomRightSizeHandle;

	MV::Scene::SafeComponent<MV::Scene::Clickable> positionHandle;

	std::shared_ptr<MV::Scene::Node> controlContainer;
};

class EditablePoints {
public:
	EditablePoints(MV::Scene::SafeComponent<MV::Scene::Drawable> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse);

	~EditablePoints() {
		controlContainer->removeFromParent();
	}

	void removeHandles();

	void resetHandles();

	std::function<void(MV::Point<>)> onSelected;
	std::function<bool(size_t, MV::Point<>)> onDragged; //true if drag should move it.

	MV::Scene::SafeComponent<MV::Scene::Drawable> elementToEdit;
private:

	void hookupSignals(MV::Scene::SafeComponent<MV::Scene::Clickable> pointHandle, int pointIndex);

	MV::Scene::Node::BasicReceiverType nodeMoved;

	MV::TapDevice *mouse;

	std::vector<MV::Scene::SafeComponent<MV::Scene::Clickable>> pointHandles;

	std::shared_ptr<MV::Scene::Node> controlContainer;
};

class EditableGrid {
public:
	EditableGrid(MV::Scene::SafeComponent<MV::Scene::Grid> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse);

	~EditableGrid() {
		controlContainer->removeFromParent();
	}

	void removeHandles();

	void resetHandles();

	MV::Scene::SafeComponent<MV::Scene::Grid> elementToEdit;
private:

	MV::Scene::Node::BasicReceiverType nodeMoved;

	MV::TapDevice *mouse;

	MV::Scene::SafeComponent<MV::Scene::Sprite> positionHandle;

	std::shared_ptr<MV::Scene::Node> controlContainer;
};

class EditableSpine {
public:
	EditableSpine(MV::Scene::SafeComponent<MV::Scene::Spine> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse);

	~EditableSpine() {
		controlContainer->removeFromParent();
	}

	void removeHandles();

	void resetHandles();

	std::function<void(EditableSpine*)> onChange;

	MV::Scene::SafeComponent<MV::Scene::Spine> elementToEdit;
private:

	MV::Scene::Node::BasicReceiverType nodeMoved;

	MV::TapDevice *mouse;

	MV::Scene::SafeComponent<MV::Scene::Sprite> positionHandle;

	std::shared_ptr<MV::Scene::Node> controlContainer;
};

class EditableParallax {
public:
	EditableParallax(MV::Scene::SafeComponent<MV::Scene::Parallax> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice* a_mouse);

	~EditableParallax() {
		controlContainer->removeFromParent();
	}

	void removeHandles();

	void resetHandles();

	std::function<void(EditableParallax*)> onChange;

	MV::Scene::SafeComponent<MV::Scene::Parallax> elementToEdit;
private:

	MV::Scene::Node::BasicReceiverType nodeMoved;

	MV::TapDevice* mouse;

	MV::Scene::SafeComponent<MV::Scene::Sprite> positionHandle;

	std::shared_ptr<MV::Scene::Node> controlContainer;
};


class EditableButton : public ResizeHandles {
public:
	EditableButton(MV::Scene::SafeComponent<MV::Scene::Button> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse);
	~EditableButton();

	MV::Scene::SafeComponent<MV::Scene::Button> elementToEdit;
};

class EditableClickable : public ResizeHandles {
public:
	EditableClickable(MV::Scene::SafeComponent<MV::Scene::Clickable> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse);
	~EditableClickable();

	MV::Scene::SafeComponent<MV::Scene::Clickable> elementToEdit;
};

class EditableRectangle : public ResizeHandles {
public:
	EditableRectangle(MV::Scene::SafeComponent<MV::Scene::Sprite> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_controlContainer, MV::TapDevice *a_mouse);

	void aspect(MV::Size<> a_newAspect);

	MV::Scene::SafeComponent<MV::Scene::Sprite> elementToEdit;
};

class EditableText : public ResizeHandles {
public:
	EditableText(MV::Scene::SafeComponent<MV::Scene::Text> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_controlContainer, MV::TapDevice *a_mouse);

	MV::Scene::SafeComponent<MV::Scene::Text> elementToEdit;
};

class Selection {
public:
	Selection(std::shared_ptr<MV::Scene::Node> a_scene, MV::TapDevice &a_mouse);

	void callback(std::function<void(const MV::BoxAABB<int> &)> a_callback);
	void enable(std::function<void(const MV::BoxAABB<int> &)> a_callback);
	void enable();
	void disable();
	void exitSelection();

private:
	std::shared_ptr<MV::Scene::Node> scene;

	MV::Scene::SafeComponent<MV::Scene::Sprite> visibleSelection;
	MV::TapDevice &mouse;
	MV::BoxAABB<int> selection;
	std::function<void(const MV::BoxAABB<int> &)> selectedCallback;

	MV::TapDevice::SignalType onMouseDownHandle;
	MV::TapDevice::SignalType onMouseUpHandle;

	MV::TapDevice::SignalType onMouseMoveHandle;

	bool inSelection = false;

	static long gid;
	long id;
};

class EditableEmitter : public ResizeHandles {
public:
	EditableEmitter(MV::Scene::SafeComponent<MV::Scene::Emitter> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse);

	void position(MV::Point<> a_newPosition);
	MV::Point<> position() const;

	void size(MV::Size<> a_newSize);
	MV::Size<> size();

	virtual void repositionHandles(bool a_fireOnChange = true, bool a_repositionElement = true, bool a_resizeElement = true) override;

	MV::Scene::SafeComponent<MV::Scene::Emitter> elementToEdit;
};

class EditablePathMap : public ResizeHandles {
public:
	EditablePathMap(MV::Scene::SafeComponent<MV::Scene::PathMap> a_elementToEdit, std::shared_ptr<MV::Scene::Node> a_rootContainer, MV::TapDevice *a_mouse);

	~EditablePathMap() {
		elementToEdit->hide();
	}

	virtual void resetHandles() override;

	void position(MV::Point<> a_newPosition);
	MV::Point<> position() const;

	void size(MV::Size<> a_newSize);
	MV::Size<> size();

	MV::Scene::SafeComponent<MV::Scene::PathMap> elementToEdit;
private:
	MV::Point<int> lastGridPosition{ -1, -1 };
};

#endif
