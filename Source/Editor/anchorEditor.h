#ifndef __MV_EDITOR_ANCHOR_EDITOR_H__
#define __MV_EDITOR_ANCHOR_EDITOR_H__

#include <memory>
#include "componentPanels.h"
#include "editorDefines.h"
#include "editorFactories.h"

class AnchorEditor {
public:
	AnchorEditor(std::shared_ptr<MV::Scene::Node> a_root, std::shared_ptr<MV::Scene::Drawable> a_target, EditorPanel& a_panel) :
		root(a_root),
		target(a_target),
		panel(a_panel),
		resources(panel.resources()){

		initializeInterface();
	}

	~AnchorEditor() {
		box->parent()->removeFromParent();
	}

private:
	void initializeInterface() {
		auto buttonSize = MV::size(116.0f, 27.0f);
		const MV::Size<> labelSize{ buttonSize.width, 20.0f };
		
		auto gridNode = MV::Scene::Node::make(root->renderer());
		grid = gridNode->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
			color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
			padding({ 2.0f, 2.0f });

		makeButton(grid->owner(), *(resources.textLibrary), *(resources.mouse), "Close", { 100.0f, 27.0f }, UTF_CHAR_STR("Close"))->
			onAccept.connect("Close", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				panel.clearAnchorEditor();
			}
		);

		std::weak_ptr<MV::Scene::Node> weakToggle = makeToggle(grid->owner(), *(resources.mouse), "AttachToggle", target->anchors().hasParent(), [&](){
			auto scaler = target->owner()->root()->component<MV::Scene::Drawable>("ScreenScaler", false);
			if (!scaler) {
				std::cout << "Couldn't find scaler" << std::endl;
			}
			else {
				target->anchors().parent(scaler.get());
			}
		}, [&](){
			target->anchors().removeFromParent();
		});

		makeToggle(grid->owner(), *(resources.mouse), "UsePosition", target->anchors().usePosition(), [&]() {
			target->anchors().usePosition(true);
		}, [&]() {
			target->anchors().usePosition(false);
		});

		makeLabel(gridNode, *panel.resources().textLibrary, "Anchors", labelSize, UTF_CHAR_STR("Anchors"));
		float halfButtonWidth = 52.0f;
		anchorMinX = makeInputField(&panel, *(resources.mouse), gridNode, *(resources.textLibrary), "minX", MV::size(halfButtonWidth, 27.0f), std::to_string(target->anchors().anchor().minPoint.x));
		anchorMinY = makeInputField(&panel, *(resources.mouse), gridNode, *(resources.textLibrary), "minY", MV::size(halfButtonWidth, 27.0f), std::to_string(target->anchors().anchor().minPoint.y));

		anchorMaxX = makeInputField(&panel, *(resources.mouse), gridNode, *(resources.textLibrary), "maxX", MV::size(halfButtonWidth, 27.0f), std::to_string(target->anchors().anchor().maxPoint.x));
		anchorMaxY = makeInputField(&panel, *(resources.mouse), gridNode, *(resources.textLibrary), "maxY", MV::size(halfButtonWidth, 27.0f), std::to_string(target->anchors().anchor().maxPoint.y));

		updateFromInputField(anchorMinX, [&]() {
			auto updatedAnchors = target->anchors().anchor();
			updatedAnchors.minPoint.x = anchorMinX->number();
			target->anchors().anchor(updatedAnchors);
		});

		updateFromInputField(anchorMinY, [&]() {
			auto updatedAnchors = target->anchors().anchor();
			updatedAnchors.minPoint.y = anchorMinY->number();
			target->anchors().anchor(updatedAnchors);
		});

		updateFromInputField(anchorMaxX, [&]() {
			auto updatedAnchors = target->anchors().anchor();
			updatedAnchors.maxPoint.x = anchorMaxX->number();
			target->anchors().anchor(updatedAnchors);
		});

		updateFromInputField(anchorMaxY, [&]() {
			auto updatedAnchors = target->anchors().anchor();
			updatedAnchors.maxPoint.y = anchorMaxY->number();
			target->anchors().anchor(updatedAnchors);
		});

		makeLabel(gridNode, *panel.resources().textLibrary, "Offset", labelSize, UTF_CHAR_STR("Offset"));
		
