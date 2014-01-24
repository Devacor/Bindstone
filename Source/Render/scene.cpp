#include "scene.h"
#include <numeric>

CEREAL_REGISTER_TYPE(MV::Scene::Node);
CEREAL_REGISTER_TYPE(MV::Scene::Pixel);
CEREAL_REGISTER_TYPE(MV::Scene::Line);
CEREAL_REGISTER_TYPE(MV::Scene::Rectangle);

namespace MV {
	/*************************\
	| ------BoundingBox------ |
	\*************************/

	void BoxAABB::initialize( const Point<> &a_startPoint ){
		minPoint = a_startPoint; maxPoint = a_startPoint;
	}

	void BoxAABB::initialize( const Point<> &a_startPoint, const Point<> &a_endPoint){
		initialize(a_startPoint);
		expandWith(a_endPoint);
	}

	void BoxAABB::initialize( const BoxAABB &a_startBox ){
		minPoint = a_startBox.minPoint; maxPoint = a_startBox.maxPoint;
	}

	void BoxAABB::expandWith( const Point<> &a_comparePoint ){
		minPoint.x = std::min(a_comparePoint.x, minPoint.x);
		minPoint.y = std::min(a_comparePoint.y, minPoint.y);
		minPoint.z = std::min(a_comparePoint.z, minPoint.z);

		maxPoint.x = std::max(a_comparePoint.x, maxPoint.x);
		maxPoint.y = std::max(a_comparePoint.y, maxPoint.y);
		maxPoint.z = std::max(a_comparePoint.z, maxPoint.z);
	}

	void BoxAABB::expandWith( const BoxAABB &a_compareBox ){
		expandWith(a_compareBox.minPoint);
		expandWith(a_compareBox.maxPoint);
	}

	bool BoxAABB::pointContainedZ( const Point<> &a_comparePoint ) const{
		if(a_comparePoint.x >= minPoint.x && a_comparePoint.y >= minPoint.y && a_comparePoint.z >= minPoint.z){
			if(a_comparePoint.x <= maxPoint.x && a_comparePoint.y <= maxPoint.y && a_comparePoint.z <= maxPoint.z){
				return true;
			}
		}
		return false;
	}

	bool BoxAABB::pointContained( const Point<> &a_comparePoint ) const{
		if((a_comparePoint.x >= minPoint.x && a_comparePoint.y >= minPoint.y) && (a_comparePoint.x <= maxPoint.x && a_comparePoint.y <= maxPoint.y)){
			return true;
		}
		return false;
	}

	void BoxAABB::sanitize() {
		if(minPoint.x > maxPoint.x){std::swap(minPoint.x, maxPoint.x);}
		if(minPoint.y > maxPoint.y){std::swap(minPoint.y, maxPoint.y);}
		if(minPoint.z > maxPoint.z){std::swap(minPoint.z, maxPoint.z);}
	}

	std::ostream& operator<<(std::ostream& a_os, const BoxAABB& a_box){
		a_os << "[" << a_box.minPoint << " - " << a_box.maxPoint << "]";
		return a_os;
	}

	std::istream& operator>>(std::istream& a_is, BoxAABB& a_box){
		a_is >> a_box.minPoint >> a_box.maxPoint;
		return a_is;
	}

	/*************************\
	| ------PointVolume------ |
	\*************************/

	bool PointVolume::pointContained(const Point<> &a_comparePoint){
		int i;
		double angle = 0;
		Point<> p1, p2;
		int totalPoints = (int)points.size();
		for(i = 0; i<totalPoints; i++) {
			p1 = points[i] - a_comparePoint;
			p2 = points[(i + 1) % totalPoints] - a_comparePoint;
			angle += getAngle(p1, p2);
		}
		if(angle < 0){ angle *= -1; }
		if(angle < PIE){
			return false;
		}
		return true;
	}
	void PointVolume::addPoint(const Point<> &a_newPoint){
		points.push_back(a_newPoint);
	}

	double PointVolume::getAngle(const Point<> &a_p1, const Point<> &a_p2){
		double theta1 = atan2(a_p1.y, a_p1.x);
		double theta2 = atan2(a_p2.y, a_p2.x);
		double dtheta = theta2 - theta1;

		while(dtheta > PIE){
			dtheta -= PIE*2.0;
		}
		while(dtheta < -PIE){
			dtheta += PIE*2.0;
		}

		return(dtheta);
	}

	Point<> PointVolume::getCenter(){
		int totalPoints = (int)points.size();
		Point<> average = std::accumulate(points.begin(), points.end(), Point<>());
		average /= static_cast<double>(totalPoints);
		return average;
	}

