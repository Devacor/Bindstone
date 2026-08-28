#include "collider.h"
#include "MV/Utility/generalUtility.h"

#include <jaiscript/core/registrar.hpp>
#include <jaiscript/core/dynamic_binder.hpp>
#include <jaiscript/signals/signal_binding.hpp>
#include "MV/Utility/services.hpp"

static jai::registrar<MV::Scene::Environment, MV::Services, jai::bases<MV::Scene::Component>> _hookEnvironment("Environment",
	[](jai::dynamic_binder<MV::Scene::Environment>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Component>();
	builder.auto_bind();
});

static jai::registrar<MV::Scene::CollisionParameters, MV::Services> _hookCollisionParameters("CollisionParameters",
	[](jai::dynamic_binder<MV::Scene::CollisionParameters>& builder, const MV::Services&) {
	builder.method("colliderA", [](MV::Scene::CollisionParameters& a_self) { return a_self.safeColliderA().self(); });
	builder.method("colliderB", [](MV::Scene::CollisionParameters& a_self) { return a_self.safeColliderB().self(); });
	builder.method("fixtureIdA", [](MV::Scene::CollisionParameters& a_self) { return a_self.fixtureA ? a_self.fixtureA->id() : std::string(); });
	builder.method("fixtureIdB", [](MV::Scene::CollisionParameters& a_self) { return a_self.fixtureB ? a_self.fixtureB->id() : std::string(); });
});

static jai::registrar<MV::Scene::Collider, MV::Services, jai::bases<MV::Scene::Component>> _hookCollider("Collider",
	[](jai::dynamic_binder<MV::Scene::Collider>& builder, const MV::Services&) {
	builder.base_class<MV::Scene::Component>();

	auto& eng = builder.bound_engine();
	jai::bind_signal_type<void(MV::Scene::CollisionParameters)>(eng, "SignalCollision");
	jai::bind_signal_type<void(size_t, MV::Scene::CollisionParameters, const MV::Point<> &)>(eng, "SignalContactStart");
	jai::bind_signal_type<void(size_t, MV::Scene::CollisionParameters)>(eng, "SignalContactEnd");
	jai::bind_signal_type<void(bool, MV::Scene::CollisionParameters)>(eng, "SignalCollisionKilled");
	jai::bind_signal_type<void(bool, size_t, MV::Scene::CollisionParameters)>(eng, "SignalContactKilled");
	jai::bind_signal_type<void(MV::Scene::CollisionParameters, const MV::Point<> &, bool&)>(eng, "SignalShouldCollide");
	builder.property("onCollisionStart", [](MV::Scene::Collider& a_self) -> jai::signal<void(MV::Scene::CollisionParameters)>& { return a_self.onCollisionStart; }, nullptr);
	builder.property("onCollisionEnd", [](MV::Scene::Collider& a_self) -> jai::signal<void(MV::Scene::CollisionParameters)>& { return a_self.onCollisionEnd; }, nullptr);
	builder.property("onContactStart", [](MV::Scene::Collider& a_self) -> jai::signal<void(size_t, MV::Scene::CollisionParameters, const MV::Point<> &)>& { return a_self.onContactStart; }, nullptr);
	builder.property("onContactEnd", [](MV::Scene::Collider& a_self) -> jai::signal<void(size_t, MV::Scene::CollisionParameters)>& { return a_self.onContactEnd; }, nullptr);
	builder.property("onCollisionKilled", [](MV::Scene::Collider& a_self) -> jai::signal<void(bool, MV::Scene::CollisionParameters)>& { return a_self.onCollisionKilled; }, nullptr);
	builder.property("onContactKilled", [](MV::Scene::Collider& a_self) -> jai::signal<void(bool, size_t, MV::Scene::CollisionParameters)>& { return a_self.onContactKilled; }, nullptr);
	builder.property("onShouldCollide", [](MV::Scene::Collider& a_self) -> jai::signal<void(MV::Scene::CollisionParameters, const MV::Point<> &, bool&)>& { return a_self.onShouldCollide; }, nullptr);

	builder.auto_bind();
});

