/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
| Primitive drawing shapes go here.  Common notation for	|
| lists of points is start at the top left corner and go	|
| clockwise (so 1 = top left, 2 = top right, 3 = bot right |
| 4 = bot left)														  |
\**********************************************************/

#ifndef _DRAWSHAPES_H_
#define _DRAWSHAPES_H_

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
		BoxAABB(){initialize(Point());}
		BoxAABB(const Point &a_startPoint){initialize(a_startPoint);}
		BoxAABB(const Point &a_startPoint, const Point &a_endPoint){initialize(a_startPoint, a_endPoint);}

		void initialize(const Point &a_startPoint);
		void initialize(const BoxAABB &a_startBox);
		void initialize(const Point &a_startPoint, const Point &a_endPoint);
		void expandWith(const Point &a_comparePoint);
		void expandWith(const BoxAABB &a_compareBox);

		bool isEmpty() const{return minPoint == maxPoint;}
		bool flatWidth() const{return minPoint.x == maxPoint.x;}
		bool flatHeight() const{return minPoint.y == maxPoint.y;}

		double getWidth() const{return (maxPoint-minPoint).x;}
		double getHeight() const{return (maxPoint-minPoint).y;}

		//includes z
		bool pointContainedZ(const Point &a_comparePoint) const;

		//ignores z
		bool pointContained(const Point &a_comparePoint) const;

		void sanitize();

		Point centerPoint(){return minPoint + ((minPoint + maxPoint) / 2.0);}
		Point minPoint, maxPoint;
	};

	std::ostream& operator<<(std::ostream& a_os, const BoxAABB& a_point);
	std::istream& operator>>(std::istream& a_is, BoxAABB& a_point);

	class PointVolume{
	public:
		void addPoint(const Point &a_newPoint);

		//ignores z
		bool pointContained(const Point &a_comparePoint);
		
		bool volumeCollision(PointVolume &a_compareVolume, Draw2D* a_renderer);
		Point getCenter();
		BoxAABB getAABB();

		std::vector<Point> points;
	private:
		//get angle within proper range between two points (-pi to +pi)
		double getAngle(const Point &a_p1, const Point &a_p2);
	};

	typedef Point AxisAngles;
	typedef Point AxisMagnitude;

	//Abstract Base Class
	class DrawNode : public std::enable_shared_from_this<DrawNode> {
	public:
		DrawNode():renderer(nullptr),myParent(nullptr),hasTexture(false),depthOverride(false),drawSorted(true),isSorted(false){}
		DrawNode(Draw2D* a_renderer):renderer(a_renderer),myParent(nullptr),hasTexture(false),depthOverride(false),drawSorted(true),isSorted(false){}
		virtual ~DrawNode(){}

		//Must be set or initialized in the constructor
		virtual void setRenderer(Draw2D* a_renderer){renderer = a_renderer;}
		Draw2D* getRenderer(){return renderer;}

		virtual void clear();

		//For composite collections
		template<typename TypeToAdd>
		std::shared_ptr<TypeToAdd> add(std::shared_ptr<TypeToAdd> a_childItem, const std::string &a_childId) {
			alertParent("NeedsSort");
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
			auto newChild = std::make_shared<TypeToMake>();
			add(newChild, a_childId);
			return newChild;
		}

		bool remove(std::shared_ptr<DrawNode> a_childItem);
		bool remove(const std::string &a_childId);

		std::shared_ptr<DrawNode> get(const std::string& a_childId);
		//convenient return value conversion get<DrawShapeType>(childId)
		template<typename DerivedClass>
		std::shared_ptr<DerivedClass> get(const std::string& a_childId){
			return get(a_childId)->getThis<DerivedClass>();
		}

		void draw();
		double getDepth();

		//Axis Aligned Bounding Box
		virtual BoxAABB getWorldAABB(bool includeChildren = true);
		virtual BoxAABB getScreenAABB(bool includeChildren = true);
		virtual BoxAABB getLocalAABB();

		//Point conversion
		Point worldFromLocal(const Point &a_local);
		Point screenFromLocal(const Point &a_local);
		Point localFromScreen(const Point &a_screen);
		Point localFromWorld(const Point &a_world);

		//If you have several points this is way more efficient.
		std::vector<Point> worldFromLocal(std::vector<Point> a_local);
		std::vector<Point> screenFromLocal(std::vector<Point> a_local);
		std::vector<Point> localFromScreen(std::vector<Point> a_screen);
		std::vector<Point> localFromWorld(std::vector<Point> a_world);

		BoxAABB worldFromLocal(BoxAABB a_local);
		BoxAABB screenFromLocal(BoxAABB a_local);
		BoxAABB localFromScreen(BoxAABB a_screen);
		BoxAABB localFromWorld(BoxAABB a_world);

		//For command type alerts
		void setParent(DrawNode* a_parentItem);

		void scale(double a_newScale);
		void scale(const Point &a_scaleValue);
		void incrementScale(double a_newScale);
		void incrementScale(const AxisMagnitude &a_scaleValue);

		//Rotation is degrees around each axis from 0 to 360, axis info supplied 
		//Typically the z axis is the only visible rotation axis, but to allow for
		//2d objects not being displayed orthographically to also twist we have
		//the full rotational axis available as well.
		void setRotate(double a_zRotation){rotateTo.z = a_zRotation;}
		void incrementRotate(double a_zRotation){rotateTo.z+= a_zRotation;}
		void setRotate(const AxisAngles &a_rotation){rotateTo = a_rotation;}
		void incrementRotate(const AxisAngles &a_rotation){rotateTo+= a_rotation;}

		void setRotateOrigin(const Point &a_origin){rotateOrigin = a_origin;}
		void centerRotateOrigin(){
			rotateOrigin = getLocalAABB().centerPoint();
		}

		void translate(const Point &a_translation){translateTo+= a_translation;}
		void placeAt(const Point &a_placement){translateTo = a_placement;}

		//By default the sort depth is calculated by averaging the z values of all points
		//setSortDepth manually overrides this calculation.  unsetSortDepth removes this override.
		void setSortDepth(double a_newDepth){
			if(a_newDepth != getDepth()){
				alertParent("NeedsSort");
			}
			depthOverride = true;
			overrideDepthValue = a_newDepth;
		}
		void unsetSortDepth(){
			double oldDepth = getDepth();
			depthOverride = false;
			if(oldDepth != getDepth()){
				alertParent("NeedsSort");
			}
		}
		//true to render the sorted list, false to render unordered.  Default behavior is true.
		void sortScene(bool a_depthMatters);

		Point getLocation();
		Point getRotation();
		Point getScale();

		virtual Point getRelativeLocation();
		virtual Point getRelativeRotation();
		virtual Point getRelativeScale();
		
		virtual void setColor(const Color &a_newColor);

		void setTexture(const GLuint *a_textureId);
		void removeTexture();

		//Used to determine sorting based on getDepth
		bool operator<(DrawNode &a_other);
		bool operator>(DrawNode &a_other);
		bool operator==(DrawNode &a_other);

		//Retrieve and convert this to the desired type, argument required for gathering the type.
		//example call DrawRectangle *rect = ourScene->getChild("id")->getThis<DrawRectangle>();
		template<typename DerivedClass>
		std::shared_ptr<DerivedClass> getThis(){
			require((typeid(DerivedClass) == typeid(*this)), PointerException(std::string("DrawShape::getThis() was called with the wrong type info supplied.  Requested conversion: (") + typeid(DerivedClass).name() + ") Actual type: (" + typeid (*this).name() + ")"));
			return std::dynamic_pointer_cast<DerivedClass>(shared_from_this());
		}
	protected:
		void defaultDraw(GLenum drawType);
		void sortedRender();
		void unsortedRender();

		//Command passing
		virtual std::string getMessage(const std::string &a_message);
		void alertParent(const std::string &a_message){if(myParent!=nullptr){myParent->getMessage(a_message);}}
		
		void pushMatrix();
		void popMatrix();

		std::shared_ptr<std::vector<GLfloat>> getPositionVertexArray();
		std::shared_ptr<std::vector<GLfloat>> getTextureVertexArray();
		std::shared_ptr<std::vector<GLfloat>> getColorVertexArray();

		Point scaleTo;
		Point translateTo;
		Point rotateTo;
		Point rotateOrigin;

		bool depthOverride;
		double overrideDepthValue;

		bool hasTexture;
		bool needsBoundRecalculate;
		const GLuint *texture;
		std::vector<DrawPoint> points;
		DrawNode *myParent;

		Draw2D *renderer;

		GLenum drawType;

		bool drawSorted, isSorted;
		typedef std::vector<std::shared_ptr<DrawNode>> DrawListVectorType;
		DrawListVectorType drawListVector;
		typedef std::map<std::string, std::shared_ptr<DrawNode>> DrawListType;
		DrawListType drawList;

	private:
		virtual void drawImplementation(){} //override this in subclasses
		void defaultDrawRenderStep(GLenum drawType);
		void bindOrDisableTexture(const std::shared_ptr<std::vector<GLfloat>> &texturePoints);
	};

	class DrawPixel : public DrawNode{
	public:
		DrawPixel(){
			points.resize(1);
		}
		DrawPixel(Draw2D *a_renderer):DrawNode(a_renderer){
			points.resize(1);
		}
		virtual ~DrawPixel(){}

		void setPoint(const DrawPoint &a_point);

		template<typename PointAssign>
		void applyToPoint(const PointAssign &a_values);

	private:
		virtual void drawImplementation();
	};

	template<typename PointAssign>
	void DrawPixel::applyToPoint(const PointAssign &a_values){
		alertParent("NeedsSort");
		points[0] = a_values;
	}

	class DrawLine : public DrawNode{
	public:
		DrawLine(){
			points.resize(2);
		}
		DrawLine(Draw2D *a_renderer):
			DrawNode(a_renderer){
			
			points.resize(2);
		}
	  
		virtual ~DrawLine(){}

		void setEnds(const DrawPoint &a_startPoint, const DrawPoint &a_endPoint);

		template<typename PointAssign>
		void applyToEnds(const PointAssign &a_startPoint, const PointAssign &a_endPoint);
	private:
		virtual void drawImplementation();
	};

	template<typename PointAssign>
	void DrawLine::applyToEnds(const PointAssign &a_startPoint, const PointAssign &a_endPoint){
		alertParent("NeedsSort");
		points[0] = a_startPoint;
		points[1] = a_endPoint;
	}

	class DrawRectangle : public DrawNode{
	public:
		DrawRectangle(){
			points.resize(4);
			resetTextureCoordinates();
		}
		DrawRectangle(Draw2D *a_renderer):DrawNode(a_renderer){
			points.resize(4);
			resetTextureCoordinates();
		}
		virtual ~DrawRectangle(){;}

		void setTwoCorners(const DrawPoint &a_TopLeft, const DrawPoint &a_TopRight);
		void setTwoCorners(const Point &a_TopLeft, const Point &a_TopRight);

		void setTwoCorners(const BoxAABB &a_bounds);

		void setSizeAndLocation(const Point &a_CenterPoint, double a_width, double a_height);
		void setSizeAndCornerLocation(const Point &a_TopLeft, double a_width, double a_height);

		template<typename PointAssign>
		void applyToCorners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight);

		void resetTextureCoordinates();
	private:
		virtual void drawImplementation();
	};

	template<typename PointAssign>
	void DrawRectangle::applyToCorners(const PointAssign & a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomRight, const PointAssign & a_BottomLeft){
		double originalDepth = getDepth();
		points[0] = a_TopLeft;
		points[1] = a_BottomLeft;
		points[2] = a_BottomRight;
		points[3] = a_TopRight;
		if(originalDepth != getDepth()){
			alertParent("NeedsSort");
		}
	}

	void AssignTextureToRectangle(DrawRectangle &a_rectangle, const SubTexture *a_texture, bool a_resize = true, bool a_flip = false);
	void AssignTextureToRectangle(DrawRectangle &a_rectangle, const MainTexture *a_texture, bool a_resize = true, bool a_flip = false);
	void AssignTextureToRectangle(DrawRectangle &a_rectangle, const GLuint *a_texture, bool a_flip = false);

}

#endif
