#ifndef __MV_EDITOR_ANCHOR_EDITOR_H__
#define __MV_EDITOR_ANCHOR_EDITOR_H__

#include "componentPanels.h"
#include "editorFactories.h"
#include "Utility/generalUtility.h"

class AnchorEditor {
public:
	AnchorEditor(std::shared_ptr<MV::Scene::Node> a_root, std::shared_ptr<MV::Scene::Drawable> a_target, EditorPanel& a_panel) :
		root(a_root),
		target(a_target),
		panel(a_panel),
		resources(panel.services()){

		initializeInterface();
	}

	~AnchorEditor() {
		box->parent()->removeFromParent();
	}

private:
	void initializeInterface() {
		auto buttonSize = MV::size(110.0f, 27.0f);
		const MV::Size<> labelSize{ buttonSize.width, 20.0f };
		
		auto gridNode = MV::Scene::Node::make(root->renderer())->make("Background");
		grid = gridNode->position({ 0.0f, 0.0f })->attach<MV::Scene::Grid>()->gridWidth(116.0f)->
			color({ BOX_BACKGROUND })->margin({ 4.0f, 4.0f })->
			padding({ 2.0f, 2.0f });

		makeButton(grid->owner(), resources, buttonSize, "Close")->
			onAccept.connect("Close", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				panel.clearAnchorEditor();
			});

		makeLabel(gridNode, resources, "UsePositionLabel", labelSize, UTF_CHAR_STR("Use Position"));

		makeToggle(grid->owner(), *(resources.get<MV::TapDevice>()), "UsePosition", target->anchors().usePosition(), [&]() {
			target->anchors().usePosition(true);
		}, [&]() {
			target->anchors().usePosition(false);
		});

		makeLabel(gridNode, *panel.services().get<MV::TextLibrary>(), "AnchorsLabel", labelSize, UTF_CHAR_STR("Anchors"));

		auto currentBoundsMethod = std::make_shared<MV::Scene::Anchors::BoundsToOffset>(MV::Scene::Anchors::BoundsToOffset::Ignore);
		std::vector<MV::Scene::Anchors::BoundsToOffset> offsetMethod{ MV::Scene::Anchors::BoundsToOffset::Ignore, MV::Scene::Anchors::BoundsToOffset::Apply, MV::Scene::Anchors::BoundsToOffset::Apply_Reposition };
		std::vector<std::string> offsetMethodStrings{ "Ignore", "Apply", "Apply Reposition" };
		auto offsetMethodButton = makeButton(gridNode, resources, "WrapMode", buttonSize, offsetMethodStrings[MV::indexOf(offsetMethod, *currentBoundsMethod)]);
		std::weak_ptr<MV::Scene::Button> weakOffsetMethodButton(offsetMethodButton);
		offsetMethodButton->onAccept.connect("click", [&, weakOffsetMethodButton, offsetMethod, offsetMethodStrings, currentBoundsMethod](std::shared_ptr<MV::Scene::Clickable>) mutable {
			auto index = MV::wrap(0, static_cast<int>(offsetMethod.size()), MV::indexOf(offsetMethod, *currentBoundsMethod) + 1);
			*currentBoundsMethod = offsetMethod[index];
			weakOffsetMethodButton.lock()->text(offsetMethodStrings[index]);
		});

		anchorParentId = makeInputField(&panel, resources, gridNode, "parentName", buttonSize, target->anchors().hasParent() ? target->anchors().parent()->id() : "");
		anchorParentId->onEnter.connect("!", [&, currentBoundsMethod](auto&&) {
			auto foundParent = target->owner()->componentInParents<MV::Scene::Drawable>(anchorParentId->text(), false);
			if(target->anchors().hasParent() && !foundParent){
				target->anchors().removeFromParent();
				MV::info("Removing anchors from parent.");
			} else if(foundParent != target->anchors().parent()){
				target->anchors().parent(foundParent.self(), *currentBoundsMethod);
				MV::info("Applied anchors for Component Id: ", anchorParentId->text(), " On Node: ", anchorParentId->owner()->id());
				auto offset = target->anchors().offset();
				updatingFields = true;
				offsetMinX->set(std::lround(offset.minPoint.x));
				offsetMinY->set(std::lround(offset.minPoint.y));

				offsetMaxX->set(std::lround(offset.maxPoint.x));
				offsetMaxY->set(std::lround(offset.maxPoint.y));
				updatingFields = false;
			}
		});


