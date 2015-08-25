#ifndef _COLLISIONOBJECTS_H_
#define _COLLISIONOBJECTS_H_

#include "Render\package.h"
#include "Utility\package.h"
#include "Box2D\Box2D.h"
#include <list>
#include <map>

namespace MV {
	namespace Scene {
		//Scales units down by 100 when dealing with collisions.  This way if we work in pixels 100 pixels is 1 unit.
		//Box2D is tuned to deal with ranges 10 to .1 ideally, so this provides a good working scale from display size
		//to world simulation size.  Be aware when dealing with raw response data that it needs to be scaled up by this
		//value to translate properly.
		const PointPrecision CollisionScale = 100.0;
		//This is the time step at which Box2D updates.
		const Stopwatch::TimeType PhysicsTimeStep = 1.0 / 60.0;

		Point<> cast(b2Vec2 a_box2DPoint, PointPrecision a_z = 0.0f);

		template<typename T>
		b2Vec2 castToPhysics(Point<T> a_M2RendPoint) {
			return b2Vec2(static_cast<float32>(a_M2RendPoint.x) / CollisionScale, static_cast<float32>(a_M2RendPoint.y) / CollisionScale);
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

			CollisionBodyAttributes& angularVelocity(PointPrecision a_value);
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

		class Collider : public Drawable {
			friend CollisionBodyAttributes;
			
			Signal<void(std::shared_ptr<Collider>)> onCollisionStartSignal;
			Signal<void(std::shared_ptr<Collider>)> onCollisionEndSignal;
			Signal<void(std::shared_ptr<Collider>)> onContactStartSignal;
			Signal<void(std::shared_ptr<Collider>)> onContactEndSignal;
		public:
			SignalRegister<void(std::shared_ptr<Collider>)> onCollisionStart;
			SignalRegister<void(std::shared_ptr<Collider>)> onCollisionEnd;
			SignalRegister<void(std::shared_ptr<Collider>)> onContactStart;
			SignalRegister<void(std::shared_ptr<Collider>)> onContactEnd;

			DrawableDerivedAccessors(Collider);

			virtual ~Collider();

			CollisionBodyAttributes& body() {
				return collisionAttributes;
			}

			void update(double a_dt) {
				if (world->updatedThisFrame()) {
					previousLocation = currentLocation;
					previousAngle = currentAngle;
					currentLocation = cast(physicsBody->GetPosition(), owner()->position().z);
					if (useBodyAngle) {
						currentAngle = toDegrees(physicsBody->GetAngle());
					} else {
						currentAngle = owner()->worldRotation().z;
					}
				}
				auto percentOfStep = static_cast<PointPrecision>(world->percentOfStep());
				double z = currentLocation.z;
				Point<> interpolatedPoint = previousLocation + ((currentLocation - previousLocation) * percentOfStep);
				interpolatedPoint.z = z; //fix the z so it isn't scaled or modified;
				double interpolatedAngle = interpolateDrawAngle(percentOfStep);
				applyScenePositionUpdate(interpolatedPoint, interpolatedAngle);
			}

			void setDefaultShapeDefinition(const b2FixtureDef &a_shapeAttributes) {
				defaultFixtureDefinition = a_shapeAttributes;
			}

			void attachBoxShape(double a_width, double a_height, const Point<> &a_location = Point<>(), double a_rotation = 0.0, b2FixtureDef *a_shapeAttributes = nullptr) {
				if (a_shapeAttributes == nullptr) { a_shapeAttributes = getDefaultFixtureDefinition(); }
				b2Vec2 centerPoint = castToPhysics(a_location);
				a_width = (a_width / CollisionScale) / 2.0;
				a_height = (a_height / CollisionScale) / 2.0;

				b2PolygonShape shape;
				shape.SetAsBox(static_cast<float32>(a_width), static_cast<float32>(a_height), centerPoint, static_cast<float32>(toRadians(a_rotation)));

				b2FixtureDef shapeDef;
				shapeDef.shape = &shape;
				applyShapeDefinition(shapeDef, *a_shapeAttributes);
				physicsBody->CreateFixture(&shapeDef);
			}

			void attachCircleShape(double a_diameter, const Point<> &a_location = Point<>(), b2FixtureDef *a_shapeAttributes = nullptr) {
				if (a_shapeAttributes == nullptr) { a_shapeAttributes = getDefaultFixtureDefinition(); }
				b2CircleShape shape;
				shape.m_radius = static_cast<float32>((a_diameter / CollisionScale) / 2.0);
				shape.m_p = castToPhysics(a_location);

				b2FixtureDef shapeDef;
				shapeDef.shape = &shape;
				applyShapeDefinition(shapeDef, *a_shapeAttributes);
				physicsBody->CreateFixture(&shapeDef);
			}

			void addCollision(Collider* a_collisionWith, const b2Vec2 &a_normal) {
				contacts[a_collisionWith].normal = a_normal;
				if (++contacts[a_collisionWith].count == 1) {
					startCollision(a_collisionWith, a_normal);
				}
			}

			void removeCollision(Collider* a_collisionWith) {
				if (--contacts[a_collisionWith].count <= 0) {
					contacts.erase(a_collisionWith);
					endCollision(a_collisionWith, false);
				}
			}

			//lock or unlock the drawing angle to the physical body
			void applyPhysicsAngle() {
				useBodyAngle = true;
			}
			void ignorePhysicsAngle() {
				useBodyAngle = false;
			}
		protected:
			//derived collision response should happen here.
			virtual void startCollision(Collider* a_collisionWith, const b2Vec2 &a_normal) {};
			virtual void endCollision(Collider* a_collisionWith, bool a_objectDestroyed) {};

			b2FixtureDef defaultFixtureDefinition;

			struct ContactInformation {
				ContactInformation() :count(0) {}
				std::vector<b2Vec2> normal;
				unsigned int count;
			};
			typedef std::map<Collider*, ContactInformation> ContactMap;
			ContactMap contacts;
			b2Body *physicsBody;

			std::shared_ptr<Environment> world;
		private:
			Collider(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<Environment> &a_world, CollisionBodyAttributes a_collisionAttributes = CollisionBodyAttributes());

			template <class Archive>
			void serialize(Archive & archive) {
				collisionAttributes.syncronize();
				archive(
					cereal::make_nvp("collisionAttributes", collisionAttributes),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(this))
				);
			}

			template <class Archive>
			static void load_and_construct(Archive & archive, cereal::construct<Collider> &construct) {
				construct(std::shared_ptr<Node>());
				archive(
					cereal::make_nvp("collisionAttributes", construct->collisionAttributes),
					cereal::make_nvp("Drawable", cereal::base_class<Drawable>(construct.ptr()))
				);
				construct->initialize();
			}

			void initializeDefaultFixtureDefinition() {
				defaultFixtureDefinition.restitution = .3f;
				defaultFixtureDefinition.friction = .3f;
				defaultFixtureDefinition.density = 1.0f;
				defaultFixtureDefinition.isSensor = false;
			}

			b2FixtureDef* getDefaultFixtureDefinition() {
				return &defaultFixtureDefinition;
			}

			void applyScenePositionUpdate(Point<> a_location, double a_angle) {
				owner()->worldPosition(world->owner()->worldFromLocal(a_location));
				owner()->worldRotation(Point<>(0.0f, 0.0f, static_cast<PointPrecision>(a_angle)));
			}

			PointPrecision interpolateDrawAngle(PointPrecision a_percentOfStep) {
				if (currentAngle == previousAngle) { return currentAngle; }
				bool wrapped;
				double rotation = wrappingDistance(currentAngle, previousAngle, 0.0f, 360.0f, &wrapped);
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
				return wrap(rotation, 0.0, 360.0);
			}

			void deleteCollisionObject(Collider* a_collisionWith) {
				contacts.erase(a_collisionWith);
				endCollision(a_collisionWith, true);
			}

			void applyShapeDefinition(b2FixtureDef &a_applyTo, const b2FixtureDef &a_applicator) {
				a_applyTo.isSensor = a_applicator.isSensor;
				a_applyTo.density = collisionAttributes.isDynamic() ? a_applicator.density : 0.0f;
				a_applyTo.restitution = a_applicator.restitution;
				a_applyTo.filter = a_applicator.filter;
				a_applyTo.userData = ((void *)this);
			}

			CollisionBodyAttributes collisionAttributes;

			Point<> currentLocation;
			Point<> previousLocation;
			PointPrecision currentAngle;
			PointPrecision previousAngle;
			bool useBodyAngle;
		};

