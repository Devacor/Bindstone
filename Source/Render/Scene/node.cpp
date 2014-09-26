#include "node.h"
#include "stddef.h"
#include <numeric>

CEREAL_REGISTER_TYPE(MV::Scene::Node);

namespace MV {
	namespace Scene {
		void appendQuadVertexIndices(std::vector<GLuint> &a_indices, GLuint a_pointOffset) {
			std::vector<GLuint> quadIndices{
				a_pointOffset, a_pointOffset + 1,
				a_pointOffset + 2, a_pointOffset + 2,
				a_pointOffset + 3, a_pointOffset
			};
			a_indices.insert(a_indices.end(), quadIndices.begin(), quadIndices.end());
		}

		/*************************\
		| ---------Node---------- |
		\*************************/

		std::shared_ptr<Node> Node::colorImplementation(const Color &a_newColor){
			int elements = (int)points.size();
			for(int i = 0; i < elements; i++){
				points[i] = a_newColor;
			}
			alertParent(VisualChange::make(shared_from_this(), false));
			return shared_from_this();
		}

		Color Node::colorImplementation() const{
			std::vector<Color> colorsToAverage;
			for(auto point : points){
				colorsToAverage.push_back(point);
			}
			return std::accumulate(colorsToAverage.begin(), colorsToAverage.end(), Color(0, 0, 0, 0)) / static_cast<PointPrecision>(colorsToAverage.size());
		}

		std::shared_ptr<Node> Node::textureImplementation(std::shared_ptr<TextureHandle> a_texture){
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
			return shared_from_this();
		}

		std::shared_ptr<TextureHandle> Node::textureImplementation() const{
			return ourTexture;
		}

		std::shared_ptr<Node> Node::clearTextureImplementation(){
			textureSizeSignal.reset();
			ourTexture.reset();
			updateTextureCoordinates();
			return shared_from_this();
		}

		std::shared_ptr<Node> Node::removeFromParentImplementation(){
			auto thisShared = shared_from_this();
			if(myParent){
				myParent->remove(thisShared);
			}
			return thisShared;
		}

		std::shared_ptr<Node> Node::remove(std::shared_ptr<Node> a_childItem){
			if(a_childItem == nullptr){
				return nullptr;
			}
			auto foundChild = std::find(drawList.begin(), drawList.end(), a_childItem);
			if(foundChild != drawList.end()){
				auto removed = *foundChild;
				drawList.erase(foundChild);
				calculateMaxDepthChild();
				alertParent(ChildRemoved::make(shared_from_this(), removed));
				alertParent(VisualChange::make(shared_from_this()));
				removed->myParent = nullptr;
				removed->onRemoved(shared_from_this());
				onChildRemoved(removed);
				return removed;
			}
			return nullptr;
		}

		std::shared_ptr<Node> Node::remove(const std::string &a_childId){
			auto foundChild = std::find_if(drawList.begin(), drawList.end(), [&](const std::shared_ptr<Node> a_node){return a_node->name() == a_childId;});
			if(foundChild != drawList.end()){
				auto removed = *foundChild;
				drawList.erase(foundChild);
				calculateMaxDepthChild();
				alertParent(ChildRemoved::make(shared_from_this(), removed));
				alertParent(VisualChange::make(shared_from_this()));
				removed->myParent = nullptr;
				removed->onRemoved(shared_from_this());
				onChildRemoved(removed);
				return removed;
			}
			return nullptr;
		}

		void Node::clear(){
			auto tmpDrawList = drawList;
			drawList.clear();

			for(auto drawItem : tmpDrawList){
				alertParent(ChildRemoved::make(shared_from_this(), drawItem));
			}
			alertParent(VisualChange::make(shared_from_this()));
		}

		std::shared_ptr<Node> Node::get(const std::string &a_childId, bool a_throwOnNull){
			auto cell = std::find_if(drawList.begin(), drawList.end(), [&](const std::shared_ptr<Node> a_node){return a_node->name() == a_childId; });
			if(cell != drawList.end()){
				return *cell;
			}
			for(auto &cell : drawList){
				if(auto foundInChild = cell->get(a_childId, a_throwOnNull)){
					return foundInChild;
				}
			}
			require<ResourceException>(!a_throwOnNull, "Node::get was unable to find child: ", a_childId);
			return nullptr;
		}

