#include "drawable.h"

#include "cereal/archives/json.hpp"
#include "cereal/archives/portable_binary.hpp"

CEREAL_REGISTER_TYPE(MV::Scene::Drawable);
CEREAL_CLASS_VERSION(MV::Scene::Drawable, 1);

namespace MV {
	namespace Scene {

		bool Drawable::draw() {
			bool allowChildrenToDraw = true;
			lazyInitializeShader();
			if (preDraw()) {
				SCOPE_EXIT{ allowChildrenToDraw = postDraw(); };
				defaultDrawImplementation();
			}
			return allowChildrenToDraw;
		}

		Color Drawable::color() const {
			std::vector<Color> colorsToAverage;
			for (auto&& point : points) {
				colorsToAverage.push_back(point);
			}
			return std::accumulate(colorsToAverage.begin(), colorsToAverage.end(), Color(0.0f, 0.0f, 0.0f, 0.0f)) / static_cast<PointPrecision>(colorsToAverage.size());
		}

		std::shared_ptr<Drawable> Drawable::color(const Color &a_newColor) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			for (auto&& point : points) {
				point = a_newColor;
			}
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		std::shared_ptr<Drawable> MV::Scene::Drawable::colors(const std::vector<Color>& a_newColors) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			MV::require<RangeException>(a_newColors.size() == points.size(), "Point and Color vector size mismatch!");
			for (size_t i = 0; i < points.size(); ++i) {
				points[i] = a_newColors[i];
			}
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		std::shared_ptr<Drawable> Drawable::hide() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			std::shared_ptr<Drawable> self = std::static_pointer_cast<Drawable>(shared_from_this());
			if (shouldDraw) {
				shouldDraw = false;
				notifyParentOfComponentChange();
			}
			return self;
		}

