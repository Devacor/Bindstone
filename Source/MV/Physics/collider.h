#ifndef _COLLISIONOBJECTS_H_
#define _COLLISIONOBJECTS_H_

#include "MV/Utility/require.hpp"
#include "MV/Render/package.h"
#include "MV/Utility/stopwatch.h"
#include "MV/Utility/visitor.hpp"
#include "Box2D/Box2D.h"
#include <jaiscript/properties.hpp>
#include <jaiscript/signals/signal.hpp>
#include <jaiscript/serialization/archive.hpp>
#include <jaiscript/serialization/construct.hpp>
#include "MV/Utility/services.hpp"
#include <functional>
#include <list>
#include <map>
#include <queue>

//Box2D failed on this, they have equality but not inequality.
bool operator!=(const b2Vec2 &a_lhs, const b2Vec2 &a_rhs);

namespace MV {
	Point<> cast(b2Vec2 a_box2DPoint, PointPrecision a_z = 0.0f);
	namespace Scene {
		//Scales units down by 50 when dealing with collisions.
		const MV::PointPrecision CollisionScale = 50.0;
		//This is the time step at which Box2D updates.
		const double PhysicsTimeStep = 1.0 / 60.0;

		template<typename T>
		b2Vec2 castToPhysics(Point<T> a_M2RendPoint) {
			return b2Vec2(static_cast<float32>(a_M2RendPoint.x) / CollisionScale, static_cast<float32>(a_M2RendPoint.y) / CollisionScale);
		}

		template<typename T>
		b2Vec2 castToPhysics(Size<T> a_M2RendPoint) {
			return b2Vec2(static_cast<float32>(a_M2RendPoint.width) / CollisionScale, static_cast<float32>(a_M2RendPoint.height) / CollisionScale);
		}

		template <typename T>
		std::function<void(jai::property<T>&, jai::property<T>&)> propertyNoClone() {
			return [](jai::property<T>&, jai::property<T>&) {};
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

			virtual void PostSolve(b2Contact* /*contact*/, const b2Manifold* /*oldManifold*/) {}

			void RunQueuedCallbacks();

		private:
			std::queue<ContactDetails> contactCallbacks;
		};

		class Environment : public jai::property_owner<Environment, Component> {
			friend Node;
			friend jai::access;

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

			Point<> gravity() const {
				return cast(world.GetGravity());
			}

			std::shared_ptr<Environment> gravity(const Point<> &a_gravity) {
				world.SetGravity(castToPhysics(a_gravity));
				syncronizeGravity();
				return std::static_pointer_cast<Environment>(shared_from_this());
			}
		protected:
			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				const_cast<Environment*>(this)->syncronizeGravity();
				property_mgr.save(archive);
				Component::save(archive, 0);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				property_mgr.load(archive);
				world.SetGravity(b2Vec2(gravityX, gravityY));
				Component::load(archive, 0);
			}

