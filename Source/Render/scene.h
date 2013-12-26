/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
| Primitive drawing shapes go here.  Common notation for   |
| lists of points is start at the top left corner and go   |
| clockwise (so 1 = top left, 2 = top right, 3 = bot right |
| 4 = bot left)                                            |
\**********************************************************/

#ifndef _MV_SCENE_H_
#define _MV_SCENE_H_

#define GL_GLEXT_PROTOTYPES
#define GLX_GLEXT_PROTOTYPES
#include <stdio.h>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <boost/lexical_cast.hpp>

#include "Render/render.h"
#include "Render/textures.h"
#include "Render/points.h"
#include "Utility/package.h"

namespace MV {
	class BoxAABB{
	public:
		BoxAABB(){ initialize(Point<>()); }
		BoxAABB(const Point<> &a_startPoint){ initialize(a_startPoint); }
		BoxAABB(const Point<> &a_startPoint, const Point<> &a_endPoint){ initialize(a_startPoint, a_endPoint); }

		void initialize(const Point<> &a_startPoint);
		void initialize(const BoxAABB &a_startBox);
		void initialize(const Point<> &a_startPoint, const Point<> &a_endPoint);
		void expandWith(const Point<> &a_comparePoint);
		void expandWith(const BoxAABB &a_compareBox);

		bool isEmpty() const{ return minPoint == maxPoint; }
		bool flatWidth() const{ return minPoint.x == maxPoint.x; }
		bool flatHeight() const{ return minPoint.y == maxPoint.y; }

		Size<> getSize() const{ return sizeFromPoint(maxPoint - minPoint); }

		//includes z
		bool pointContainedZ(const Point<> &a_comparePoint) const;

		//ignores z
		bool pointContained(const Point<> &a_comparePoint) const;

		void sanitize();

		Point<> centerPoint(){ return minPoint + ((minPoint + maxPoint) / 2.0); }
		Point<> minPoint, maxPoint;
	};

	std::ostream& operator<<(std::ostream& a_os, const BoxAABB& a_point);
	std::istream& operator>>(std::istream& a_is, BoxAABB& a_point);

	class PointVolume{
	public:
		void addPoint(const Point<> &a_newPoint);

		//ignores z
		bool pointContained(const Point<> &a_comparePoint);

		bool volumeCollision(PointVolume &a_compareVolume, Draw2D* a_renderer);
		Point<> getCenter();
		BoxAABB getAABB();

		std::vector<Point<>> points;
	private:
		//get angle within proper range between two points (-pi to +pi)
		double getAngle(const Point<> &a_p1, const Point<> &a_p2);
	};

	typedef Point<> AxisAngles;
	typedef Point<> AxisMagnitude;

	namespace Scene {
		//Abstract Base Class
		class Node : public std::enable_shared_from_this<Node> {
		public:
			virtual ~Node(){}

			static std::shared_ptr<Node> make(Draw2D* a_renderer, const Point<> &a_placement = Point<>());

			//Must be set or initialized in the constructor
			virtual void setRenderer(Draw2D* a_renderer){ renderer = a_renderer; }
			Draw2D* getRenderer(){ return renderer; }

			virtual void clear();

			//For composite collections
			template<typename TypeToAdd>
			std::shared_ptr<TypeToAdd> add(const std::string &a_childId, std::shared_ptr<TypeToAdd> a_childItem) {
				alertParent("NeedsSort");
				alertParent("Change");
				isSorted = 0;
				if(renderer){
					a_childItem->setRenderer(renderer);
				}
				a_childItem->setParent(this);
				drawList[a_childId] = a_childItem;
				return a_childItem;
			}

			template<typename TypeToMake>
			std::shared_ptr<TypeToMake> make(const std::string &a_childId) {
				auto newChild = TypeToMake::make(renderer);
				add(a_childId, newChild);
				return newChild;
			}

			template<typename TypeToMake, typename ...Arg>
			std::shared_ptr<TypeToMake> make(const std::string &a_childId, Arg... a_parameters) {
				auto newChild = TypeToMake::make(renderer, std::forward<Arg>(a_parameters)...);
				add(a_childId, newChild);
				return newChild;
			}

			bool remove(std::shared_ptr<Node> a_childItem);
			bool remove(const std::string &a_childId);

			void draw();
			double getDepth();

			//Axis Aligned Bounding Box
			virtual BoxAABB getWorldAABB(bool includeChildren = true);
			virtual BoxAABB getScreenAABB(bool includeChildren = true);
			virtual BoxAABB getLocalAABB();

			//Point conversion
			Point<> worldFromLocal(const Point<> &a_local);
			Point<int> screenFromLocal(const Point<> &a_local);
			Point<> localFromScreen(const Point<int> &a_screen);
			Point<> localFromWorld(const Point<> &a_world);