		std::shared_ptr<Drawable> MV::Scene::Drawable::show() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			std::shared_ptr<Drawable> self = std::static_pointer_cast<Drawable>(shared_from_this());
			if (!shouldDraw) {
				shouldDraw = true;
				notifyParentOfComponentChange();
			}
			return self;
		}

		std::shared_ptr<Drawable> Drawable::shader(const std::string &a_shaderProgramId) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			shaderProgramId = a_shaderProgramId;
			auto self = std::static_pointer_cast<Drawable>(shared_from_this());
			forceInitializeShader();
			return self;
		}

		std::shared_ptr<Drawable> Drawable::texture(std::shared_ptr<TextureHandle> a_texture) {
			std::lock_guard<std::recursive_mutex> guard(lock);
			if (ourTexture && textureSizeSignal) {
				ourTexture->sizeObserver.disconnect(textureSizeSignal);
			}
			textureSizeSignal.reset();
			ourTexture = a_texture;
			if (ourTexture) {
				textureSizeSignal = TextureHandle::SignalType::make([&](std::shared_ptr<MV::TextureHandle> a_handle) {
					updateTextureCoordinates();
				});
				ourTexture->sizeObserver.connect(textureSizeSignal);
			}
			updateTextureCoordinates();
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		std::shared_ptr<Drawable> Drawable::clearTexture() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			if (ourTexture && textureSizeSignal) {
				ourTexture->sizeObserver.disconnect(textureSizeSignal);
			}
			ourTexture = nullptr;
			updateTextureCoordinates();
			return std::static_pointer_cast<Drawable>(shared_from_this());
		}

		void Drawable::drawOpenGL(GLuint& a_bufferId, const std::vector<MV::DrawPoint>& a_points, const std::vector<unsigned int> &a_vertexIndices, MV::Shader* a_shader, const std::shared_ptr<MV::TextureHandle> &a_texture, const MV::TransformMatrix& a_matrix, GLenum a_drawType) {
			if (owner()->renderer().headless()) { return; }

			if (!a_vertexIndices.empty()) {
				require<ResourceException>(a_shader, "No shader program for Drawable!");
				a_shader->use();

				if (a_bufferId == 0) {
					glGenBuffers(1, &a_bufferId);
				}

				glBindBuffer(GL_ARRAY_BUFFER, a_bufferId);
				auto structSize = static_cast<GLsizei>(sizeof(a_points[0]));
				glBufferData(GL_ARRAY_BUFFER, a_points.size() * structSize, &(a_points[0]), GL_STATIC_DRAW);

				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glEnableVertexAttribArray(2);

				auto positionOffset = static_cast<size_t>(offsetof(DrawPoint, x));
				auto textureOffset = static_cast<size_t>(offsetof(DrawPoint, textureX));
				auto colorOffset = static_cast<size_t>(offsetof(DrawPoint, R));
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)positionOffset); //Point
				glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)textureOffset); //UV
				glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)colorOffset); //Color

				a_shader->set("texture", a_texture);
				a_shader->set("transformation", a_matrix);

				glDrawElements(a_drawType, static_cast<GLsizei>(a_vertexIndices.size()), GL_UNSIGNED_INT, &a_vertexIndices[0]);

				glDisableVertexAttribArray(0);
				glDisableVertexAttribArray(1);
				glDisableVertexAttribArray(2);
				glUseProgram(0);
			}
		}

		void Drawable::boundsImplementation(const BoxAABB<> &a_bounds)
		{
			points[0] = a_bounds.minPoint;
			points[1].x = a_bounds.minPoint.x;	points[1].y = a_bounds.maxPoint.y;	points[1].z = (a_bounds.maxPoint.z + a_bounds.minPoint.z) / 2.0f;
			points[2] = a_bounds.maxPoint;
			points[3].x = a_bounds.maxPoint.x;	points[3].y = a_bounds.minPoint.y;	points[3].z = points[1].z;

			refreshBounds();
		}

		void Drawable::defaultDrawImplementation() {
			if (owner()->renderer().headless()) { return; }

			std::lock_guard<std::recursive_mutex> guard(lock);
			if (!vertexIndices.empty()) {
				require<ResourceException>(shaderProgram, "No shader program for Drawable!");
				shaderProgram->use();

				if (bufferId == 0) {
					glGenBuffers(1, &bufferId);
				}

				glBindBuffer(GL_ARRAY_BUFFER, bufferId);
				auto structSize = static_cast<GLsizei>(sizeof(points[0]));
				glBufferData(GL_ARRAY_BUFFER, points.size() * structSize, &(points[0]), GL_STATIC_DRAW);

				glEnableVertexAttribArray(0);
				glEnableVertexAttribArray(1);
				glEnableVertexAttribArray(2);

				auto positionOffset = static_cast<size_t>(offsetof(DrawPoint, x));
				auto textureOffset = static_cast<size_t>(offsetof(DrawPoint, textureX));
				auto colorOffset = static_cast<size_t>(offsetof(DrawPoint, R));
				glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)positionOffset); //Point
				glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)textureOffset); //UV
				glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, structSize, (GLvoid*)colorOffset); //Color

				shaderProgram->set("texture", ourTexture);
				shaderProgram->set("transformation", owner()->renderer().projectionMatrix().top() * owner()->worldTransform());
				if (shaderUpdater) {
					shaderUpdater(shaderProgram);
				}

				glDrawElements(drawType, static_cast<GLsizei>(vertexIndices.size()), GL_UNSIGNED_INT, &vertexIndices[0]);

				glDisableVertexAttribArray(0);
				glDisableVertexAttribArray(1);
				glDisableVertexAttribArray(2);
				glUseProgram(0);
			}
		}

		void Drawable::refreshBounds() {
			std::lock_guard<std::recursive_mutex> guard(lock);
			auto originalBounds = localBounds;
			if (!points.empty()) {
				localBounds.initialize(points[0]);
				for (size_t i = 1; i < points.size(); ++i) {
					localBounds.expandWith(points[i]);
				}
			} else {
				localBounds = BoxAABB<>();
			}
			if (originalBounds != localBounds) {
				for (auto&& childAnchor : childAnchors) {
					childAnchor->apply();
				}
				notifyParentOfBoundsChange();
			}
		}

		void Drawable::lazyInitializeShader() {
			if (!shaderProgram) {
				forceInitializeShader();
			}
		}

		void Drawable::forceInitializeShader() {
			if (owner()->renderer().hasShader(shaderProgramId)) {
				shaderProgram = owner()->renderer().getShader(shaderProgramId);
			} else {
				shaderProgram = owner()->renderer().defaultShader();
			}
		}

		Drawable::Drawable(const std::weak_ptr<Node> &a_owner) :
			Component(a_owner),
			ourAnchors(this) {
			points.resize(4);
		}

		void Drawable::initialize() {
			if (ourTexture && !textureSizeSignal) {
				textureSizeSignal = TextureHandle::SignalType::make([&](std::shared_ptr<MV::TextureHandle> a_handle) {
					updateTextureCoordinates();
				});
				ourTexture->sizeObserver.connect(textureSizeSignal);
			}
		}

		std::shared_ptr<Component> Drawable::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Component::cloneHelper(a_clone);
			auto drawableClone = std::static_pointer_cast<Drawable>(a_clone);
			drawableClone->shouldDraw = shouldDraw;
			drawableClone->ourTexture = (ourTexture) ? ourTexture->clone() : nullptr;
			drawableClone->shader(shaderProgramId);
			drawableClone->vertexIndices = vertexIndices;
			drawableClone->localBounds = localBounds;
			drawableClone->drawType = drawType;
			drawableClone->points = points;
			drawableClone->notifyParentOfBoundsChange();
			return a_clone;
		}

		Anchors::Anchors(Drawable *a_self) :
			selfReference(a_self) {
		}

		Anchors::~Anchors() {
			removeFromParent();
		}

		Anchors& Anchors::parent(const std::weak_ptr<Drawable> &a_parent, BoundsToOffset a_offsetFromBounds) {
			removeFromParent();
			parentReference = a_parent;

			applyBoundsToOffset(a_offsetFromBounds);

			registerWithParent();
			return *this;
		}

		std::shared_ptr<MV::Scene::Drawable> Anchors::parent() const {
			if (!parentReference.expired()) {
				return parentReference.lock();
			} else {
				return nullptr;
			}
		}

		Anchors& Anchors::anchor(const BoxAABB<> &a_anchor) {
			parentAnchors = a_anchor;
			return *this;
		}

		Anchors& Anchors::anchor(const Point<> &a_anchor) {
			parentAnchors.minPoint = a_anchor;
			parentAnchors.maxPoint = a_anchor;
			return *this;
		}

		Anchors& Anchors::offset(const BoxAABB<> &a_offset) {
			ourOffset = a_offset;
			return *this;
		}

		Anchors& Anchors::pivot(const Point<> &a_pivot) {
			pivotPercent = a_pivot;
			return *this;
		}

		Anchors& Anchors::apply() {
			if (!applying && !parentReference.expired()) {
				applying = true;
				SCOPE_EXIT{ applying = false; };
				auto parentBounds = parentReference.lock()->worldBounds();
				auto parentSize = parentBounds.size();
				
				BoxAABB<> childBounds { (parentAnchors * toScale(parentSize)) + ourOffset + parentBounds.minPoint };
				if (applyingPosition) {
					selfReference->owner()->worldPosition(childBounds.minPoint);
					selfReference->bounds({ Point<>(), childBounds.size() / selfReference->owner()->worldScale() });
				} else {
					selfReference->worldBounds(childBounds);
				}
			}
			return *this;
		}

		MV::Scene::Anchors& Anchors::usePosition(bool a_newValue) {
			applyingPosition = a_newValue;
			apply();
			return *this;
		}

		Anchors& Anchors::applyBoundsToOffset(BoundsToOffset a_offsetFromBounds) {
			if (!applying && a_offsetFromBounds != BoundsToOffset::Ignore && !parentReference.expired()) {
				auto childBounds = selfReference->worldBounds();
				auto parentBounds = parentReference.lock()->worldBounds();
				auto parentSize = parentBounds.size();

				auto scaledParentAnchors = parentAnchors * toScale(parentSize);
				
				if (a_offsetFromBounds == BoundsToOffset::Apply_Reposition) {
					childBounds += scaledParentAnchors - selfReference->owner()->worldPosition();
				}

				ourOffset = childBounds - (scaledParentAnchors + parentBounds.minPoint);
			}
			return *this;
		}

		Anchors& Anchors::removeFromParent() {
			if (!parentReference.expired()) {
				auto parentShared = parentReference.lock();
				auto position = std::find(parentShared->childAnchors.begin(), parentShared->childAnchors.end(), this);
				if (position != parentShared->childAnchors.end()) {
					parentShared->childAnchors.erase(position);
				}
				parentReference.reset();
			}
			return *this;
		}

		void Anchors::registerWithParent() {
			if (!parentReference.expired()) {
				parentReference.lock()->childAnchors.push_back(this);
				apply();
			} else {
				ourOffset.clear();
			}
		}
	}
}