bool operator!=(const b2Vec2 &a_lhs, const b2Vec2 &a_rhs) {
	return !(a_lhs == a_rhs);
}

namespace MV {
	Point<> cast(b2Vec2 a_box2DPoint, PointPrecision a_z) {
		return Point<>(static_cast<PointPrecision>(a_box2DPoint.x) * MV::Scene::CollisionScale, static_cast<PointPrecision>(a_box2DPoint.y) * MV::Scene::CollisionScale, a_z);
	}

	namespace Scene {

		Collider::Collider(const std::weak_ptr<Node> &a_owner, CollisionBodyAttributes a_collisionAttributes, bool a_maintainOwnerPosition):
			jai::property_owner<Collider, Component>(a_owner),
			onCollisionStart(onCollisionStartSignal),
			onCollisionEnd(onCollisionEndSignal),
			onContactStart(onContactStartSignal),
			onContactEnd(onContactEndSignal),
			onCollisionKilled(onCollisionKilledSignal),
			onShouldCollide(onShouldCollideSignal),
			onContactKilled(onContactKilledSignal){

			world = a_owner.lock()->componentInParents<Environment>().self();
			adoptAttributes(a_collisionAttributes, a_maintainOwnerPosition);
		}

		Collider::Collider(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<Environment> &a_world, CollisionBodyAttributes a_collisionAttributes, bool a_maintainOwnerPosition):
			jai::property_owner<Collider, Component>(a_owner),
			onCollisionStart(onCollisionStartSignal),
			onCollisionEnd(onCollisionEndSignal),
			onContactStart(onContactStartSignal),
			onContactEnd(onContactEndSignal),
			onCollisionKilled(onCollisionKilledSignal),
			onShouldCollide(onShouldCollideSignal),
			onContactKilled(onContactKilledSignal){

			world = a_world;
			adoptAttributes(a_collisionAttributes, a_maintainOwnerPosition);
		}

		Collider::Collider(const std::weak_ptr<Node> &a_owner, const SafeComponent<Environment> &a_world, CollisionBodyAttributes a_collisionAttributes, bool a_maintainOwnerPosition) :
			jai::property_owner<Collider, Component>(a_owner),
			onCollisionStart(onCollisionStartSignal),
			onCollisionEnd(onCollisionEndSignal),
			onContactStart(onContactStartSignal),
			onContactEnd(onContactEndSignal),
			onCollisionKilled(onCollisionKilledSignal),
			onShouldCollide(onShouldCollideSignal),
			onContactKilled(onContactKilledSignal) {

			world = a_world.self();
			adoptAttributes(a_collisionAttributes, a_maintainOwnerPosition);
		}

		void Collider::adoptAttributes(const CollisionBodyAttributes &a_collisionAttributes, bool a_maintainOwnerPosition) {
			collisionAttributes = a_collisionAttributes;
			if (a_maintainOwnerPosition) {
				collisionAttributes->physicsPosition(castToPhysics(world->owner()->localFromWorld(owner()->worldPosition())));
			}
		}

		std::shared_ptr<Component> Collider::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Component::cloneHelper(a_clone);
			auto colliderClone = std::static_pointer_cast<Collider>(a_clone);
			for (auto&& attribute : collisionParts) {
				if (attribute.shapeType == FixtureParameters::CIRCLE) {
					colliderClone->attachInternal(*attribute.diameter, *attribute.position, *attribute.attributes);
				} else if(attribute.shapeType == FixtureParameters::RECTANGLE) {
					colliderClone->attachInternal(*attribute.size, *attribute.position, *attribute.rotation, *attribute.attributes);
				} else if (attribute.shapeType == FixtureParameters::POLYGON) {
					colliderClone->attachInternal(*attribute.points, *attribute.position, *attribute.attributes);
				}
			}
			return a_clone;
		}