	bool PointVolume::volumeCollision(PointVolume &a_compareVolume, Draw2D* a_renderer){
		require(a_renderer != nullptr, PointerException("PointVolume::volumeCollision was passed a null renderer."));
		Point<> point1 = getCenter();
		Point<> point2 = a_compareVolume.getCenter();

		double angle = getAngle(point1, point2);
		angle = angle * (180.0 / PIE);
		angle += 90.0;

		PointVolume tmpVolume1, tmpVolume2;
		int totalPoints;
		a_renderer->modelviewMatrix().push().makeIdentity().rotateZ(angle);

		totalPoints = (int)points.size();
		for(int i = 0; i < totalPoints; i++){
			tmpVolume1.addPoint(a_renderer->worldFromLocal(points[i]));
		}

		totalPoints = (int)a_compareVolume.points.size();
		for(int i = 0; i < totalPoints; i++){
			tmpVolume2.addPoint(a_renderer->worldFromLocal(a_compareVolume.points[i]));
		}

		a_renderer->modelviewMatrix().pop();

		BoxAABB box1 = tmpVolume1.getAABB();
		BoxAABB box2 = tmpVolume2.getAABB();

		if((box2.minPoint.x > box1.minPoint.x && box2.maxPoint.x > box1.maxPoint.x) ||
			(box2.minPoint.x < box1.minPoint.x && box2.maxPoint.x < box1.maxPoint.x)) {
			return false;
		}
		return true;
	}

	BoxAABB PointVolume::getAABB(){
		BoxAABB result;
		int totalPoints = (int)points.size();
		if(totalPoints > 0){
			result.initialize(points[0]);
			for(int i = 1; i < totalPoints; i++){
				result.expandWith(points[i]);
			}
		}
		return result;
	}

	namespace Scene {
		/*************************\
		| ---------Node---------- |
		\*************************/

		void Node::setColor(const Color &a_newColor){
			int elements = (int)points.size();
			for(int i = 0; i < elements; i++){
				points[i] = a_newColor;
			}
			alertParent(VisualChange::make(shared_from_this()));
		}

		void Node::setTexture(std::shared_ptr<TextureHandle> a_texture){
			if(texture && textureSizeSignal){
				texture->sizeObserver.disconnect(textureSizeSignal);
			}
			textureSizeSignal.reset();
			texture = a_texture;
			if(texture){
				textureSizeSignal = TextureHandle::SignalType::make([&](std::shared_ptr<MV::TextureHandle> a_handle){
					updateTextureCoordinates();
				});
				texture->sizeObserver.connect(textureSizeSignal);
			}
			updateTextureCoordinates();
		}

		std::shared_ptr<TextureHandle> Node::getTexture() const{
			return texture;
		}

		void Node::clearTexture(){
			textureSizeSignal.reset();
			texture.reset();
			updateTextureCoordinates();
		}

		bool Node::remove(std::shared_ptr<Node> a_childItem){
			auto foundChild = std::find_if(drawList.begin(), drawList.end(), [&](const DrawListPairType &item){
				return item.second == a_childItem;
			});
			if(foundChild != drawList.end()){
				auto removed = foundChild->second;
				drawList.erase(foundChild);
				alertParent(ChildRemoved::make(shared_from_this(), removed));
				alertParent(VisualChange::make(shared_from_this()));
				return true;
			}
			return false;
		}

		bool Node::remove(const std::string &a_childId){
			auto foundChild = drawList.find(a_childId);
			if(foundChild != drawList.end()){
				auto removed = foundChild->second;
				drawList.erase(foundChild);
				alertParent(ChildRemoved::make(shared_from_this(), removed));
				alertParent(VisualChange::make(shared_from_this()));
				return true;
			}
			return false;
		}

		void Node::clear(){
			auto tmpDrawList = drawList;
			drawList.clear();

			for(auto drawItem : tmpDrawList){
				alertParent(ChildRemoved::make(shared_from_this(), drawItem.second));
			}
			alertParent(VisualChange::make(shared_from_this()));
		}

