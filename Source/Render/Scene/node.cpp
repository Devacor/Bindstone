#include "node.h"
#include <numeric>

CEREAL_REGISTER_TYPE(MV::Scene::Node);

namespace MV {
	namespace Scene {
		/*************************\
		| ---------Node---------- |
		\*************************/

		Color Node::color(const Color &a_newColor){
			int elements = (int)points.size();
			for(int i = 0; i < elements; i++){
				points[i] = a_newColor;
			}
			alertParent(VisualChange::make(shared_from_this()));
			return a_newColor;
		}

		Color Node::color() const{
			std::vector<Color> colorsToAverage;
			for(auto point : points){
				colorsToAverage.push_back(point);
			}
			return std::accumulate(colorsToAverage.begin(), colorsToAverage.end(), Color(0, 0, 0, 0)) / static_cast<double>(colorsToAverage.size());
		}

		std::shared_ptr<TextureHandle> Node::texture(std::shared_ptr<TextureHandle> a_texture){
			if(ourTexture && textureSizeSignal){
				ourTexture->sizeObserver.disconnect(textureSizeSignal);
			}
			textureSizeSignal.reset();
			ourTexture = a_texture;
			if(ourTexture){
				textureSizeSignal = TextureHandle::SignalType::make([&](std::shared_ptr<MV::TextureHandle> a_handle){
					updateTextureCoordinates();
				});
				ourTexture->sizeObserver.connect(textureSizeSignal);
			}
			updateTextureCoordinates();
			return a_texture;
		}

		std::shared_ptr<TextureHandle> Node::texture() const{
			return ourTexture;
		}

		void Node::clearTexture(){
			textureSizeSignal.reset();
			ourTexture.reset();
			updateTextureCoordinates();
		}

		std::shared_ptr<Node> Node::removeFromParent(){
			auto thisShared = shared_from_this();
			if(myParent){
				myParent->remove(thisShared);
			}
			return thisShared;
		}

		std::shared_ptr<Node> Node::remove(std::shared_ptr<Node> a_childItem){
			auto foundChild = std::find_if(drawList.begin(), drawList.end(), [&](const DrawListPairType &item){
				return item.second == a_childItem;
			});
			if(foundChild != drawList.end()){
				auto removed = foundChild->second;
				drawList.erase(foundChild);
				alertParent(ChildRemoved::make(shared_from_this(), removed));
				alertParent(VisualChange::make(shared_from_this()));
				return removed;
			}
			return nullptr;
		}

		std::shared_ptr<Node> Node::remove(const std::string &a_childId){
			auto foundChild = drawList.find(a_childId);
			if(foundChild != drawList.end()){
				auto removed = foundChild->second;
				drawList.erase(foundChild);
				alertParent(ChildRemoved::make(shared_from_this(), removed));
				alertParent(VisualChange::make(shared_from_this()));
				return removed;
			}
			return nullptr;
		}

		void Node::clear(){
			auto tmpDrawList = drawList;
			drawList.clear();

			for(auto drawItem : tmpDrawList){
				alertParent(ChildRemoved::make(shared_from_this(), drawItem.second));
			}
			alertParent(VisualChange::make(shared_from_this()));
		}

		std::shared_ptr<Node> Node::get(const std::string &a_childId, bool a_throwIfNotFound){
			auto cell = drawList.find(a_childId);
			if(cell != drawList.end()){
				return cell->second;
			}
			for(auto &cell : drawList){
				if(auto foundInChild = cell.second->get(a_childId, a_throwIfNotFound)){
					return foundInChild;
				}
			}
			return nullptr;
		}

		double Node::getDepth(){
			if(depthOverride){
				return overrideDepthValue;
			}
			size_t elements = points.size();
			double total = 0;
			for(size_t i = 0; i < elements; i++){
				total += points[i].z;
			}
			return total /= static_cast<double>(elements);
		}

		bool Node::operator<(Node &a_other){
			return getDepth() < a_other.getDepth();
		}

