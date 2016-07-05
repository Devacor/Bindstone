#ifndef _COLLISIONOBJECTS_H_
#define _COLLISIONOBJECTS_H_

#include "Render\package.h"
#include "Utility\package.h"
#include "Box2D\Box2D.h"
#include <list>
#include <map>

//Box2D failed on this, they have equality but not inequality.
bool operator!=(const b2Vec2 &a_lhs, const b2Vec2 &a_rhs);

namespace MV {
	namespace Scene {
		//Scales units down by 50 when dealing with collisions.
		const PointPrecision CollisionScale = 50.0;
		//This is the time step at which Box2D updates.
		const Stopwatch::TimeType PhysicsTimeStep = 1.0 / 60.0;

		Point<> cast(b2Vec2 a_box2DPoint, PointPrecision a_z = 0.0f);

		template<typename T>
		b2Vec2 castToPhysics(Point<T> a_M2RendPoint) {
			return b2Vec2(static_cast<float32>(a_M2RendPoint.x) / CollisionScale, static_cast<float32>(a_M2RendPoint.y) / CollisionScale);
		}

		template<typename T>
		b2Vec2 castToPhysics(Size<T> a_M2RendPoint) {
			return b2Vec2(static_cast<float32>(a_M2RendPoint.width) / CollisionScale, static_cast<float32>(a_M2RendPoint.height) / CollisionScale);
		}

		class Collider;
		class ContactListener : public b2ContactListener {
			struct ContactDetails {
				ContactDetails(size_t a_contactId, b2Fixture* a_A, b2Fixture* a_B) :
					contactId(a_contactId),
					A(a_A),
					B(a_B),
					beginContact(false) {
				}

				ContactDetails(size_t a_contactId, b2Fixture* a_A, b2Fixture* a_B, b2Vec2 a_normal):
					contactId(a_contactId),
					A(a_A),
					B(a_B),
					normal(a_normal),
					beginContact(true) {
				}
				size_t contactId;
				b2Fixture* A;
				b2Fixture* B;
				b2Vec2 normal;
				bool beginContact;
			};

		public:
			virtual void BeginContact(b2Contact* contact);

			virtual void EndContact(b2Contact* contact);

			virtual void PreSolve(b2Contact* contact, const b2Manifold* oldManifold);

			virtual void PostSolve(b2Contact* contact, const b2Manifold* oldManifold) {}

			void RunQueuedCallbacks();

		private:
			std::queue<ContactDetails> contactCallbacks;
		};

		class Environment : public Component {
			friend Node;
			friend cereal::access;

		public:
			ComponentDerivedAccessors(Environment)

			double percentOfStep() const {
				return accumulatedDelta / PhysicsTimeStep;
			}

			bool updatedThisFrame() const {
				return steppedThisUpdate;
			}

			b2Body* createBody(const b2BodyDef& a_bodyDef = b2BodyDef()) {
				return world.CreateBody(&a_bodyDef);
			}

			void destroyBody(b2Body* a_body) {
				world.DestroyBody(a_body);
			}

			template <typename T = b2Joint>
			T* createJoint(const b2JointDef &a_jointDef) {
				return static_cast<T*>(world.CreateJoint(&a_jointDef));
			}

			void destroyJoint(b2Joint* a_joint) {
				world.DestroyJoint(a_joint);
			}