		std::shared_ptr<Node> Node::get(const std::string &a_childId){
			auto cell = drawList.find(a_childId);
			if(cell != drawList.end()){
				return cell->second;
			}
			require(0, ResourceException("Scene::getChild was unable to find an element matching the ID: (" + a_childId + ")"));
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

		Point<> Node::getScale(){
			return scaleTo;
		}

		AxisAngles Node::getRotation(){
			return rotateTo;
		}

		Point<> Node::getPosition(){
			return translateTo;
		}

		void Node::scale(double a_newScale){
			scale(Point<>(a_newScale, a_newScale, a_newScale));
		}

		void Node::scale(const Point<> &a_scaleValue){
			scaleTo = a_scaleValue;
			alertParent(VisualChange::make(shared_from_this()));
		}

		void Node::incrementScale(double a_newScale){
			incrementScale(Point<>(a_newScale, a_newScale, a_newScale));
		}

		void Node::incrementScale(const AxisMagnitude &a_scaleValue){
			scaleTo += a_scaleValue;
			alertParent(VisualChange::make(shared_from_this()));
		}

		void Node::setParent(Node* a_parentItem){
			myParent = a_parentItem;
		}

		std::shared_ptr<Node> Node::parent() const{
			return myParent->shared_from_this();
		}

		BoxAABB Node::getWorldAABB(bool a_includeChildren){
			return getWorldAABBImplementation(a_includeChildren, false);
		}

		BoxAABB Node::getWorldAABBImplementation(bool a_includeChildren, bool a_nestedCall){
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

		BoxAABB Node::getScreenAABB(bool a_includeChildren){
			return getScreenAABBImplementation(a_includeChildren, false);
		}

		BoxAABB Node::getScreenAABBImplementation(bool a_includeChildren, bool a_nestedCall){
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

		BoxAABB Node::getLocalAABB(bool a_includeChildren){
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

		MV::BoxAABB Node::getPointAABB() {
			BoxAABB tmpBox;

			if(!points.empty()){
				tmpBox.initialize(points[0]);
				std::for_each(points.begin()++, points.end(), [&](Point<> &point){
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
			if(texture != nullptr && texture->texture() == nullptr){
				std::cerr << "Warning: TextureHandle with an unloaded texture: " << texture->name << std::endl;
			}
			if(texture != nullptr && texture->texture() != nullptr){
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, texture->texture()->textureId());
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
			if(texture != nullptr){
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

					if(drawSorted){
						sortedRender();
					} else{
						unsortedRender();
					}
					drawImplementation();
				}
				postDraw();
			}
		}

		void Node::sortedRender(){
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
			std::for_each(drawListVector.begin(), drawListVector.end(), [](DrawListVectorType::value_type &shape){
				if(!shape.expired()){
					shape.lock()->draw();
				}else{
					std::cout << "Error: expired shape in drawListVector!" << std::endl;
				}
			});
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
			hasTexture(false),
			depthOverride(false),
			drawSorted(true),
			isSorted(false),
			isVisible(true){
		}

		std::shared_ptr<Node> Node::make(Draw2D* a_renderer, const Point<> &a_placement /*= Point<>()*/) {
			auto node = std::shared_ptr<Node>(new Node(a_renderer));
			node->locate(a_placement);
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

		/*************************\
		| ---------Pixel--------- |
		\*************************/

		void Pixel::setPoint(const DrawPoint &a_point){
			points[0] = a_point;
			if(!depthOverride){
				depthChanged();
			}

			alertParent(VisualChange::make(shared_from_this()));
		}

		void Pixel::drawImplementation(){
			defaultDraw(GL_POINTS);
		}

		std::shared_ptr<Pixel> Pixel::make(Draw2D* a_renderer, const DrawPoint &a_point /*= DrawPoint()*/) {
			auto point = std::shared_ptr<Pixel>(new Pixel(a_renderer));
			point->setPoint(a_point);
			return point;
		}


		/*************************\
		| ----------Line--------- |
		\*************************/

		void Line::setEnds(const DrawPoint &a_startPoint, const DrawPoint &a_endPoint){
			points[0] = a_startPoint;
			points[1] = a_endPoint;
			depthChanged();
		}

		void Line::drawImplementation(){
			defaultDraw(GL_LINES);
		}

		std::shared_ptr<Line> Line::make(Draw2D* a_renderer) {
			return std::shared_ptr<Line>(new Line(a_renderer));
		}

		std::shared_ptr<Line> Line::make(Draw2D* a_renderer, const DrawPoint &a_startPoint, const DrawPoint &a_endPoint) {
			auto line = std::shared_ptr<Line>(new Line(a_renderer));
			line->setEnds(a_startPoint, a_endPoint);
			return line;
		}



		/*************************\
		| -------Rectangle------- |
		\*************************/

		void Rectangle::setTwoCorners(const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight){
			bool callDepthChanged = (!depthOverride && (a_topLeft.z + a_bottomRight.z) / 2.0 != getDepth());
			DrawPoint topLeft = a_topLeft, bottomRight = a_bottomRight;
			topLeft.x = std::min(a_topLeft.x, a_bottomRight.x);
			bottomRight.x = std::max(a_topLeft.x, a_bottomRight.x);
			topLeft.y = std::min(a_topLeft.y, a_bottomRight.y);
			bottomRight.y = std::max(a_topLeft.y, a_bottomRight.y);
			topLeft.z = a_topLeft.z; bottomRight.z = a_bottomRight.z;

			points[0] = topLeft;
			points[1].x = topLeft.x;	points[1].y = bottomRight.y;	points[1].z = (bottomRight.z + topLeft.z) / 2;
			points[2] = bottomRight;
			points[3].x = bottomRight.x;	points[3].y = topLeft.y;	points[3].z = (bottomRight.z + topLeft.z) / 2;
			if(callDepthChanged){
				depthChanged(); //also alerts
			} else{
				alertParent(VisualChange::make(shared_from_this()));
			}
		}

		void Rectangle::setTwoCorners(const Point<> &a_topLeft, const Point<> &a_bottomRight){
			bool callDepthChanged = !depthOverride && (a_topLeft.z + a_bottomRight.z) / 2.0 != getDepth();
			Point<> topLeft = a_topLeft, bottomRight = a_bottomRight;
			topLeft.x = std::min(a_topLeft.x, a_bottomRight.x);
			bottomRight.x = std::max(a_topLeft.x, a_bottomRight.x);
			topLeft.y = std::min(a_topLeft.y, a_bottomRight.y);
			bottomRight.y = std::max(a_topLeft.y, a_bottomRight.y);
			topLeft.z = a_topLeft.z; bottomRight.z = a_bottomRight.z;

			points[0] = topLeft;
			points[1].x = topLeft.x;	points[1].y = bottomRight.y;	points[1].z = (bottomRight.z + topLeft.z) / 2;
			points[2] = bottomRight;
			points[3].x = bottomRight.x;	points[3].y = topLeft.y;	points[3].z = (bottomRight.z + topLeft.z) / 2;
			if(callDepthChanged){
				depthChanged(); //also alerts
			} else{
				alertParent(VisualChange::make(shared_from_this()));
			}
		}

		void Rectangle::setTwoCorners(const BoxAABB &a_bounds){
			setTwoCorners(a_bounds.minPoint, a_bounds.maxPoint);
		}

		void Rectangle::setSizeAndCenterPoint(const Point<> &a_centerPoint, const Size<> &a_size){
			double halfWidth = a_size.width / 2, halfHeight = a_size.height / 2;

			Point<> topLeft(-halfWidth, -halfHeight);
			Point<> bottomRight(halfWidth, halfHeight);

			setTwoCorners(topLeft, bottomRight);
			locate(a_centerPoint);
		}

		void Rectangle::setSizeAndCornerPoint(const Point<> &a_topLeft, const Size<> &a_size){
			Point<> bottomRight(pointFromSize(a_size));

			setTwoCorners(Point<>(), bottomRight);

			locate(a_topLeft);
		}

		void Rectangle::clearTextureCoordinates(){
			points[0].textureX = 0.0; points[0].textureY = 0.0;
			points[1].textureX = 0.0; points[1].textureY = 1.0;
			points[2].textureX = 1.0; points[2].textureY = 1.0;
			points[3].textureX = 1.0; points[3].textureY = 0.0;
			alertParent(VisualChange::make(shared_from_this()));
		}

		void Rectangle::updateTextureCoordinates(){
			if(texture != nullptr){
				points[0].textureX = texture->percentLeft(); points[0].textureY = texture->percentTop();
				points[1].textureX = texture->percentLeft(); points[1].textureY = texture->percentBottom();
				points[2].textureX = texture->percentRight(); points[2].textureY = texture->percentBottom();
				points[3].textureX = texture->percentRight(); points[3].textureY = texture->percentTop();
				alertParent(VisualChange::make(shared_from_this()));
			} else{
				clearTextureCoordinates();
			}
		}

		void Rectangle::drawImplementation(){
			defaultDraw(GL_TRIANGLE_FAN);
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer) {
			return std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			rectangle->setTwoCorners(a_topLeft, a_bottomRight);
			return rectangle;
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const Point<> &a_topLeft, const Point<> &a_bottomRight) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			rectangle->setTwoCorners(a_topLeft, a_bottomRight);
			return rectangle;
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const Point<> &a_point, Size<> &a_size, bool a_center) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			if(a_center){
				rectangle->setSizeAndCenterPoint(a_point, a_size);
			}else{
				rectangle->setSizeAndCornerPoint(a_point, a_size);
			}
			return rectangle;
		}

		std::shared_ptr<Rectangle> Rectangle::make(Draw2D* a_renderer, const Size<> &a_size) {
			auto rectangle = std::shared_ptr<Rectangle>(new Rectangle(a_renderer));
			rectangle->setSize(a_size);
			return rectangle;
		}

		void Rectangle::setSize(const Size<> &a_size) {
			points[2] = points[0] + static_cast<DrawPoint>(pointFromSize(a_size));
			points[1].y = points[2].y;
			points[3].x = points[2].x;
			alertParent(VisualChange::make(shared_from_this()));
		}

	}
}
