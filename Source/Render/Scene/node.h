/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
| Primitive drawing shapes go here.  Common notation for   |
| lists of points is start at the top left corner and go   |
| clockwise (so 1 = top left, 2 = top right, 3 = bot right |
| 4 = bot left)                                            |
\**********************************************************/

#ifndef _MV_SCENE_NODE_H_
#define _MV_SCENE_NODE_H_

#define GL_GLEXT_PROTOTYPES
#define GLX_GLEXT_PROTOTYPES
#include <stdio.h>
#include <iostream>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <boost/lexical_cast.hpp>

#include "Render/render.h"
#include "Render/textures.h"
#include "Render/points.h"
#include "Render/boxaabb.h"
#include "sceneMessages.hpp"
#include "Utility/package.h"

namespace MV {
	//I had three options here: 
	//1) In derived classes specify using Node::make and then rename Node's static make function to something else so it cannot clash
	//or
	//2) Rename the member make functions to something like "create" or "add".
	//or
	//3) Redefine these two methods in each derived class and rely on method name hiding to do the right thing. A macro does this easiest.
	//
	//I chose 3 because I like the simplicity of "make" for everything and I care more about the user interface than the class definition.
	//I don't want to use two synonyms for object creation (create/make) and add should not imply creation.
#define SCENE_MAKE_FACTORY_METHODS 	\
	template<typename TypeToMake> \
	std::shared_ptr<TypeToMake> make(const std::string &a_childId) { \
		auto newChild = TypeToMake::make(renderer); \
		add(a_childId, newChild); \
		return newChild; \
	} \
	\
	template<typename TypeToMake, typename ...Arg> \
	std::shared_ptr<TypeToMake> make(const std::string &a_childId, Arg... a_parameters) { \
		auto newChild = TypeToMake::make(renderer, std::forward<Arg>(a_parameters)...); \
		add(a_childId, newChild); \
		return newChild; \
	}