		bool Node::operator<(Node &a_other){
			return (depth() == a_other.depth() && name() < a_other.name()) || depth() < a_other.depth();
		}

		bool Node::operator>(Node &a_other){
			return (depth() == a_other.depth() && name() > a_other.name()) || depth() > a_other.depth();
		}

		bool Node::operator==(Node &a_other){
			return equals(depth(), a_other.depth());
		}

		bool Node::operator!=(Node &a_other){
			return !equals(depth(), a_other.depth());
		}

		AxisAngles Node::rotationImplementation() const{
			return rotateTo;
		}

		std::shared_ptr<Node> Node::rotationImplementation(PointPrecision a_zRotation) {
			if(!equals(a_zRotation, 0.0f)){
				rotateTo.z = a_zRotation;
				alertParent(VisualChange::make(shared_from_this()));
			}
			return shared_from_this();
		}

		std::shared_ptr<Node> Node::rotationImplementation(const AxisAngles &a_rotation) {
			if(a_rotation != AxisAngles()){
				rotateTo = a_rotation;
				alertParent(VisualChange::make(shared_from_this()));
			}
			return shared_from_this();
		}

		Point<> Node::positionImplementation() const{
			return translateTo;
		}

		std::shared_ptr<Node> Node::positionImplementation(const Point<> &a_rhs){
			if(translateTo != a_rhs){
				translateTo = a_rhs;
				alertParent(VisualChange::make(shared_from_this()));
			}
			return shared_from_this();
		}

		Scale Node::scaleImplementation() const{
			return scaleTo;
		}
		std::shared_ptr<Node> Node::scaleImplementation(const Scale &a_rhs){
			if(scaleTo != a_rhs){
				scaleTo = a_rhs;
				alertParent(VisualChange::make(shared_from_this()));
			}
			return shared_from_this();
		}

		std::shared_ptr<Node> Node::scaleImplementation(PointPrecision a_newScale){
			scaleImplementation(Scale(a_newScale));
			return shared_from_this();
		}

		Scale Node::incrementScale(PointPrecision a_newScale){
			return incrementScale(Scale(a_newScale));
		}

		Scale Node::incrementScale(const Scale &a_scaleValue){
			scaleTo += a_scaleValue;
			alertParent(VisualChange::make(shared_from_this()));
			return scaleTo;
		}

		std::shared_ptr<Node> Node::parentImplementation(Node* a_parentItem){
			myParent = a_parentItem;
			return shared_from_this();
		}

		std::shared_ptr<Node> Node::parentImplementation() const{
			if(myParent == nullptr){
				return nullptr;
			}else{
				return myParent->shared_from_this();
			}
		}

		BoxAABB<> Node::worldAABB(bool a_includeChildren){
			return worldAABBImplementation(a_includeChildren, false);
		}

		BoxAABB<> Node::worldAABBImplementation(bool a_includeChildren, bool a_nestedCall){
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

			BoxAABB<> tmpBox;

			if(!points.empty()){
				tmpBox.initialize(renderer->worldFromLocal(points[0]));
				std::for_each(points.begin()++, points.end(), [&](Point<> &point){
					tmpBox.expandWith(renderer->worldFromLocal(point));
				});
			}
			if(a_includeChildren && !drawList.empty()){
				if(points.empty()){
					tmpBox.initialize(drawList.front()->worldAABBImplementation(a_includeChildren, true));
				} else{
					tmpBox.expandWith(drawList.front()->worldAABBImplementation(a_includeChildren, true));
				}
				std::for_each(drawList.begin()++, drawList.end(), [&](const DrawListType::value_type &cell){
					tmpBox.expandWith(cell->worldAABBImplementation(a_includeChildren, true));
				});
			}
			return tmpBox;
		}

		BoxAABB<int> Node::screenAABB(bool a_includeChildren){
			return screenAABBImplementation(a_includeChildren, false);
		}

