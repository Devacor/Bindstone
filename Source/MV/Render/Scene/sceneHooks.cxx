#include "button.h"
#include "emitter.h"
#include "path.h"
#include "spineMV.h"
#include "parallax.h"
#include "text.h"

namespace MV {
	using namespace MV::Scene;
	Script::Registrar<Button> _hookButton([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		MV::TapDevice* tapDevice = a_services.get<MV::TapDevice>();

		a_script.add(chaiscript::user_type<Button>(), "Button");
		a_script.add(chaiscript::base_class<Clickable, Button>());
		a_script.add(chaiscript::base_class<Sprite, Button>());
		a_script.add(chaiscript::base_class<Drawable, Button>());
		a_script.add(chaiscript::base_class<Component, Button>());

		a_script.add(chaiscript::fun([=](Node& a_self) { return a_self.attach<Button>(*tapDevice); }), "attachButton");

		a_script.add(chaiscript::fun([](Node& a_self) { return a_self.componentInChildren<Button>(true, false, true); }), "componentButton");

		a_script.add(chaiscript::fun([](Button& a_self, const std::string& a_newValue) { return a_self.text(a_newValue); }), "text");
		a_script.add(chaiscript::fun([](Button& a_self) { return a_self.text(); }), "text");

		a_script.add(chaiscript::fun([](Button& a_self, const std::shared_ptr<Node>& a_activeView) { return a_self.activeNode(a_activeView); }), "activeNode");
		a_script.add(chaiscript::fun([](Button& a_self) { return a_self.activeNode(); }), "activeNode");

		a_script.add(chaiscript::fun([](Button& a_self, const std::shared_ptr<Node>& a_disabledView) { return a_self.disabledNode(a_disabledView); }), "disabledNode");
		a_script.add(chaiscript::fun([](Button& a_self) { return a_self.disabledNode(); }), "disabledNode");

		a_script.add(chaiscript::fun([](Button& a_self, const std::shared_ptr<Node>& a_idleView) { return a_self.idleNode(a_idleView); }), "idleNode");
		a_script.add(chaiscript::fun([](Button& a_self) { return a_self.idleNode(); }), "idleNode");

		a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Button>>([](const SafeComponent<Button>& a_item) { return a_item.self(); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Clickable>>([](const SafeComponent<Button>& a_item) { return std::static_pointer_cast<Clickable>(a_item.self()); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Sprite>>([](const SafeComponent<Button>& a_item) { return std::static_pointer_cast<Sprite>(a_item.self()); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Drawable>>([](const SafeComponent<Button>& a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Button>, std::shared_ptr<Component>>([](const SafeComponent<Button>& a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
	});

	Script::Registrar<Clickable> _hookClickable([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		MV::TapDevice* tapDevice = a_services.get<MV::TapDevice>();

		a_script.add(chaiscript::user_type<Clickable>(), "Clickable");
		a_script.add(chaiscript::base_class<Sprite, Clickable>());
		a_script.add(chaiscript::base_class<Drawable, Clickable>());
		a_script.add(chaiscript::base_class<Component, Clickable>());

		a_script.add(chaiscript::fun([=](Node& a_self) { return a_self.attach<Clickable>(*tapDevice); }), "attachClickable");

		a_script.add(chaiscript::fun([](Node& a_self) { return a_self.componentInChildren<Clickable>(true, false, true); }), "componentClickable");

		a_script.add(chaiscript::fun(&Clickable::eatingTouches), "eatingTouches");
		a_script.add(chaiscript::fun(&Clickable::startEatingTouches), "startEatingTouches");
		a_script.add(chaiscript::fun(&Clickable::stopEatingTouches), "stopEatingTouches");

		a_script.add(chaiscript::fun([](Clickable& a_self) { return a_self.clickDetectionType(); }), "clickDetectionType");
		a_script.add(chaiscript::fun([](Clickable& a_self, Clickable::BoundsType a_boundsType) { return a_self.clickDetectionType(a_boundsType); }), "clickDetectionType");

		a_script.add(chaiscript::fun(&Clickable::inPressEvent), "inPressEvent");

		a_script.add(chaiscript::fun(&Clickable::disable), "disable");

		a_script.add(chaiscript::fun(&Clickable::enabled), "enabled");
		a_script.add(chaiscript::fun(&Clickable::disabled), "disabled");

		a_script.add(chaiscript::fun(&Clickable::mouse), "mouse");

		a_script.add(chaiscript::fun(&Clickable::dragTime), "dragTime");
		a_script.add(chaiscript::fun(&Clickable::dragDelta), "dragDelta");
		a_script.add(chaiscript::fun(&Clickable::totalDragDistance), "totalDragDistance");

		a_script.add(chaiscript::fun(&Clickable::press), "press");
		a_script.add(chaiscript::fun(&Clickable::cancelPress), "cancelPress");

		a_script.add(chaiscript::fun(&Clickable::onPress), "onPress");
		a_script.add(chaiscript::fun(&Clickable::onRelease), "onRelease");
		a_script.add(chaiscript::fun(&Clickable::onAccept), "onAccept");
		a_script.add(chaiscript::fun(&Clickable::onCancel), "onCancel");
		a_script.add(chaiscript::fun(&Clickable::onDrag), "onDrag");
		a_script.add(chaiscript::fun(&Clickable::onDrop), "onDrop");

		a_script.add(chaiscript::type_conversion<SafeComponent<Clickable>, std::shared_ptr<Clickable>>([](const SafeComponent<Clickable>& a_item) { return a_item.self(); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Clickable>, std::shared_ptr<Sprite>>([](const SafeComponent<Clickable>& a_item) { return std::static_pointer_cast<Sprite>(a_item.self()); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Clickable>, std::shared_ptr<Drawable>>([](const SafeComponent<Clickable>& a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Clickable>, std::shared_ptr<Component>>([](const SafeComponent<Clickable>& a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
	});

	ScriptSignalRegistrar<Clickable::ButtonSignalSignature> _clickableButtonSignal{};
	ScriptSignalRegistrar<Clickable::DragSignalSignature> _clickableDragSignal{};
	ScriptSignalRegistrar<Clickable::DragSignalSignature> _clickableDropSignal{};

	Script::Registrar<Component> _hookComponent([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<Component>(), "Component");

		a_script.add(chaiscript::fun(&Component::task), "task");

		a_script.add(chaiscript::fun(&Component::detach), "detach");
		a_script.add(chaiscript::fun(&Component::clone), "clone");
		a_script.add(chaiscript::fun(&Component::owner), "owner");

		a_script.add(chaiscript::fun(static_cast<std::string(Component::*)() const>(&Component::id)), "id");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Component>(Component::*)(const std::string&)>(&Component::id)), "id");

		a_script.add(chaiscript::fun(static_cast<bool(Component::*)() const>(&Component::serializable)), "serializable");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Component>(Component::*)(bool)>(&Component::serializable)), "serializable");

		a_script.add(chaiscript::fun(static_cast<BoxAABB<>(Component::*)()>(&Component::bounds)), "bounds");
		a_script.add(chaiscript::fun(static_cast<BoxAABB<int>(Component::*)()>(&Component::screenBounds)), "screenBounds");
		a_script.add(chaiscript::fun(static_cast<BoxAABB<>(Component::*)()>(&Component::worldBounds)), "worldBounds");

		a_script.add(chaiscript::type_conversion<SafeComponent<Component>, std::shared_ptr<Component>>([](const SafeComponent<Component>& a_item) { return a_item.self(); }));
	});

	Script::Registrar<Drawable> _hookDrawable([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<Drawable>(), "Drawable");
		a_script.add(chaiscript::base_class<Component, Drawable>());

		a_script.add(chaiscript::fun(&Drawable::visible), "visible");
		a_script.add(chaiscript::fun(&Drawable::hide), "hide");
		a_script.add(chaiscript::fun(&Drawable::show), "show");

		a_script.add(chaiscript::fun(&Drawable::point), "point");
		a_script.add(chaiscript::fun(&Drawable::pointSize), "pointSize");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(size_t a_index, const DrawPoint & a_value)>(&Drawable::setPoint)), "setPoint");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(size_t a_index, const Color & a_value)>(&Drawable::setPoint)), "setPoint");
		a_script.add(chaiscript::fun(&Drawable::getPoints), "getPoints");

		a_script.add(chaiscript::fun(&Drawable::setPoints), "setPoints");

		a_script.add(chaiscript::fun(&Drawable::pointIndices), "pointIndices");

		a_script.add(chaiscript::fun(&Drawable::materialSettings), "materialSettings");

		a_script.add(chaiscript::fun(static_cast<Color(Drawable::*)() const>(&Drawable::color)), "color");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(const Color&)>(&Drawable::color)), "color");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(const std::vector<Color>&)>(&Drawable::colors)), "colors");

		a_script.add(chaiscript::fun([](Drawable& a_self, std::shared_ptr<TextureHandle> a_texture, size_t a_textureId) {return a_self.texture(a_texture, a_textureId); }), "texture");
		a_script.add(chaiscript::fun([](Drawable& a_self, std::shared_ptr<TextureHandle> a_texture) {return a_self.texture(a_texture); }), "texture");
		a_script.add(chaiscript::fun([](Drawable& a_self, size_t a_textureId) {return a_self.texture(a_textureId); }), "texture");
		a_script.add(chaiscript::fun([](Drawable& a_self) {return a_self.texture(); }), "texture");
		a_script.add(chaiscript::fun([](Drawable& a_self, size_t a_textureId) {return a_self.clearTexture(a_textureId); }), "clearTexture");
		a_script.add(chaiscript::fun([](Drawable& a_self) {return a_self.clearTexture(); }), "clearTexture");
		a_script.add(chaiscript::fun([](Drawable& a_self) {return a_self.clearTextures(); }), "clearTextures");

		a_script.add(chaiscript::fun(static_cast<std::string(Drawable::*)() const>(&Drawable::shader)), "shader");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Drawable>(Drawable::*)(const std::string&)>(&Drawable::shader)), "shader");

		a_script.add(chaiscript::type_conversion<SafeComponent<Drawable>, std::shared_ptr<Drawable>>([](const SafeComponent<Drawable>& a_item) { return a_item.self(); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Drawable>, std::shared_ptr<Component>>([](const SafeComponent<Drawable>& a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
	});

	Script::Registrar<ParticleChangeValues> _hookParticleChangeValues([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<ParticleChangeValues>(), "ParticleChangeValues");

		a_script.add(chaiscript::fun(&ParticleChangeValues::rateOfChange), "rateOfChange");
		a_script.add(chaiscript::fun([](ParticleChangeValues& a_self) {return a_self.directionalChangeDeg();}), "directionalChange");
		a_script.add(chaiscript::fun([](ParticleChangeValues& a_self, const AxisAngles &a_newValue) { a_self.directionalChangeDeg(a_newValue); }), "directionalChange");
		a_script.add(chaiscript::fun(&ParticleChangeValues::rotationalChange), "rotationalChange");

		a_script.add(chaiscript::fun(&ParticleChangeValues::beginSpeed), "beginSpeed");
		a_script.add(chaiscript::fun(&ParticleChangeValues::endSpeed), "endSpeed");

		a_script.add(chaiscript::fun(&ParticleChangeValues::beginColor), "beginColor");
		a_script.add(chaiscript::fun(&ParticleChangeValues::endColor), "endColor");

		a_script.add(chaiscript::fun(&ParticleChangeValues::maxLifespan), "maxLifespan");

		a_script.add(chaiscript::fun(&ParticleChangeValues::gravityMagnitude), "gravityMagnitude");
		a_script.add(chaiscript::fun(&ParticleChangeValues::gravityDirection), "gravityDirection");
	});

	Script::Registrar<Node> _hookNode([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<Node>(), "Node");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)()>(&Node::make)), "make");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const std::string&)>(&Node::make)), "make");
		a_script.add(chaiscript::fun(&Node::makeOrGet), "makeOrGet");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const std::string&, bool)>(&Node::remove)), "remove");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const std::shared_ptr<Node>&, bool)>(&Node::remove)), "remove");
		a_script.add(chaiscript::fun(&Node::removeFromParent), "removeFromParent");
		a_script.add(chaiscript::fun(&Node::clear), "clear");
		a_script.add(chaiscript::fun(&Node::root), "root");
		a_script.add(chaiscript::fun(&Node::hasImmediate), "hasImmediate");
		a_script.add(chaiscript::fun(&Node::has), "has");
		a_script.add(chaiscript::fun(&Node::hasParent), "hasParent");
		a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id) {return a_self.getParent(a_id); }), "getParent");
		a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id, bool a_throw) {return a_self.getParent(a_id, a_throw); }), "getParent");
		a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id) {return a_self.getImmediate(a_id); }), "getImmediate");
		a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id, bool a_throw) {return a_self.getImmediate(a_id, a_throw); }), "getImmediate");
		a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id) {return a_self.get(a_id); }), "get");
		a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_id, bool a_throw) {return a_self.get(a_id, a_throw); }), "get");
		a_script.add(chaiscript::fun([](Node& a_self, const std::string& a_componentId) { return a_self.componentInChildren<Component>(a_componentId, false, true); }), "component");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)() const>(&Node::parent)), "parent");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const std::shared_ptr<Node>&)>(&Node::parent)), "parent");
		a_script.add(chaiscript::fun(&Node::operator[]), "[]");
		a_script.add(chaiscript::fun(&Node::size), "size");
		a_script.add(chaiscript::fun(&Node::empty), "empty");
		a_script.add(chaiscript::fun(&Node::task), "task");
		a_script.add(chaiscript::fun(static_cast<std::string(Node::*)() const>(&Node::id)), "id");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const std::string&)>(&Node::id)), "id");
		a_script.add(chaiscript::fun(static_cast<PointPrecision(Node::*)() const>(&Node::depth)), "depth");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(PointPrecision)>(&Node::depth)), "depth");

		a_script.add(chaiscript::fun(static_cast<Point<>(Node::*)() const>(&Node::position)), "position");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Point<>&)>(&Node::position)), "position");
		a_script.add(chaiscript::fun(static_cast<Point<>(Node::*)()>(&Node::worldPosition)), "worldPosition");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Point<>&)>(&Node::worldPosition)), "worldPosition");
		a_script.add(chaiscript::fun(static_cast<Point<int>(Node::*)()>(&Node::screenPosition)), "screenPosition");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Point<int>&)>(&Node::screenPosition)), "screenPosition");

		a_script.add(chaiscript::fun(static_cast<Point<>(Node::*)(const Point<>&)>(&Node::worldFromLocal)), "worldFromLocal");
		a_script.add(chaiscript::fun(static_cast<Point<int>(Node::*)(const Point<>&)>(&Node::screenFromLocal)), "screenFromLocal");
		a_script.add(chaiscript::fun(static_cast<Point<>(Node::*)(const Point<int>&)>(&Node::localFromScreen)), "localFromScreen");
		a_script.add(chaiscript::fun(static_cast<Point<>(Node::*)(const Point<>&)>(&Node::localFromWorld)), "localFromWorld");

		a_script.add(chaiscript::fun(static_cast<std::vector<Point<>>(Node::*)(std::vector<Point<>>)>(&Node::worldFromLocal)), "worldFromLocal");
		a_script.add(chaiscript::fun(static_cast<std::vector<Point<int>>(Node::*)(const std::vector<Point<>>&)>(&Node::screenFromLocal)), "screenFromLocal");
		a_script.add(chaiscript::fun(static_cast<std::vector<Point<>>(Node::*)(const std::vector<Point<int>>&)>(&Node::localFromScreen)), "localFromScreen");
		a_script.add(chaiscript::fun(static_cast<std::vector<Point<>>(Node::*)(std::vector<Point<>>)>(&Node::localFromWorld)), "localFromWorld");

		a_script.add(chaiscript::fun(static_cast<BoxAABB<>(Node::*)(const BoxAABB<>&)>(&Node::worldFromLocal)), "worldFromLocal");
		a_script.add(chaiscript::fun(static_cast<BoxAABB<int>(Node::*)(const BoxAABB<>&)>(&Node::screenFromLocal)), "screenFromLocal");
		a_script.add(chaiscript::fun(static_cast<BoxAABB<>(Node::*)(const BoxAABB<int>&)>(&Node::localFromScreen)), "localFromScreen");
		a_script.add(chaiscript::fun(static_cast<BoxAABB<>(Node::*)(const BoxAABB<>&)>(&Node::localFromWorld)), "localFromWorld");

		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Point<>&)>(&Node::translate)), "translate");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Point<>&)>(&Node::worldTranslate)), "worldTranslate");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Point<int>&)>(&Node::screenTranslate)), "screenTranslate");

		a_script.add(chaiscript::fun(static_cast<AxisAngles(Node::*)() const>(&Node::rotation)), "rotation");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const AxisAngles&)>(&Node::rotation)), "rotation");
		a_script.add(chaiscript::fun(static_cast<AxisAngles(Node::*)() const>(&Node::worldRotation)), "worldRotation");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const AxisAngles&)>(&Node::worldRotation)), "worldRotation");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const AxisAngles&)>(&Node::addRotation)), "addRotation");

		a_script.add(chaiscript::fun(static_cast<AxisAngles(Node::*)() const>(&Node::rotationRad)), "rotationRad");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const AxisAngles&)>(&Node::rotationRad)), "rotationRad");
		a_script.add(chaiscript::fun(static_cast<AxisAngles(Node::*)() const>(&Node::worldRotationRad)), "worldRotationRad");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const AxisAngles&)>(&Node::worldRotationRad)), "worldRotationRad");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const AxisAngles&)>(&Node::addRotationRad)), "addRotationRad");

		a_script.add(chaiscript::fun(static_cast<Scale(Node::*)() const>(&Node::scale)), "scale");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Scale&)>(&Node::scale)), "scale");
		a_script.add(chaiscript::fun(static_cast<Scale(Node::*)() const>(&Node::worldScale)), "worldScale");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Scale&)>(&Node::worldScale)), "worldScale");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(const Scale&)>(&Node::addScale)), "addScale");

		a_script.add(chaiscript::fun(static_cast<bool(Node::*)() const>(&Node::visible)), "visible");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(bool)>(&Node::visible)), "visible");
		a_script.add(chaiscript::fun(static_cast<bool(Node::*)() const>(&Node::visible)), "updating");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(bool)>(&Node::visible)), "updating");
		a_script.add(chaiscript::fun(static_cast<bool(Node::*)() const>(&Node::visible)), "active");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Node>(Node::*)(bool)>(&Node::visible)), "active");

		a_script.add(chaiscript::fun(&Node::show), "show");
		a_script.add(chaiscript::fun(&Node::hide), "hide");
		a_script.add(chaiscript::fun(&Node::pause), "pause");
		a_script.add(chaiscript::fun(&Node::resume), "resume");
		a_script.add(chaiscript::fun(&Node::disable), "disable");
		a_script.add(chaiscript::fun(&Node::enable), "enable");

		a_script.add(chaiscript::fun(&Node::bounds), "bounds");
		a_script.add(chaiscript::fun(&Node::worldBounds), "worldBounds");
		a_script.add(chaiscript::fun(&Node::screenBounds), "screenBounds");

		a_script.add(chaiscript::fun(&Node::clone), "clone");
	});

	Script::Registrar<Parallax> _hookParallax([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<Parallax>(), "Parallax");
		a_script.add(chaiscript::base_class<Component, Parallax>());

		a_script.add(chaiscript::fun([](Parallax& a_self) {return a_self.translateRatio(); }), "translateRatio");
		a_script.add(chaiscript::fun([](Parallax& a_self, const MV::Point<>& a_point) {return a_self.translateRatio(a_point); }), "translateRatio");

		a_script.add(chaiscript::fun([](Parallax& a_self) {return a_self.localOffset(); }), "localOffset");
		a_script.add(chaiscript::fun([](Parallax& a_self, const MV::Point<>& a_point) {return a_self.localOffset(a_point); }), "localOffset");

		a_script.add(chaiscript::type_conversion<SafeComponent<Parallax>, std::shared_ptr<Parallax>>([](const SafeComponent<Parallax>& a_item) { return a_item.self(); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Parallax>, std::shared_ptr<Component>>([](const SafeComponent<Parallax>& a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
	});

	Script::Registrar<PathMap> _hookPath([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<PathMap>(), "PathMap");
		a_script.add(chaiscript::base_class<Drawable, PathMap>());
		a_script.add(chaiscript::base_class<Component, PathMap>());

		a_script.add(chaiscript::fun([](Node& a_self, const Size<int>& a_gridSize, bool a_useCorners) { return a_self.attach<PathMap>(a_gridSize, a_useCorners); }), "attachPathMap");
		a_script.add(chaiscript::fun([](Node& a_self, const Size<>& a_size, const Size<int>& a_gridSize, bool a_useCorners) { return a_self.attach<PathMap>(a_size, a_gridSize, a_useCorners); }), "attachPathMap");

		a_script.add(chaiscript::fun(&PathMap::inBounds), "inBounds");
		a_script.add(chaiscript::fun(&PathMap::traverseCorners), "traverseCorners");
		a_script.add(chaiscript::fun(&PathMap::resizeGrid), "resizeGrid");
		a_script.add(chaiscript::fun(&PathMap::gridSize), "gridSize");
		a_script.add(chaiscript::fun(&PathMap::blocked), "blocked");
		a_script.add(chaiscript::fun(&PathMap::staticallyBlocked), "staticallyBlocked");

		a_script.add(chaiscript::fun(static_cast<Size<>(PathMap::*)() const>(&PathMap::cellSize)), "cellSize");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathMap>(PathMap::*)(const Size<>&)>(&PathMap::cellSize)), "cellSize");

		a_script.add(chaiscript::fun(static_cast<MapNode & (PathMap::*)(const Point<int>&)>(&PathMap::nodeFromGrid)), "nodeFromGrid");
		a_script.add(chaiscript::fun(static_cast<MapNode & (PathMap::*)(const Point<>&)>(&PathMap::nodeFromGrid)), "nodeFromGrid");

		a_script.add(chaiscript::fun(static_cast<MapNode & (PathMap::*)(const Point<>&)>(&PathMap::nodeFromLocal)), "nodeFromLocal");

		a_script.add(chaiscript::fun(static_cast<Point<>(PathMap::*)(const Point<>&)>(&PathMap::gridFromLocal)), "gridFromLocal");

		a_script.add(chaiscript::fun(static_cast<Point<>(PathMap::*)(const Point<>&)>(&PathMap::localFromGrid)), "localFromGrid");
		a_script.add(chaiscript::fun(static_cast<Point<>(PathMap::*)(const Point<int>&)>(&PathMap::localFromGrid)), "localFromGrid");

		a_script.add(chaiscript::type_conversion<SafeComponent<PathMap>, std::shared_ptr<PathMap>>([](const SafeComponent<PathMap>& a_item) { return a_item.self(); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<PathMap>, std::shared_ptr<Drawable>>([](const SafeComponent<PathMap>& a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<PathMap>, std::shared_ptr<Component>>([](const SafeComponent<PathMap>& a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
	});

	Script::Registrar<AnimationEventData> _hookAnimationEventData([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<AnimationEventData>(), "AnimationEventData");

		a_script.add(chaiscript::fun(&AnimationEventData::name), "name");
		a_script.add(chaiscript::fun(&AnimationEventData::name), "stringValue");
		a_script.add(chaiscript::fun(&AnimationEventData::name), "intValue");
		a_script.add(chaiscript::fun(&AnimationEventData::name), "floatValue");
	});

	Script::Registrar<AnimationTrack> _hookAnimationTrack([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<AnimationTrack>(), "AnimationTrack");

		a_script.add(chaiscript::fun([](AnimationTrack &a_self) {
			return a_self.time();
		}), "time");
		a_script.add(chaiscript::fun([](AnimationTrack &a_self, double a_dt) {
			return &a_self.time(a_dt);
		}), "time");

		a_script.add(chaiscript::fun([](AnimationTrack &a_self) {
			return a_self.crossfade();
		}), "crossfade");
		a_script.add(chaiscript::fun([](AnimationTrack &a_self, double a_dt) {
			return &a_self.crossfade(a_dt);
		}), "crossfade");

		a_script.add(chaiscript::fun([](AnimationTrack &a_self) {
			return a_self.timeScale();
		}), "timeScale");
		a_script.add(chaiscript::fun([](AnimationTrack &a_self, double a_dt) {
			return &a_self.timeScale(a_dt);
		}), "timeScale");

		a_script.add(chaiscript::fun([](AnimationTrack &a_self, const std::string &a_animationName) {
			return &a_self.animate(a_animationName);
		}), "animate");
		a_script.add(chaiscript::fun([](AnimationTrack &a_self, const std::string &a_animationName, bool a_loop) {
			return &a_self.animate(a_animationName, a_loop);
		}), "animate");

		a_script.add(chaiscript::fun([](AnimationTrack &a_self, const std::string &a_animationName) {
			return &a_self.queueAnimation(a_animationName);
		}), "queueAnimation");
		a_script.add(chaiscript::fun([](AnimationTrack &a_self, const std::string &a_animationName, bool a_loop) {
			return &a_self.queueAnimation(a_animationName, a_loop);
		}), "queueAnimation");

		a_script.add(chaiscript::fun([](AnimationTrack &a_self, const std::string &a_animationName, double a_delay) {
			return &a_self.queueAnimation(a_animationName, a_delay);
		}), "queueAnimation");
		a_script.add(chaiscript::fun([](AnimationTrack &a_self, const std::string &a_animationName, double a_delay, bool a_loop) {
			return &a_self.queueAnimation(a_animationName, a_delay, a_loop);
		}), "queueAnimation");

		a_script.add(chaiscript::fun(&AnimationTrack::name), "name");
		a_script.add(chaiscript::fun(&AnimationTrack::trackIndex), "trackIndex");
		a_script.add(chaiscript::fun(&AnimationTrack::duration), "duration");

        a_script.add(chaiscript::fun([](AnimationTrack &a_self) {
            return &a_self.stop();
        }), "stop");

		a_script.add(chaiscript::fun(&AnimationTrack::onDispose), "onDispose");
		a_script.add(chaiscript::fun(&AnimationTrack::onStart), "onStart");
		a_script.add(chaiscript::fun(&AnimationTrack::onEnd), "onEnd");
		a_script.add(chaiscript::fun(&AnimationTrack::onComplete), "onComplete");
		a_script.add(chaiscript::fun(&AnimationTrack::onEvent), "onEvent");
	});

	ScriptSignalRegistrar<void(AnimationTrack&)> _spineAnimationTrackSignal{};
	ScriptSignalRegistrar<void(AnimationTrack&, int)> _spineAnimationTrackIntSignal{};
	ScriptSignalRegistrar<void(AnimationTrack&, const AnimationEventData&)> _spineAnimationTrackEventSignal{};

	Script::Registrar<Spine> _hookSpine([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<Spine>(), "Spine");
		a_script.add(chaiscript::base_class<Drawable, Spine>());
		a_script.add(chaiscript::base_class<Component, Spine>());

		a_script.add(chaiscript::fun([](Node& a_self, const Spine::FileBundle& a_fileBundle) {
			return a_self.attach<Spine>(a_fileBundle);
		}), "attachSpine");
		a_script.add(chaiscript::fun([](Node& a_self) {
			return a_self.attach<Spine>();
		}), "attachSpine");

		a_script.add(chaiscript::fun([](Node& a_self) {
			return a_self.componentInChildren<Spine>();
		}), "spineComponent");

		a_script.add(chaiscript::fun([](Spine& a_self) {
			return &a_self.track();
		}), "track");
		a_script.add(chaiscript::fun([](Spine& a_self, int a_index) {
			return &a_self.track(a_index);
		}), "track");
		a_script.add(chaiscript::fun([](Spine& a_self) {
			return a_self.currentTrack();
		}), "currentTrack");

		a_script.add(chaiscript::fun([](Spine& a_self, const std::string& a_animationName) {
			return a_self.animate(a_animationName);
		}), "animate");
		a_script.add(chaiscript::fun([](Spine& a_self, const std::string& a_animationName, bool a_loop) {
			return a_self.animate(a_animationName, a_loop);
		}), "animate");
		a_script.add(chaiscript::fun([](Spine& a_self, const std::string& a_animationName) {
			return a_self.queueAnimation(a_animationName);
		}), "queueAnimation");
		a_script.add(chaiscript::fun([](Spine& a_self, const std::string& a_animationName, double a_delay) {
			return a_self.queueAnimation(a_animationName, a_delay);
		}), "queueAnimation");
		a_script.add(chaiscript::fun([](Spine& a_self, const std::string& a_animationName, bool a_loop) {
			return a_self.queueAnimation(a_animationName, a_loop);
		}), "queueAnimation");
		a_script.add(chaiscript::fun([](Spine& a_self, const std::string& a_animationName, double a_delay, bool a_loop) {
			return a_self.queueAnimation(a_animationName, a_delay, a_loop);
		}), "queueAnimation");

		a_script.add(chaiscript::fun([](Spine& a_self) {
			return a_self.timeScale();
		}), "timeScale");
		a_script.add(chaiscript::fun([](Spine& a_self, double a_scale) {
			return a_self.timeScale(a_scale);
		}), "timeScale");

		a_script.add(chaiscript::fun([](Spine& a_self, const Spine::FileBundle& a_fileBundle) {
			return a_self.load(a_fileBundle);
		}), "load");
		a_script.add(chaiscript::fun(&Spine::unload), "unload");
		a_script.add(chaiscript::fun(&Spine::loaded), "loaded");

		a_script.add(chaiscript::fun(&Spine::crossfade), "crossfade");

		a_script.add(chaiscript::fun(&Spine::bundle), "bundle");

		a_script.add(chaiscript::fun(&Spine::bindNode), "bindNode");
		a_script.add(chaiscript::fun(&Spine::unbindSlot), "unbindSlot");
		a_script.add(chaiscript::fun(&Spine::unbindNode), "unbindNode");
		a_script.add(chaiscript::fun(&Spine::unbindAll), "unbindAll");

		a_script.add(chaiscript::fun(&Spine::slotPosition), "slotPosition");

		a_script.add(chaiscript::fun(&Spine::onDispose), "onDispose");
		a_script.add(chaiscript::fun(&Spine::onStart), "onStart");
		a_script.add(chaiscript::fun(&Spine::onEnd), "onEnd");
		a_script.add(chaiscript::fun(&Spine::onComplete), "onComplete");
		a_script.add(chaiscript::fun(&Spine::onEvent), "onEvent");

		a_script.add(chaiscript::type_conversion<SafeComponent<Spine>, std::shared_ptr<Spine>>([](const SafeComponent<Spine>& a_item) { return a_item.self(); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Spine>, std::shared_ptr<Drawable>>([](const SafeComponent<Spine>& a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Spine>, std::shared_ptr<Component>>([](const SafeComponent<Spine>& a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
	});

	ScriptSignalRegistrar<void(Spine*, int)> _spineSignalRawInt{};
	ScriptSignalRegistrar<void(std::shared_ptr<Spine>, int)> _spineSignalInt{};
	ScriptSignalRegistrar<void(std::shared_ptr<Spine>, int, int)> _spineSignalIntInt{};
	ScriptSignalRegistrar<void(std::shared_ptr<Spine>, int, const AnimationEventData&)> _spineSignalEvent{};

	Script::Registrar<Spine::FileBundle> _hookFileBundle([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<Spine::FileBundle>(), "SpineFileBundle");

		a_script.add(chaiscript::fun(&Spine::FileBundle::skeletonFile), "skeletonFile");
		a_script.add(chaiscript::fun(&Spine::FileBundle::atlasFile), "atlasFile");
		a_script.add(chaiscript::fun(&Spine::FileBundle::loadScale), "loadScale");
	});

	Script::Registrar<Sprite> _hookSprite([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<Sprite>(), "Sprite");
		a_script.add(chaiscript::base_class<Drawable, Sprite>());
		a_script.add(chaiscript::base_class<Component, Sprite>());

		a_script.add(chaiscript::fun([](Node &a_self) { 
			return a_self.attach<Sprite>(); 
		}), "attachSprite");

		a_script.add(chaiscript::fun([](Node &a_self) {
			return a_self.componentInChildren<Sprite>();
		}), "spriteComponent");

		a_script.add(chaiscript::fun([](const std::shared_ptr<Sprite> &a_self, uint16_t a_subdivisions) {
			return a_self->subdivide(a_subdivisions);
		}), "subdivide");
		a_script.add(chaiscript::fun([](const std::shared_ptr<Sprite> &a_self) {
			return a_self->subdivisions();
		}), "subdivisions");

		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Sprite>(Sprite::*)(const Point<> &, const Point<> &, const Point<> &, const Point<> &)>(&Sprite::corners<Point<>>)), "corners");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Sprite>(Sprite::*)(const Color &, const Color &, const Color &, const Color &)>(&Sprite::corners<Color>)), "corners");

		a_script.add(chaiscript::type_conversion<SafeComponent<Sprite>, std::shared_ptr<Sprite>>([](const SafeComponent<Sprite> &a_item) { return a_item.self(); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Sprite>, std::shared_ptr<Drawable>>([](const SafeComponent<Sprite> &a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Sprite>, std::shared_ptr<Component>>([](const SafeComponent<Sprite> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
	});

	Script::Registrar<Text> _hookText([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<Text>(), "Text");
		a_script.add(chaiscript::base_class<Drawable, Text>());
		a_script.add(chaiscript::base_class<Component, Text>());

		a_script.add(chaiscript::fun([](Node &a_self, TextLibrary& a_textLibrary) { return a_self.attach<Text>(a_textLibrary); }), "attachText");
		a_script.add(chaiscript::fun([](Node &a_self, TextLibrary& a_textLibrary, const std::string &a_defaultFontIdentifier) { return a_self.attach<Text>(a_textLibrary, a_defaultFontIdentifier); }), "attachText");

		a_script.add(chaiscript::fun([](Node &a_self) { return a_self.componentInChildren<Text>(true, false, true); }), "componentText");

		a_script.add(chaiscript::fun(&Text::enableCursor), "enableCursor");
		a_script.add(chaiscript::fun(&Text::disableCursor), "disableCursor");

		a_script.add(chaiscript::fun(&Text::onEnter), "onEnter");
		a_script.add(chaiscript::fun(&Text::onChange), "onChange");

		a_script.add(chaiscript::fun(static_cast<UtfString(Text::*)() const>(&Text::text)), "text");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(const UtfString &)>(&Text::text)), "text");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(UtfChar)>(&Text::text)), "text");

		a_script.add(chaiscript::fun(static_cast<PointPrecision(Text::*)() const>(&Text::number)), "number");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(PointPrecision)>(&Text::number)), "number");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(int)>(&Text::number)), "number");

		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(const UtfString &)>(&Text::append)), "append");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Text>(Text::*)(UtfChar a_char)>(&Text::append)), "append");

		a_script.add(chaiscript::type_conversion<SafeComponent<Text>, std::shared_ptr<Text>>([](const SafeComponent<Text> &a_item) { return a_item.self(); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Text>, std::shared_ptr<Drawable>>([](const SafeComponent<Text> &a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Text>, std::shared_ptr<Component>>([](const SafeComponent<Text> &a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
	});

	ScriptSignalRegistrar<Text::TextSignalSignature> _textSignalType{};

	Script::Registrar<EmitterSpawnProperties> _hookEmitterSpawnProperties([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<EmitterSpawnProperties>(), "EmitterSpawnProperties");

		a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximumParticles), "maximumParticles");

		a_script.add(chaiscript::fun(&EmitterSpawnProperties::minimumSpawnRate), "minimumSpawnRate");
		a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximumSpawnRate), "maximumSpawnRate");

		a_script.add(chaiscript::fun(&EmitterSpawnProperties::minimumPosition), "minimumPosition");
		a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximumPosition), "maximumPosition");

		a_script.add(chaiscript::fun(&EmitterSpawnProperties::minimumDirection), "minimumDirection");
		a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximumDirection), "maximumDirection");

		a_script.add(chaiscript::fun(&EmitterSpawnProperties::minimumRotation), "minimumRotation");
		a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximumRotation), "maximumRotation");

		a_script.add(chaiscript::fun(&EmitterSpawnProperties::minimum), "minimum");
		a_script.add(chaiscript::fun(&EmitterSpawnProperties::maximum), "maximum");
	});

	Script::Registrar<Emitter> _hookEmitter([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<Emitter>(), "Emitter");
		a_script.add(chaiscript::base_class<Drawable, Emitter>());
		a_script.add(chaiscript::base_class<Component, Emitter>());

		auto pool = a_services.get<ThreadPool>();
		a_script.add(chaiscript::fun([=](Node& a_self) {
			return a_self.attach<Emitter>(*pool);
		}), "attachEmitter");

		a_script.add(chaiscript::fun([](Node& a_self) {
			return a_self.componentInChildren<Emitter>();
		}), "emitterComponent");

		a_script.add(chaiscript::fun(&Emitter::enabled), "enabled");
		a_script.add(chaiscript::fun(&Emitter::disabled), "disabled");

		a_script.add(chaiscript::fun(&Emitter::enable), "enable");
		a_script.add(chaiscript::fun(&Emitter::disable), "disable");

		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<Emitter>(Emitter::*)(const EmitterSpawnProperties & a_emitterProperties)>(&Emitter::properties)), "properties");
		a_script.add(chaiscript::fun(static_cast<EmitterSpawnProperties & (Emitter::*)()>(&Emitter::properties)), "properties");
		a_script.add(chaiscript::fun(static_cast<const EmitterSpawnProperties & (Emitter::*)() const>(&Emitter::properties)), "properties");

		a_script.add(chaiscript::type_conversion<SafeComponent<Emitter>, std::shared_ptr<Emitter>>([](const SafeComponent<Emitter>& a_item) { return a_item.self(); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Emitter>, std::shared_ptr<Drawable>>([](const SafeComponent<Emitter>& a_item) { return std::static_pointer_cast<Drawable>(a_item.self()); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<Emitter>, std::shared_ptr<Component>>([](const SafeComponent<Emitter>& a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
	});

	Script::Registrar<PathAgent> _hookPathAgent([](chaiscript::ChaiScript& a_script, const MV::Services& a_services) {
		a_script.add(chaiscript::user_type<PathAgent>(), "PathAgent");
		a_script.add(chaiscript::base_class<Component, PathAgent>());

		a_script.add(chaiscript::fun([](Node& a_self, const std::shared_ptr<PathMap>& a_map, const Point<>& a_gridPosition) { return a_self.attach<PathAgent>(a_map, a_gridPosition); }), "attachPathAgent");
		a_script.add(chaiscript::fun([](Node& a_self, const std::shared_ptr<PathMap>& a_map, const Point<int>& a_gridPosition) { return a_self.attach<PathAgent>(a_map, a_gridPosition); }), "attachPathAgent");

		a_script.add(chaiscript::fun([](Node& a_self, const std::shared_ptr<PathMap>& a_map, const Point<>& a_gridPosition, int a_unitSize) { return a_self.attach<PathAgent>(a_map, a_gridPosition, a_unitSize); }), "attachPathAgent");
		a_script.add(chaiscript::fun([](Node& a_self, const std::shared_ptr<PathMap>& a_map, const Point<int>& a_gridPosition, int a_unitSize) { return a_self.attach<PathAgent>(a_map, a_gridPosition, a_unitSize); }), "attachPathAgent");

		a_script.add(chaiscript::fun(&PathAgent::pathfinding), "pathfinding");
		a_script.add(chaiscript::fun(&PathAgent::path), "path");

		a_script.add(chaiscript::fun(&PathAgent::stop), "stop");
		a_script.add(chaiscript::fun(&PathAgent::gridOverlaps), "gridOverlaps");
		a_script.add(chaiscript::fun(&PathAgent::localOverlaps), "localOverlaps");

		a_script.add(chaiscript::fun(&PathAgent::hasFootprint), "hasFootprint");
		a_script.add(chaiscript::fun(&PathAgent::disableFootprint), "disableFootprint");
		a_script.add(chaiscript::fun(&PathAgent::enableFootprint), "enableFootprint");

		a_script.add(chaiscript::fun(static_cast<PointPrecision(PathAgent::*)() const>(&PathAgent::gridSpeed)), "gridSpeed");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(PointPrecision)>(&PathAgent::gridSpeed)), "gridSpeed");

		a_script.add(chaiscript::fun(static_cast<PointPrecision(PathAgent::*)() const>(&PathAgent::localSpeed)), "localSpeed");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(PointPrecision)>(&PathAgent::localSpeed)), "localSpeed");

		a_script.add(chaiscript::fun(static_cast<Point<PointPrecision>(PathAgent::*)() const>(&PathAgent::gridGoal)), "gridGoal");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<PointPrecision>&, PointPrecision)>(&PathAgent::gridGoal)), "gridGoal");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<int>&, PointPrecision)>(&PathAgent::gridGoal)), "gridGoal");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<PointPrecision>&)>(&PathAgent::gridGoal)), "gridGoal");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<int>&)>(&PathAgent::gridGoal)), "gridGoal");

		a_script.add(chaiscript::fun(static_cast<Point<PointPrecision>(PathAgent::*)() const>(&PathAgent::localGoal)), "localGoal");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<PointPrecision>&, PointPrecision)>(&PathAgent::localGoal)), "localGoal");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<PointPrecision>&)>(&PathAgent::localGoal)), "localGoal");

		a_script.add(chaiscript::fun(static_cast<Point<PointPrecision>(PathAgent::*)() const>(&PathAgent::gridPosition)), "gridPosition");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<PointPrecision>&)>(&PathAgent::gridPosition)), "gridPosition");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<int>&)>(&PathAgent::gridPosition)), "gridPosition");

		a_script.add(chaiscript::fun(static_cast<Point<PointPrecision>(PathAgent::*)() const>(&PathAgent::localPosition)), "localPosition");
		a_script.add(chaiscript::fun(static_cast<std::shared_ptr<PathAgent>(PathAgent::*)(const Point<PointPrecision>&)>(&PathAgent::localPosition)), "localPosition");

		a_script.add(chaiscript::fun(&PathAgent::onArrive), "onArrive");
		a_script.add(chaiscript::fun(&PathAgent::onBlocked), "onBlocked");
		a_script.add(chaiscript::fun(&PathAgent::onStop), "onStop");
		a_script.add(chaiscript::fun(&PathAgent::onStart), "onStart");

		a_script.add(chaiscript::type_conversion<SafeComponent<PathAgent>, std::shared_ptr<PathAgent>>([](const SafeComponent<PathAgent>& a_item) { return a_item.self(); }));
		a_script.add(chaiscript::type_conversion<SafeComponent<PathAgent>, std::shared_ptr<Component>>([](const SafeComponent<PathAgent>& a_item) { return std::static_pointer_cast<Component>(a_item.self()); }));
	});

	ScriptSignalRegistrar<PathAgent::CallbackSignature> _pathAgentSignalRegister{};
}