		class ContactListener : public b2ContactListener {
		public:
			virtual void BeginContact(b2Contact* contact) {
				Collider *fixtureAObject = static_cast<Collider*>(contact->GetFixtureA()->GetUserData());
				Collider *fixtureBObject = static_cast<Collider*>(contact->GetFixtureB()->GetUserData());
				b2WorldManifold worldManifold;
				contact->GetWorldManifold(&worldManifold);
				fixtureAObject->addCollision(fixtureBObject, worldManifold.normal);
				fixtureBObject->addCollision(fixtureAObject, -worldManifold.normal);
			}
			virtual void EndContact(b2Contact* contact) {
				Collider *fixtureAObject = static_cast<Collider*>(contact->GetFixtureA()->GetUserData());
				Collider *fixtureBObject = static_cast<Collider*>(contact->GetFixtureB()->GetUserData());
				b2WorldManifold worldManifold;
				contact->GetWorldManifold(&worldManifold);
				fixtureAObject->removeCollision(fixtureBObject);
				fixtureBObject->removeCollision(fixtureAObject);
			}
			virtual void PreSolve(b2Contact* contact, const b2Manifold* oldManifold) {
// 				int multiply = 0;
// 				if (static_cast<Collider*>(contact->GetFixtureA()->GetUserData())->getType() == Collider::PLATFORM) {
// 					b2WorldManifold worldManifold;
// 					contact->GetWorldManifold(&worldManifold);
// 
// 					if ((worldManifold.normal.y) > 0.5f) {
// 						contact->SetEnabled(false);
// 					}
// 				}
// 				else if (static_cast<Collider*>(contact->GetFixtureB()->GetUserData())->getType() == Collider::PLATFORM) {
// 					b2WorldManifold worldManifold;
// 					contact->GetWorldManifold(&worldManifold);
// 
// 					if ((worldManifold.normal.y) < -0.5f) {
// 						contact->SetEnabled(false);
// 					}
// 				}
			}
			virtual void PostSolve(b2Contact* contact, const b2Manifold* oldManifold) {}
		};