		bool Node::operator>(Node &a_other){
			return getDepth() > a_other.getDepth();
		}

		bool Node::operator==(Node &a_other){
			return equals(getDepth(), a_other.getDepth());
		}

		bool Node::operator!=(Node &a_other){
			return !equals(getDepth(), a_other.getDepth());
		}

		AxisAngles Node::rotation() const{
			return rotateTo;
		}

		double Node::rotation(double a_zRotation) {
			if(!equals(a_zRotation, 0.0)){
				rotateTo.z = a_zRotation;
				alertParent(VisualChange::make(shared_from_this()));
			}
			return a_zRotation;
		}

		MV::AxisAngles Node::rotation(const AxisAngles &a_rotation) {
			if(a_rotation != AxisAngles()){
				rotateTo = a_rotation;
				alertParent(VisualChange::make(shared_from_this()));
			}
			return a_rotation;
		}

		Point<> Node::position() const{
			return translateTo;
		}

		Point<> Node::position(const Point<> &a_rhs){
			if(translateTo != a_rhs){
				translateTo = a_rhs;
				alertParent(VisualChange::make(shared_from_this()));
			}
			return translateTo;
		}

		AxisMagnitude Node::scale() const{
			return scaleTo;
		}
		AxisMagnitude Node::scale(const AxisMagnitude &a_rhs){
			if(scaleTo != a_rhs){
				scaleTo = a_rhs;
				alertParent(VisualChange::make(shared_from_this()));
			}
			return scaleTo;
		}

		double Node::scale(double a_newScale){
			scale(Point<>(a_newScale, a_newScale, a_newScale));
			return a_newScale;
		}

		AxisMagnitude Node::incrementScale(double a_newScale){
			return incrementScale(Point<>(a_newScale, a_newScale, a_newScale));
		}

		AxisMagnitude Node::incrementScale(const AxisMagnitude &a_scaleValue){
			scaleTo += a_scaleValue;
			alertParent(VisualChange::make(shared_from_this()));
			return scaleTo;
		}

		Node* Node::parent(Node* a_parentItem){
			myParent = a_parentItem;
			if(myParent == nullptr){
				return nullptr;
			}else{
				return myParent;
			}
		}

		std::shared_ptr<Node> Node::parent() const{
			if(myParent == nullptr){
				return nullptr;
			}else{
				return myParent->shared_from_this();
			}
		}

		BoxAABB Node::worldAABB(bool a_includeChildren){
			return getWorldAABBImplementation(a_includeChildren, false);
		}

		BoxAABB Node::getWorldAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			auto parentPopMatrix = scopeGuard([&](){alertParent(PopMatrix::make(shared_from_this())); renderer->modelviewMatrix().pop(); });
			if(!a_nestedCall){
				renderer->modelviewMatrix().push();
				renderer->modelviewMatrix().top().makeIdentity();
				alertParent(PushMatrix::make(shared_from_this()));
			}else{
				parentPopMatrix.dismiss();
			}

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			BoxAABB tmpBox;

			if(!points.empty()){
				tmpBox.initialize(renderer->worldFromLocal(points[0]));
				std::for_each(points.begin()++, points.end(), [&](Point<> &point){
					tmpBox.expandWith(renderer->worldFromLocal(point));
				});
			}
			if(a_includeChildren && !drawList.empty()){
				if(points.empty()){
					tmpBox.initialize(drawList.begin()->second->getWorldAABBImplementation(a_includeChildren, true));
				} else{
					tmpBox.expandWith(drawList.begin()->second->getWorldAABBImplementation(a_includeChildren, true));
				}
				std::for_each(drawList.begin()++, drawList.end(), [&](const DrawListType::value_type &cell){
					tmpBox.expandWith(cell.second->getWorldAABBImplementation(a_includeChildren, true));
				});
			}
			return tmpBox;
		}