			b2World* getWorld() {
				return &world;
			}
		protected:
			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					cereal::make_nvp("gravityX", world.GetGravity().x),
					cereal::make_nvp("gravityY", world.GetGravity().y),
					cereal::make_nvp("Component", cereal::base_class<Component>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Environment> &construct, std::uint32_t const /*version*/) {
				b2Vec2 loadedGravity;
				archive(
					cereal::make_nvp("gravityX", loadedGravity.x),
					cereal::make_nvp("gravityY", loadedGravity.y)
				);

				construct(std::shared_ptr<Node>(), cast(loadedGravity));

				archive(
					cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
				);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Environment>(cast(world.GetGravity())).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

		private:
			Environment(const std::weak_ptr<Node> &a_owner, const Point<> &a_gravity = Point<>(0.0f, 1000.0f)) :
				Component(a_owner),
				world(castToPhysics(a_gravity)) {

				world.SetGravity(castToPhysics(a_gravity));
				world.SetContactListener(&listener);
			}

			virtual void updateImplementation(double a_dt) override {
				steppedThisUpdate = false;
				accumulatedDelta += a_dt;
				accumulatedDelta = std::min(accumulatedDelta, PhysicsTimeStep * 10.0f);
				while (accumulatedDelta > PhysicsTimeStep) {
					world.Step(float32(PhysicsTimeStep), Box2dVelocityIterations, Box2dPositionIterations);
					listener.RunQueuedCallbacks();
					accumulatedDelta -= PhysicsTimeStep;
					steppedThisUpdate = true;
				}
			}

			ContactListener listener;
			b2World world;

			double accumulatedDelta = 0.0;

			bool steppedThisUpdate = false;

			static const int Box2dVelocityIterations = 10;
			static const int Box2dPositionIterations = 4;
		};

		class CollisionBodyAttributes {
			friend Collider;
		public:
			CollisionBodyAttributes();

			bool isStatic() const;
			bool isDynamic() const;
			bool isKinematic() const;

			CollisionBodyAttributes& makeStatic();
			CollisionBodyAttributes& makeDynamic();
			CollisionBodyAttributes& makeKinematic();

			CollisionBodyAttributes& bullet(bool a_isBullet);
			bool bullet() const;

			CollisionBodyAttributes& stop();

			CollisionBodyAttributes& transform(const Point<> &a_position, PointPrecision a_newAngle);
			CollisionBodyAttributes& position(const Point<> &a_position);
			CollisionBodyAttributes& angle(PointPrecision a_newAngle);
			PointPrecision angle() const;
			Point<> position() const;

			CollisionBodyAttributes& impulse(const Point<> &a_impulse);
			CollisionBodyAttributes& force(const Point<> &a_force);
			CollisionBodyAttributes& impulse(const Point<> &a_impulse, const Point<> &a_position);
			CollisionBodyAttributes& force(const Point<> &a_force, const Point<> &a_position);
			CollisionBodyAttributes& torque(PointPrecision a_amount);
			CollisionBodyAttributes& angularImpulse(PointPrecision a_amount);

			CollisionBodyAttributes& gravityScale(PointPrecision a_newScale);

			CollisionBodyAttributes& disableRotation();
			CollisionBodyAttributes& enableRotation();

			CollisionBodyAttributes& velocity(const Point<> &a_velocity);
			Point<> velocity() const;

			CollisionBodyAttributes& angularVelocity(PointPrecision a_value);
			PointPrecision angularVelocity() const;

			CollisionBodyAttributes& movementDamping(PointPrecision a_value);
			CollisionBodyAttributes& angularDamping(PointPrecision a_value);

			CollisionBodyAttributes& allowSleep();
			CollisionBodyAttributes& disallowSleep();

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const /*version*/) {
				syncronize();
				archive(
					cereal::make_nvp("x", details.position.x),
					cereal::make_nvp("y", details.position.y),
					cereal::make_nvp("angle", details.angle),
					cereal::make_nvp("vX", details.linearVelocity.x),
					cereal::make_nvp("vY", details.linearVelocity.y),
					cereal::make_nvp("vAngle", details.angularVelocity),
					cereal::make_nvp("linearDamping", details.linearDamping),
					cereal::make_nvp("angularDamping", details.angularDamping),
					cereal::make_nvp("allowSleep", details.allowSleep),
					cereal::make_nvp("awake", details.awake),
					cereal::make_nvp("fixedRotation", details.fixedRotation),
					cereal::make_nvp("bullet", details.bullet),
					cereal::make_nvp("type", details.type),
					cereal::make_nvp("active", details.active),
					cereal::make_nvp("gravityScale", details.gravityScale),
					cereal::make_nvp("parent", parent)
				);
			}
		private:
			void syncronize();

			std::weak_ptr<Collider> parent;
			b2BodyDef details;
		};

		class RotationJointAttributes {
			friend Collider;
		public:
			~RotationJointAttributes() {
				destroy();
			}