		class Environment : public Component {
		public:

			Point<> scenePositionFromPhysics(const Point<> &a_scaledBoxPoint) {
				
			}

			template<typename CollisionObjectType>
			std::shared_ptr<CollisionObjectType> makeCollisionObject(std::shared_ptr<DrawShape> a_drawShape, const b2BodyDef &a_collisionAttributes = b2BodyDef(), Collider::ObjectType a_objectType = Collider::NORMAL) {
				auto thisShared = shared_from_this();
				auto newObject = std::make_shared<CollisionObjectType>(thisShared, a_drawShape, a_collisionAttributes, a_objectType);
				collisionObjects.push_back(newObject);
				return newObject;
			}

			b2Body* createBody(const b2BodyDef& a_bodyDef = b2BodyDef()) {
				return world.CreateBody(&a_bodyDef);
			}

			double percentOfStep() const {
				return accumulatedDelta / PhysicsTimeStep;
			}

			bool updatedThisFrame() const {
				return steppedThisUpdate;
			}

			void update(double a_dt) {
				steppedThisUpdate = false;
				accumulatedDelta += a_dt;
				while (accumulatedDelta > PhysicsTimeStep) {
					world.Step(float32(PhysicsTimeStep), Box2dVelocityIterations, Box2dPositionIterations);
					accumulatedDelta -= PhysicsTimeStep;
					steppedThisUpdate = true;
				}
			}
		private:

			Environment(const std::weak_ptr<Node> &a_owner, const Point<> &a_gravity = Point<>(0.0f, -10.0f)) :
				Component(a_owner){
				world.SetGravity(castToPhysics(a_gravity));
				world.SetContactListener(&listener);
			}

			typedef std::vector<std::shared_ptr<Collider>> CollisionObjectList;
			CollisionObjectList collisionObjects;
			Stopwatch timer;
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