		BoxAABB<int> Node::screenAABBImplementation(bool a_includeChildren, bool a_nestedCall){
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

			BoxAABB<int> tmpBox;
			if(!points.empty()){
				tmpBox.initialize(renderer->screenFromLocal(points[0]));
				std::for_each(points.begin(), points.end(), [&](Point<> &point){
					tmpBox.expandWith(renderer->screenFromLocal(point));
				});
			}
			if(a_includeChildren && !drawList.empty()){
				if(points.empty()){
					tmpBox.initialize(drawList.front()->screenAABBImplementation(a_includeChildren, true));
				} else{
					tmpBox.expandWith(drawList.front()->screenAABBImplementation(a_includeChildren, true));
				}
				std::for_each(drawList.begin()++, drawList.end(), [&](const DrawListType::value_type &cell){
					tmpBox.expandWith(cell->screenAABBImplementation(a_includeChildren, true));
				});
			}
			return tmpBox;
		}

		BoxAABB<> Node::localAABB(bool a_includeChildren){
			return localAABBImplementation(a_includeChildren, false);
		}

		BoxAABB<> Node::localAABBImplementation(bool a_includeChildren, bool a_nestedCall){
			auto parentPopMatrix = scopeGuard([&](){if(myParent){ myParent->popMatrix(); } renderer->modelviewMatrix().pop(); });
			if(!a_nestedCall){
				renderer->modelviewMatrix().push();
				renderer->modelviewMatrix().top().makeIdentity();
				if(myParent){
					myParent->pushMatrix();
				}
			} else{
				parentPopMatrix.dismiss();
			}

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			TransformMatrix transformationMatrix(renderer->projectionMatrix().top() * renderer->modelviewMatrix().top());

			BoxAABB<> tmpBox;

			if(!points.empty()){
				tmpBox.initialize(renderer->worldFromLocal(points[0]));
				std::for_each(points.begin()++, points.end(), [&](Point<> &point){
					tmpBox.expandWith(renderer->worldFromLocal(point));
				});
			}
			if(a_includeChildren && !drawList.empty()){
				if(points.empty()){
					tmpBox.initialize(drawList.front()->localAABBImplementation(a_includeChildren, true));
				} else{
					tmpBox.expandWith(drawList.front()->localAABBImplementation(a_includeChildren, true));
				}
				std::for_each(drawList.begin()++, drawList.end(), [&](const DrawListType::value_type &cell){
					tmpBox.expandWith(cell->localAABBImplementation(a_includeChildren, true));
				});
			}
			return tmpBox;
		}

		MV::BoxAABB<> Node::basicAABB() const  {
			return basicAABBImplementation();
		}

		MV::BoxAABB<> Node::basicAABBImplementation() const {
			BoxAABB<> tmpBox;

			if(!points.empty()){
				tmpBox.initialize(points[0]);
				std::for_each(points.begin()++, points.end(), [&](const DrawPoint &point){
					tmpBox.expandWith(point);
				});
			}

			return tmpBox;
		}