		void Collider::attachLoadedParts() {
			std::vector<FixtureParameters> loadedParts;
			loadedParts.swap(*collisionParts);
			for (auto&& attribute : loadedParts) {
				if (attribute.shapeType == FixtureParameters::CIRCLE) {
					attachInternal(*attribute.diameter, *attribute.position, *attribute.attributes);
				} else if (attribute.shapeType == FixtureParameters::RECTANGLE) {
					attachInternal(*attribute.size, *attribute.position, *attribute.rotation, *attribute.attributes);
				} else if (attribute.shapeType == FixtureParameters::POLYGON) {
					attachInternal(*attribute.points, *attribute.position, *attribute.attributes);
				}
			}
		}

		void Collider::applyScenePositionUpdate(Point<> a_location, double a_angle) {
			owner()->position(a_location);
			owner()->worldRotation(Point<>(0.0f, 0.0f, static_cast<PointPrecision>(a_angle)));
		}

		void Collider::initialize() {
			Component::initialize();
			if (!loadedFromJson) {
				MV::visit(owner()->components<Collider>(), [&](const MV::Scene::SafeComponent<MV::Scene::Collider> &a_collider) {
					if (a_collider.self().get() != this) {
						if (a_collider->observePhysicsAngle()) {
							useBodyAngle = false;
						}
						if (a_collider->observePhysicsPosition()) {
							useBodyPosition = false;
						}
					}
				});
			}
			physicsBody = world->createBody(collisionAttributes->definition());
			physicsBody->SetUserData((void *)this);
			if (!loadedFromJson && collisionAttributes->parent->expired()) {
				collisionAttributes->parent = std::static_pointer_cast<Collider>(shared_from_this());
				previousPosition = body().position();
				currentPosition = previousPosition;
				previousAngle = owner()->worldRotation().z;
				currentAngle = previousAngle;
			} else {
				previousPosition = *currentPosition;
				previousAngle = *currentAngle;
			}
		}

		Collider::~Collider() {
			std::vector<Collider*> contactListToDestroy;
			std::for_each(contacts.begin(), contacts.end(), [&](ContactMap::value_type contact) {
				contactListToDestroy.push_back(contact.first);
			});
			for (auto&& contact : contactListToDestroy) {
				contact->deleteCollisionObject(this);
			}
			std::for_each(rotationJoints.begin(), rotationJoints.end(), [&](const std::shared_ptr<RotationJointAttributes> &joint) {
				joint->destroy();
			});
			for (auto&& fixture : fixtureMap) {
				fixture.first->SetUserData(nullptr);
			}
			physicsBody->SetUserData(nullptr);
			world->destroyBody(physicsBody);
		}

		void Collider::updateImplementation(double a_dt) {
			if (!body().isStatic()) {
				updatePhysicsPosition();
				updateInterpolatedPositionAndApply();
			}
		}

		void Collider::updateInterpolatedPositionAndApply() {
			auto percentOfStep = static_cast<PointPrecision>(world->percentOfStep());
			if (useBodyPosition && useBodyAngle) {
				auto percentOfStep = static_cast<PointPrecision>(world->percentOfStep());
				Point<> interpolatedPoint = physicsLocalPosition();
				double interpolatedAngle = interpolateDrawAngle(percentOfStep);

				applyScenePositionUpdate(interpolatedPoint, *currentAngle);
			} else if (useBodyPosition) {
				auto percentOfStep = static_cast<PointPrecision>(world->percentOfStep());
				Point<> interpolatedPoint = physicsLocalPosition();

				owner()->position(interpolatedPoint);
			} else if (useBodyAngle) {
				double interpolatedAngle = interpolateDrawAngle(percentOfStep);
				owner()->worldRotation(Point<>(0.0f, 0.0f, static_cast<PointPrecision>(interpolatedAngle)));
			}
		}

		void Collider::updatePhysicsPosition() {
			if (world->updatedThisFrame()){
				previousPosition = *currentPosition;
				currentPosition = body().position();

				previousAngle = *currentAngle;
				currentAngle = toDegrees(physicsBody->GetAngle());
			}
		}