			//If you have several points this is way more efficient.
			std::vector<Point<>> worldFromLocal(std::vector<Point<>> a_local);
			std::vector<Point<int>> screenFromLocal(const std::vector<Point<>> &a_local);
			std::vector<Point<>> localFromScreen(const std::vector<Point<int>> &a_screen);
			std::vector<Point<>> localFromWorld(std::vector<Point<>> a_world);

			BoxAABB worldFromLocal(BoxAABB a_local);
			BoxAABB screenFromLocal(BoxAABB a_local);
			BoxAABB localFromScreen(BoxAABB a_screen);
			BoxAABB localFromWorld(BoxAABB a_world);

			//For command type alerts
			void setParent(Node* a_parentItem);

			void scale(double a_newScale);
			void scale(const Point<> &a_scaleValue);
			void incrementScale(double a_newScale);
			void incrementScale(const AxisMagnitude &a_scaleValue);

			//Rotation is degrees around each axis from 0 to 360, axis info supplied 
			//Typically the z axis is the only visible rotation axis, but to allow for
			//2d objects not being displayed orthographically to also twist we have
			//the full rotational axis available as well.
			void setRotate(double a_zRotation){
				rotateTo.z = a_zRotation;
				alertParent("Change");
			}
			void incrementRotate(double a_zRotation){ 
				rotateTo.z += a_zRotation;
				alertParent("Change");
			}
			void setRotate(const AxisAngles &a_rotation){
				rotateTo = a_rotation;
				alertParent("Change");
			}
			void incrementRotate(const AxisAngles &a_rotation){
				rotateTo += a_rotation;
				alertParent("Change");
			}

			void setRotateOrigin(const Point<> &a_origin){
				rotateOrigin = a_origin;
				alertParent("Change");
			}
			void centerRotateOrigin(){
				rotateOrigin = getLocalAABB().centerPoint();
				alertParent("Change");
			}

			void translate(const Point<> &a_translation){
				translateTo += a_translation;
				alertParent("Change"); 
			}
			void placeAt(const Point<> &a_placement){
				translateTo = a_placement;
				alertParent("Change");
			}

			//By default the sort depth is calculated by averaging the z values of all points
			//setSortDepth manually overrides this calculation.  unsetSortDepth removes this override.
			void setSortDepth(double a_newDepth){
				bool changed = a_newDepth != getDepth();
				depthOverride = true;
				overrideDepthValue = a_newDepth;
				if(a_newDepth != getDepth()){
					alertParent("NeedsSort");
					alertParent("Change");
				}
			}
			void unsetSortDepth(){
				double oldDepth = getDepth();
				depthOverride = false;
				if(oldDepth != getDepth()){
					alertParent("NeedsSort");
					alertParent("Change");
				}
			}
			//true to render the sorted list, false to render unordered.  Default behavior is true.
			void sortScene(bool a_depthMatters);

			Point<> getLocation();
			Point<> getRotation();
			Point<> getScale();

			virtual Point<> getRelativeLocation();
			virtual Point<> getRelativeRotation();
			virtual Point<> getRelativeScale();

			virtual void setColor(const Color &a_newColor);

			void setTexture(std::shared_ptr<TextureHandle> a_texture);
			std::shared_ptr<TextureHandle> getTexture() const;
			void clearTexture();
			virtual void clearTextureCoordinates(){}
			virtual void updateTextureCoordinates(){}

			bool visible() const;
			void hide();
			void show();

			//Used to determine sorting based on getDepth
			bool operator<(Node &a_other);
			bool operator>(Node &a_other);
			bool operator==(Node &a_other);
			bool operator!=(Node &a_other);

			//Retrieve and convert this to the desired type, argument required for gathering the type.
			//example call DrawRectangle *rect = ourScene->getChild("id")->getThis<DrawRectangle>();
			template<typename DerivedClass>
			std::shared_ptr<DerivedClass> get(){
				require((typeid(DerivedClass) == typeid(*this)), PointerException(std::string("DrawShape::getThis() was called with the wrong type info supplied.  Requested conversion: (") + typeid(DerivedClass).name() + ") Actual type: (" + typeid (*this).name() + ")"));
				return std::dynamic_pointer_cast<DerivedClass>(shared_from_this());
			}

			std::shared_ptr<Node> get(const std::string& a_childId);
			//convenient return value conversion get<DrawShapeType>(childId)
			template<typename DerivedClass>
			std::shared_ptr<DerivedClass> get(const std::string& a_childId){
				return get(a_childId)->get<DerivedClass>();
			}
		protected:
			Node(Draw2D* a_renderer);
			void defaultDraw(GLenum drawType);
			void sortedRender();
			void unsortedRender();