			template <typename Archive>
			static void load_and_construct(Archive & ar, jai::serialization::construct<Environment> &construct) {
				construct(std::shared_ptr<Node>());
				construct->load(ar, 0);
				construct->initialize();
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Environment>(cast(world.GetGravity())).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

		private:
			Environment(const Environment&) = delete;
			Environment& operator=(const Environment&) = delete;
			Environment(Environment&&) = delete;
			Environment& operator=(Environment&&) = delete;
			Environment(const std::weak_ptr<Node> &a_owner, const Point<> &a_gravity = Point<>(0.0f, 1000.0f)) :
				jai::property_owner<Environment, Component>(a_owner),
				world(castToPhysics(a_gravity)) {

				world.SetGravity(castToPhysics(a_gravity));
				world.SetContactListener(&listener);
				syncronizeGravity();
			}

			void syncronizeGravity() {
				gravityX = world.GetGravity().x;
				gravityY = world.GetGravity().y;
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

			JAI_PROPERTY((float), gravityX, 0.0f);
			JAI_PROPERTY((float), gravityY, 20.0f);

			static const int Box2dVelocityIterations = 10;
			static const int Box2dPositionIterations = 4;
		};

		class CollisionBodyAttributes : public jai::property_owner<CollisionBodyAttributes> {
			friend Collider;
			friend jai::access;
		public:
			CollisionBodyAttributes();
			CollisionBodyAttributes(const CollisionBodyAttributes &a_rhs) : jai::property_owner<CollisionBodyAttributes>() {
				*this = a_rhs;
			}
			CollisionBodyAttributes& operator=(const CollisionBodyAttributes &a_rhs) {
				jai::property_owner<CollisionBodyAttributes>::operator=(a_rhs);
				return *this;
			}

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

			b2BodyDef definition() const;

		protected:
			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				syncronize();
				property_mgr.save(archive);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				property_mgr.load(archive);
			}

		private:
			void syncronize() const;
			void physicsPosition(const b2Vec2 &a_position);
			b2Vec2 physicsPosition() const;

			JAI_NAMED_PROPERTY((float), "x", positionX, 0.0f);
			JAI_NAMED_PROPERTY((float), "y", positionY, 0.0f);
			JAI_NAMED_PROPERTY((float), "angle", bodyAngle, 0.0f);
			JAI_NAMED_PROPERTY((float), "vX", velocityX, 0.0f);
			JAI_NAMED_PROPERTY((float), "vY", velocityY, 0.0f);
			JAI_NAMED_PROPERTY((float), "vAngle", bodyAngularVelocity, 0.0f);
			JAI_NAMED_PROPERTY((float), "linearDamping", bodyLinearDamping, 0.0f);
			JAI_NAMED_PROPERTY((float), "angularDamping", bodyAngularDamping, 0.0f);
			JAI_NAMED_PROPERTY((bool), "allowSleep", bodyAllowSleep, true);
			JAI_NAMED_PROPERTY((bool), "awake", bodyAwake, true);
			JAI_NAMED_PROPERTY((bool), "fixedRotation", bodyFixedRotation, false);
			JAI_NAMED_PROPERTY((bool), "bullet", bodyBullet, false);
			JAI_NAMED_PROPERTY((b2BodyType), "type", bodyType, b2_staticBody);
			JAI_NAMED_PROPERTY((bool), "active", bodyActive, true);
			JAI_NAMED_PROPERTY((float), "gravityScale", bodyGravityScale, 1.0f);
			JAI_PROPERTY((std::weak_ptr<Collider>), parent, propertyNoClone<std::weak_ptr<Collider>>());
		};

		class RotationJointAttributes : public jai::property_owner<RotationJointAttributes> {
			friend Collider;
			friend jai::access;
		public:
			RotationJointAttributes() = default;
			RotationJointAttributes(const RotationJointAttributes &a_rhs) : jai::property_owner<RotationJointAttributes>() {
				*this = a_rhs;
			}
			RotationJointAttributes& operator=(const RotationJointAttributes &a_rhs) {
				jai::property_owner<RotationJointAttributes>::operator=(a_rhs);
				return *this;
			}

			~RotationJointAttributes() {
				destroy();
			}

			RotationJointAttributes& torque(PointPrecision a_torque) {
				enableMotor = true;
				maxMotorTorque = a_torque;
				if (joint) {
					joint->EnableMotor(true);
					joint->SetMaxMotorTorque(a_torque);
				}
				return *this;
			}

			RotationJointAttributes& speed(PointPrecision a_speed) {
				enableMotor = true;
				motorSpeed = a_speed;
				if (joint) {
					joint->EnableMotor(true);
					joint->SetMotorSpeed(a_speed);
				}
				return *this;
			}

			RotationJointAttributes& removeMotor() {
				enableMotor = false;
				if (joint) {
					joint->EnableMotor(false);
				}
				return *this;
			}

			RotationJointAttributes& limits(PointPrecision a_min, PointPrecision a_max) {
				enableLimit = true;
				lowerAngle = a_min;
				upperAngle = a_max;
				if (joint) {
					joint->EnableLimit(true);
					joint->SetLimits(a_min, a_max);
				}
				return *this;
			}

			RotationJointAttributes& removeLimits() {
				enableLimit = false;
				if (joint) {
					joint->EnableLimit(false);
				}
				return *this;
			}

		protected:
			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				syncronize();
				property_mgr.save(archive);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				property_mgr.load(archive);
			}

		private:
			void initialize(const std::shared_ptr<Collider> &a_lhs, const std::shared_ptr<Collider> &a_rhs, Point<> a_offset);
			void loadedCollider(Collider* a_loaded);
			void syncronize() const;
			b2RevoluteJointDef definition() const;

			void destroy();

			int loadCount = 0;
			Collider* firstLoaded = nullptr;

			JAI_PROPERTY((std::weak_ptr<Collider>), A);
			JAI_PROPERTY((std::weak_ptr<Collider>), B);
			JAI_NAMED_PROPERTY((Point<>), "Offset", offset);
			JAI_NAMED_PROPERTY((Point<>), "Position", worldPosition);
			JAI_NAMED_PROPERTY((bool), "Motor", enableMotor, false);
			JAI_NAMED_PROPERTY((bool), "Limit", enableLimit, false);
			JAI_NAMED_PROPERTY((float), "Angle", referenceAngle, 0.0f);
			JAI_NAMED_PROPERTY((float), "Speed", motorSpeed, 0.0f);
			JAI_NAMED_PROPERTY((float), "MaxTorque", maxMotorTorque, 0.0f);
			JAI_NAMED_PROPERTY((float), "Min", lowerAngle, 0.0f);
			JAI_NAMED_PROPERTY((float), "Max", upperAngle, 0.0f);

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

