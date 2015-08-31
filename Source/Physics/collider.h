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
		//Scales units down by 100 when dealing with collisions.  This way if we work in pixels 10 pixels is 1 unit.
		//Box2D is tuned to deal with ranges 10 to .1 ideally, so this provides a good working scale from display size
		//to world simulation size.  Be aware when dealing with raw response data that it needs to be scaled up by this
		//value to translate properly.
		const PointPrecision CollisionScale = 10.0;
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

		class Environment;

		class Collider;
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
			void serialize(Archive & archive) {
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
					cereal::make_nvp("parent", details.parent)
				);
			}
		private:
			void syncronize();

			std::weak_ptr<Collider> parent;
			b2BodyDef details;
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
			void serialize(Archive & archive) {
				archive(
					cereal::make_nvp("restitution", details.restitution),
					cereal::make_nvp("friction", details.friction),
					cereal::make_nvp("density", details.density),
					cereal::make_nvp("sensor", details.isSensor),
					cereal::make_nvp("filterCategory", details.filter.categoryBits),
					cereal::make_nvp("filterInteractions", details.filter.maskBits)
				);
			}
		private:
			
			b2FixtureDef details;
		};

		class ContactListener;
		class Collider : public Drawable {
			friend Node;
			friend cereal::access;
			friend CollisionBodyAttributes;
			friend ContactListener;
			
			Signal<void(std::shared_ptr<Collider>, std::shared_ptr<Collider>)> onCollisionStartSignal;
			Signal<void(std::shared_ptr<Collider>, std::shared_ptr<Collider>)> onCollisionEndSignal;
			Signal<void(size_t, std::shared_ptr<Collider>, std::shared_ptr<Collider>, Point<>)> onContactStartSignal;
			Signal<void(size_t, std::shared_ptr<Collider>, std::shared_ptr<Collider>)> onContactEndSignal;
			Signal<void(std::shared_ptr<Collider>, Collider*)> onCollisionKilledSignal;
			Signal<void(std::shared_ptr<Collider>, std::shared_ptr<Collider>, Point<>, bool&)> onShouldCollideSignal;
		public:
			SignalRegister<void(std::shared_ptr<Collider>, std::shared_ptr<Collider>)> onCollisionStart;
			SignalRegister<void(std::shared_ptr<Collider>, std::shared_ptr<Collider>)> onCollisionEnd;
			SignalRegister<void(size_t, std::shared_ptr<Collider>, std::shared_ptr<Collider>, Point<>)> onContactStart;
			SignalRegister<void(size_t, std::shared_ptr<Collider>, std::shared_ptr<Collider>)> onContactEnd;
			SignalRegister<void(std::shared_ptr<Collider>, Collider*)> onCollisionKilled;
			SignalRegister<void(std::shared_ptr<Collider>, std::shared_ptr<Collider>, Point<>, bool&)> onShouldCollide;

			DrawableDerivedAccessors(Collider);

			virtual ~Collider();

			CollisionBodyAttributes& body() {
				return collisionAttributes;
			}

			std::shared_ptr<Collider> attach(const Size<> &a_size, const Point<> &a_position = Point<>(), PointPrecision a_rotation = 0.0f, CollisionPartAttributes a_attributes = CollisionPartAttributes());
			std::shared_ptr<Collider> attach(PointPrecision a_diameter, const Point<> &a_position = Point<>(), CollisionPartAttributes a_attributes = CollisionPartAttributes());

			void addCollision(Collider* a_collisionWith, b2Contact* a_contact, const b2Vec2 &a_normal) {
				contacts[a_collisionWith].normals[a_contact] = a_normal;
				if (contacts[a_collisionWith].normals.size() == 1) {
					onCollisionStartSignal(std::static_pointer_cast<Collider>(shared_from_this()), std::static_pointer_cast<Collider>(a_collisionWith->shared_from_this()));
				}
				onContactStartSignal(reinterpret_cast<size_t>(a_contact), std::static_pointer_cast<Collider>(shared_from_this()), std::static_pointer_cast<Collider>(a_collisionWith->shared_from_this()), Point<>(a_normal.x, a_normal.y));
			}

			void removeCollision(Collider* a_collisionWith, b2Contact* a_contact) {
				auto foundCollision = contacts.find(a_collisionWith);
				if (foundCollision != contacts.end()) {
					foundCollision->second.normals.erase(a_contact);
					if (foundCollision->second.normals.empty()) {
						contacts.erase(foundCollision);
						onCollisionEndSignal(std::static_pointer_cast<Collider>(shared_from_this()), std::static_pointer_cast<Collider>(a_collisionWith->shared_from_this()));
					}
				}
				onContactEndSignal(reinterpret_cast<size_t>(a_contact), std::static_pointer_cast<Collider>(shared_from_this()), std::static_pointer_cast<Collider>(a_collisionWith->shared_from_this()));
			}

			//lock or unlock the drawing angle to the physical body
			std::shared_ptr<Collider> applyPhysicsAngle() {
				useBodyAngle = true;
			}
			std::shared_ptr<Collider> ignorePhysicsAngle() {
				useBodyAngle = false;
			}
		protected:
			b2FixtureDef defaultFixtureDefinition;

			virtual void initialize() override;

			struct ContactInformation {
				std::map<b2Contact*, b2Vec2> normals;
			};
			typedef std::map<Collider*, ContactInformation> ContactMap;
			ContactMap contacts;
			b2Body *physicsBody;

			SafeComponent<Environment> world;
		private:
			struct FixtureParameters {
				bool isCircle = true;
				Size<> size; //Rect
				PointPrecision rotation = 0.0f; //Rect
				PointPrecision diameter = 0.0f; //Circle
				Point<> position;
				CollisionPartAttributes attributes;

				template <class Archive>
				void serialize(Archive & archive) {
					archive(
						cereal::make_nvp("circle", isCircle),
						cereal::make_nvp("size", size),
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

			template <class Archive>
			void serialize(Archive & archive) {
				collisionAttributes.syncronize();
				archive(
					cereal::make_nvp("world", world),
					cereal::make_nvp("useBodyAngle", useBodyAngle),
					cereal::make_nvp("collisionAttributes", collisionAttributes),
					cereal::make_nvp("collisionParts", collisionParts),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Collider> &construct) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("world", construct->world),
					cereal::make_nvp("useBodyAngle", construct->useBodyAngle),
					cereal::make_nvp("collisionAttributes", construct->collisionAttributes),
					cereal::make_nvp("collisionParts", construct->collisionParts),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->initialize();
				for (auto&& attribute : construct->collisionParts) {
					if (attribute.isCircle) {
						construct->attach(attribute.diameter, attribute.position, attribute.attributes);
					} else {
						construct->attach(attribute.size, attribute.position, attribute.rotation, attribute.attributes);
					}
				}
			}

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
				contacts.erase(a_collisionWith);
				onCollisionKilledSignal(std::static_pointer_cast<Collider>(shared_from_this()), a_collisionWith);
			}

			CollisionBodyAttributes collisionAttributes;
			std::vector<FixtureParameters> collisionParts;

			Point<> currentPosition;
			Point<> previousPosition;
			PointPrecision currentAngle;
			PointPrecision previousAngle;
			b2Vec2 currentPhysicsPosition;
			float32 currentPhysicsAngle;
			bool useBodyAngle = true;
		};

		class ContactListener : public b2ContactListener {
		public:
			virtual void BeginContact(b2Contact* contact) {
				Collider *fixtureAObject = static_cast<Collider*>(contact->GetFixtureA()->GetUserData());
				Collider *fixtureBObject = static_cast<Collider*>(contact->GetFixtureB()->GetUserData());
				b2WorldManifold worldManifold;
				contact->GetWorldManifold(&worldManifold);
				fixtureAObject->addCollision(fixtureBObject, contact, worldManifold.normal);
				fixtureBObject->addCollision(fixtureAObject, contact, -worldManifold.normal);
			}

			virtual void EndContact(b2Contact* contact) {
				Collider *fixtureAObject = static_cast<Collider*>(contact->GetFixtureA()->GetUserData());
				Collider *fixtureBObject = static_cast<Collider*>(contact->GetFixtureB()->GetUserData());
				b2WorldManifold worldManifold;
				contact->GetWorldManifold(&worldManifold);
				fixtureAObject->removeCollision(fixtureBObject, contact);
				fixtureBObject->removeCollision(fixtureAObject, contact);
			}

			virtual void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) {
				auto sharedA = std::static_pointer_cast<Collider>(static_cast<Collider*>(contact->GetFixtureA()->GetUserData())->shared_from_this());
				auto sharedB = std::static_pointer_cast<Collider>(static_cast<Collider*>(contact->GetFixtureA()->GetUserData())->shared_from_this());

				b2WorldManifold worldManifold;
				contact->GetWorldManifold(&worldManifold);
				bool shouldCollide = true;

				sharedA->onShouldCollideSignal(sharedA, sharedB, Point<>(worldManifold.normal.x, worldManifold.normal.y), shouldCollide);
				sharedB->onShouldCollideSignal(sharedB, sharedA, Point<>(worldManifold.normal.x, worldManifold.normal.y) * -1.0f, shouldCollide);

				contact->SetEnabled(shouldCollide);
			}

			virtual void PostSolve(b2Contact* contact, const b2Manifold* oldManifold) {}
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
		private:
			Environment(const std::weak_ptr<Node> &a_owner, const Point<> &a_gravity = Point<>(0.0f, 1000.0f)) :
				Component(a_owner),
				world(castToPhysics(a_gravity)){

				world.SetGravity(castToPhysics(a_gravity));
				world.SetContactListener(&listener);
			}

			template <class Archive>
			void serialize(Archive & archive) {
				collisionAttributes.syncronize();
				archive(
					cereal::make_nvp("gravityX", world.GetGravity().x),
					cereal::make_nvp("gravityY", world.GetGravity().y),
					cereal::make_nvp("Component", cereal::base_class<Component>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Environment> &construct) {
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

			virtual void updateImplementation(double a_dt) override {
				steppedThisUpdate = false;
				accumulatedDelta += a_dt;
				accumulatedDelta = std::min(accumulatedDelta, PhysicsTimeStep * 10.0f);
				while (accumulatedDelta > PhysicsTimeStep) {
					world.Step(float32(PhysicsTimeStep), Box2dVelocityIterations, Box2dPositionIterations);
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
	}
}
#endif