			RotationJointAttributes& torque(PointPrecision a_torque) {
				jointDef.enableMotor = true;
				jointDef.maxMotorTorque = a_torque;
				if (joint) {
					joint->EnableMotor(true);
					joint->SetMaxMotorTorque(a_torque);
				}
				return *this;
			}

			RotationJointAttributes& speed(PointPrecision a_speed) {
				jointDef.enableMotor = true;
				jointDef.motorSpeed = a_speed;
				if (joint) {
					joint->EnableMotor(true);
					joint->SetMotorSpeed(a_speed);
				}
				return *this;
			}

			RotationJointAttributes& removeMotor() {
				jointDef.enableMotor = false;
				if (joint) {
					joint->EnableMotor(false);
				}
				return *this;
			}

			RotationJointAttributes& limits(PointPrecision a_min, PointPrecision a_max) {
				jointDef.enableLimit = true;
				if (joint) {
					joint->EnableLimit(true);
					joint->SetLimits(a_min, a_max);
				}
				return *this;
			}

			RotationJointAttributes& removeLimits() {
				jointDef.enableLimit = false;
				if (joint) {
					joint->EnableLimit(false);
				}
				return *this;
			}

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const version);

		private:
			void initialize(const std::shared_ptr<Collider> &a_lhs, const std::shared_ptr<Collider> &a_rhs, Point<> a_offset);
			void loadedCollider(Collider* a_loaded);

			void destroy();

			int loadCount = 0;
			Collider* firstLoaded = nullptr;
			Point<> offset;
			Point<> worldPosition;

			std::weak_ptr<Collider> A;
			std::weak_ptr<Collider> B;

			b2RevoluteJointDef jointDef;
			b2RevoluteJoint *joint = nullptr;
		};

		enum CollisionLayers {
			ALL_LAYERS = 0x0000,
			LAYER_1 = 0x0001,
			LAYER_2 = 0x0002,
			LAYER_3 = 0x0004,
			LAYER_4 = 0x0008,
			LAYER_5 = 0x0010,
			LAYER_6 = 0x0020,
			LAYER_7 = 0x0040,
			LAYER_8 = 0x0080,
			LAYER_9 = 0x0100,
			LAYER_10 = 0x0200,
			LAYER_11 = 0x0400,
			LAYER_12 = 0x0800,
			LAYER_13 = 0x1000,
			LAYER_14 = 0x2000,
			LAYER_15 = 0x4000,
			LAYER_16 = 0x8000
		};

		class CollisionPartAttributes {
			friend Collider;
		public:
			CollisionPartAttributes() {
				details.restitution = 0.0f;
				details.friction = .3f;
				details.density = 1.0f;
				details.isSensor = false;
			}

			CollisionPartAttributes& id(const std::string &a_id) {
				ourId = a_id;
				return *this;
			}

			std::string id() {
				return ourId;
			}

			//Very basic built in filtering based on flags.
			//example: filter(LAYER_1, LAYER_2 | LAYER_3 | LAYER_4)
			CollisionPartAttributes& filter(int16_t a_category, int16_t a_categoriesWeCanHit) {
				details.filter.categoryBits = a_category;
				details.filter.maskBits = a_categoriesWeCanHit;
				return *this;
			}

			CollisionPartAttributes& filterCategory(int16_t a_category) {
				details.filter.categoryBits = a_category;
				return *this;
			}

			CollisionPartAttributes& addCollidableCategory(int16_t a_newCategory) {
				details.filter.maskBits |= a_newCategory;
				return *this;
			}

			CollisionPartAttributes& density(PointPrecision a_density) {
				details.density = a_density;
				return *this;
			}

			CollisionPartAttributes& sensor(bool a_isSensor) {
				details.isSensor = a_isSensor;
				return *this;
			}

			CollisionPartAttributes& friction(PointPrecision a_friction) {
				details.friction = a_friction;
				return *this;
			}

