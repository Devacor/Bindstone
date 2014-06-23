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
#define SCENE_MAKE_FACTORY_METHODS(T)	\
	template < typename TypeToMake, typename FirstParameter, typename ...Arg, \
	typename std::enable_if< \
	!std::is_same<FirstParameter, const char*>::value && \
	!std::is_same<FirstParameter, std::string>::value, \
	int \
	>::type = 0 \
	> \
	std::shared_ptr<TypeToMake> make(FirstParameter a_firstParam, Arg... a_parameters) { \
	auto newChild = TypeToMake::make(renderer, a_firstParam, std::forward<Arg>(a_parameters)...); \
	add(guid(), newChild); \
	return newChild; \
	} \
	\
	template < typename TypeToMake, typename FirstParameter, typename ...Arg, \
	typename std::enable_if< \
	std::is_same<FirstParameter, const char*>::value || \
	std::is_same<FirstParameter, std::string>::value, \
	int \
	>::type = 0 \
	> \
	std::shared_ptr<TypeToMake> make(FirstParameter a_childId, Arg... a_parameters) { \
	auto newChild = TypeToMake::make(renderer, std::forward<Arg>(a_parameters)...); \
	add(a_childId, newChild); \
	return newChild; \
	} \
	std::shared_ptr<T> parent(Node* a_parentItem){ return std::static_pointer_cast<T>(parentImplementation(a_parentItem)); } \
	std::shared_ptr<T> removeFromParent(){ return std::static_pointer_cast<T>(removeFromParentImplementation()); } \
	std::shared_ptr<T> position(const Point<> &a_rhs){ return std::static_pointer_cast<T>(positionImplementation(a_rhs)); } \
	std::shared_ptr<T> scale(PointPrecision a_newScale){ return std::static_pointer_cast<T>(scaleImplementation(a_newScale)); } \
	std::shared_ptr<T> scale(const AxisMagnitude &a_scaleValue){ return std::static_pointer_cast<T>(scaleImplementation(a_scaleValue)); } \
	std::shared_ptr<T> rotation(PointPrecision a_zRotation){ return std::static_pointer_cast<T>(rotationImplementation(a_zRotation)); } \
	std::shared_ptr<T> rotation(const AxisAngles &a_rotation){ return std::static_pointer_cast<T>(rotationImplementation(a_rotation)); } \
	std::shared_ptr<T> shader(const std::string &a_id){ return std::static_pointer_cast<T>(shaderImplementation(a_id)); } \
	std::shared_ptr<T> texture(std::shared_ptr<TextureHandle> a_texture){ return std::static_pointer_cast<T>(textureImplementation(a_texture)); } \
	std::shared_ptr<T> clearTexture(){ return std::static_pointer_cast<T>(clearTextureImplementation()); } \
	std::shared_ptr<T> color(const Color &a_newColor){ return std::static_pointer_cast<T>(colorImplementation(a_newColor)); } \
	std::shared_ptr<T> sortScene(bool a_depthMatters){ return std::static_pointer_cast<T>(sortSceneImplementation(a_depthMatters)); } \
	std::shared_ptr<T> sortDepth(PointPrecision a_newDepth){ return std::static_pointer_cast<T>(sortDepthImplementation(a_newDepth)); } \
	std::shared_ptr<T> defaultSortDepth(){ return std::static_pointer_cast<T>(defaultSortDepthImplementation()); } \
	std::shared_ptr<T> hide(){ return std::static_pointer_cast<T>(hideImplementation()); } \
	std::shared_ptr<T> show(){ return std::static_pointer_cast<T>(showImplementation()); } \
	std::shared_ptr<Node> blockSerialize(){ return std::static_pointer_cast<T>(blockSerializeImplementation()); } \
	std::shared_ptr<Node> allowSerialize(){ return std::static_pointer_cast<T>(allowSerializeImplementation()); } \
	std::shared_ptr<Node> parent() const{ return parentImplementation(); } \
	AxisMagnitude scale() const { return scaleImplementation(); } \
	virtual Point<> position() const{ return positionImplementation(); } \
	AxisAngles rotation() const{ return rotationImplementation(); } \
	virtual Color color() const { return colorImplementation(); } \
	std::shared_ptr<TextureHandle> texture() const{ return textureImplementation(); } \
	std::string shader() const { return shaderImplementation(); }

	namespace Scene {
		class Node : 
			public std::enable_shared_from_this<Node>,
			public MessageHandler<PushMatrix>,
			public MessageHandler<PopMatrix>,
			public MessageHandler<SetShader>{
			friend cereal::access;
		public:
			virtual ~Node(){
				myParent = nullptr;
				for(auto &child : drawList){
					child.second->parent(nullptr);
				}
			}

			SCENE_MAKE_FACTORY_METHODS(Node)

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
				if(a_childItem->parent().get() == this){
					remove(a_childItem);
				}
				isSorted = 0;
				drawListVector.clear();
				if(renderer){
					a_childItem->setRenderer(renderer, true, false);
				}
				a_childItem->parent(this);
				drawList[a_childId] = a_childItem;
				alertParent(ChildAdded::make(shared_from_this(), a_childItem));
				alertParent(VisualChange::make(shared_from_this()));
				a_childItem->onAdded(shared_from_this());
				onChildAdded(a_childItem);
				return a_childItem;
			}

			std::shared_ptr<Node> remove(std::shared_ptr<Node> a_childItem);
			std::shared_ptr<Node> remove(const std::string &a_childId);

			void draw();
			PointPrecision getDepth();

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

			AxisMagnitude incrementScale(PointPrecision a_newScale);
			AxisMagnitude incrementScale(const AxisMagnitude &a_scaleValue);

			//Rotation is degrees around each axis from 0 to 360, axis info supplied typically the z axis
			//is the only visible rotation axis, so we default to that with a single value supplied;
			PointPrecision incrementRotation(PointPrecision a_zRotation);
			AxisAngles incrementRotation(const AxisAngles &a_rotation);

			Point<> rotationOrigin(const Point<> &a_origin);
			Point<> centerRotationOrigin();

			Point<> translate(const Point<> &a_translation);

			virtual void clearTextureCoordinates(){ }
			virtual void updateTextureCoordinates(){ }

			bool visible() const;

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

			virtual void handleBegin(std::shared_ptr<SetShader>){
			}
			virtual void handleEnd(std::shared_ptr<SetShader> a_message){
				shader(a_message->shaderId);
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

			void sortLess(){
				isSorted = isSorted && shouldSortLessThan;
				shouldSortLessThan = true;
				sortFunction = [](DrawListVectorType::value_type one, DrawListVectorType::value_type two){
					return *one.lock() < *two.lock();
				};
			}
			void sortGreater(){
				isSorted = isSorted && !shouldSortLessThan;
				shouldSortLessThan = false;
				sortFunction = [](DrawListVectorType::value_type one, DrawListVectorType::value_type two){
					return *one.lock() > *two.lock();
				};
			}
		protected:
			virtual std::shared_ptr<Node> parentImplementation(Node* a_parentItem);
			virtual std::shared_ptr<Node> removeFromParentImplementation();
			virtual std::shared_ptr<Node> positionImplementation(const Point<> &a_rhs);
			virtual std::shared_ptr<Node> scaleImplementation(PointPrecision a_newScale);
			virtual std::shared_ptr<Node> scaleImplementation(const AxisMagnitude &a_scaleValue);
			virtual std::shared_ptr<Node> rotationImplementation(PointPrecision a_zRotation);
			virtual std::shared_ptr<Node> rotationImplementation(const AxisAngles &a_rotation);
			virtual std::shared_ptr<Node> shaderImplementation(const std::string &a_id);
			virtual std::shared_ptr<Node> textureImplementation(std::shared_ptr<TextureHandle> a_texture);
			virtual std::shared_ptr<Node> clearTextureImplementation();
			virtual std::shared_ptr<Node> colorImplementation(const Color &a_newColor);
			virtual std::shared_ptr<Node> sortSceneImplementation(bool a_depthMatters);
			virtual std::shared_ptr<Node> sortDepthImplementation(PointPrecision a_newDepth);
			virtual std::shared_ptr<Node> defaultSortDepthImplementation();
			virtual std::shared_ptr<Node> hideImplementation();
			virtual std::shared_ptr<Node> showImplementation();
			std::shared_ptr<Node> blockSerializeImplementation();
			std::shared_ptr<Node> allowSerializeImplementation();

			std::shared_ptr<Node> parentImplementation() const;
			AxisMagnitude scaleImplementation() const;
			virtual Point<> positionImplementation() const;
			AxisAngles rotationImplementation() const;
			virtual Color colorImplementation() const;
			std::shared_ptr<TextureHandle> textureImplementation() const;
			std::string shaderImplementation() const;

			Node(Draw2D* a_renderer);

			void defaultDraw(GLenum drawType);
			void sortedRender();
			void unsortedRender();

			void alertForTextureChange(){ alertParent(VisualChange::make(shared_from_this())); }

			void pushMatrix();
			void popMatrix();

			Point<> scaleTo;
			Point<> translateTo;
			Point<> rotateTo;
			Point<> rotateOrigin;

			bool depthOverride;
			PointPrecision overrideDepthValue;

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

			Shader* shaderProgram = nullptr;
			std::string shaderProgramId;
			GLuint bufferId;
		private:
			bool markedTemporary = false;
			bool shouldSortLessThan;
			std::function<bool(DrawListVectorType::value_type, DrawListVectorType::value_type)> sortFunction;

			void childDepthChanged(){
				drawListVector.clear();
				isSorted = false;
			}

			template <class Archive>
			void serialize(Archive & archive){
				if(!markedTemporary){
					archive(CEREAL_NVP(translateTo),
						CEREAL_NVP(rotateTo), CEREAL_NVP(rotateOrigin),
						CEREAL_NVP(scaleTo),
						CEREAL_NVP(depthOverride), CEREAL_NVP(overrideDepthValue),
						CEREAL_NVP(isVisible),
						CEREAL_NVP(drawType), CEREAL_NVP(drawSorted),
						CEREAL_NVP(points),
						CEREAL_NVP(drawList),
						cereal::make_nvp("shaderId", (shaderProgram != nullptr) ? shaderProgram->id() : ""),
						cereal::make_nvp("texture", ourTexture)
						);
				}
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Node> &construct){
				Draw2D *renderer = nullptr;
				archive.extract(cereal::make_nvp("renderer", renderer));
				construct(renderer);
				std::string shaderId;
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
					cereal::make_nvp("texture", construct->ourTexture),
					cereal::make_nvp("shaderId", shaderId)
				);
				construct->shader(shaderId);
				for(auto &drawItem : construct->drawList){
					drawItem.second->parent(construct.ptr());
				}
			}

			virtual bool preDraw(){ return true; }
			virtual void postDraw(){}
			virtual void drawImplementation(){} //override this in subclasses
			virtual void onAdded(std::shared_ptr<Node> a_parent){}
			virtual void onRemoved(std::shared_ptr<Node> a_parent){}
			virtual void onChildAdded(std::shared_ptr<Node> a_child){}
			virtual void onChildRemoved(std::shared_ptr<Node> a_child){}
			void defaultDrawRenderStep(GLenum drawType);
			void bindOrDisableTexture(const std::shared_ptr<std::vector<GLfloat>> &texturePoints);
		};
	}

}

#endif