		Point<> Node::worldFromLocal(const Point<> &a_local){
			require<PointerException>(renderer != nullptr, "DrawShape::worldFromLocal requires a rendering context.");
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
			require<PointerException>(renderer != nullptr, "DrawShape::screenFromLocal requires a rendering context.");
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
			require<PointerException>(renderer != nullptr, "DrawShape::localFromScreen requires a rendering context.");
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
			require<PointerException>(renderer != nullptr, "DrawShape::localFromWorld requires a rendering context.");
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
			require<PointerException>(renderer != nullptr, "DrawShape::worldFromLocal requires a rendering context.");
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
			require<PointerException>(renderer != nullptr, "DrawShape::screenFromLocal requires a rendering context.");
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
			require<PointerException>(renderer != nullptr, "DrawShape::localFromWorld requires a rendering context.");
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
			require<PointerException>(renderer != nullptr, "DrawShape::localFromScreen requires a rendering context.");
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

		BoxAABB<> Node::worldFromLocal(const BoxAABB<>& a_local){
			require<PointerException>(renderer != nullptr, "DrawShape::worldFromLocal requires a rendering context.");
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			return{renderer->worldFromLocal(a_local.minPoint), renderer->worldFromLocal(a_local.maxPoint)};
		}
		BoxAABB<int> Node::screenFromLocal(const BoxAABB<>& a_local){
			require<PointerException>(renderer != nullptr, "DrawShape::screenFromLocal requires a rendering context.");
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			return {renderer->screenFromLocal(a_local.minPoint), renderer->screenFromLocal(a_local.maxPoint)};
		}
		BoxAABB<> Node::localFromScreen(const BoxAABB<int> &a_screen){
			require<PointerException>(renderer != nullptr, "DrawShape::localFromScreen requires a rendering context.");
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			return {renderer->localFromScreen(a_screen.minPoint), renderer->localFromScreen(a_screen.maxPoint)};
		}
		BoxAABB<> Node::localFromWorld(const BoxAABB<> &a_world){
			require<PointerException>(renderer != nullptr, "DrawShape::localFromWorld requires a rendering context.");
			renderer->modelviewMatrix().push();
			renderer->modelviewMatrix().top().makeIdentity();
			SCOPE_EXIT{renderer->modelviewMatrix().pop();};

			alertParent(PushMatrix::make(shared_from_this()));
			SCOPE_EXIT{alertParent(PopMatrix::make(shared_from_this())); };

			pushMatrix();
			SCOPE_EXIT{popMatrix();};

			return {renderer->localFromWorld(a_world.minPoint), renderer->localFromWorld(a_world.maxPoint)};
		}

		void Node::pushMatrix(){
			renderer->modelviewMatrix().push();
			if(!translateTo.atOrigin()){
				renderer->modelviewMatrix().top().translate(translateTo.x, translateTo.y, translateTo.z);
			}
			if(rotateTo != 0.0f){
				renderer->modelviewMatrix().top().translate(rotateOrigin.x, rotateOrigin.y, rotateOrigin.z);
				renderer->modelviewMatrix().top().rotateX(rotateTo.x).rotateY(rotateTo.y).rotateZ(rotateTo.z);
				renderer->modelviewMatrix().top().translate(-rotateOrigin.x, -rotateOrigin.y, -rotateOrigin.z);
			}
			if(scaleTo != 1.0f){
				renderer->modelviewMatrix().top().scale(scaleTo.x, scaleTo.y, scaleTo.z);
			}
		}

		void Node::popMatrix(){
			renderer->modelviewMatrix().pop();
		}

		void Node::defaultDrawRenderStep(GLenum drawType){
			shaderProgram->use();

			if(bufferId == 0){
				glGenBuffers(1, &bufferId);
			}

			glBindBuffer(GL_ARRAY_BUFFER, bufferId);
			auto structSize = static_cast<GLsizei>(sizeof(points[0]));
			glBufferData(GL_ARRAY_BUFFER, points.size() * structSize, &(points[0]), GL_STATIC_DRAW);

			glEnableVertexAttribArray(0);
			glEnableVertexAttribArray(1);
			glEnableVertexAttribArray(2);

			auto positionOffset = static_cast<GLsizei>(offsetof(DrawPoint, x));
			auto textureOffset = static_cast<GLsizei>(offsetof(DrawPoint, textureX));
			auto colorOffset = static_cast<GLsizei>(offsetof(DrawPoint, R));
			glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, structSize, (void*)positionOffset); //Point
			glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, structSize, (void*)textureOffset); //UV
			glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, structSize, (void*)colorOffset); //Color

			TransformMatrix transformationMatrix(renderer->projectionMatrix().top() * renderer->modelviewMatrix().top());

			shaderProgram->set("texture", ourTexture);
			shaderProgram->set("transformation", transformationMatrix);
			
			if(!vertexIndices.empty()){
				glDrawElements(drawType, static_cast<GLsizei>(vertexIndices.size()), GL_UNSIGNED_INT, &vertexIndices[0]);
			} else{
				glDrawArrays(drawType, 0, static_cast<GLsizei>(points.size()));
			}

