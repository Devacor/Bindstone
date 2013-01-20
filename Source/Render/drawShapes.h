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
		bool pointContained(const Point &a_comparePoint) const;

		//ignores z
		bool pointContainedXY(const Point &a_comparePoint) const;

		Point centerPoint(){return minPoint + ((minPoint + maxPoint) / 2.0);}
		Point minPoint, maxPoint;
	};

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
		double getAngle(double a_x1, double a_y1, double a_x2, double a_y2);
	};

	typedef Point AxisAngles;
	typedef Point AxisMagnitude;

	//Abstract Base Class
	class DrawShape : public std::enable_shared_from_this<DrawShape> {
	public:
		DrawShape():renderer(nullptr),myParent(nullptr),hasTexture(false),depthOverride(false){}
		DrawShape(Draw2D* a_renderer):renderer(a_renderer),myParent(nullptr),hasTexture(false),depthOverride(false){}
		virtual ~DrawShape(){}

		//Must be set or initialized in the constructor
		virtual void setRenderer(Draw2D* a_renderer){renderer = a_renderer;}
		Draw2D* getRenderer(){return renderer;}

		//For composite collections
		virtual std::shared_ptr<DrawShape> add(std::shared_ptr<DrawShape> a_childItem, std::string a_childId){return nullptr;}
		virtual bool remove(std::shared_ptr<DrawShape> a_childItem, bool a_deleteOnRemove = true){return false;}
		virtual bool remove(std::string a_childId, bool a_deleteOnRemove = true){return false;}

		virtual void draw() = 0;
		virtual double getDepth();

		//Axis Aligned Bounding Box
		virtual BoxAABB getWorldAABB();
		virtual BoxAABB getLocalAABB();

		//Get all points
		virtual PointVolume getWorldPoints();
		virtual Point getWorldPoint(Point a_local);
		//For command type alerts
		void setParent(DrawShape* a_parentItem);

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
		bool operator<(DrawShape &a_other);
		bool operator>(DrawShape &a_other);
		bool operator==(DrawShape &a_other);

		//Retrieve and convert this to the desired type, argument required for gathering the type.
		//example call DrawRectangle *rect = ourScene->getChild("id")->getThis<DrawRectangle>();
		template<typename DerivedClass>
		std::shared_ptr<DerivedClass> getThis(){
			require((typeid(DerivedClass) == typeid(*this)), PointerException(std::string("DrawShape::getThis() was called with the wrong type info supplied.  Requested conversion: (") + typeid(DerivedClass).name() + ") Actual type: (" + typeid (*this).name() + ")"));
			return std::dynamic_pointer_cast<DerivedClass>(shared_from_this());
		}
	protected:
		void defaultDraw(GLenum drawType);

		//Command passing
		virtual std::string getMessage(const std::string &a_message){return "UNHANDLED";}
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
		std::vector<DrawPoint> Pnt;
		DrawShape *myParent;

		Draw2D *renderer;

		GLenum drawType;

	private:
		void defaultDrawRenderStep(GLenum drawType);
		void bindOrDisableTexture(const std::shared_ptr<std::vector<GLfloat>> &texturePoints);
	};

	class DrawPixel : public DrawShape{
	public:
		DrawPixel(){
			Pnt.resize(1);
		}
		DrawPixel(Draw2D *a_renderer):DrawShape(a_renderer){
			Pnt.resize(1);
		}
		virtual ~DrawPixel(){}

		virtual void draw();

		void setPoint(const DrawPoint &a_point);

		template<typename PointAssign>
		void applyToPoint(const PointAssign &a_values);
	};

	template<typename PointAssign>
	void DrawPixel::applyToPoint(const PointAssign &a_values){
		alertParent("NeedsSort");
		Pnt[0] = a_values;
	}

	class DrawLine : public DrawShape{
	public:
		DrawLine(){
			Pnt.resize(2);
		}
		DrawLine(Draw2D *a_renderer):
			DrawShape(a_renderer){
			
			Pnt.resize(2);
		}
	  
		virtual ~DrawLine(){}

		virtual void draw();

		void setEnds(const DrawPoint &a_startPoint, const DrawPoint &a_endPoint);

		template<typename PointAssign>
		void applyToEnds(const PointAssign &a_startPoint, const PointAssign &a_endPoint);
	};

	template<typename PointAssign>
	void DrawLine::applyToEnds(const PointAssign &a_startPoint, const PointAssign &a_endPoint){
		alertParent("NeedsSort");
		Pnt[0] = a_startPoint;
		Pnt[1] = a_endPoint;
	}

	class DrawRectangle : public DrawShape{
	public:
		DrawRectangle(){
			Pnt.resize(4);
			resetTextureCoordinates();
		}
		DrawRectangle(Draw2D *a_renderer):DrawShape(a_renderer){
			Pnt.resize(4);
			resetTextureCoordinates();
		}
		virtual ~DrawRectangle(){;}

		virtual void draw();

		void setTwoCorners(const DrawPoint &a_TopLeft, const DrawPoint &a_TopRight);
		void setSizeAndLocation(const DrawPoint &a_CenterPoint, double a_width, double a_height);
		void setSizeAndCornerLocation(const DrawPoint &a_TopLeft, double a_width, double a_height);

		template<typename PointAssign>
		void applyToCorners(const PointAssign &a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomLeft, const PointAssign & a_BottomRight);

		void resetTextureCoordinates();
	};

	template<typename PointAssign>
	void DrawRectangle::applyToCorners(const PointAssign & a_TopLeft, const PointAssign & a_TopRight, const PointAssign & a_BottomRight, const PointAssign & a_BottomLeft){
		alertParent("NeedsSort");
		Pnt[0] = a_TopLeft;
		Pnt[1] = a_BottomLeft;
		Pnt[2] = a_BottomRight;
		Pnt[3] = a_TopRight;
	}

	void AssignTextureToRectangle(DrawRectangle &a_rectangle, const SubTexture *a_texture, bool a_flip = false);
	void AssignTextureToRectangle(DrawRectangle &a_rectangle, const MainTexture *a_texture, bool a_flip = false);
	void AssignTextureToRectangle(DrawRectangle &a_rectangle, const GLuint *a_texture, bool a_flip = false);

	class Scene : public DrawShape{
	public:
		Scene();
		Scene(Draw2D *a_renderer);
		virtual ~Scene();

		virtual void setRenderer(Draw2D *a_renderer);

		std::shared_ptr<DrawShape> add(std::shared_ptr<DrawShape> a_childItem, const std::string &a_childId);

		template<typename TypeToMake>
		std::shared_ptr<TypeToMake> make(const std::string &a_childId) {
			auto newChild = std::make_shared<TypeToMake>();
			add(newChild, a_childId);
			return newChild;
		}

		virtual bool remove(std::shared_ptr<DrawShape> a_childItem);
		virtual bool remove(const std::string &a_childId);

		virtual void clear();

		virtual std::shared_ptr<DrawShape> get(const std::string& a_childId);
		//convenient return value conversion get<DrawShapeType>(childId)
		template<typename DerivedClass>
		std::shared_ptr<DerivedClass> get(const std::string& a_childId){
			return get(a_childId)->getThis<DerivedClass>();
		}

		virtual double getDepth();

		virtual BoxAABB getWorldAABB();
		virtual BoxAABB getLocalAABB();

		virtual void setColor(Color a_newColor);

		virtual void draw();

		//true to render the sorted list, false to render unordered.  Default behavior is true.
		void sortScene(bool a_depthMatters);
	protected:
		virtual std::string getMessage(const std::string &a_message);

		bool drawSorted, isSorted;
		typedef std::vector<std::shared_ptr<DrawShape>> DrawListVectorType;
		DrawListVectorType DrawListVector;
		typedef std::map<std::string, std::shared_ptr<DrawShape>> DrawListType;
		DrawListType DrawList;

	private:
		void sortedRender();
		void unsortedRender();
	};

}

#endif