		float halfButtonWidth = 52.0f;
		anchorMinX = makeInputField(&panel, resources, gridNode, "minX", MV::size(halfButtonWidth, 27.0f), std::to_string(target->anchors().anchor().minPoint.x));
		anchorMinY = makeInputField(&panel, resources, gridNode, "minY", MV::size(halfButtonWidth, 27.0f), std::to_string(target->anchors().anchor().minPoint.y));

		anchorMaxX = makeInputField(&panel, resources, gridNode, "maxX", MV::size(halfButtonWidth, 27.0f), std::to_string(target->anchors().anchor().maxPoint.x));
		anchorMaxY = makeInputField(&panel, resources, gridNode, "maxY", MV::size(halfButtonWidth, 27.0f), std::to_string(target->anchors().anchor().maxPoint.y));

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

		makeLabel(gridNode, resources, "Offset", labelSize, UTF_CHAR_STR("Offset"));
		
		offsetMinX = makeInputField(&panel, resources, gridNode, "offMinX", MV::size(halfButtonWidth, 27.0f), std::to_string(std::lround(target->anchors().offset().minPoint.x)));
		offsetMinY = makeInputField(&panel, resources, gridNode, "offMinY", MV::size(halfButtonWidth, 27.0f), std::to_string(std::lround(target->anchors().offset().minPoint.y)));
		
		offsetMaxX = makeInputField(&panel, resources, gridNode, "offMaxX", MV::size(halfButtonWidth, 27.0f), std::to_string(std::lround(target->anchors().offset().maxPoint.x)));
		offsetMaxY = makeInputField(&panel, resources, gridNode, "offMaxY", MV::size(halfButtonWidth, 27.0f), std::to_string(std::lround(target->anchors().offset().maxPoint.y)));

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

		makeButton(grid->owner(), resources, "Calculate", { 100.0f, 27.0f }, UTF_CHAR_STR("Calculate"))->
			onAccept.connect("Calculate", [&](std::shared_ptr<MV::Scene::Clickable> a_clickable) {
				target->anchors().applyBoundsToOffset(MV::Scene::Anchors::BoundsToOffset::Apply);
					
				auto offset = target->anchors().offset();
				updatingFields = true;
				offsetMinX->set(std::lround(offset.minPoint.x));
				offsetMinY->set(std::lround(offset.minPoint.y));

				offsetMaxX->set(std::lround(offset.maxPoint.x));
				offsetMaxY->set(std::lround(offset.maxPoint.y));
				updatingFields = false;
			}
		);

		auto pos = box ? box->parent()->position() : MV::Point<>(100.0f, 0.0f);
		box = makeDraggableBox("AnchorEditor", root, gridNode->bounds().size(), *(resources.get<MV::TapDevice>()));
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

	std::shared_ptr<MV::Scene::Text> anchorParentId;

	std::shared_ptr<MV::Scene::Text> anchorMinX;
	std::shared_ptr<MV::Scene::Text> anchorMinY;
	std::shared_ptr<MV::Scene::Text> anchorMaxX;
	std::shared_ptr<MV::Scene::Text> anchorMaxY;

	std::shared_ptr<MV::Scene::Text> offsetMinX;
	std::shared_ptr<MV::Scene::Text> offsetMinY;
	std::shared_ptr<MV::Scene::Text> offsetMaxX;
	std::shared_ptr<MV::Scene::Text> offsetMaxY;

	EditorPanel& panel;

	MV::Services& resources;
	bool updatingFields = false;
};

#endif