			glDisableVertexAttribArray(0);
			glDisableVertexAttribArray(1);
			glDisableVertexAttribArray(2);
			glUseProgram(0);
		}

		void Node::defaultDraw(GLenum drawType){
			defaultDrawRenderStep(drawType);
		}
		
		void Node::draw(){
			if(preDraw() && isVisible){
				{
					pushMatrix();
					SCOPE_EXIT{popMatrix();};

					drawImplementation();
					sortedRender();

				}
				postDraw();
			}
		}

		void Node::sortedRender(){
			for(auto&& shape : drawList){
				shape->draw();
			}
		}

		Node::Node(Draw2D* a_renderer):
			renderer(a_renderer),
			myParent(nullptr),
			sortDepth(0.0f),
			isVisible(true),
			bufferId(0),
			shaderProgramId(DEFAULT_ID),
			shaderProgram(nullptr){
		}

		std::shared_ptr<Node> Node::make(Draw2D* a_renderer, const Point<> &a_placement /*= Point<>()*/) {
			auto node = std::shared_ptr<Node>(new Node(a_renderer));
			node->registerShader();
			return node->position(a_placement);
		}

		bool Node::visible() const {
			return isVisible;
		}

		std::shared_ptr<Node> Node::hideImplementation() {
			isVisible = false;
			alertParent(VisualChange::make(shared_from_this()));
			return shared_from_this();
		}

		std::shared_ptr<Node> Node::showImplementation() {
			isVisible = true;
			alertParent(VisualChange::make(shared_from_this()));
			return shared_from_this();
		}

		std::vector<std::shared_ptr<Node>> Node::children() {
			std::vector<std::shared_ptr<Node>> childNodes;
			std::transform(drawList.begin(), drawList.end(), std::back_inserter(childNodes), [](DrawListType::value_type shape){
				return shape;
			});
			return childNodes;
		}

		PointPrecision Node::incrementRotation(PointPrecision a_zRotation) {
			if(!equals(a_zRotation, 0.0f)){
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

		void Node::normalizeTest(Point<> a_offset){
			translate(a_offset);
			for(auto &point : points){
				point -= a_offset;
			}
		}

		void Node::normalizeToPoint(Point<> a_offset){
			translate(a_offset);
			for(auto &point : points){
				point -= a_offset;
			}
		}

		Point<> Node::normalizeToTopLeft() {
			auto offset = localAABB().topLeftPoint();
			normalizeToPoint(offset);
			return offset;
		}

		Point<> Node::normalizeToCenter() {
			auto aabb = localAABB();
			auto offset = (aabb.topLeftPoint() + aabb.bottomRightPoint()) / 2.0f;
			normalizeToPoint(offset);
			return offset;
		}

		std::string Node::shaderImplementation() const {
			return shaderProgramId;
		}

		void Node::shaderImplementation(const std::string &a_id) {
			shaderProgramId = a_id;
			if(renderer->hasShader(a_id)){
				shaderProgram = renderer->getShader(a_id);
			} else{
				renderer->registerShader(shared_from_this());
			}
		}

		Point<> Node::translate(const Point<> &a_translation) {
			position(translateTo + a_translation);
			return position();
		}

		std::shared_ptr<Node> Node::depthImplementation(PointPrecision a_newDepth) {
			auto notifyOnChanged = makeScopedDepthChangeNote(this, false);
			sortDepth = a_newDepth;
			return shared_from_this();
		}

		std::shared_ptr<Node> Node::blockSerializeImplementation() {
			markedTemporary = true;
			return shared_from_this();
		}

		std::shared_ptr<Node> Node::allowSerializeImplementation() {
			markedTemporary = false;
			return shared_from_this();
		}

		void Node::childDepthChanged(std::shared_ptr<Node> a_child) {
			auto foundChild = std::find(drawList.begin(), drawList.end(), a_child);
			if(foundChild != drawList.end()){
				auto removed = *foundChild;
				drawList.erase(foundChild);
				insertSorted(drawList, a_child);
			}
		}

		bool Node::toggleVisible() {
			isVisible = !isVisible;
			alertParent(VisualChange::make(shared_from_this()));
			return isVisible;
		}

		void Node::setRenderer(Draw2D* a_renderer, bool includeChildren /*= true*/, bool includeParents /*= true*/) {
			renderer = a_renderer;

			if(includeParents){
				Node* currentParent = this;
				while(currentParent = currentParent->myParent){
					setRenderer(a_renderer, false, true);
				}
			}
			if(includeChildren){
				for(auto &child : drawList){
					child->setRenderer(a_renderer, true, false);
				}
			}
		}

		size_t Node::indexOf(std::shared_ptr<Node> a_childItem) {
			size_t i = 0;
			for(auto&& cell : drawList){
				if(cell == a_childItem){
					return i;
				}
				++i;
			}
			return i;
		}

		std::vector<size_t> Node::parentIndexList() {
			std::vector<size_t> list;
			Node* current = this;
			Node* previous = myParent;
			while(current){
				list.push_back(current->myIndex());
				previous = current;
				current = current->myParent;
			}
			std::reverse(list.begin(), list.end());
			return list;
		}

		size_t Node::myIndex() {
			size_t i = 0;
			if(myParent){
				i = myParent->indexOf(shared_from_this());
			}
			return i;
		}

	}
}