		class CollisionPartAttributes : public jai::property_owner<CollisionPartAttributes> {
			friend Collider;
			friend jai::access;
		public:
			CollisionPartAttributes() = default;
			CollisionPartAttributes(const CollisionPartAttributes &a_rhs) : jai::property_owner<CollisionPartAttributes>() {
				*this = a_rhs;
			}
			CollisionPartAttributes& operator=(const CollisionPartAttributes &a_rhs) {
				jai::property_owner<CollisionPartAttributes>::operator=(a_rhs);
				return *this;
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
				ourCategoryBits = static_cast<uint16_t>(a_category);
				ourMaskBits = static_cast<uint16_t>(a_categoriesWeCanHit);
				return *this;
			}

			CollisionPartAttributes& filterCategory(int16_t a_category) {
				ourCategoryBits = static_cast<uint16_t>(a_category);
				return *this;
			}

			CollisionPartAttributes& addCollidableCategory(int16_t a_newCategory) {
				ourMaskBits = static_cast<uint16_t>(*ourMaskBits | static_cast<uint16_t>(a_newCategory));
				return *this;
			}

			CollisionPartAttributes& density(PointPrecision a_density) {
				ourDensity = a_density;
				return *this;
			}

			CollisionPartAttributes& sensor(bool a_isSensor) {
				ourSensor = a_isSensor;
				return *this;
			}

			CollisionPartAttributes& friction(PointPrecision a_friction) {
				ourFriction = a_friction;
				return *this;
			}

			CollisionPartAttributes& restitution(PointPrecision a_restitution) {
				ourRestitution = a_restitution;
				return *this;
			}

			b2FixtureDef definition() const {
				b2FixtureDef result;
				result.restitution = ourRestitution;
				result.friction = ourFriction;
				result.density = ourDensity;
				result.isSensor = ourSensor;
				result.filter.categoryBits = ourCategoryBits;
				result.filter.maskBits = ourMaskBits;
				return result;
			}

		protected:
			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				property_mgr.save(archive);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				property_mgr.load(archive);
			}

		private:
			JAI_NAMED_PROPERTY((float), "restitution", ourRestitution, 0.0f);
			JAI_NAMED_PROPERTY((float), "friction", ourFriction, 0.3f);
			JAI_NAMED_PROPERTY((float), "density", ourDensity, 1.0f);
			JAI_NAMED_PROPERTY((bool), "sensor", ourSensor, false);
			JAI_NAMED_PROPERTY((uint16_t), "filterCategory", ourCategoryBits, uint16_t(0x0001));
			JAI_NAMED_PROPERTY((uint16_t), "filterInteractions", ourMaskBits, uint16_t(0xFFFF));
			JAI_NAMED_PROPERTY((std::string), "id", ourId);
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

		class Collider : public jai::property_owner<Collider, Component> {
			friend Node;
			friend jai::access;
			friend CollisionBodyAttributes;
			friend RotationJointAttributes;
			friend ContactListener;

			jai::signal_emitter<void(CollisionParameters)> onCollisionStartSignal;
			jai::signal_emitter<void(CollisionParameters)> onCollisionEndSignal;
			jai::signal_emitter<void(size_t, CollisionParameters, const Point<> &)> onContactStartSignal;
			jai::signal_emitter<void(size_t, CollisionParameters)> onContactEndSignal;
			jai::signal_emitter<void(bool, CollisionParameters)> onCollisionKilledSignal; //first param true if A is destroyed otherwise B
			jai::signal_emitter<void(bool, size_t, CollisionParameters)> onContactKilledSignal; //first param true if A is destroyed otherwise B
			jai::signal_emitter<void(CollisionParameters, const Point<> &, bool&)> onShouldCollideSignal;
		public:
			jai::signal<void(CollisionParameters)> onCollisionStart;
			jai::signal<void(CollisionParameters)> onCollisionEnd;
			jai::signal<void(size_t, CollisionParameters, const Point<> &)> onContactStart;
			jai::signal<void(size_t, CollisionParameters)> onContactEnd;
			jai::signal<void(bool, CollisionParameters)> onCollisionKilled; //first param true if A is destroyed otherwise B
			jai::signal<void(bool, size_t, CollisionParameters)> onContactKilled; //first param true if A is destroyed otherwise B
			jai::signal<void(CollisionParameters, const Point<> &, bool&)> onShouldCollide;

