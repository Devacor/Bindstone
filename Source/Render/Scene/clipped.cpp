#include "clipped.h"
#include <memory>
#include "cereal/archives/json.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Clipped);

namespace MV {
	namespace Scene {

		void Clipped::refreshTexture(bool a_forceRefreshEvenIfNotDirty /*= true*/) {
			if (a_forceRefreshEvenIfNotDirty || dirtyTexture) {
				bool emptyCapturedBounds = capturedBounds.empty();
				auto pointAABB = emptyCapturedBounds ? bounds() : capturedBounds;
				auto textureSize = cast<int>(pointAABB.size());
				if (!clippedTexture || clippedTexture->size() != textureSize) {
					clippedTexture = DynamicTextureDefinition::make("", textureSize, { 0.0f, 0.0f, 0.0f, 0.0f });
				}

				texture(clippedTexture->makeHandle(textureSize));
				{
					auto framebuffer = owner()->renderer().makeFramebuffer(cast<int>(pointAABB.minPoint), textureSize, clippedTexture->textureId())->start();

					owner()->renderer().setBlendFunction(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_ONE);
					SCOPE_EXIT{ owner()->renderer().defaultBlendFunction(); };

					owner()->drawChildren(TransformMatrix());
				}
				notifyParentOfComponentChange();
				dirtyTexture = false;
			}
		}

		bool Clipped::preDraw() {
			if (shouldDraw) {
				refreshTexture(false);
				owner()->renderer().setBlendFunction(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
				return true;
			}
			return false;
		}

		bool Clipped::postDraw() {
			owner()->renderer().defaultBlendFunction();
			return !shouldDraw;
		}

		Clipped::Clipped(const std::weak_ptr<Node> &a_owner) :
			Sprite(a_owner) {
			
		}

		void Clipped::observeNode(const std::shared_ptr<Node>& a_node) {
			std::vector<std::list<Node::BasicSharedSignalType>::iterator> localBasicSignals;
			basicSignals.push_back(a_node->onChildAdd.connect([&](const std::shared_ptr<Node> &a_this) {
				dirtyTexture = true;
				observeNode(a_this);
			}));
			localBasicSignals.push_back(basicSignals.end()--);
			auto markDirty = [&](const std::shared_ptr<Node> &a_this) {
				dirtyTexture = true;
			};
			basicSignals.push_back(a_node->onDepthChange.connect(markDirty));
			localBasicSignals.push_back(basicSignals.end()--);
			basicSignals.push_back(a_node->onAlphaChange.connect(markDirty));
			localBasicSignals.push_back(basicSignals.end()--);
			basicSignals.push_back(a_node->onShow.connect(markDirty));
			localBasicSignals.push_back(basicSignals.end()--);
			basicSignals.push_back(a_node->onHide.connect(markDirty));
			localBasicSignals.push_back(basicSignals.end()--);
			basicSignals.push_back(a_node->onTransformChange.connect(markDirty));
			localBasicSignals.push_back(basicSignals.end()--);
			basicSignals.push_back(a_node->onLocalBoundsChange.connect(markDirty));
			localBasicSignals.push_back(basicSignals.end()--);
			
			std::weak_ptr<Component> weakSelf = shared_from_this();
			componentSignals.push_back(a_node->onComponentUpdate.connect([&, weakSelf](const std::shared_ptr<Component> &a_this){
				if (a_this != weakSelf.lock()) {
					dirtyTexture = true;
				}
			}));
			std::list<Node::ComponentSharedSignalType>::iterator localComponentSignal = componentSignals.end()--;

			basicSignals.push_back(nullptr);
			localBasicSignals.push_back(basicSignals.end()--);

			auto removeSignalResponder = basicSignals.end()--;
			*(removeSignalResponder) = Node::BasicSignalType::make([&,localBasicSignals, localComponentSignal](const std::shared_ptr<Node> &a_this) mutable {
				dirtyTexture = true;
				componentSignals.erase(localComponentSignal);
				for (auto&& signalIterator : localBasicSignals) {
					basicSignals.erase(signalIterator);
				}
			});
			a_node->onRemove.connect(*removeSignalResponder);

			for (auto&& child : *a_node) {
				observeNode(child);
			}
		}

		std::shared_ptr<Clipped> Clipped::clearCaptureBounds() {
			capturedBounds = BoxAABB<>();
			return std::static_pointer_cast<Clipped>(shared_from_this());
		}

		std::shared_ptr<Clipped> Clipped::captureBounds(const BoxAABB<> &a_newCapturedBounds) {
			capturedBounds = a_newCapturedBounds;
			dirtyTexture = true;
			return std::static_pointer_cast<Clipped>(shared_from_this());
		}

		BoxAABB<> Clipped::captureBounds() {
			return capturedBounds;
		}

		std::shared_ptr<Sprite> Clipped::captureSize(const Size<> &a_size, const Point<> &a_centerPoint) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			Point<> topLeft;
			Point<> bottomRight = toPoint(a_size);

			topLeft -= a_centerPoint;
			bottomRight -= a_centerPoint;
			
			return captureBounds({ topLeft, bottomRight });
		}

		std::shared_ptr<Sprite> Clipped::captureSize(const Size<> &a_size, bool a_center /*= false*/) {
			return captureSize(a_size, (a_center) ? point(a_size.width / 2.0f, a_size.height / 2.0f) : point(0.0f, 0.0f));
		}

		std::shared_ptr<Clipped> Clipped::bounds(const BoxAABB<> &a_bounds) {
			dirtyTexture = true;
			return std::static_pointer_cast<Clipped>(Sprite::bounds(a_bounds));
		}

		BoxAABB<> Clipped::bounds() {
			return boundsImplementation();
		}

		std::shared_ptr<Clipped> Clipped::size(const Size<> &a_size, const Point<> &a_centerPoint) {
			dirtyTexture = true;
			return std::static_pointer_cast<Clipped>(Sprite::size(a_size, a_centerPoint));
		}

		std::shared_ptr<Clipped> Clipped::size(const Size<> &a_size, bool a_center /*= false*/) {
			dirtyTexture = true;
			return std::static_pointer_cast<Clipped>(Sprite::size(a_size, a_center));
		}

	}
}
