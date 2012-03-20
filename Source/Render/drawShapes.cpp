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

   bool BoxAABB::pointContained( const Point &a_comparePoint ) const{
      if(a_comparePoint.x >= minPoint.x && a_comparePoint.y >= minPoint.y && a_comparePoint.z >= minPoint.z){
         if(a_comparePoint.x <= maxPoint.x && a_comparePoint.y <= maxPoint.y && a_comparePoint.z <= maxPoint.z){
            return true;
         }
      }
      return false;
   }

   bool BoxAABB::pointContainedXY( const Point &a_comparePoint ) const{
      if((a_comparePoint.x >= minPoint.x && a_comparePoint.y >= minPoint.y) && (a_comparePoint.x <= maxPoint.x && a_comparePoint.y <= maxPoint.y)){
         return true;
      }
      return false;
   }

   /*************************\
   | -------DrawShape------- |
   \*************************/

   void DrawShape::setColor( const Color &a_newColor ){
      int elements = (int)Pnt.size();
      for(int i = 0;i < elements;i++){
         Pnt[i] = a_newColor;
      }
   }

   void DrawShape::setTexture( GLuint *a_textureId ){
      texture = a_textureId;
      hasTexture = true;
   }

   void DrawShape::removeTexture(){
      hasTexture = false;
   }

   double DrawShape::getDepth(){
      if(depthOverride){
         return overrideDepthValue;
      }
      int elements = (int)Pnt.size();
      double total = 0;
      for(int i = 0;i < elements;i++){
         total+=Pnt[i].z;
      }
      total/=(double)elements;
      return total;
   }

   bool DrawShape::operator<( DrawShape &a_other ){
      return getDepth() < a_other.getDepth();
   }

   bool DrawShape::operator>( DrawShape &a_other ){
      return getDepth() > a_other.getDepth();
   }

   bool DrawShape::operator==( DrawShape &a_other ){
      return getDepth() == a_other.getDepth();
   }

   Point DrawShape::getScale(){
      return scaleTo;
   }

   AxisAngles DrawShape::getRotation(){
      return rotateTo;
   }

   Point DrawShape::getLocation(){
      return translateTo;
   }

   Point DrawShape::getRelativeLocation(){
      Point resultPoint;
      resultPoint = translateTo;
      if(myParent){
         resultPoint += myParent->getRelativeLocation();
      }
      return resultPoint;
   }

   Point DrawShape::getRelativeScale(){
      Point resultPoint;
      resultPoint = scaleTo;
      if(myParent){
         resultPoint += myParent->getRelativeScale();
      }
      return resultPoint;
   }

   Point DrawShape::getRelativeRotation(){
      Point resultPoint;
      resultPoint = rotateTo;
      if(myParent){
         resultPoint += myParent->getRelativeRotation();
      }
      return resultPoint;
   }

   void DrawShape::scale( double a_newScale ){
      scale( Point(a_newScale, a_newScale, a_newScale) );
   }

   void DrawShape::scale( const Point &a_scaleValue ){
      scaleTo = a_scaleValue;
   }

   void DrawShape::incrementScale( double a_newScale ){
      incrementScale( Point(a_newScale, a_newScale, a_newScale) );
   }

   void DrawShape::incrementScale( const AxisMagnitude &a_scaleValue ){
      scaleTo += a_scaleValue;
   }

   void DrawShape::setParent( DrawShape* a_parentItem ){
      myParent = a_parentItem;
   }

   BoxAABB DrawShape::getWorldAABB(){
      require(renderer != nullptr, PointerException("DrawShape::getAABB requires a rendering context."));
      alertParent("pushMatrix");
      pushMatrix();
      int elements = (int)Pnt.size();
      BoxAABB tmpBox;
      if(elements > 0){
         tmpBox.initialize(renderer->getObjectToWorldPoint(Pnt[0]));
         for(int i = 0;i < elements;i++){
            tmpBox.expandWith(renderer->getObjectToWorldPoint(Pnt[i]));
         }
      }
      popMatrix();
      alertParent("popMatrix");
      return tmpBox;
   }

   BoxAABB DrawShape::getLocalAABB(){
      BoxAABB tmpBox;
      if(!Pnt.empty()){
         tmpBox.initialize(Pnt[0]);
         std::for_each(Pnt.begin(), Pnt.end(), [&](Point &point){
            tmpBox.expandWith(point);
         });
      }
      return tmpBox;
   }

   PointVolume DrawShape::getWorldPoints(){
      require(renderer != nullptr, PointerException("DrawShape::getWorldPoints requires a rendering context."));
      alertParent("pushMatrix");
      pushMatrix();
      int elements = (int)Pnt.size();
      PointVolume pointList;
      for(int i = 0;i < elements;i++){
         pointList.addPoint(renderer->getObjectToWorldPoint(Pnt[i]));
      }
      popMatrix();
      alertParent("popMatrix");
      return pointList;
   }

   Point DrawShape::getWorldPoint(Point a_local){
      require(renderer != nullptr, PointerException("DrawShape::getWorldPoint requires a rendering context."));
      alertParent("pushMatrix");
      pushMatrix();
      Point ourPoint;
      ourPoint = renderer->getObjectToWorldPoint(a_local);
      popMatrix();
      alertParent("popMatrix");
      return ourPoint;
   }

   void DrawShape::pushMatrix(){
      modelviewMatrix().push();
      if(!translateTo.atOrigin()){
         modelviewMatrix().top().translate(translateTo.x, translateTo.y, translateTo.z);
      }
      if(!scaleTo.atOrigin()){
         modelviewMatrix().top().scale(scaleTo.x, scaleTo.y, scaleTo.z);
      }
      if(!rotateTo.atOrigin()){
         modelviewMatrix().top().translate(rotateOrigin.x, rotateOrigin.y, rotateOrigin.z);
         modelviewMatrix().top().rotateX(rotateTo.x).rotateY(rotateTo.y).rotateZ(rotateTo.z);
         modelviewMatrix().top().translate(-rotateOrigin.x, -rotateOrigin.y, -rotateOrigin.z);
      }
   }

   void DrawShape::popMatrix(){
      modelviewMatrix().pop();
   }

   void DrawShape::bindOrDisableTexture(const std::shared_ptr<std::vector<GLfloat>> &texturePoints){
      if(hasTexture && texture != NULL){
         glBindTexture(GL_TEXTURE_2D, *texture);
         glEnable(GL_TEXTURE_2D);
         glEnableClientState(GL_TEXTURE_COORD_ARRAY);

         glTexCoordPointer(2, GL_FLOAT, 0, &(*texturePoints)[0]);
      }else{
         glDisable(GL_TEXTURE_2D);
      }
   }

   void DrawShape::defaultDrawRenderStep( GLenum drawType ){
      auto textureVertexArray = getTextureVertexArray();
      bindOrDisableTexture(textureVertexArray);

      glEnableClientState(GL_COLOR_ARRAY);
      auto colorVertexArray = getColorVertexArray();
      glColorPointer(4, GL_FLOAT, 0, &(*colorVertexArray)[0]);

      glEnableClientState(GL_VERTEX_ARRAY);
      auto positionVertexArray = getPositionVertexArray();
      glVertexPointer(3, GL_FLOAT, 0, &(*(positionVertexArray))[0]);
      
      glDrawArrays(drawType,0,Pnt.size());

      glDisableClientState(GL_COLOR_ARRAY);
      glDisableClientState(GL_VERTEX_ARRAY);
      if(hasTexture && texture != NULL){
         glDisableClientState(GL_TEXTURE_COORD_ARRAY);
         glDisable(GL_TEXTURE_2D);
      }
   }

   void DrawShape::defaultDraw( GLenum drawType ){
      pushMatrix();
      defaultDrawRenderStep(drawType);
      popMatrix();
   }

   std::shared_ptr<std::vector<GLfloat>> DrawShape::getPositionVertexArray(){
      auto returnArray = std::make_shared<std::vector<GLfloat>>(Pnt.size()*3);
      TransformMatrix transformationMatrix(projectionMatrix().top() * modelviewMatrix().top());
      for(size_t i = 0;i < Pnt.size();++i){
         TransformMatrix transformedPoint(transformationMatrix * TransformMatrix(Pnt[i]));
         (*returnArray)[i*3+0] = static_cast<float>(transformedPoint.getX());
         (*returnArray)[i*3+1] = static_cast<float>(transformedPoint.getY());
         (*returnArray)[i*3+2] = static_cast<float>(transformedPoint.getZ());
      }
      return returnArray;
   }

   std::shared_ptr<std::vector<GLfloat>> DrawShape::getTextureVertexArray(){
      auto returnArray = std::make_shared<std::vector<GLfloat>>(Pnt.size()*2);
      for(size_t i = 0;i < Pnt.size();++i){
         (*returnArray)[i*2+0] = static_cast<float>(Pnt[i].textureX);
         (*returnArray)[i*2+1] = static_cast<float>(Pnt[i].textureY);
      }
      return returnArray;
   }

   std::shared_ptr<std::vector<GLfloat>> DrawShape::getColorVertexArray(){
      auto returnArray = std::make_shared<std::vector<GLfloat>>(Pnt.size()*4);
      for(size_t i = 0;i < Pnt.size();++i){
         (*returnArray)[i*4+0] = static_cast<float>(Pnt[i].R);
         (*returnArray)[i*4+1] = static_cast<float>(Pnt[i].G);
         (*returnArray)[i*4+2] = static_cast<float>(Pnt[i].B);
         (*returnArray)[i*4+3] = static_cast<float>(Pnt[i].A);
      }
      return returnArray;
   }

   /*************************\
   | -------DrawPixel------- |
   \*************************/

   void DrawPixel::setPoint( const DrawPoint &a_point ){
      alertParent("NeedsSort");
      Pnt[0] = a_point;
   }

   void DrawPixel::draw(){
      defaultDraw(GL_POINTS);
   }

   /*************************\
   | --------DrawLine------- |
   \*************************/

   void DrawLine::setEnds( const Point &a_startPoint, const Point &a_endPoint ){
      alertParent("NeedsSort");
      Pnt[0] = a_startPoint;
      Pnt[1] = a_endPoint;
   }

   void DrawLine::draw(){
      defaultDraw(GL_LINES);
   }

   /*************************\
   | -----DrawRectangle----- |
   \*************************/

   void DrawRectangle::setTwoCorners( const Point &a_topLeft, const Point &a_bottomRight ){
      alertParent("NeedsSort");
      Point topLeft, bottomRight;
      topLeft.x = std::min(a_topLeft.x,a_bottomRight.x);
      bottomRight.x = std::max(a_topLeft.x,a_bottomRight.x);
      topLeft.y = std::min(a_topLeft.y,a_bottomRight.y);
      bottomRight.y = std::max(a_topLeft.y,a_bottomRight.y);
      topLeft.z = a_topLeft.z; bottomRight.z = a_bottomRight.z;

      Pnt[0] = topLeft;
      Pnt[1].x = topLeft.x;   Pnt[1].y = bottomRight.y;   Pnt[1].z = (bottomRight.z + topLeft.z) / 2;
      Pnt[2] = bottomRight;
      Pnt[3].x = bottomRight.x;   Pnt[3].y = topLeft.y;   Pnt[3].z = (bottomRight.z + topLeft.z) / 2;
   }

   void DrawRectangle::setSizeAndLocation( const Point &a_centerPoint, double a_width, double a_height ){
      double halfWidth = a_width/2, halfHeight = a_height/2;
      setTwoCorners(Point(-halfWidth, -halfHeight), Point(halfWidth, halfHeight));
      placeAt(a_centerPoint);
   }

   void DrawRectangle::resetTextureCoordinates(){
      Pnt[0].textureX = 0.0; Pnt[0].textureY = 0.0;
      Pnt[1].textureX = 0.0; Pnt[1].textureY = 1.0;
      Pnt[2].textureX = 1.0; Pnt[2].textureY = 1.0;
      Pnt[3].textureX = 1.0; Pnt[3].textureY = 0.0;
   }

   void DrawRectangle::draw(){
      defaultDraw(GL_QUADS);
   }

   void AssignTextureToRectangle(DrawRectangle &a_rectangle, SubTexture *a_texture, bool a_flip){
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
   }

   void AssignTextureToRectangle(DrawRectangle &a_rectangle, MainTexture *a_texture, bool a_flip){
      AssignTextureToRectangle(a_rectangle, &(a_texture->texture), a_flip);
   }

   void AssignTextureToRectangle(DrawRectangle &a_rectangle, GLuint *a_texture, bool a_flip){
      TexturePoint TmpPoint[4];
      //*
      TmpPoint[0].textureX = 0.0;   TmpPoint[0].textureY = 0.0;
      TmpPoint[1].textureX = 1.0;   TmpPoint[1].textureY = 0.0;
      TmpPoint[2].textureX = 1.0;   TmpPoint[2].textureY = 1.0;
      TmpPoint[3].textureX = 0.0;   TmpPoint[3].textureY = 1.0;
      /*/
      TmpPoint[0].textureX = 0.0;   TmpPoint[0].textureY = 1.0;
      TmpPoint[1].textureX = 0.0;   TmpPoint[1].textureY = 0.0;
      TmpPoint[2].textureX = 1.0;   TmpPoint[2].textureY = 1.0;
      TmpPoint[3].textureX = 1.0;   TmpPoint[3].textureY = 0.0;
      //*/
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
   | ---------Scene--------- |
   \*************************/

   Scene::Scene():drawSorted(true),isSorted(false){}

   Scene::Scene(Draw2D *a_renderer):DrawShape(a_renderer),drawSorted(true),isSorted(false){}

   Scene::~Scene(){
      clear();
   }

   void Scene::setRenderer( Draw2D* a_renderer ){
      renderer = a_renderer;
      for(auto cell = DrawList.begin();cell != DrawList.end();cell++){
         cell->second->setRenderer(a_renderer);
      }
   }

   void Scene::clear(){
      DrawList.clear();
   }

   std::shared_ptr<DrawShape> Scene::add(std::shared_ptr<DrawShape> a_childItem, const std::string &a_childId){
      alertParent("NeedsSort");
      isSorted = 0;
      auto existingObject = DrawList.find(a_childId);
      if(existingObject != DrawList.end()){
         DrawList.erase(existingObject);
      }
      a_childItem->setParent(this);
      if(renderer){
         a_childItem->setRenderer(renderer);
      }
      DrawList[a_childId] = a_childItem;
      return a_childItem;
   }

   bool Scene::remove(std::shared_ptr<DrawShape> a_childItem, bool a_deleteOnRemove ){
      auto cell = DrawList.begin();
      for(;cell != DrawList.end() && cell->second != a_childItem;cell++){
         ;
      }
      if(cell->second == a_childItem){
         DrawList.erase(cell);
         return true;
      }
      return false;
   }

   bool Scene::remove(const std::string &a_childId, bool a_deleteOnRemove ){
      auto cell = DrawList.find(a_childId);
      if(cell != DrawList.end()){
         DrawList.erase(cell);
         return true;
      }
      return false;
   }

   std::shared_ptr<DrawShape> Scene::get(const std::string &a_childId){
      auto cell = DrawList.find(a_childId);
      if(cell != DrawList.end()){
         return cell->second;
      }
      require(0, ResourceException("Scene::getChild was unable to find an element matching the ID: (" + a_childId + ")"));
      return nullptr;
   }

   void Scene::draw(){
      pushMatrix();
      if(drawSorted){
         sortedRender();
      }else{
         unsortedRender();
      }
      popMatrix();
   }

   void Scene::sortedRender(){
      if(!isSorted){
         DrawListVector.clear();
         std::for_each(DrawList.begin(), DrawList.end(), [&](DrawListType::value_type &cell){
            DrawListVector.push_back(std::shared_ptr<DrawShape>(cell.second));
         });
         std::sort(DrawListVector.begin(), DrawListVector.end(), [](DrawListVectorType::value_type one, DrawListVectorType::value_type two){
            return *one < *two;
         });
         isSorted = true;
      }
      std::for_each(DrawListVector.begin(), DrawListVector.end(), [](DrawListVectorType::value_type shape){shape->draw();});
   }

   void Scene::unsortedRender(){
      std::for_each(DrawList.begin(), DrawList.end(), [](DrawListType::value_type &cell){
         cell.second->draw();
      });
   }

   double Scene::getDepth(){
      int elements = 0;
      double total = 0;
      for(auto cell = DrawList.begin();cell != DrawList.end();cell++){
         elements++;
         total+=cell->second->getDepth();
      }
      total/=(double)elements;
      return total;
   }

   std::string Scene::getMessage(const std::string &a_message ){
      if(a_message == "NeedsSort"){
         alertParent(a_message);
         isSorted = 0;
      }else if(a_message == "pushMatrix"){
         alertParent(a_message);
         pushMatrix();
      }else if(a_message == "popMatrix"){
         popMatrix();
         alertParent(a_message);
      }
      return "";
   }

   void Scene::sortScene( bool a_depthMatters ){
      drawSorted = a_depthMatters;
   }

   void Scene::setColor( Color a_newColor ){
      for(auto cell = DrawList.begin();cell != DrawList.end();cell++){
         cell->second->setColor(a_newColor);
      }
   }

   BoxAABB Scene::getWorldAABB(){
      BoxAABB tmpBox1, tmpBox2;
      bool first = true;
      std::for_each(DrawList.begin(), DrawList.end(), [&](const DrawListType::value_type &cell){
         tmpBox1 = cell.second->getWorldAABB();
         if(first){
            tmpBox2.initialize(tmpBox1);
            first = false;
         }else{
            tmpBox2.expandWith(tmpBox1);
         }
      });
      return tmpBox2;
   }

   BoxAABB Scene::getLocalAABB(){
      BoxAABB tmpBox1, tmpBox2;
      bool first = true;
      std::for_each(DrawList.begin(), DrawList.end(), [&](const DrawListType::value_type &cell){
         tmpBox1 = cell.second->getLocalAABB();
         tmpBox1.maxPoint += cell.second->getLocation();
         tmpBox1.minPoint += cell.second->getLocation();
         if(first){
            tmpBox2.initialize(tmpBox1);
            first = false;
         }else{
            tmpBox2.expandWith(tmpBox1);
         }
      });
      return tmpBox2;
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
         p1.x = points[i].x - a_comparePoint.x;
         p1.y = points[i].y - a_comparePoint.y;
         p2.x = points[(i+1)%totalPoints].x - a_comparePoint.x;
         p2.y = points[(i+1)%totalPoints].y - a_comparePoint.y;
         angle += getAngle((float)p1.x,(float)p1.y,(float)p2.x,(float)p2.y);
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

   double PointVolume::getAngle( double a_x1, double a_y1, double a_x2, double a_y2 ){
      double dtheta,theta1,theta2;

      theta1 = atan2(a_y1,a_x1);
      theta2 = atan2(a_y2,a_x2);
      dtheta = theta2 - theta1;
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

   bool PointVolume::volumeCollision( PointVolume &a_compareVolume,  Draw2D* a_renderer){
      require(a_renderer != nullptr, PointerException("PointVolume::volumeCollision was passed a null renderer."));
      Point point1 = getCenter();
      Point point2 = a_compareVolume.getCenter();

      double angle = getAngle(point1.x, point1.y, point2.x, point2.y);
      angle = angle * (180.0 / PIE);
      angle+=90.0;

      PointVolume tmpVolume1, tmpVolume2;
      int totalPoints;
      modelviewMatrix().push().makeIdentity().rotateZ(angle);

      totalPoints = (int)points.size();
      for(int i = 0;i < totalPoints;i++){
         tmpVolume1.addPoint(a_renderer->getObjectToWorldPoint(points[i]));
      }

      totalPoints = (int)a_compareVolume.points.size();
      for(int i = 0;i < totalPoints;i++){
         tmpVolume2.addPoint(a_renderer->getObjectToWorldPoint(a_compareVolume.points[i]));
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