			ComponentDerivedAccessors(Collider);

			virtual ~Collider();

			CollisionBodyAttributes& body() {
				return *collisionAttributes;
			}

			std::shared_ptr<Collider> attach(const BoxAABB<> &a_bounds, PointPrecision a_rotation = 0.0f, CollisionPartAttributes a_attributes = CollisionPartAttributes());
			std::shared_ptr<Collider> attach(PointPrecision a_diameter, const Point<> &a_position = Point<>(), CollisionPartAttributes a_attributes = CollisionPartAttributes());
			std::shared_ptr<Collider> attach(const std::vector<Point<>> &a_points, const Point<> &a_offset = Point<>(), CollisionPartAttributes a_attributes = CollisionPartAttributes());

			std::shared_ptr<RotationJointAttributes> rotationJoint(const std::shared_ptr<Collider> &a_other, const RotationJointAttributes &a_attributes, const Point<> &a_offset = Point<>()) {
				auto sharedAttributes = std::make_shared<RotationJointAttributes>();
				*sharedAttributes = a_attributes;
				sharedAttributes->initialize(std::static_pointer_cast<Collider>(shared_from_this()), a_other, a_offset);
				rotationJoints->push_back(sharedAttributes);
				a_other->rotationJoints->push_back(sharedAttributes);
				return sharedAttributes;
			}

			std::shared_ptr<RotationJointAttributes> rotationJoint(int a_index = 0) const {
				if (a_index < rotationJoints->size()) {
					return rotationJoints[a_index];
				} else {
					return nullptr;
				}
			}

			Point<> physicsPosition() const {
				auto percentOfWorldStep = static_cast<PointPrecision>(world->percentOfStep());
				return mix(previousPosition, *currentPosition, percentOfWorldStep);
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
					MV::visit(owner()->components<Collider>(), [&](const MV::Scene::SafeComponent<MV::Scene::Collider> &a_collider) {
						if (a_collider.self().get() != this) {
							a_collider->observePhysicsAngle(false);
						}
					});
				}
				return std::static_pointer_cast<Collider>(shared_from_this());
			}
			bool observePhysicsAngle() const {
				return useBodyAngle;
			}

			std::shared_ptr<Collider> observePhysicsPosition(bool a_newValue) {
				useBodyPosition = a_newValue;
				if (a_newValue == true) {
					MV::visit(owner()->components<Collider>(), [&](const MV::Scene::SafeComponent<MV::Scene::Collider> &a_collider) {
						if (a_collider.self().get() != this) {
							a_collider->observePhysicsPosition(false);
						}
					});
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
					CollisionPartAttributes* A = nullptr;
					CollisionPartAttributes* B = nullptr;
				};
				std::map<size_t, ContactInformationData> fixtureContacts;
			};
			typedef std::map<Collider*, ContactInformation> ContactMap;
			ContactMap contacts;
			b2Body *physicsBody = nullptr;

			JAI_PROPERTY((std::shared_ptr<Environment>), world);

			void attachInternal(const Size<> &a_size, const Point<> &a_position = Point<>(), PointPrecision a_rotation = 0.0f, CollisionPartAttributes a_attributes = CollisionPartAttributes());
			void attachInternal(PointPrecision a_diameter, const Point<> &a_position = Point<>(), CollisionPartAttributes a_attributes = CollisionPartAttributes());
			void attachInternal(const std::vector<Point<>> &a_points, const Point<> &a_offset = Point<>(), CollisionPartAttributes a_attributes = CollisionPartAttributes());

			template <class Archive>
			void save(Archive & archive, std::uint32_t const /*version*/) const {
				property_mgr.save(archive);
				Component::save(archive, 0);
			}

			template <class Archive>
			void load(Archive & archive, std::uint32_t const /*version*/) {
				property_mgr.load(archive);
				Component::load(archive, 0);
			}

			template <typename Archive>
			static void load_and_construct(Archive & ar, jai::serialization::construct<Collider> &construct) {
				construct(std::shared_ptr<Node>(), std::shared_ptr<Environment>(), CollisionBodyAttributes(), false);
				construct->loadedFromJson = true;
				construct->load(ar, 0);
				construct->initialize();
				construct->attachLoadedParts();
				for (auto&& joint : construct->rotationJoints) {
					joint->loadedCollider(construct.get_ptr().get());
				}
			}