			CollisionPartAttributes& restitution(PointPrecision a_restitution) {
				details.restitution = a_restitution;
				return *this;
			}

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const /*version*/) {
				archive(
					cereal::make_nvp("restitution", details.restitution),
					cereal::make_nvp("friction", details.friction),
					cereal::make_nvp("density", details.density),
					cereal::make_nvp("sensor", details.isSensor),
					cereal::make_nvp("filterCategory", details.filter.categoryBits),
					cereal::make_nvp("filterInteractions", details.filter.maskBits),
					cereal::make_nvp("id", ourId)
				);
			}
		private:
			std::string ourId;
			b2FixtureDef details;
		};

		class ContactListener;
		struct CollisionParameters {
			CollisionParameters(CollisionPartAttributes* a_ourFixture, Collider *a_ourCollider,
				CollisionPartAttributes* a_theirFixture, Collider *a_theirCollider):
				fixtureA(a_ourFixture), colliderA(a_ourCollider),
				fixtureB(a_theirFixture), colliderB(a_theirCollider){
			}

			CollisionPartAttributes* fixtureA;
			Collider* colliderA;

			SafeComponent<Collider> safeColliderA() const;

			CollisionPartAttributes* fixtureB;
			Collider* colliderB;

			SafeComponent<Collider> safeColliderB() const;
		};

		class Collider : public Component {
			friend Node;
			friend cereal::access;
			friend CollisionBodyAttributes;
			friend RotationJointAttributes;
			friend ContactListener;
			
			Signal<void(CollisionParameters)> onCollisionStartSignal;
			Signal<void(CollisionParameters)> onCollisionEndSignal;
			Signal<void(size_t, CollisionParameters, const Point<> &)> onContactStartSignal;
			Signal<void(size_t, CollisionParameters)> onContactEndSignal;
			Signal<void(bool, CollisionParameters)> onCollisionKilledSignal; //first param true if A is destroyed otherwise B
			Signal<void(bool, size_t, CollisionParameters)> onContactKilledSignal; //first param true if A is destroyed otherwise B
			Signal<void(CollisionParameters, const Point<> &, bool&)> onShouldCollideSignal;
		public:
			SignalRegister<void(CollisionParameters)> onCollisionStart;
			SignalRegister<void(CollisionParameters)> onCollisionEnd;
			SignalRegister<void(size_t, CollisionParameters, const Point<> &)> onContactStart;
			SignalRegister<void(size_t, CollisionParameters)> onContactEnd;
			SignalRegister<void(bool, CollisionParameters)> onCollisionKilled; //first param true if A is destroyed otherwise B
			SignalRegister<void(bool, size_t, CollisionParameters)> onContactKilled; //first param true if A is destroyed otherwise B
			SignalRegister<void(CollisionParameters, const Point<> &, bool&)> onShouldCollide;

			ComponentDerivedAccessors(Collider);

			virtual ~Collider();

			CollisionBodyAttributes& body() {
				return collisionAttributes;
			}

			std::shared_ptr<Collider> attach(const Size<> &a_size, const Point<> &a_position = Point<>(), PointPrecision a_rotation = 0.0f, CollisionPartAttributes a_attributes = CollisionPartAttributes());
			std::shared_ptr<Collider> attach(PointPrecision a_diameter, const Point<> &a_position = Point<>(), CollisionPartAttributes a_attributes = CollisionPartAttributes());
			std::shared_ptr<Collider> attach(const std::vector<Point<>> &a_points, const Point<> &a_offset = Point<>(), CollisionPartAttributes a_attributes = CollisionPartAttributes());

			std::shared_ptr<RotationJointAttributes> rotationJoint(const std::shared_ptr<Collider> &a_other, const RotationJointAttributes &a_attributes, const Point<> &a_offset = Point<>()) {
				auto sharedAttributes = std::make_shared<RotationJointAttributes>();
				*sharedAttributes = a_attributes;
				sharedAttributes->initialize(std::static_pointer_cast<Collider>(shared_from_this()), a_other, a_offset);
				rotationJoints.push_back(sharedAttributes);
				a_other->rotationJoints.push_back(sharedAttributes);
				return sharedAttributes;
			}

			std::shared_ptr<RotationJointAttributes> rotationJoint(int a_index = 0) const {
				if (a_index < rotationJoints.size()) {
					return rotationJoints[a_index];
				} else {
					return nullptr;
				}
			}

			Point<> physicsPosition() const {
				auto percentOfWorldStep = static_cast<PointPrecision>(world->percentOfStep());
				return mix(previousPosition, currentPosition, percentOfWorldStep);
			}

			Point<> physicsWorldPosition() const {
				return world->owner()->worldFromLocal(physicsPosition());
			}

			Point<> physicsLocalPosition() const {
				return owner()->parent()->localFromWorld(physicsWorldPosition());
			}

			void addCollision(Collider* a_collisionWith, CollisionPartAttributes *a_ourFixture, CollisionPartAttributes *a_theirFixture, size_t a_contactId, const b2Vec2 &a_normal) {
				auto& fixtureContact = contacts[a_collisionWith].fixtureContacts[a_contactId];
				fixtureContact.normal = a_normal;
				fixtureContact.A = a_ourFixture;
				fixtureContact.B = a_theirFixture;
				if (contacts[a_collisionWith].fixtureContacts.size() == 1) {
					onCollisionStartSignal(CollisionParameters(a_ourFixture, this, a_theirFixture, a_collisionWith));
				}
				onContactStartSignal(a_contactId, CollisionParameters(a_ourFixture, this, a_theirFixture, a_collisionWith), Point<>(a_normal.x, a_normal.y));
			}

			void removeCollision(Collider* a_collisionWith, CollisionPartAttributes *a_ourFixture, CollisionPartAttributes *a_theirFixture, size_t a_contactId) {
				auto foundCollision = contacts.find(a_collisionWith);
				if (foundCollision != contacts.end()) {
					foundCollision->second.fixtureContacts.erase(a_contactId);
					if (foundCollision->second.fixtureContacts.empty()) {
						contacts.erase(foundCollision);
						onCollisionEndSignal(CollisionParameters(a_ourFixture, this, a_theirFixture, a_collisionWith));
					}
				}
				onContactEndSignal(a_contactId, CollisionParameters(a_ourFixture, this, a_theirFixture, a_collisionWith));
			}

			//lock or unlock the drawing angle to the physical body
			std::shared_ptr<Collider> observePhysicsAngle(bool a_newValue) {
				useBodyAngle = a_newValue;
				if (a_newValue == true) {
					auto colliders = owner()->components<Collider>();
					for (auto&& collider : colliders) {
						visit(collider, [&](const MV::Scene::SafeComponent<MV::Scene::Collider> &a_collider) {
							if (a_collider.self().get() != this) {
								a_collider->observePhysicsAngle(false);
							}
						});
					}
				}
				return std::static_pointer_cast<Collider>(shared_from_this());
			}
			bool observePhysicsAngle() const {
				return useBodyAngle;
			}

			std::shared_ptr<Collider> observePhysicsPosition(bool a_newValue) {
				useBodyPosition = a_newValue;
				if (a_newValue == true) {
					auto colliders = owner()->components<Collider>();
					for (auto&& collider : colliders) {
						visit(collider, [&](const MV::Scene::SafeComponent<MV::Scene::Collider> &a_collider) {
							if (a_collider.self().get() != this) {
								a_collider->observePhysicsPosition(false);
							}
						});
					}
				}
				return std::static_pointer_cast<Collider>(shared_from_this());
			}
			bool observePhysicsPosition() const {
				return useBodyPosition;
			}
		protected:
			b2FixtureDef defaultFixtureDefinition;

			virtual void initialize() override;

			struct ContactInformation {
				struct ContactInformationData {
					b2Vec2 normal;
					CollisionPartAttributes* A;
					CollisionPartAttributes* B;
				};
				std::map<size_t, ContactInformationData> fixtureContacts;
			};
			typedef std::map<Collider*, ContactInformation> ContactMap;
			ContactMap contacts;
			b2Body *physicsBody;

			std::shared_ptr<Environment> world;

			void attachInternal(const Size<> &a_size, const Point<> &a_position = Point<>(), PointPrecision a_rotation = 0.0f, CollisionPartAttributes a_attributes = CollisionPartAttributes());
			void attachInternal(PointPrecision a_diameter, const Point<> &a_position = Point<>(), CollisionPartAttributes a_attributes = CollisionPartAttributes());
			void attachInternal(const std::vector<Point<>> &a_points, const Point<> &a_offset = Point<>(), CollisionPartAttributes a_attributes = CollisionPartAttributes());

			template <class Archive>
			void serialize(Archive & archive, std::uint32_t const /*version*/) {
				collisionAttributes.syncronize();
				archive(
					cereal::make_nvp("world", world),
					cereal::make_nvp("collisionAttributes", collisionAttributes),
 					cereal::make_nvp("useBodyAngle", useBodyAngle),
 					cereal::make_nvp("collisionParts", collisionParts),
					cereal::make_nvp("currentPosition", currentPosition),
					cereal::make_nvp("currentAngle", currentAngle),
					cereal::make_nvp("useBodyPosition", useBodyPosition),
					cereal::make_nvp("rotationJoints", rotationJoints),
					cereal::make_nvp("Component", cereal::base_class<Component>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Collider> &construct) {
				CollisionBodyAttributes collisionAttributes;
				std::shared_ptr<Environment> world;
				archive(
					cereal::make_nvp("world", world),
					cereal::make_nvp("collisionAttributes", collisionAttributes)
				);
				construct(std::shared_ptr<Node>(), world, collisionAttributes, false);
				construct->loadedFromJson = true;
				archive(
 					cereal::make_nvp("useBodyAngle", construct->useBodyAngle),
 					cereal::make_nvp("collisionParts", construct->collisionParts),
					cereal::make_nvp("currentPosition", construct->currentPosition),
					cereal::make_nvp("currentAngle", construct->currentAngle),
					cereal::make_nvp("useBodyPosition", construct->useBodyPosition),
					cereal::make_nvp("rotationJoints", construct->rotationJoints),
					cereal::make_nvp("Component", cereal::base_class<Component>(construct.ptr()))
				);
				construct->initialize();
				for (auto&& attribute : construct->collisionParts) {
					if (attribute.shapeType == FixtureParameters::CIRCLE) {
						construct->attachInternal(attribute.diameter, attribute.position, attribute.attributes);
					} else if (attribute.shapeType == FixtureParameters::RECTANGLE) {
						construct->attachInternal(attribute.size, attribute.position, attribute.rotation, attribute.attributes);
					} else if (attribute.shapeType == FixtureParameters::POLYGON) {
						construct->attachInternal(attribute.points, attribute.position, attribute.attributes);
					}
				}

				for (auto&& joint : construct->rotationJoints) {
					joint->loadedCollider(construct.ptr());
				}
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Collider>(world, collisionAttributes).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

		private:
			struct FixtureParameters {
				enum ShapeType {CIRCLE, RECTANGLE, POLYGON};
				int shapeType;
				Size<> size; //Rect
				PointPrecision rotation = 0.0f; //Rect
				PointPrecision diameter = 0.0f; //Circle
				Point<> position;
				std::vector<Point<>> points;
				CollisionPartAttributes attributes;

				template <class Archive>
				void serialize(Archive & archive, std::uint32_t const /*version*/) {
					archive(
						cereal::make_nvp("type", shapeType),
						cereal::make_nvp("size", size),
						cereal::make_nvp("points", points),
						cereal::make_nvp("rotation", rotation),
						cereal::make_nvp("diameter", diameter),
						cereal::make_nvp("position", position),
						cereal::make_nvp("attributes", attributes)
					);
				}
			};

			Collider(const std::weak_ptr<Node> &a_owner, CollisionBodyAttributes a_collisionAttributes = CollisionBodyAttributes(), bool a_maintainOwnerPosition = true);
			Collider(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<Environment> &a_world, CollisionBodyAttributes a_collisionAttributes = CollisionBodyAttributes(), bool a_maintainOwnerPosition = true);
			Collider(const std::weak_ptr<Node> &a_owner, const SafeComponent<Environment> &a_world, CollisionBodyAttributes a_collisionAttributes = CollisionBodyAttributes(), bool a_maintainOwnerPosition = true);

			void updateInterpolatedPositionAndApply();

			void updatePhysicsPosition();
			virtual void updateImplementation(double a_dt) override;

			b2FixtureDef* getDefaultFixtureDefinition() {
				return &defaultFixtureDefinition;
			}

			void applyScenePositionUpdate(Point<> a_location, double a_angle);

			PointPrecision interpolateDrawAngle(PointPrecision a_percentOfStep) {
				if (currentAngle == previousAngle) { return currentAngle; }
				bool wrapped;
				PointPrecision rotation = wrappingDistance(currentAngle, previousAngle, 0.0f, 360.0f, &wrapped);
				if (wrapped) {
					if (previousAngle > currentAngle) {
						rotation = previousAngle + ((rotation)* a_percentOfStep);
					}
					else {
						rotation = previousAngle - ((rotation)* a_percentOfStep);
					}
				} else {
					if (previousAngle > currentAngle) {
						rotation = previousAngle - ((rotation)* a_percentOfStep);
					}
					else {
						rotation = previousAngle + ((rotation)* a_percentOfStep);
					}
				}
				return wrap(rotation, 0.0f, 360.0f);
			}

			void deleteCollisionObject(Collider* a_collisionWith) {
				auto contact = contacts.find(a_collisionWith);
				if (contact != contacts.end()) {
					for (auto&& fixtureContact : contact->second.fixtureContacts) {
						a_collisionWith->onContactKilledSignal(true, fixtureContact.first, CollisionParameters(fixtureContact.second.A, a_collisionWith, fixtureContact.second.B, this));
						onContactKilledSignal(false, fixtureContact.first, CollisionParameters(fixtureContact.second.A, this, fixtureContact.second.B, a_collisionWith));
					}
					if (!contact->second.fixtureContacts.empty()) {
						auto&& fixtureContact = contact->second.fixtureContacts.begin();
						a_collisionWith->onCollisionKilledSignal(true, CollisionParameters(fixtureContact->second.B, a_collisionWith, fixtureContact->second.A, this));
						onCollisionKilledSignal(false, CollisionParameters(fixtureContact->second.A, this, fixtureContact->second.B, a_collisionWith));
					}
					for (auto&& fixtureContact : contact->second.fixtureContacts) {
						fixtureContact.second.A = nullptr;
						fixtureContact.second.B = nullptr;
					}
					contacts.erase(contact);
				}
			}

			CollisionBodyAttributes collisionAttributes;
			std::vector<FixtureParameters> collisionParts;
			std::map<b2Fixture*, CollisionPartAttributes> fixtureMap;

			std::vector<std::shared_ptr<RotationJointAttributes>> rotationJoints;

			PointPrecision currentAngle;
			PointPrecision previousAngle;

			Point<> currentPosition; //cast but not moved to local from world
			Point<> previousPosition;

			bool useBodyAngle = true;
			bool useBodyPosition = true;
			bool loadedFromJson = false;
		};

		template <class Archive>
		void RotationJointAttributes::serialize(Archive & archive, std::uint32_t const /*version*/) {
			worldPosition = Point<>();
			if (joint) {
				jointDef.motorSpeed = joint->GetMotorSpeed();
				jointDef.maxMotorTorque = joint->GetMaxMotorTorque();
				jointDef.referenceAngle = A.lock()->physicsBody->GetAngle() - B.lock()->physicsBody->GetAngle() - joint->GetJointAngle();
				worldPosition = A.lock()->world->owner()->localFromWorld(A.lock()->owner()->worldFromLocal(offset));
			}
			
			archive(
				cereal::make_nvp("A", A),
				cereal::make_nvp("B", B),
				cereal::make_nvp("Offset", offset),
				cereal::make_nvp("Position", worldPosition),
				cereal::make_nvp("Motor", jointDef.enableMotor),
				cereal::make_nvp("Limit", jointDef.enableLimit),
				cereal::make_nvp("Angle", jointDef.referenceAngle)
			);

			if (jointDef.enableMotor) {
				archive(
					cereal::make_nvp("Speed", jointDef.motorSpeed),
					cereal::make_nvp("MaxTorque", jointDef.maxMotorTorque)
				);
			}

			if (jointDef.enableLimit) {
				archive(
					cereal::make_nvp("Min", jointDef.lowerAngle),
					cereal::make_nvp("Max", jointDef.upperAngle)
				);
			}
		}
	}
}

#endif