		offsetMinX = makeInputField(&panel, *(resources.mouse), gridNode, *(resources.textLibrary), "offMinX", MV::size(halfButtonWidth, 27.0f), std::to_string(std::lround(target->anchors().offset().minPoint.x)));
		offsetMinY = makeInputField(&panel, *(resources.mouse), gridNode, *(resources.textLibrary), "offMinY", MV::size(halfButtonWidth, 27.0f), std::to_string(std::lround(target->anchors().offset().minPoint.y)));
		
		offsetMaxX = makeInputField(&panel, *(resources.mouse), gridNode, *(resources.textLibrary), "offMaxX", MV::size(halfButtonWidth, 27.0f), std::to_string(std::lround(target->anchors().offset().maxPoint.x)));
		offsetMaxY = makeInputField(&panel, *(resources.mouse), gridNode, *(resources.textLibrary), "offMaxY", MV::size(halfButtonWidth, 27.0f), std::to_string(std::lround(target->anchors().offset().maxPoint.y)));

		updateFromInputField(offsetMinX, [&]() {
			auto updatedOffset = target->anchors().offset();
			updatedOffset.minPoint.x = offsetMinX->number();
			target->anchors().offset(updatedOffset);
		});

		updateFromInputField(offsetMinY, [&]() {
			auto updatedOffset = target->anchors().offset();
			updatedOffset.minPoint.y = offsetMinY->number();
			target->anchors().offset(updatedOffset);
		});

		updateFromInputField(offsetMaxX, [&]() {
			auto updatedOffset = target->anchors().offset();
			updatedOffset.maxPoint.x = offsetMaxX->number();
			target->anchors().offset(updatedOffset);
		});

		updateFromInputField(offsetMaxY, [&]() {
			auto updatedOffset = target->anchors().offset();
			updatedOffset.maxPoint.y = offsetMaxY->number();
			target->anchors().offset(updatedOffset);
		});

		makeButton(grid->owner(), *(resources.textLibrary), *(resources.mouse), "Calculate", { 100.0f, 27.0f }, UTF_CHAR_STR("Calculate"))->
			onAccept.connect("Calculate", [&, weakToggle](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				auto scaler = target->owner()->root()->component<MV::Scene::Drawable>("ScreenScaler", false);
				if (!scaler) {
					std::cout << "Couldn't find scaler" << std::endl;
				} else {
					target->anchors().parent(scaler.get(), MV::Scene::Anchors::BoundsToOffset::Apply);
					
					weakToggle.lock()->get("toggle")->show();

					auto offset = target->anchors().offset();
					updatingFields = true;
					offsetMinX->number(std::lround(offset.minPoint.x));
					offsetMinY->number(std::lround(offset.minPoint.y));

					offsetMaxX->number(std::lround(offset.maxPoint.x));
					offsetMaxY->number(std::lround(offset.maxPoint.y));
					updatingFields = false;
				}
			}
		);

		auto pos = box ? box->parent()->position() : MV::Point<>(100.0f, 0.0f);
		box = makeDraggableBox("AnchorEditor", root, grid->bounds().size(), *(resources.mouse));
		box->parent()->position(pos);
		box->add(gridNode);
	}

	void updateFromInputField(std::shared_ptr<MV::Scene::Text> a_inputField, std::function<void ()> a_updateMethod) {
		a_inputField->owner()->component<MV::Scene::Clickable>()->onAccept.connect("!", [=](std::shared_ptr<MV::Scene::Clickable>) {
			if (!updatingFields) {
				a_updateMethod();
			}
		});
		a_inputField->onEnter.connect("!", [=](std::shared_ptr<MV::Scene::Text>) {
			if (!updatingFields) {
				a_updateMethod();
			}
		});
	}

	std::shared_ptr<MV::Scene::Node> root;
	std::shared_ptr<MV::Scene::Node> box;
	std::shared_ptr<MV::Scene::Grid> grid;

	std::shared_ptr<MV::Scene::Drawable> target;

	std::shared_ptr<MV::Scene::Text> anchorMinX;
	std::shared_ptr<MV::Scene::Text> anchorMinY;
	std::shared_ptr<MV::Scene::Text> anchorMaxX;
	std::shared_ptr<MV::Scene::Text> anchorMaxY;

	std::shared_ptr<MV::Scene::Text> offsetMinX;
	std::shared_ptr<MV::Scene::Text> offsetMinY;
	std::shared_ptr<MV::Scene::Text> offsetMaxX;
	std::shared_ptr<MV::Scene::Text> offsetMaxY;

	EditorPanel& panel;

	SharedResources resources;
	bool updatingFields = false;
};

#endif