		std::shared_ptr<Collider> Collider::attach(const BoxAABB<> &a_bounds, PointPrecision a_rotation /*= 0.0f*/, CollisionPartAttributes a_attributes /*= CollisionPartAttributes()*/) {
			attachInternal(a_bounds.size(), a_bounds.topLeftPoint(), a_rotation, a_attributes);
			return std::static_pointer_cast<Collider>(shared_from_this());
		}

		void Collider::attachInternal(const Size<> &a_size, const Point<> &a_position /*= Point<>()*/, PointPrecision a_rotation /*= 0.0f*/, CollisionPartAttributes a_attributes /*= CollisionPartAttributes()*/) {
			b2Vec2 centerPoint = castToPhysics(a_position);
			auto shapeSize = castToPhysics(a_size / 2.0f); //using half width and half height

			b2PolygonShape shape;
			shape.SetAsBox(static_cast<float32>(shapeSize.x), static_cast<float32>(shapeSize.y), centerPoint, static_cast<float32>(toRadians(a_rotation)));

			auto definition = a_attributes.definition();
			definition.shape = &shape;
			definition.userData = ((void *)this);
			fixtureMap[physicsBody->CreateFixture(&definition)] = a_attributes;

			FixtureParameters paramsToStore;
			paramsToStore.shapeType = FixtureParameters::RECTANGLE;
			paramsToStore.size = a_size;
			paramsToStore.rotation = a_rotation;
			paramsToStore.position = a_position;
			paramsToStore.attributes = a_attributes;

			collisionParts->push_back(paramsToStore);
		}

		std::shared_ptr<Collider> Collider::attach(PointPrecision a_diameter, const Point<> &a_position /*= Point<>()*/, CollisionPartAttributes a_attributes /*= CollisionPartAttributes()*/) {
			attachInternal(a_diameter, a_position, a_attributes);
			return std::static_pointer_cast<Collider>(shared_from_this());
		}

		void Collider::attachInternal(PointPrecision a_diameter, const Point<> &a_position /*= Point<>()*/, CollisionPartAttributes a_attributes /*= CollisionPartAttributes()*/) {
			b2CircleShape shape;
			shape.m_radius = (a_diameter / 2.0f) / CollisionScale;
			shape.m_p = castToPhysics(a_position);

			auto definition = a_attributes.definition();
			definition.shape = &shape;
			definition.userData = ((void *)this);

			fixtureMap[physicsBody->CreateFixture(&definition)] = a_attributes;

			FixtureParameters paramsToStore;
			paramsToStore.shapeType = FixtureParameters::CIRCLE;
			paramsToStore.diameter = a_diameter;
			paramsToStore.position = a_position;
			paramsToStore.attributes = a_attributes;

			collisionParts->push_back(paramsToStore);
		}

		std::shared_ptr<Collider> Collider::attach(const std::vector<Point<>> &a_points, const Point<> &a_offset /*= Point<>()*/, CollisionPartAttributes a_attributes /*= CollisionPartAttributes()*/) {
			attachInternal(a_points, a_offset, a_attributes);
			return std::static_pointer_cast<Collider>(shared_from_this());
		}

		void Collider::attachInternal(const std::vector<Point<>> &a_points, const Point<> &a_offset /*= Point<>()*/, CollisionPartAttributes a_attributes /*= CollisionPartAttributes()*/) {
			std::vector<b2Vec2> convertedPoints(a_points.size());
			std::transform(a_points.begin(), a_points.end(), convertedPoints.begin(), [&](const Point<> &a_point) {return castToPhysics((a_point + a_offset)); });

			b2PolygonShape shape;
			shape.Set(&(convertedPoints[0]), static_cast<int32_t>(convertedPoints.size()));

			auto definition = a_attributes.definition();
			definition.shape = &shape;
			definition.userData = ((void *)this);

			fixtureMap[physicsBody->CreateFixture(&definition)] = a_attributes;

			FixtureParameters paramsToStore;
			paramsToStore.shapeType = FixtureParameters::POLYGON;
			paramsToStore.points = a_points;
			paramsToStore.position = a_offset;
			paramsToStore.attributes = a_attributes;

			collisionParts->push_back(paramsToStore);
		}

