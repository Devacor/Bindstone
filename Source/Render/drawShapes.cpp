#include "drawShapes.h"
#include <numeric>
namespace MV {
	/*************************\
	| ------BoundingBox------ |
	\*************************/

	void BoxAABB::initialize( const Point &a_startPoint ){
		minPoint = a_startPoint; maxPoint = a_startPoint;
	}

	void BoxAABB::initialize( const Point &a_startPoint, const Point &a_endPoint){
		initialize(a_startPoint);
		expandWith(a_endPoint);
	}

	void BoxAABB::initialize( const BoxAABB &a_startBox ){
		minPoint = a_startBox.minPoint; maxPoint = a_startBox.maxPoint;
	}

	void BoxAABB::expandWith( const Point &a_comparePoint ){
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

	bool BoxAABB::pointContainedZ( const Point &a_comparePoint ) const{
		if(a_comparePoint.x >= minPoint.x && a_comparePoint.y >= minPoint.y && a_comparePoint.z >= minPoint.z){
			if(a_comparePoint.x <= maxPoint.x && a_comparePoint.y <= maxPoint.y && a_comparePoint.z <= maxPoint.z){
				return true;
			}
		}
		return false;
	}

	bool BoxAABB::pointContained( const Point &a_comparePoint ) const{
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
	| -------DrawShape------- |
	\*************************/

	void DrawNode::setColor( const Color &a_newColor ){
		int elements = (int)points.size();
		for(int i = 0;i < elements;i++){
			points[i] = a_newColor;
		}
	}

	void DrawNode::setTexture( const GLuint *a_textureId ){
		texture = a_textureId;
		hasTexture = true;
	}

	bool DrawNode::remove(std::shared_ptr<DrawNode> a_childItem){
		size_t originalSize = drawList.size();
		for(auto cell = drawList.begin();cell != drawList.end();){
			if(*(cell->second) == *a_childItem){
				drawList.erase(cell++);
			}else{
				++cell;
			}
		}
		return originalSize != drawList.size();
	}

	bool DrawNode::remove(const std::string &a_childId){
		size_t originalSize = drawList.size();
		drawList.erase(a_childId);
		return originalSize != drawList.size();
	}

	void DrawNode::clear(){
		drawList.clear();
	}

	std::shared_ptr<DrawNode> DrawNode::get(const std::string &a_childId){
		auto cell = drawList.find(a_childId);
		if(cell != drawList.end()){
			return cell->second;
		}
		require(0, ResourceException("Scene::getChild was unable to find an element matching the ID: (" + a_childId + ")"));
		return nullptr;
	}

	void DrawNode::removeTexture(){
		hasTexture = false;
	}

	double DrawNode::getDepth(){
		if(depthOverride){
			return overrideDepthValue;
		}
		int elements = (int)points.size();
		double total = 0;
		for(int i = 0;i < elements;i++){
			total+=points[i].z;
		}
		total/=(double)elements;
		return total;
	}

	bool DrawNode::operator<( DrawNode &a_other ){
		return getDepth() < a_other.getDepth();
	}

	bool DrawNode::operator>( DrawNode &a_other ){
		return getDepth() > a_other.getDepth();
	}

	bool DrawNode::operator==( DrawNode &a_other ){
		return getDepth() == a_other.getDepth();
	}

	Point DrawNode::getScale(){
		return scaleTo;
	}

	AxisAngles DrawNode::getRotation(){
		return rotateTo;
	}

	Point DrawNode::getLocation(){
		return translateTo;
	}

	Point DrawNode::getRelativeLocation(){
		Point resultPoint;
		resultPoint = translateTo;
		if(myParent){
			resultPoint += myParent->getRelativeLocation();
		}
		return resultPoint;
	}

	Point DrawNode::getRelativeScale(){
		Point resultPoint;
		resultPoint = scaleTo;
		if(myParent){
			resultPoint += myParent->getRelativeScale();
		}
		return resultPoint;
	}

	Point DrawNode::getRelativeRotation(){
		Point resultPoint;
		resultPoint = rotateTo;
		if(myParent){
			resultPoint += myParent->getRelativeRotation();
		}
		return resultPoint;
	}

	void DrawNode::scale( double a_newScale ){
		scale( Point(a_newScale, a_newScale, a_newScale) );
	}

	void DrawNode::scale( const Point &a_scaleValue ){
		scaleTo = a_scaleValue;
	}

	void DrawNode::incrementScale( double a_newScale ){
		incrementScale( Point(a_newScale, a_newScale, a_newScale) );
	}

	void DrawNode::incrementScale( const AxisMagnitude &a_scaleValue ){
		scaleTo += a_scaleValue;
	}

	void DrawNode::setParent( DrawNode* a_parentItem ){
		myParent = a_parentItem;
	}

	BoxAABB DrawNode::getWorldAABB(bool includeChildren){
		require(renderer != nullptr, PointerException("DrawShape::getWorldAABB requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		BoxAABB tmpBox;
		if(!points.empty()){
			tmpBox.initialize(renderer->worldFromLocal(points[0]));
			std::for_each(points.begin()++, points.end(), [&](Point &point){
				tmpBox.expandWith(renderer->worldFromLocal(point));
			});
		}
		if(includeChildren && !drawList.empty()){
			if(!points.empty()){
				tmpBox.initialize(drawList[0]->getWorldAABB());
			}else{
				tmpBox.expandWith(drawList[0]->getWorldAABB());
			}
			std::for_each(drawList.begin()++, drawList.end(), [&](const DrawListType::value_type &cell){
				tmpBox.expandWith(cell.second->getWorldAABB());
			});
		}
		popMatrix();
		alertParent("popMatrix");
		return tmpBox;
	}

	BoxAABB DrawNode::getScreenAABB(bool includeChildren){
		require(renderer != nullptr, PointerException("DrawShape::getScreenAABB requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		BoxAABB tmpBox;
		if(!points.empty()){
			tmpBox.initialize(renderer->screenFromLocal(points[0]));
			std::for_each(points.begin(), points.end(), [&](Point &point){
				tmpBox.expandWith(renderer->screenFromLocal(point));
			});
		}
		if(includeChildren && !drawList.empty()){
			if(!points.empty()){
				tmpBox.initialize(drawList[0]->getScreenAABB());
			}else{
				tmpBox.expandWith(drawList[0]->getScreenAABB());
			}
			std::for_each(drawList.begin()++, drawList.end(), [&](const DrawListType::value_type &cell){
				tmpBox.expandWith(cell.second->getScreenAABB());
			});
		}
		popMatrix();
		alertParent("popMatrix");
		return tmpBox;
	}

	BoxAABB DrawNode::getLocalAABB(){
		BoxAABB tmpBox;
		if(!points.empty()){
			tmpBox.initialize(points[0]);
			std::for_each(points.begin(), points.end(), [&](Point &point){
				tmpBox.expandWith(point);
			});
		}
		return tmpBox;
	}

	Point DrawNode::worldFromLocal(const Point &a_local){
		require(renderer != nullptr, PointerException("DrawShape::worldFromLocal requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		Point ourPoint = renderer->worldFromLocal(a_local);
		popMatrix();
		alertParent("popMatrix");
		return ourPoint;
	}
	Point DrawNode::screenFromLocal(const Point &a_local){
		require(renderer != nullptr, PointerException("DrawShape::screenFromLocal requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		Point ourPoint = renderer->screenFromLocal(a_local);
		popMatrix();
		alertParent("popMatrix");
		return ourPoint;
	}
	Point DrawNode::localFromScreen(const Point &a_screen){
		require(renderer != nullptr, PointerException("DrawShape::localFromScreen requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		Point ourPoint = renderer->localFromScreen(a_screen);
		popMatrix();
		alertParent("popMatrix");
		return ourPoint;
	}
	Point DrawNode::localFromWorld(const Point &a_world){
		require(renderer != nullptr, PointerException("DrawShape::localFromWorld requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		Point ourPoint = renderer->localFromWorld(a_world);
		popMatrix();
		alertParent("popMatrix");
		return ourPoint;
	}

	std::vector<Point> DrawNode::worldFromLocal(std::vector<Point> a_local){
		require(renderer != nullptr, PointerException("DrawShape::worldFromLocal requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		for(Point& point : a_local){
			point = renderer->worldFromLocal(point);
		}
		popMatrix();
		alertParent("popMatrix");
		return a_local;
	}

	std::vector<Point> DrawNode::screenFromLocal(std::vector<Point> a_local){
		require(renderer != nullptr, PointerException("DrawShape::screenFromLocal requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		for(Point& point : a_local){
			point = renderer->screenFromLocal(point);
		}
		popMatrix();
		alertParent("popMatrix");
		return a_local;
	}

	std::vector<Point> DrawNode::localFromWorld(std::vector<Point> a_world){
		require(renderer != nullptr, PointerException("DrawShape::localFromWorld requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		for(Point& point : a_world){
			point = renderer->localFromWorld(point);
		}
		popMatrix();
		alertParent("popMatrix");
		return a_world;
	}

	std::vector<Point> DrawNode::localFromScreen(std::vector<Point> a_screen){
		require(renderer != nullptr, PointerException("DrawShape::localFromScreen requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		for(Point& point : a_screen){
			point = renderer->localFromScreen(point);
		}
		popMatrix();
		alertParent("popMatrix");
		return a_screen;
	}

	BoxAABB DrawNode::worldFromLocal(BoxAABB a_local){
		require(renderer != nullptr, PointerException("DrawShape::worldFromLocal requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		a_local.minPoint = renderer->worldFromLocal(a_local.minPoint);
		a_local.maxPoint = renderer->worldFromLocal(a_local.maxPoint);
		a_local.sanitize();
		popMatrix();
		alertParent("popMatrix");
		return a_local;
	}
	BoxAABB DrawNode::screenFromLocal(BoxAABB a_local){
		require(renderer != nullptr, PointerException("DrawShape::screenFromLocal requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		a_local.minPoint = renderer->screenFromLocal(a_local.minPoint);
		a_local.maxPoint = renderer->screenFromLocal(a_local.maxPoint);
		a_local.sanitize();
		popMatrix();
		alertParent("popMatrix");
		return a_local;
	}
	BoxAABB DrawNode::localFromScreen(BoxAABB a_screen){
		require(renderer != nullptr, PointerException("DrawShape::localFromScreen requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		a_screen.minPoint = renderer->localFromScreen(a_screen.minPoint);
		a_screen.maxPoint = renderer->localFromScreen(a_screen.maxPoint);
		a_screen.sanitize();
		popMatrix();
		alertParent("popMatrix");
		return a_screen;
	}
	BoxAABB DrawNode::localFromWorld(BoxAABB a_world){
		require(renderer != nullptr, PointerException("DrawShape::localFromWorld requires a rendering context."));
		alertParent("pushMatrix");
		pushMatrix();
		a_world.minPoint = renderer->localFromWorld(a_world.minPoint);
		a_world.maxPoint = renderer->localFromWorld(a_world.maxPoint);
		a_world.sanitize();
		popMatrix();
		alertParent("popMatrix");
		return a_world;
	}

	void DrawNode::pushMatrix(){
		modelviewMatrix().push();
		if(!scaleTo.atOrigin()){
			modelviewMatrix().top().scale(scaleTo.x, scaleTo.y, scaleTo.z);
		}
		if(!translateTo.atOrigin()){
			modelviewMatrix().top().translate(translateTo.x, translateTo.y, translateTo.z);
		}
		if(!rotateTo.atOrigin()){
			modelviewMatrix().top().translate(rotateOrigin.x, rotateOrigin.y, rotateOrigin.z);
			modelviewMatrix().top().rotateX(rotateTo.x).rotateY(rotateTo.y).rotateZ(rotateTo.z);
			modelviewMatrix().top().translate(-rotateOrigin.x, -rotateOrigin.y, -rotateOrigin.z);
		}
	}

	void DrawNode::popMatrix(){
		modelviewMatrix().pop();
	}

	void DrawNode::bindOrDisableTexture(const std::shared_ptr<std::vector<GLfloat>> &texturePoints){
		if(hasTexture && texture != NULL){
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, *texture);
			glEnableClientState(GL_TEXTURE_COORD_ARRAY);
			
			glTexCoordPointer(2, GL_FLOAT, 0, &(*texturePoints)[0]);
		}else{
			glDisable(GL_TEXTURE_2D);
		}
	}

	void DrawNode::defaultDrawRenderStep( GLenum drawType ){
		auto textureVertexArray = getTextureVertexArray();
		bindOrDisableTexture(textureVertexArray);

		glEnableClientState(GL_COLOR_ARRAY);
		auto colorVertexArray = getColorVertexArray();
		glColorPointer(4, GL_FLOAT, 0, &(*colorVertexArray)[0]);

		glEnableClientState(GL_VERTEX_ARRAY);
		auto positionVertexArray = getPositionVertexArray();
		glVertexPointer(3, GL_FLOAT, 0, &(*(positionVertexArray))[0]);
		
		glDrawArrays(drawType,0,static_cast<GLsizei>(points.size()));

		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);
		if(hasTexture && texture != NULL){
			glDisableClientState(GL_TEXTURE_COORD_ARRAY);
			glDisable(GL_TEXTURE_2D);
		}
	}

	void DrawNode::defaultDraw( GLenum drawType ){
		pushMatrix();
		defaultDrawRenderStep(drawType);
		popMatrix();
	}

	std::shared_ptr<std::vector<GLfloat>> DrawNode::getPositionVertexArray(){
		auto returnArray = std::make_shared<std::vector<GLfloat>>(points.size()*3);
		TransformMatrix transformationMatrix(projectionMatrix().top() * modelviewMatrix().top());
		for(size_t i = 0;i < points.size();++i){
			TransformMatrix transformedPoint(transformationMatrix * TransformMatrix(points[i]));
			(*returnArray)[i*3+0] = static_cast<float>(transformedPoint.getX());
			(*returnArray)[i*3+1] = static_cast<float>(transformedPoint.getY());
			(*returnArray)[i*3+2] = static_cast<float>(transformedPoint.getZ());
		}
		return returnArray;
	}

	std::shared_ptr<std::vector<GLfloat>> DrawNode::getTextureVertexArray(){
		auto returnArray = std::make_shared<std::vector<GLfloat>>(points.size()*2);
		for(size_t i = 0;i < points.size();++i){
			(*returnArray)[i*2+0] = static_cast<float>(points[i].textureX);
			(*returnArray)[i*2+1] = static_cast<float>(points[i].textureY);
		}
		return returnArray;
	}

	std::shared_ptr<std::vector<GLfloat>> DrawNode::getColorVertexArray(){
		auto returnArray = std::make_shared<std::vector<GLfloat>>(points.size()*4);
		for(size_t i = 0;i < points.size();++i){
			(*returnArray)[i*4+0] = static_cast<float>(points[i].R);
			(*returnArray)[i*4+1] = static_cast<float>(points[i].G);
			(*returnArray)[i*4+2] = static_cast<float>(points[i].B);
			(*returnArray)[i*4+3] = static_cast<float>(points[i].A);
		}
		return returnArray;
	}

	void DrawNode::draw(){
		pushMatrix();
		if(drawSorted){
			sortedRender();
		}else{
			unsortedRender();
		}
		drawImplementation();
		popMatrix();
	}

	void DrawNode::sortedRender(){
		if(!isSorted){
			drawListVector.clear();
			std::for_each(drawList.begin(), drawList.end(), [&](DrawListType::value_type &cell){
				drawListVector.push_back(std::shared_ptr<DrawNode>(cell.second));
			});
			std::sort(drawListVector.begin(), drawListVector.end(), [](DrawListVectorType::value_type one, DrawListVectorType::value_type two){
				return *one < *two;
			});
			isSorted = true;
		}
		std::for_each(drawListVector.begin(), drawListVector.end(), [](DrawListVectorType::value_type &shape){
			shape->draw();
		});
	}

	void DrawNode::unsortedRender(){
		std::for_each(drawList.begin(), drawList.end(), [](DrawListType::value_type &shape){
			shape.second->draw();
		});
	}

	void DrawNode::sortScene( bool a_depthMatters ){
		drawSorted = a_depthMatters;
	}

	std::string DrawNode::getMessage(const std::string &a_message ){
		if(a_message == "NeedsSort"){
			alertParent(a_message);
			isSorted = false;
		}else if(a_message == "pushMatrix"){
			alertParent(a_message);
			pushMatrix();
		}else if(a_message == "popMatrix"){
			popMatrix();
			alertParent(a_message);
		}
		return "";
	}

	/*************************\
	| -------DrawPixel------- |
	\*************************/

	void DrawPixel::setPoint( const DrawPoint &a_point ){
		alertParent("NeedsSort");
		points[0] = a_point;
	}

	void DrawPixel::drawImplementation(){
		defaultDraw(GL_POINTS);
	}

	/*************************\
	| --------DrawLine------- |
	\*************************/

	void DrawLine::setEnds( const DrawPoint &a_startPoint, const DrawPoint &a_endPoint ){
		alertParent("NeedsSort");
		points[0] = a_startPoint;
		points[1] = a_endPoint;
	}

	void DrawLine::drawImplementation(){
		defaultDraw(GL_LINES);
	}

	/*************************\
	| -----DrawRectangle----- |
	\*************************/

	void DrawRectangle::setTwoCorners( const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight ){
		if((a_topLeft.z + a_bottomRight.z) / 2.0 != getDepth()){
			alertParent("NeedsSort");
		}
		DrawPoint topLeft = a_topLeft, bottomRight = a_bottomRight;
		topLeft.x = std::min(a_topLeft.x,a_bottomRight.x);
		bottomRight.x = std::max(a_topLeft.x,a_bottomRight.x);
		topLeft.y = std::min(a_topLeft.y,a_bottomRight.y);
		bottomRight.y = std::max(a_topLeft.y,a_bottomRight.y);
		topLeft.z = a_topLeft.z; bottomRight.z = a_bottomRight.z;

		points[0] = topLeft;
		points[1].x = topLeft.x;	points[1].y = bottomRight.y;	points[1].z = (bottomRight.z + topLeft.z) / 2;
		points[2] = bottomRight;
		points[3].x = bottomRight.x;	points[3].y = topLeft.y;	points[3].z = (bottomRight.z + topLeft.z) / 2;
	}

	void DrawRectangle::setTwoCorners( const Point &a_topLeft, const Point &a_bottomRight ){
		if((a_topLeft.z + a_bottomRight.z) / 2.0 != getDepth()){
			alertParent("NeedsSort");
		}
		Point topLeft = a_topLeft, bottomRight = a_bottomRight;
		topLeft.x = std::min(a_topLeft.x,a_bottomRight.x);
		bottomRight.x = std::max(a_topLeft.x,a_bottomRight.x);
		topLeft.y = std::min(a_topLeft.y,a_bottomRight.y);
		bottomRight.y = std::max(a_topLeft.y,a_bottomRight.y);
		topLeft.z = a_topLeft.z; bottomRight.z = a_bottomRight.z;

		points[0] = topLeft;
		points[1].x = topLeft.x;	points[1].y = bottomRight.y;	points[1].z = (bottomRight.z + topLeft.z) / 2;
		points[2] = bottomRight;
		points[3].x = bottomRight.x;	points[3].y = topLeft.y;	points[3].z = (bottomRight.z + topLeft.z) / 2;
	}

	void DrawRectangle::setTwoCorners( const BoxAABB &a_bounds){
		setTwoCorners(a_bounds.minPoint, a_bounds.maxPoint);
	}

	void DrawRectangle::setSizeAndLocation( const Point &a_centerPoint, double a_width, double a_height ){
		double halfWidth = a_width/2, halfHeight = a_height/2;
		
		Point topLeft = a_centerPoint;
		topLeft.x=-halfHeight; topLeft.y=-halfHeight;
		
		Point bottomRight = a_centerPoint;
		bottomRight.x=halfWidth; bottomRight.y=halfHeight;
		
		setTwoCorners(topLeft, bottomRight);
		placeAt(a_centerPoint);
	}

	void DrawRectangle::setSizeAndCornerLocation( const Point &a_topLeft, double a_width, double a_height ){
		Point topLeft(0, 0);
		Point bottomRight(a_width, a_height);
		
		setTwoCorners(topLeft, bottomRight);
		placeAt(a_topLeft);
	}

	void DrawRectangle::resetTextureCoordinates(){
		points[0].textureX = 0.0; points[0].textureY = 0.0;
		points[1].textureX = 0.0; points[1].textureY = 1.0;
		points[2].textureX = 1.0; points[2].textureY = 1.0;
		points[3].textureX = 1.0; points[3].textureY = 0.0;
	}

	void DrawRectangle::drawImplementation(){
		defaultDraw(GL_TRIANGLE_FAN);
	}

	void AssignTextureToRectangle(DrawRectangle &a_rectangle, const SubTexture *a_texture, bool a_resize, bool a_flip){
		TexturePoint TmpPoint[4];
		TmpPoint[0].textureX = a_texture->percentX;
		TmpPoint[0].textureY = a_texture->percentY;

		TmpPoint[1].textureX = a_texture->percentX+a_texture->percentWidth;
		TmpPoint[1].textureY = a_texture->percentY;

		TmpPoint[2].textureX = a_texture->percentX+a_texture->percentWidth;
		TmpPoint[2].textureY = a_texture->percentY+a_texture->percentHeight;

		TmpPoint[3].textureX = a_texture->percentX;
		TmpPoint[3].textureY = a_texture->percentY+a_texture->percentHeight;

		if(a_flip){
			std::swap(TmpPoint[0].textureY, TmpPoint[1].textureY);
			std::swap(TmpPoint[0].textureX, TmpPoint[1].textureX);
			std::swap(TmpPoint[2].textureY, TmpPoint[3].textureY);
			std::swap(TmpPoint[2].textureX, TmpPoint[3].textureX);
		}
		a_rectangle.applyToCorners(TmpPoint[0], TmpPoint[1], TmpPoint[2], TmpPoint[3]);
		a_rectangle.setTexture(a_texture->parentTexture);

		if(a_resize){
			a_rectangle.setTwoCorners(MV::Point(0, 0), MV::Point(a_texture->width, a_texture->height));
		}
	}

	void AssignTextureToRectangle(DrawRectangle &a_rectangle, const MainTexture *a_texture, bool a_resize, bool a_flip){
		AssignTextureToRectangle(a_rectangle, &(a_texture->texture), a_flip);
		if(a_resize){
			a_rectangle.setTwoCorners(MV::Point(0, 0), MV::Point(a_texture->width, a_texture->height));
		}
	}

	void AssignTextureToRectangle(DrawRectangle &a_rectangle, const GLuint *a_texture, bool a_flip){
		TexturePoint TmpPoint[4];

		TmpPoint[0].textureX = 0.0;	TmpPoint[0].textureY = 0.0;
		TmpPoint[1].textureX = 1.0;	TmpPoint[1].textureY = 0.0;
		TmpPoint[2].textureX = 1.0;	TmpPoint[2].textureY = 1.0;
		TmpPoint[3].textureX = 0.0;	TmpPoint[3].textureY = 1.0;

		if(a_flip){
			std::swap(TmpPoint[0].textureY, TmpPoint[1].textureY);
			std::swap(TmpPoint[0].textureX, TmpPoint[1].textureX);
			std::swap(TmpPoint[2].textureY, TmpPoint[3].textureY);
			std::swap(TmpPoint[2].textureX, TmpPoint[3].textureX);
		}
		a_rectangle.applyToCorners(TmpPoint[0], TmpPoint[1], TmpPoint[2], TmpPoint[3]);
		a_rectangle.setTexture(a_texture);
	}

	/*************************\
	| ------PointVolume------ |
	\*************************/

	bool PointVolume::pointContained( const Point &a_comparePoint ){
		int i;
		double angle=0;
		Point p1,p2;
		int totalPoints = (int)points.size();
		for (i=0;i<totalPoints;i++) {
			p1 = points[i]-a_comparePoint;
			p2 = points[(i+1)%totalPoints] - a_comparePoint;
			angle += getAngle(p1, p2);
		}
		if(angle < 0){angle*=-1;}
		if (angle < PIE){
			return false;
		}
		return true;
	}
	void PointVolume::addPoint( const Point &a_newPoint ){
		points.push_back(a_newPoint);
	}

	double PointVolume::getAngle(const Point &a_p1, const Point &a_p2){
		double theta1 = atan2(a_p1.y,a_p1.x);
		double theta2 = atan2(a_p2.y,a_p2.x);
		double dtheta = theta2 - theta1;

		while (dtheta > PIE){
			dtheta -= PIE*2.0;
		}
		while (dtheta < -PIE){
			dtheta += PIE*2.0;
		}

		return(dtheta);
	}

	Point PointVolume::getCenter(){
		int totalPoints = (int)points.size();
		Point average = std::accumulate(points.begin(), points.end(), Point());
		average/=double(totalPoints);
		return average;
	}

	bool PointVolume::volumeCollision(PointVolume &a_compareVolume, Draw2D* a_renderer){
		require(a_renderer != nullptr, PointerException("PointVolume::volumeCollision was passed a null renderer."));
		Point point1 = getCenter();
		Point point2 = a_compareVolume.getCenter();

		double angle = getAngle(point1, point2);
		angle = angle * (180.0 / PIE);
		angle+=90.0;

		PointVolume tmpVolume1, tmpVolume2;
		int totalPoints;
		modelviewMatrix().push().makeIdentity().rotateZ(angle);

		totalPoints = (int)points.size();
		for(int i = 0;i < totalPoints;i++){
			tmpVolume1.addPoint(a_renderer->worldFromLocal(points[i]));
		}

		totalPoints = (int)a_compareVolume.points.size();
		for(int i = 0;i < totalPoints;i++){
			tmpVolume2.addPoint(a_renderer->worldFromLocal(a_compareVolume.points[i]));
		}

		modelviewMatrix().pop();

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
			for(int i = 1;i < totalPoints;i++){
				result.expandWith(points[i]);
			}
		}
		return result;
	}
}