		BoxAABB Node::screenAABB(bool a_includeChildren){
			return getScreenAABBImplementation(a_includeChildren, false);
		}

		BoxAABB Node::getScreenAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			auto self = shared_from_this();
			auto parentPopMatrix = scopeGuard([&](){alertParent(PopMatrix::make(self)); renderer->modelviewMatrix().pop(); });
			if(!a_nestedCall){
				renderer->modelviewMatrix().push();
				renderer->modelviewMatrix().top().makeIdentity();
				alertParent(PushMatrix::make(self));
			} else{
				parentPopMatrix.dismiss();
			}

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			BoxAABB tmpBox;
			if(!points.empty()){
				tmpBox.initialize(castPoint<double>(renderer->screenFromLocal(points[0])));
				std::for_each(points.begin(), points.end(), [&](Point<> &point){
					tmpBox.expandWith(castPoint<double>(renderer->screenFromLocal(point)));
				});
			}
			if(a_includeChildren && !drawList.empty()){
				if(points.empty()){
					tmpBox.initialize(drawList.begin()->second->getScreenAABBImplementation(a_includeChildren, true));
				} else{
					tmpBox.expandWith(drawList.begin()->second->getScreenAABBImplementation(a_includeChildren, true));
				}
				std::for_each(drawList.begin()++, drawList.end(), [&](const DrawListType::value_type &cell){
					tmpBox.expandWith(cell.second->getScreenAABBImplementation(a_includeChildren, true));
				});
			}
			return tmpBox;
		}

		BoxAABB Node::localAABB(bool a_includeChildren){
			return getLocalAABBImplementation(a_includeChildren, false);
		}

		BoxAABB Node::getLocalAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			auto parentPopMatrix = scopeGuard([&](){alertParent(PopMatrix::make(shared_from_this())); renderer->modelviewMatrix().pop(); });
			if(!a_nestedCall){
				renderer->modelviewMatrix().push();
				renderer->modelviewMatrix().top().makeIdentity();
				alertParent(PushMatrix::make(shared_from_this()));
			} else{
				parentPopMatrix.dismiss();
			}

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			BoxAABB tmpBox;

			if(!points.empty()){
				tmpBox.initialize(renderer->worldFromLocal(points[0]));
				std::for_each(points.begin()++, points.end(), [&](Point<> &point){
					tmpBox.expandWith(renderer->worldFromLocal(point));
				});
			}
			if(a_includeChildren && !drawList.empty()){
				if(points.empty()){
					tmpBox.initialize(drawList.begin()->second->getLocalAABBImplementation(a_includeChildren, true));
				} else{
					tmpBox.expandWith(drawList.begin()->second->getLocalAABBImplementation(a_includeChildren, true));
				}
				std::for_each(drawList.begin()++, drawList.end(), [&](const DrawListType::value_type &cell){
					tmpBox.expandWith(cell.second->getLocalAABBImplementation(a_includeChildren, true));
				});
			}
			return tmpBox;
		}

		MV::BoxAABB Node::basicAABB() const  {
			return getBasicAABBImplementation();
		}

		MV::BoxAABB Node::getBasicAABBImplementation() const {
			BoxAABB tmpBox;

			if(!points.empty()){
				tmpBox.initialize(points[0]);
				std::for_each(points.begin()++, points.end(), [&](const DrawPoint &point){
					tmpBox.expandWith(point);
				});
			}

			return tmpBox;
		}

		Point<> Node::worldFromLocal(const Point<> &a_local){
			require(renderer != nullptr, PointerException("DrawShape::worldFromLocal requires a rendering context."));
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this()));};

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			Point<> ourPoint = renderer->worldFromLocal(a_local);
			return ourPoint;
		}
		Point<int> Node::screenFromLocal(const Point<> &a_local){
			require(renderer != nullptr, PointerException("DrawShape::screenFromLocal requires a rendering context."));
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop(); };

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			Point<int> ourPoint = renderer->screenFromLocal(a_local);
			return ourPoint;
		}
		Point<> Node::localFromScreen(const Point<int> &a_screen){
			require(renderer != nullptr, PointerException("DrawShape::localFromScreen requires a rendering context."));
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			Point<> ourPoint = renderer->localFromScreen(a_screen);
			return ourPoint;
		}
		Point<> Node::localFromWorld(const Point<> &a_world){
			require(renderer != nullptr, PointerException("DrawShape::localFromWorld requires a rendering context."));
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			Point<> ourPoint = renderer->localFromWorld(a_world);
			return ourPoint;
		}

		std::vector<Point<>> Node::worldFromLocal(std::vector<Point<>> a_local){
			require(renderer != nullptr, PointerException("DrawShape::worldFromLocal requires a rendering context."));
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			for(Point<>& point : a_local){
				point = renderer->worldFromLocal(point);
			}
			return a_local;
		}

		std::vector<Point<int>> Node::screenFromLocal(const std::vector<Point<>> &a_local){
			require(renderer != nullptr, PointerException("DrawShape::screenFromLocal requires a rendering context."));
			std::vector<Point<int>> processed;
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			for(const Point<>& point : a_local){
				processed.push_back(renderer->screenFromLocal(point));
			}
			return processed;
		}

		std::vector<Point<>> Node::localFromWorld(std::vector<Point<>> a_world){
			require(renderer != nullptr, PointerException("DrawShape::localFromWorld requires a rendering context."));
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			for(Point<>& point : a_world){
				point = renderer->localFromWorld(point);
			}
			return a_world;
		}

		std::vector<Point<>> Node::localFromScreen(const std::vector<Point<int>> &a_screen){
			require(renderer != nullptr, PointerException("DrawShape::localFromScreen requires a rendering context."));
			std::vector<Point<>> processed;
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			for(const Point<int>& point : a_screen){
				processed.push_back(renderer->localFromScreen(point));
			}
			return processed;
		}

		BoxAABB Node::worldFromLocal(BoxAABB a_local){
			require(renderer != nullptr, PointerException("DrawShape::worldFromLocal requires a rendering context."));
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			a_local.minPoint = renderer->worldFromLocal(a_local.minPoint);
			a_local.maxPoint = renderer->worldFromLocal(a_local.maxPoint);
			a_local.sanitize();
			return a_local;
		}
		BoxAABB Node::screenFromLocal(BoxAABB a_local){
			require(renderer != nullptr, PointerException("DrawShape::screenFromLocal requires a rendering context."));
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			a_local.minPoint = castPoint<double>(renderer->screenFromLocal(a_local.minPoint));
			a_local.maxPoint = castPoint<double>(renderer->screenFromLocal(a_local.maxPoint));
			a_local.sanitize();
			return a_local;
		}
		BoxAABB Node::localFromScreen(BoxAABB a_screen){
			require(renderer != nullptr, PointerException("DrawShape::localFromScreen requires a rendering context."));
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			a_screen.minPoint = renderer->localFromScreen(castPoint<int>(a_screen.minPoint));
			a_screen.maxPoint = renderer->localFromScreen(castPoint<int>(a_screen.maxPoint));
			a_screen.sanitize();
			return a_screen;
		}
		BoxAABB Node::localFromWorld(BoxAABB a_world){
			require(renderer != nullptr, PointerException("DrawShape::localFromWorld requires a rendering context."));
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			a_world.minPoint = renderer->localFromWorld(a_world.minPoint);
			a_world.maxPoint = renderer->localFromWorld(a_world.maxPoint);
			a_world.sanitize();
			return a_world;
		}

		void Node::pushMatrix(){
			renderer->modelviewMatrix().push();
			auto rollback = scopeGuard([&](){renderer->modelviewMatrix().pop();});
			if(!scaleTo.atOrigin()){
				renderer->modelviewMatrix().top().scale(scaleTo.x, scaleTo.y, scaleTo.z);
			}
			if(!translateTo.atOrigin()){
				renderer->modelviewMatrix().top().translate(translateTo.x, translateTo.y, translateTo.z);
			}
			if(!rotateTo.atOrigin()){
				renderer->modelviewMatrix().top().translate(rotateOrigin.x, rotateOrigin.y, rotateOrigin.z);
				renderer->modelviewMatrix().top().rotateX(rotateTo.x).rotateY(rotateTo.y).rotateZ(rotateTo.z);
				renderer->modelviewMatrix().top().translate(-rotateOrigin.x, -rotateOrigin.y, -rotateOrigin.z);
			}
			rollback.dismiss();
		}

		void Node::popMatrix(){
			renderer->modelviewMatrix().pop();
		}

		void Node::bindOrDisableTexture(const std::shared_ptr<std::vector<GLfloat>> &texturePoints){
			if(ourTexture != nullptr && ourTexture->texture() == nullptr){
				std::cerr << "Warning: TextureHandle with an unloaded texture: " << ourTexture->name << std::endl;
			}
			if(ourTexture != nullptr && ourTexture->texture() != nullptr){
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, ourTexture->texture()->textureId());
				glEnableClientState(GL_TEXTURE_COORD_ARRAY);

				glTexCoordPointer(2, GL_FLOAT, 0, &(*texturePoints)[0]);
			} else{
				glDisable(GL_TEXTURE_2D);
			}
		}

		void Node::defaultDrawRenderStep(GLenum drawType){
			auto textureVertexArray = getTextureVertexArray();
			bindOrDisableTexture(textureVertexArray);

			glEnableClientState(GL_COLOR_ARRAY);
			auto colorVertexArray = getColorVertexArray();
			glColorPointer(4, GL_FLOAT, 0, &(*colorVertexArray)[0]);

			glEnableClientState(GL_VERTEX_ARRAY);
			auto positionVertexArray = getPositionVertexArray();
			glVertexPointer(3, GL_FLOAT, 0, &(*(positionVertexArray))[0]);

			glDrawArrays(drawType, 0, static_cast<GLsizei>(points.size()));

			glDisableClientState(GL_COLOR_ARRAY);
			glDisableClientState(GL_VERTEX_ARRAY);
			if(ourTexture != nullptr){
				glDisableClientState(GL_TEXTURE_COORD_ARRAY);
				glDisable(GL_TEXTURE_2D);
			}
		}

		void Node::defaultDraw(GLenum drawType){
			defaultDrawRenderStep(drawType);
		}

		std::shared_ptr<std::vector<GLfloat>> Node::getPositionVertexArray(){
			auto returnArray = std::make_shared<std::vector<GLfloat>>(points.size() * 3);
			TransformMatrix transformationMatrix(renderer->projectionMatrix().top() * renderer->modelviewMatrix().top());
			for(size_t i = 0; i < points.size(); ++i){
				TransformMatrix transformedPoint(transformationMatrix * TransformMatrix(points[i]));
				(*returnArray)[i * 3 + 0] = static_cast<float>(transformedPoint.getX());
				(*returnArray)[i * 3 + 1] = static_cast<float>(transformedPoint.getY());
				(*returnArray)[i * 3 + 2] = static_cast<float>(transformedPoint.getZ());
			}
			return returnArray;
		}

		std::shared_ptr<std::vector<GLfloat>> Node::getTextureVertexArray(){
			auto returnArray = std::make_shared<std::vector<GLfloat>>(points.size() * 2);
			for(size_t i = 0; i < points.size(); ++i){
				(*returnArray)[i * 2 + 0] = static_cast<float>(points[i].textureX);
				(*returnArray)[i * 2 + 1] = static_cast<float>(points[i].textureY);
			}
			return returnArray;
		}

		std::shared_ptr<std::vector<GLfloat>> Node::getColorVertexArray(){
			auto returnArray = std::make_shared<std::vector<GLfloat>>(points.size() * 4);
			for(size_t i = 0; i < points.size(); ++i){
				(*returnArray)[i * 4 + 0] = static_cast<float>(points[i].R);
				(*returnArray)[i * 4 + 1] = static_cast<float>(points[i].G);
				(*returnArray)[i * 4 + 2] = static_cast<float>(points[i].B);
				(*returnArray)[i * 4 + 3] = static_cast<float>(points[i].A);
			}
			return returnArray;
		}

		void Node::draw(){
			if(isVisible && preDraw()){
				{
					pushMatrix();
					SCOPE_EXIT{popMatrix();};

					drawImplementation();
					if(drawSorted){
						sortedRender();
					} else{
						unsortedRender();
					}

				}
				postDraw();
			}
		}

		void Node::sortedRender(){
			sortDrawListVector();
			drawListVector.erase(remove_if(drawListVector.begin(), drawListVector.end(), [](DrawListVectorType::value_type &shape){
				if(!shape.expired()){
					shape.lock()->draw();
					return false;
				} else{
					return true;
				}
			}), drawListVector.end());
		}

		void Node::unsortedRender(){
			std::for_each(drawList.begin(), drawList.end(), [](DrawListType::value_type &shape){
				shape.second->draw();
			});
		}

		void Node::sortScene(bool a_depthMatters){
			drawSorted = a_depthMatters;
		}

		Node::Node(Draw2D* a_renderer):
			renderer(a_renderer),
			myParent(nullptr),
			depthOverride(false),
			drawSorted(true),
			isSorted(false),
			isVisible(true){
		}

		std::shared_ptr<Node> Node::make(Draw2D* a_renderer, const Point<> &a_placement /*= Point<>()*/) {
			auto node = std::shared_ptr<Node>(new Node(a_renderer));
			node->position(a_placement);
			return node;
		}

		bool Node::visible() const {
			return isVisible;
		}

		void Node::hide() {
			isVisible = false;
			alertParent(VisualChange::make(shared_from_this()));
		}

		void Node::show() {
			isVisible = true;
			alertParent(VisualChange::make(shared_from_this()));
		}

		void Node::sortDrawListVector() {
			if(!isSorted){
				drawListVector.clear();
				std::transform(drawList.begin(), drawList.end(), std::back_inserter(drawListVector), [](DrawListType::value_type shape){
					return shape.second;
				});
				std::sort(drawListVector.begin(), drawListVector.end(), [](DrawListVectorType::value_type one, DrawListVectorType::value_type two){
					return *one.lock() < *two.lock();
				});
				isSorted = true;
			}
		}

		std::vector<std::shared_ptr<Node>> Node::children() {
			std::vector<std::shared_ptr<Node>> childNodes;
			std::transform(drawList.begin(), drawList.end(), std::back_inserter(childNodes), [](DrawListType::value_type shape){
				return shape.second;
			});
			return childNodes;
		}

		double Node::incrementRotation(double a_zRotation) {
			if(!equals(a_zRotation, 0.0)){
				rotateTo.z += a_zRotation;
				alertParent(VisualChange::make(shared_from_this()));
			}
			return rotateTo.z;
		}

		MV::AxisAngles Node::incrementRotation(const AxisAngles &a_rotation) {
			if(a_rotation != AxisAngles()){
				rotateTo += a_rotation;
				alertParent(VisualChange::make(shared_from_this()));
			}
			return rotateTo;
		}

		Point<> Node::rotationOrigin(const Point<> &a_origin) {
			if(rotateOrigin != a_origin){
				rotateOrigin = a_origin;
				alertParent(VisualChange::make(shared_from_this()));
			}
			return rotateOrigin;
		}

		Point<> Node::centerRotationOrigin() {
			auto centerPoint = localAABB().centerPoint();
			if(rotateOrigin != centerPoint){
				alertParent(VisualChange::make(shared_from_this()));
			}
			return centerPoint;
		}

	}
}