		CollisionBodyAttributes::CollisionBodyAttributes() {
		}

		b2BodyDef CollisionBodyAttributes::definition() const {
			b2BodyDef result;
			result.position = physicsPosition();
			result.angle = bodyAngle;
			result.linearVelocity.Set(velocityX, velocityY);
			result.angularVelocity = bodyAngularVelocity;
			result.linearDamping = bodyLinearDamping;
			result.angularDamping = bodyAngularDamping;
			result.allowSleep = bodyAllowSleep;
			result.awake = bodyAwake;
			result.fixedRotation = bodyFixedRotation;
			result.bullet = bodyBullet;
			result.type = bodyType;
			result.active = bodyActive;
			result.gravityScale = bodyGravityScale;
			return result;
		}

		void CollisionBodyAttributes::physicsPosition(const b2Vec2 &a_position) {
			positionX = a_position.x;
			positionY = a_position.y;
		}

		b2Vec2 CollisionBodyAttributes::physicsPosition() const {
			return b2Vec2(positionX, positionY);
		}

		CollisionBodyAttributes& CollisionBodyAttributes::stop() {
			velocityX = 0.0f;
			velocityY = 0.0f;
			bodyAngularVelocity = 0.0f;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetLinearVelocity(b2Vec2(0, 0));
				lockedParent->physicsBody->SetAngularVelocity(0);
			}
			return *this;
		}

		bool CollisionBodyAttributes::isStatic() const {
			return bodyType == b2_staticBody;
		}

		bool CollisionBodyAttributes::isDynamic() const {
			return bodyType == b2_dynamicBody;
		}

		bool CollisionBodyAttributes::isKinematic() const {
			return bodyType == b2_kinematicBody;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::makeStatic() {
			bodyType = b2_staticBody;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetType(b2_staticBody);
			}
			return *this;
		}
		CollisionBodyAttributes& CollisionBodyAttributes::makeDynamic() {
			bodyType = b2_dynamicBody;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetType(b2_dynamicBody);
			}
			return *this;
		}
		CollisionBodyAttributes& CollisionBodyAttributes::makeKinematic() {
			bodyType = b2_kinematicBody;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetType(b2_kinematicBody);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::bullet(bool a_isBullet) {
			bodyBullet = a_isBullet;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetBullet(a_isBullet);
			}
			return *this;
		}

		bool CollisionBodyAttributes::bullet() const {
			return bodyBullet;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::position(const Point<> &a_position) {
			physicsPosition(castToPhysics(a_position));
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetTransform(physicsPosition(), lockedParent->physicsBody->GetAngle());
				lockedParent->currentPosition = cast(physicsPosition(), lockedParent->owner()->position().z);
				lockedParent->previousPosition = *lockedParent->currentPosition;
				lockedParent->updateInterpolatedPositionAndApply();
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::impulse(const Point<> &a_impulse) {
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->ApplyLinearImpulse(castToPhysics(a_impulse), lockedParent->physicsBody->GetPosition(), true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::force(const Point<> &a_force) {
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->ApplyForceToCenter(castToPhysics(a_force), true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::impulse(const Point<> &a_impulse, const Point<> &a_position) {
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->ApplyLinearImpulse(castToPhysics(a_impulse), castToPhysics(a_position), true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::force(const Point<> &a_force, const Point<> &a_position) {
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->ApplyForce(castToPhysics(a_force), castToPhysics(a_position), true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::torque(PointPrecision a_amount) {
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->ApplyTorque(a_amount, true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::angularImpulse(PointPrecision a_amount) {
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->ApplyAngularImpulse(a_amount, true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::angle(PointPrecision a_newAngle) {
			bodyAngle = toRadians(a_newAngle);
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetTransform(lockedParent->physicsBody->GetPosition(), bodyAngle);
				lockedParent->currentAngle = *bodyAngle;
				lockedParent->previousAngle = *bodyAngle;
				lockedParent->updateInterpolatedPositionAndApply();
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::transform(const Point<> &a_position, PointPrecision a_newAngle) {
			physicsPosition(castToPhysics(a_position));
			bodyAngle = toRadians(a_newAngle);
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetTransform(physicsPosition(), bodyAngle);
				lockedParent->currentPosition = cast(physicsPosition(), lockedParent->owner()->position().z);
				lockedParent->previousPosition = *lockedParent->currentPosition;
				lockedParent->currentAngle = *bodyAngle;
				lockedParent->previousAngle = *bodyAngle;
				lockedParent->updateInterpolatedPositionAndApply();
			}
			return *this;
		}

		PointPrecision CollisionBodyAttributes::angle() const {
			if (auto lockedParent = parent->lock()) {
				return toDegrees(lockedParent->physicsBody->GetAngle());
			} else {
				return toDegrees(*bodyAngle);
			}
		}

		Point<> CollisionBodyAttributes::position() const {
			if (auto lockedParent = parent->lock()) {
				auto pysicsPosition = lockedParent->physicsBody->GetPosition();
				return cast(pysicsPosition, lockedParent->owner()->position().z);
			} else {
				return cast(physicsPosition());
			}
		}

		CollisionBodyAttributes& CollisionBodyAttributes::gravityScale(PointPrecision a_newScale) {
			bodyGravityScale = a_newScale;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetGravityScale(a_newScale);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::disableRotation() {
			bodyFixedRotation = true;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetFixedRotation(true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::enableRotation() {
			bodyFixedRotation = false;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetFixedRotation(false);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::velocity(const Point<> &a_velocity) {
			auto physicsVelocity = castToPhysics(a_velocity);
			velocityX = physicsVelocity.x;
			velocityY = physicsVelocity.y;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetLinearVelocity(physicsVelocity);
			}
			return *this;
		}

		Point<> CollisionBodyAttributes::velocity() const {
			if (auto lockedParent = parent->lock()) {
				return cast(lockedParent->physicsBody->GetLinearVelocity());
			}
			return cast(b2Vec2(velocityX, velocityY));
		}

		CollisionBodyAttributes& CollisionBodyAttributes::angularVelocity(PointPrecision a_value) {
			bodyAngularVelocity = a_value;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetAngularVelocity(a_value);
			}
			return *this;
		}

		PointPrecision CollisionBodyAttributes::angularVelocity() const {
			if (auto lockedParent = parent->lock()) {
				return lockedParent->physicsBody->GetAngularVelocity();
			}
			return bodyAngularVelocity;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::movementDamping(PointPrecision a_value) {
			bodyLinearDamping = a_value;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetLinearDamping(a_value);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::angularDamping(PointPrecision a_value) {
			bodyAngularDamping = a_value;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetAngularDamping(a_value);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::allowSleep() {
			bodyAllowSleep = true;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetSleepingAllowed(true);
			}
			return *this;
		}
		CollisionBodyAttributes& CollisionBodyAttributes::disallowSleep() {
			bodyAllowSleep = false;
			if (auto lockedParent = parent->lock()) {
				lockedParent->physicsBody->SetSleepingAllowed(false);
			}
			return *this;
		}

		void CollisionBodyAttributes::syncronize() const {
			if (auto lockedParent = parent->lock()) {
				b2Body* body = lockedParent->physicsBody;
				auto& self = const_cast<CollisionBodyAttributes&>(*this);
				self.physicsPosition(body->GetPosition());
				self.bodyAngle = body->GetAngle();
				self.velocityX = body->GetLinearVelocity().x;
				self.velocityY = body->GetLinearVelocity().y;
				self.bodyAngularVelocity = body->GetAngularVelocity();
				self.bodyLinearDamping = body->GetLinearDamping();
				self.bodyAngularDamping = body->GetAngularDamping();
				self.bodyAllowSleep = body->IsSleepingAllowed();
				self.bodyAwake = body->IsAwake();
				self.bodyFixedRotation = body->IsFixedRotation();
				self.bodyBullet = body->IsBullet();
				self.bodyType = body->GetType();
				self.bodyActive = body->IsActive();
				self.bodyGravityScale = body->GetGravityScale();
			}
		}

		std::shared_ptr<Component> Environment::cloneHelper(const std::shared_ptr<Component> &a_clone) {
			Component::cloneHelper(a_clone);
			auto colliderClone = std::static_pointer_cast<Environment>(a_clone);
			return a_clone;
		}

		void ContactListener::RunQueuedCallbacks() {
			while (!contactCallbacks.empty()) {
				auto contact = contactCallbacks.front();
				contactCallbacks.pop();

				Collider *fixtureAObject = static_cast<Collider*>(contact.A->GetUserData());
				Collider *fixtureBObject = static_cast<Collider*>(contact.B->GetUserData());
				if (fixtureAObject && fixtureBObject) {
					auto attributesAIt = fixtureAObject->fixtureMap.find(contact.A);
					auto attributesBIt = fixtureBObject->fixtureMap.find(contact.B);
					CollisionPartAttributes* attributesA = (attributesAIt != fixtureAObject->fixtureMap.end()) ? &(attributesAIt->second)  : nullptr;
					CollisionPartAttributes* attributesB = (attributesBIt != fixtureBObject->fixtureMap.end()) ? &(attributesBIt->second) : nullptr;
					if (contact.beginContact) {
						fixtureAObject->addCollision(fixtureBObject, attributesA, attributesB, contact.contactId, contact.normal);
						fixtureBObject->addCollision(fixtureAObject, attributesB, attributesA, contact.contactId, -contact.normal);
					} else {
						fixtureAObject->removeCollision(fixtureBObject, attributesA, attributesB, contact.contactId);
						fixtureBObject->removeCollision(fixtureAObject, attributesB, attributesA, contact.contactId);
					}
				}
			}
		}

		void ContactListener::BeginContact(b2Contact* a_contact) {
			b2WorldManifold worldManifold;
			a_contact->GetWorldManifold(&worldManifold);
			contactCallbacks.push(ContactDetails(reinterpret_cast<size_t>(a_contact), a_contact->GetFixtureA(), a_contact->GetFixtureB(), worldManifold.normal));
		}

		void ContactListener::EndContact(b2Contact* a_contact) {
			contactCallbacks.push(ContactDetails(reinterpret_cast<size_t>(a_contact), a_contact->GetFixtureA(), a_contact->GetFixtureB()));
		}

		void ContactListener::PreSolve(b2Contact* contact, const b2Manifold* oldManifold) {
			Collider* contactA = static_cast<Collider*>(contact->GetFixtureA()->GetUserData());
			Collider* contactB = static_cast<Collider*>(contact->GetFixtureB()->GetUserData());

			b2WorldManifold worldManifold;
			contact->GetWorldManifold(&worldManifold);
			bool shouldCollide = true;

			auto attributesAIt = contactA->fixtureMap.find(contact->GetFixtureA());
			auto attributesBIt = contactB->fixtureMap.find(contact->GetFixtureB());
			CollisionPartAttributes* attributesA = (attributesAIt != contactA->fixtureMap.end()) ? &(attributesAIt->second) : nullptr;
			CollisionPartAttributes* attributesB = (attributesBIt != contactB->fixtureMap.end()) ? &(attributesBIt->second) : nullptr;

			contactA->onShouldCollideSignal(CollisionParameters(attributesA, contactA, attributesB, contactB), Point<>(worldManifold.normal.x, worldManifold.normal.y), shouldCollide);
			contactB->onShouldCollideSignal(CollisionParameters(attributesB, contactB, attributesA, contactA), Point<>(worldManifold.normal.x, worldManifold.normal.y) * -1.0f, shouldCollide);

			contact->SetEnabled(shouldCollide);
		}

		b2RevoluteJointDef RotationJointAttributes::definition() const {
			b2RevoluteJointDef result;
			result.enableMotor = enableMotor;
			result.enableLimit = enableLimit;
			result.referenceAngle = referenceAngle;
			result.motorSpeed = motorSpeed;
			result.maxMotorTorque = maxMotorTorque;
			result.lowerAngle = lowerAngle;
			result.upperAngle = upperAngle;
			return result;
		}

		void RotationJointAttributes::initialize(const std::shared_ptr<Collider> &a_lhs, const std::shared_ptr<Collider> &a_rhs, Point<> a_offset) {
			A = a_lhs;
			B = a_rhs;

			offset = a_offset;
			auto jointDef = definition();
			jointDef.Initialize(a_lhs->physicsBody, a_rhs->physicsBody, castToPhysics(a_lhs->world->owner()->localFromWorld(a_lhs->owner()->worldFromLocal(a_offset))));
			joint = a_lhs->world->createJoint<b2RevoluteJoint>(jointDef);
			joint->SetUserData(a_lhs->world->getWorld());
		}

		void RotationJointAttributes::loadedCollider(Collider* a_loaded) {
			if (++loadCount == 2) {
				Collider *loadedA = nullptr;
				Collider *loadedB = nullptr;
				b2World* world = nullptr;
				if (!A->expired() && firstLoaded == A->lock().get()) {
					loadedA = firstLoaded;
					loadedB = a_loaded;
					world = firstLoaded->world->getWorld();
				} else {
					loadedA = a_loaded;
					loadedB = firstLoaded;
					world = a_loaded->world->getWorld();
				}
				auto jointDef = definition();
				jointDef.Initialize(loadedA->physicsBody, loadedB->physicsBody, castToPhysics(*worldPosition));
				joint = static_cast<b2RevoluteJoint*>(world->CreateJoint(&jointDef));
				joint->SetUserData(world);
			} else {
				firstLoaded = a_loaded;
			}
		}

		void RotationJointAttributes::syncronize() const {
			auto& self = const_cast<RotationJointAttributes&>(*this);
			self.worldPosition = Point<>();
			if (joint) {
				auto lockedA = A->lock();
				auto lockedB = B->lock();
				self.motorSpeed = joint->GetMotorSpeed();
				self.maxMotorTorque = joint->GetMaxMotorTorque();
				self.referenceAngle = lockedA->physicsBody->GetAngle() - lockedB->physicsBody->GetAngle() - joint->GetJointAngle();
				self.worldPosition = lockedA->world->owner()->localFromWorld(lockedA->owner()->worldFromLocal(*offset));
			}
		}

		void RotationJointAttributes::destroy() {
			if (auto lockedB = B->lock()) {
				lockedB->rotationJoints->erase(std::remove_if(lockedB->rotationJoints->begin(), lockedB->rotationJoints->end(), [&](const std::shared_ptr<RotationJointAttributes> &a_object) { return a_object.get() == this; }), lockedB->rotationJoints->end());
			}
			if (auto lockedA = A->lock()) {
				lockedA->rotationJoints->erase(std::remove_if(lockedA->rotationJoints->begin(), lockedA->rotationJoints->end(), [&](const std::shared_ptr<RotationJointAttributes> &a_object) { return a_object.get() == this; }), lockedA->rotationJoints->end());
			}
			if (joint) {
				b2World* world = reinterpret_cast<b2World*>(joint->GetUserData());
				world->DestroyJoint(joint);
				joint = nullptr;
			}
		}

		SafeComponent<Collider> CollisionParameters::safeColliderA() const {
			return colliderA->safe();
		}

		SafeComponent<Collider> CollisionParameters::safeColliderB() const {
			return colliderB->safe();
		}

	}
}