			virtual std::shared_ptr<Component> cloneImplementation(const std::shared_ptr<Node> &a_parent) {
				return cloneHelper(a_parent->attach<Collider>(*world, *collisionAttributes).self());
			}

			virtual std::shared_ptr<Component> cloneHelper(const std::shared_ptr<Component> &a_clone);

		private:
			struct FixtureParameters : public jai::property_owner<FixtureParameters> {
				enum ShapeType {CIRCLE, RECTANGLE, POLYGON};

				FixtureParameters() = default;
				FixtureParameters(const FixtureParameters &a_rhs) : jai::property_owner<FixtureParameters>() {
					*this = a_rhs;
				}
				FixtureParameters& operator=(const FixtureParameters &a_rhs) {
					jai::property_owner<FixtureParameters>::operator=(a_rhs);
					return *this;
				}

				JAI_NAMED_PROPERTY((ShapeType), "type", shapeType, CIRCLE);
				JAI_PROPERTY((Size<>), size); //Rect
				JAI_PROPERTY((PointPrecision), rotation, 0.0f); //Rect
				JAI_PROPERTY((PointPrecision), diameter, 0.0f); //Circle
				JAI_PROPERTY((Point<>), position);
				JAI_PROPERTY((std::vector<Point<>>), points);
				JAI_PROPERTY((CollisionPartAttributes), attributes);

				template <class Archive>
				void save(Archive & archive, std::uint32_t const /*version*/) const {
					property_mgr.save(archive);
				}

				template <class Archive>
				void load(Archive & archive, std::uint32_t const /*version*/) {
					property_mgr.load(archive);
				}
			};
			typedef std::vector<std::shared_ptr<RotationJointAttributes>> RotationJointList;

			Collider(const std::weak_ptr<Node> &a_owner, CollisionBodyAttributes a_collisionAttributes = CollisionBodyAttributes(), bool a_maintainOwnerPosition = true);
			Collider(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<Environment> &a_world, CollisionBodyAttributes a_collisionAttributes = CollisionBodyAttributes(), bool a_maintainOwnerPosition = true);
			Collider(const std::weak_ptr<Node> &a_owner, const SafeComponent<Environment> &a_world, CollisionBodyAttributes a_collisionAttributes = CollisionBodyAttributes(), bool a_maintainOwnerPosition = true);
			Collider(const Collider&) = delete;
			Collider& operator=(const Collider&) = delete;
			Collider(Collider&&) = delete;
			Collider& operator=(Collider&&) = delete;

			void adoptAttributes(const CollisionBodyAttributes &a_collisionAttributes, bool a_maintainOwnerPosition);
			void attachLoadedParts();

			void updateInterpolatedPositionAndApply();

			void updatePhysicsPosition();
			virtual void updateImplementation(double a_dt) override;

			b2FixtureDef* getDefaultFixtureDefinition() {
				return &defaultFixtureDefinition;
			}

			void applyScenePositionUpdate(Point<> a_location, double a_angle);

			PointPrecision interpolateDrawAngle(PointPrecision a_percentOfStep) {
				PointPrecision current = *currentAngle;
				if (current == previousAngle) { return current; }
				bool wrapped;
				PointPrecision rotation = wrappingDistance(0.0f, 360.0f, current, previousAngle, &wrapped);
				if (wrapped) {
					if (previousAngle > current) {
						rotation = previousAngle + ((rotation)* a_percentOfStep);
					}
					else {
						rotation = previousAngle - ((rotation)* a_percentOfStep);
					}
				} else {
					if (previousAngle > current) {
						rotation = previousAngle - ((rotation)* a_percentOfStep);
					}
					else {
						rotation = previousAngle + ((rotation)* a_percentOfStep);
					}
				}
				return wrap(0.0f, 360.0f, rotation);
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

			JAI_PROPERTY((CollisionBodyAttributes), collisionAttributes);
			JAI_PROPERTY((std::vector<FixtureParameters>), collisionParts);
			std::map<b2Fixture*, CollisionPartAttributes> fixtureMap;

			JAI_PROPERTY((RotationJointList), rotationJoints, propertyNoClone<RotationJointList>());

			JAI_PROPERTY((PointPrecision), currentAngle, propertyNoClone<PointPrecision>());
			PointPrecision previousAngle = 0.0f;

			JAI_PROPERTY((Point<>), currentPosition, propertyNoClone<Point<>>()); //cast but not moved to local from world
			Point<> previousPosition;

			JAI_PROPERTY((bool), useBodyAngle, true);
			JAI_PROPERTY((bool), useBodyPosition, true);
			bool loadedFromJson = false;
		};
	}
}

#endif