			//Command passing
			virtual bool getMessage(const std::string &a_message);
			void alertParent(const std::string &a_message){
				if(myParent != nullptr){
					if(!myParent->getMessage(a_message)){
						myParent->alertParent(a_message);
					}
				}
			}

			void pushMatrix();
			void popMatrix();

			std::shared_ptr<std::vector<GLfloat>> getPositionVertexArray();
			std::shared_ptr<std::vector<GLfloat>> getTextureVertexArray();
			std::shared_ptr<std::vector<GLfloat>> getColorVertexArray();

			Point<> scaleTo;
			Point<> translateTo;
			Point<> rotateTo;
			Point<> rotateOrigin;

			bool depthOverride;
			double overrideDepthValue;

			bool isVisible;
			bool hasTexture;
			bool needsBoundRecalculate;
			std::shared_ptr<TextureHandle> texture;
			std::shared_ptr<TextureHandle::Signal> textureSizeSignal;
			std::vector<DrawPoint> points;
			Node *myParent;

			Draw2D *renderer;

			GLenum drawType;

			bool drawSorted, isSorted;
			typedef std::vector<std::shared_ptr<Node>> DrawListVectorType;
			DrawListVectorType drawListVector;
			typedef std::map<std::string, std::shared_ptr<Node>> DrawListType;
			DrawListType drawList;

		private:
			virtual bool preDraw(){ return true; }
			virtual void postDraw(){}
			virtual void drawImplementation(){} //override this in subclasses
			void defaultDrawRenderStep(GLenum drawType);
			void bindOrDisableTexture(const std::shared_ptr<std::vector<GLfloat>> &texturePoints);
		};

		class Pixel : public Node{
			friend Node;
		public:
			static std::shared_ptr<Pixel> make(Draw2D* a_renderer, const DrawPoint &a_point = DrawPoint());
			virtual ~Pixel(){}

			void setPoint(const DrawPoint &a_point);

			template<typename PointAssign>
			void applyToPoint(const PointAssign &a_values);
		protected:
			Pixel(Draw2D *a_renderer):Node(a_renderer){
				points.resize(1);
			}

		private:
			virtual void drawImplementation();
		};

		template<typename PointAssign>
		void Pixel::applyToPoint(const PointAssign &a_values){
			alertParent("NeedsSort");
			points[0] = a_values;
		}

		class Line : public Node{
			friend Node;
		public:
			static std::shared_ptr<Line> make(Draw2D* a_renderer);
			static std::shared_ptr<Line> make(Draw2D* a_renderer, const DrawPoint &a_startPoint, const DrawPoint &a_endPoint);

			virtual ~Line(){}

			void setEnds(const DrawPoint &a_startPoint, const DrawPoint &a_endPoint);

			template<typename PointAssign>
			void applyToEnds(const PointAssign &a_startPoint, const PointAssign &a_endPoint);
		protected:
			Line(Draw2D *a_renderer):
				Node(a_renderer){

				points.resize(2);
			}
		private:
			virtual void drawImplementation();
		};

		template<typename PointAssign>
		void Line::applyToEnds(const PointAssign &a_startPoint, const PointAssign &a_endPoint){
			alertParent("NeedsSort");
			points[0] = a_startPoint;
			points[1] = a_endPoint;
		}

		class Rectangle : public Node{
			friend Node;
		public:
			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer);
			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer, const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight);
			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer, const Point<> &a_topLeft, const Point<> &a_topRight);
			static std::shared_ptr<Rectangle> make(Draw2D* a_renderer, const Point<> &a_point, Size<> &a_size, bool a_center = false);

			virtual ~Rectangle(){ ; }

			void setTwoCorners(const DrawPoint &a_topLeft, const DrawPoint &a_bottomRight);
			void setTwoCorners(const Point<> &a_topLeft, const Point<> &a_bottomRight);

			void setTwoCorners(const BoxAABB &a_bounds);

			void setSizeAndLocation(const Point<> &a_centerPoint, const Size<> &a_size);
			void setSizeAndCornerLocation(const Point<> &a_cornerPoint, const Size<> &a_size);

			template<typename PointAssign>
			void applyToCorners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight);

			virtual void clearTextureCoordinates();
			virtual void updateTextureCoordinates();
		protected:
			Rectangle(Draw2D *a_renderer):Node(a_renderer){
				points.resize(4);
				clearTextureCoordinates();
			}
		private:
			virtual void drawImplementation();
		};

		template<typename PointAssign>
		void Rectangle::applyToCorners(const PointAssign & a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomRight, const PointAssign & a_BottomLeft){
			double originalDepth = getDepth();
			points[0] = a_TopLeft;
			points[1] = a_BottomLeft;
			points[2] = a_BottomRight;
			points[3] = a_TopRight;
			if(originalDepth != getDepth()){
				alertParent("NeedsSort");
			}
		}
	}

}

#endif