	namespace Scene {
		class Node : 
			public std::enable_shared_from_this<Node>,
			public MessageHandler<PushMatrix>,
			public MessageHandler<PopMatrix>{
			friend cereal::access;
		public:
			virtual ~Node(){
				myParent = nullptr;
				for(auto &child : drawList){
					child.second->parent(nullptr);
				}
			}

			SCENE_MAKE_FACTORY_METHODS

			static std::shared_ptr<Node> make(Draw2D* a_renderer, const Point<> &a_placement = Point<>());

			//Sets the renderer
			virtual void setRenderer(Draw2D* a_renderer, bool includeChildren = true, bool includeParents = true){
				renderer = a_renderer;
				if(includeParents){
					Node* currentParent = this;
					while(currentParent = currentParent->myParent){
						setRenderer(a_renderer, false, true);
					}
				}else if(includeChildren){
					for(auto &child : drawList){
						child.second->setRenderer(a_renderer, true, false);
					}
				}
			}
			Draw2D* getRenderer() const{ return renderer; }

			virtual void clear();

			//For composite collections
			template<typename TypeToAdd>
			std::shared_ptr<TypeToAdd> add(const std::string &a_childId, std::shared_ptr<TypeToAdd> a_childItem) {
				require(a_childItem, MV::PointerException("Node::add was supplied a null child for: " + a_childId));
				isSorted = 0;
				drawListVector.clear();
				if(renderer){
					a_childItem->setRenderer(renderer, true, false);
				}
				a_childItem->parent(this);
				drawList[a_childId] = a_childItem;
				alertParent(ChildAdded::make(shared_from_this(), a_childItem));
				alertParent(VisualChange::make(shared_from_this()));
				return a_childItem;
			}

			std::shared_ptr<Node> Node::removeFromParent();
			std::shared_ptr<Node> remove(std::shared_ptr<Node> a_childItem);
			std::shared_ptr<Node> remove(const std::string &a_childId);

			void draw();
			double getDepth();

			virtual void normalizeTest(Point<> a_offset);
			virtual void normalizeToPoint(Point<> a_offset);
			virtual Point<> normalizeToTopLeft();
			virtual Point<> normalizeToCenter();

			//Axis Aligned Bounding Box
			BoxAABB worldAABB(bool a_includeChildren = true);
			BoxAABB screenAABB(bool a_includeChildren = true);
			BoxAABB localAABB(bool a_includeChildren = true);
			BoxAABB basicAABB() const;

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
			Node* parent(Node* a_parentItem); //returns Node* so that it is safe to call this method in a constructor
			std::shared_ptr<Node> parent() const;

			double scale(double a_newScale);
			AxisMagnitude scale(const AxisMagnitude &a_scaleValue);
			AxisMagnitude incrementScale(double a_newScale);
			AxisMagnitude incrementScale(const AxisMagnitude &a_scaleValue);
			AxisMagnitude scale() const;

			//Rotation is degrees around each axis from 0 to 360, axis info supplied typically the z axis
			//is the only visible rotation axis, so we default to that with a single value supplied;
			double incrementRotation(double a_zRotation);
			AxisAngles incrementRotation(const AxisAngles &a_rotation);

			double rotation(double a_zRotation);
			AxisAngles rotation(const AxisAngles &a_rotation);
			AxisAngles rotation() const;

			Point<> rotationOrigin(const Point<> &a_origin);
			Point<> centerRotationOrigin();

			//By default the sort depth is calculated by averaging the z values of all points
			//setSortDepth manually overrides this calculation.  unsetSortDepth removes this override.
			void setSortDepth(double a_newDepth){
				auto notifyOnChanged = makeScopedDepthChangeNote(this, false);
				depthOverride = true;
				overrideDepthValue = a_newDepth;
			}
			void unsetSortDepth(){
				auto notifyOnChanged = makeScopedDepthChangeNote(this, false);
				depthOverride = false;
			}
			//true to render the sorted list, false to render unordered.  Default behavior is true.
			void sortScene(bool a_depthMatters);

			Point<> position(const Point<> &a_rhs);
			Point<> position() const;

			Point<> translate(const Point<> &a_translation){
				return position(translateTo + a_translation);
			}

			virtual Color color(const Color &a_newColor);
			virtual Color color() const;
			std::shared_ptr<TextureHandle> texture(std::shared_ptr<TextureHandle> a_texture);
			std::shared_ptr<TextureHandle> texture() const;
			void clearTexture();

			virtual void clearTextureCoordinates(){ }
			virtual void updateTextureCoordinates(){ }

			bool visible() const;
			void hide();
			void show();

			//Used to determine sorting based on getDepth
			bool operator<(Node &a_other);
			bool operator>(Node &a_other);
			bool operator==(Node &a_other);
			bool operator!=(Node &a_other);

			//Retrieve and convert this to the desired type, argument required for gathering the type.
			//example call Rectangle *rect = ourScene->getChild("id")->getThis<Rectangle>();
			template<typename DerivedClass>
			std::shared_ptr<DerivedClass> get(){
				require((typeid(DerivedClass) == typeid(*this)), PointerException(std::string("DrawShape::getThis() was called with the wrong type info supplied.  Requested conversion: (") + typeid(DerivedClass).name() + ") Actual type: (" + typeid (*this).name() + ")"));
				return std::dynamic_pointer_cast<DerivedClass>(shared_from_this());
			}

			std::shared_ptr<Node> get(const std::string& a_childId, bool a_throwOnNull = true);
			//convenient return value conversion get<DrawShapeType>(childId)
			template<typename DerivedClass>
			std::shared_ptr<DerivedClass> get(const std::string& a_childId, bool a_throwOnNull = true){
				auto value = get(a_childId, a_throwOnNull);
				if(value == nullptr){
					return nullptr;
				}else{
					return value->get<DerivedClass>();
				}
			}

			std::vector<std::shared_ptr<Node>> children();

			void sortDrawListVector();


			virtual void handleBegin(std::shared_ptr<PushMatrix>){
			}
			virtual void handleEnd(std::shared_ptr<PushMatrix>){
				pushMatrix();
			}

			virtual void handleBegin(std::shared_ptr<PopMatrix>){
				popMatrix();
			}
			virtual void handleEnd(std::shared_ptr<PopMatrix>){
			}
			void shared_from_this_test(){
				auto testIt = shared_from_this();
				for(auto child : drawList){
					child.second->shared_from_this_test();
				}
			}

			void depthChanged(){
				if(myParent){
					myParent->childDepthChanged();
					alertParent(VisualChange::make(shared_from_this()));
				}
			}

			template<typename T>
			void alertParent(std::shared_ptr<T> a_message){
				if(myParent != nullptr){
					a_message->tryToHandleBegin(myParent, a_message);
					myParent->alertParent(a_message);
					a_message->tryToHandleEnd(myParent, a_message);
				}
			}

			template<typename T>
			void alertChildren(std::shared_ptr<T> a_message){
				for(auto& child : drawList){
					a_message->tryToHandleBegin(child.second.get(), a_message);
					child.second->alertChildren(a_message);
					a_message->tryToHandleEnd(child.second.get(), a_message);
				}
			}

			//would have preferred these private, but I moved them public for more flexibility as per the Button class.
			virtual BoxAABB worldAABBImplementation(bool a_includeChildren, bool a_nestedCall);
			virtual BoxAABB screenAABBImplementation(bool a_includeChildren, bool a_nestedCall);
			virtual BoxAABB localAABBImplementation(bool a_includeChildren, bool a_nestedCall);
			virtual BoxAABB basicAABBImplementation() const;

		protected:
			Node(Draw2D* a_renderer);

			void defaultDraw(GLenum drawType);
			void sortedRender();
			void unsortedRender();

			void alertForTextureChange(){ alertParent(VisualChange::make(shared_from_this())); }

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

			std::shared_ptr<TextureHandle> ourTexture;
			TextureHandle::SignalType::SharedType textureSizeSignal;
			std::vector<DrawPoint> points;
			Node *myParent;

			Draw2D *renderer;

			GLenum drawType;

			bool drawSorted, isSorted;
			typedef std::vector<std::weak_ptr<Node>> DrawListVectorType;
			DrawListVectorType drawListVector;
			typedef std::map<std::string, std::shared_ptr<Node>> DrawListType;
			typedef std::pair<std::string, std::shared_ptr<Node>> DrawListPairType;
			DrawListType drawList;

		private:

			void childDepthChanged(){
				drawListVector.clear();
				isSorted = false;
			}

			template <class Archive>
			void serialize(Archive & archive){
				archive(CEREAL_NVP(translateTo),
					CEREAL_NVP(rotateTo), CEREAL_NVP(rotateOrigin),
					CEREAL_NVP(scaleTo),
					CEREAL_NVP(depthOverride), CEREAL_NVP(overrideDepthValue),
					CEREAL_NVP(isVisible),
					CEREAL_NVP(drawType), CEREAL_NVP(drawSorted),
					CEREAL_NVP(points),
					CEREAL_NVP(drawList),
					cereal::make_nvp("texture", ourTexture)
				).extract(
					CEREAL_NVP(renderer)
				);

				for(auto &drawItem : drawList){
					drawItem.second->parent(this);
				}
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Node> &construct){
				construct(nullptr);
				archive(
					cereal::make_nvp("translateTo", construct->translateTo),
					cereal::make_nvp("rotateTo", construct->rotateTo),
					cereal::make_nvp("rotateOrigin", construct->rotateOrigin),
					cereal::make_nvp("scaleTo", construct->scaleTo),
					cereal::make_nvp("depthOverride", construct->depthOverride),
					cereal::make_nvp("overrideDepthValue", construct->overrideDepthValue),
					cereal::make_nvp("isVisible", construct->isVisible),
					cereal::make_nvp("drawType", construct->drawType),
					cereal::make_nvp("drawSorted", construct->drawSorted),
					cereal::make_nvp("points", construct->points),
					cereal::make_nvp("drawList", construct->drawList),
					cereal::make_nvp("texture", construct->ourTexture)
				).extract(
					cereal::make_nvp("renderer", construct->renderer)
				);

				for(auto &drawItem : construct->drawList){
					drawItem.second->parent(construct.ptr());
				}
			}

			virtual bool preDraw(){ return true; }
			virtual void postDraw(){}
			virtual void drawImplementation(){} //override this in subclasses
			virtual void onAdded(){}
			virtual void onRemoved(){}
			void defaultDrawRenderStep(GLenum drawType);
			void bindOrDisableTexture(const std::shared_ptr<std::vector<GLfloat>> &texturePoints);
		};
	}

}

#endif
