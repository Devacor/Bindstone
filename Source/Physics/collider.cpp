#include "collider.h"
#include "Utility/generalUtility.h"

bool operator!=(const b2Vec2 &a_lhs, const b2Vec2 &a_rhs) {
	return !(a_lhs == a_rhs);
}

namespace MV {
	namespace Scene {
		Point<> cast(b2Vec2 a_box2DPoint, PointPrecision a_z) {
			return Point<>(static_cast<PointPrecision>(a_box2DPoint.x) * CollisionScale, static_cast<PointPrecision>(a_box2DPoint.y) * CollisionScale, a_z);
		}

		Collider::Collider(const std::weak_ptr<Node> &a_owner, CollisionBodyAttributes a_collisionAttributes, bool a_maintainOwnerPosition):
			Drawable(a_owner),
			world(a_owner.lock()->componentInParents<Environment>()),
			collisionAttributes(a_collisionAttributes),
			onCollisionStart(onCollisionStartSignal),
			onCollisionEnd(onCollisionEndSignal),
			onContactStart(onContactStartSignal),
			onContactEnd(onContactEndSignal),
			onCollisionKilled(onCollisionKilledSignal),
			onShouldCollide(onShouldCollideSignal){

			if (a_maintainOwnerPosition) {
				collisionAttributes.details.position = castToPhysics(world->owner()->localFromWorld(owner()->worldPosition()));
			}
		}

		Collider::Collider(const std::weak_ptr<Node> &a_owner, const std::shared_ptr<Environment> &a_world, CollisionBodyAttributes a_collisionAttributes, bool a_maintainOwnerPosition):
			Drawable(a_owner),
			world(a_world->safe()),
			collisionAttributes(a_collisionAttributes),
			onCollisionStart(onCollisionStartSignal),
			onCollisionEnd(onCollisionEndSignal),
			onContactStart(onContactStartSignal),
			onContactEnd(onContactEndSignal),
			onCollisionKilled(onCollisionKilledSignal),
			onShouldCollide(onShouldCollideSignal) {

			if (a_maintainOwnerPosition) {
				collisionAttributes.details.position = castToPhysics(world->owner()->localFromWorld(owner()->worldPosition()));
			}
		}

		Collider::Collider(const std::weak_ptr<Node> &a_owner, const SafeComponent<Environment> &a_world, CollisionBodyAttributes a_collisionAttributes, bool a_maintainOwnerPosition) :
			Drawable(a_owner),
			world(a_world),
			collisionAttributes(a_collisionAttributes),
			onCollisionStart(onCollisionStartSignal),
			onCollisionEnd(onCollisionEndSignal),
			onContactStart(onContactStartSignal),
			onContactEnd(onContactEndSignal),
			onCollisionKilled(onCollisionKilledSignal),
			onShouldCollide(onShouldCollideSignal) {

			if (a_maintainOwnerPosition) {
				collisionAttributes.details.position = castToPhysics(world->owner()->localFromWorld(owner()->worldPosition()));
			}
		}

		void Collider::applyScenePositionUpdate(Point<> a_location, double a_angle) {
			owner()->position(a_location);
			owner()->worldRotation(Point<>(0.0f, 0.0f, static_cast<PointPrecision>(a_angle)));
		}

		void Collider::initialize() {
			Drawable::initialize();
			collisionAttributes.parent = std::static_pointer_cast<Collider>(shared_from_this());
			physicsBody = world->createBody(collisionAttributes.details);
			physicsBody->SetUserData((void *)this);
			currentAngle = previousAngle = owner()->worldRotation().z;
			currentPosition = previousPosition = owner()->localFromWorld(world->owner()->worldFromLocal(body().position()));
		}

		Collider::~Collider() {
			physicsBody->SetUserData(nullptr);
			std::for_each(contacts.begin(), contacts.end(), [&](ContactMap::value_type contact) {
				contact.first->deleteCollisionObject(this);
			});
		}

		void Collider::updateImplementation(double a_dt) {
			updatePhysicsPosition();

			updateInterpolatedPositionAndApply();
		}

		void Collider::updateInterpolatedPositionAndApply() {
			auto percentOfStep = static_cast<PointPrecision>(world->percentOfStep());
			PointPrecision z = currentPosition.z;
			Point<> interpolatedPoint = mix(previousPosition, currentPosition, percentOfStep);
			interpolatedPoint.z = z; //fix the z so it isn't scaled or modified;
			double interpolatedAngle = interpolateDrawAngle(percentOfStep);
			applyScenePositionUpdate(interpolatedPoint, currentAngle);
		}

		void Collider::updatePhysicsPosition() {
			if (world->updatedThisFrame())
			{
				previousPosition = currentPosition;
				previousAngle = currentAngle;
				currentPhysicsPosition = physicsBody->GetPosition();
				currentPosition = cast(currentPhysicsPosition, owner()->position().z);

				if (useBodyAngle) {
					currentPhysicsAngle = physicsBody->GetAngle();
					currentAngle = toDegrees(physicsBody->GetAngle());
				} else if (!useBodyAngle) {
					currentAngle = owner()->worldRotation().z;
				} else {
					currentPhysicsAngle = physicsBody->GetAngle();
					currentAngle = toDegrees(physicsBody->GetAngle());
				}
			}
		}

		std::shared_ptr<Collider> Collider::attach(const Size<> &a_size, const Point<> &a_position /*= Point<>()*/, PointPrecision a_rotation /*= 0.0f*/, CollisionPartAttributes a_attributes /*= CollisionPartAttributes()*/) {
			b2Vec2 centerPoint = castToPhysics(a_position);
			auto shapeSize = castToPhysics(a_size / 2.0f); //using half width and half height

			b2PolygonShape shape;
			shape.SetAsBox(static_cast<float32>(shapeSize.x), static_cast<float32>(shapeSize.y), centerPoint, static_cast<float32>(toRadians(a_rotation)));

			a_attributes.details.shape = &shape;
			a_attributes.details.userData = ((void *)this);
			physicsBody->CreateFixture(&a_attributes.details);

			FixtureParameters paramsToStore;
			paramsToStore.isCircle = false;
			paramsToStore.size = a_size;
			paramsToStore.rotation = a_rotation;
			paramsToStore.position = a_position;
			paramsToStore.attributes = a_attributes;

			collisionParts.push_back(paramsToStore);
			return std::static_pointer_cast<Collider>(shared_from_this());
		}

		std::shared_ptr<Collider> Collider::attach(PointPrecision a_diameter, const Point<> &a_position /*= Point<>()*/, CollisionPartAttributes a_attributes /*= CollisionPartAttributes()*/) {
			b2CircleShape shape;
			shape.m_radius = static_cast<float32>((a_diameter / CollisionScale) / 2.0);
			shape.m_p = castToPhysics(a_position);

			a_attributes.details.shape = &shape;
			a_attributes.details.userData = ((void *)this);
			physicsBody->CreateFixture(&a_attributes.details);

			FixtureParameters paramsToStore;
			paramsToStore.isCircle = true;
			paramsToStore.diameter = a_diameter;
			paramsToStore.position = a_position;
			paramsToStore.attributes = a_attributes;

			collisionParts.push_back(paramsToStore);
			return std::static_pointer_cast<Collider>(shared_from_this());
		}

		CollisionBodyAttributes::CollisionBodyAttributes() {
		}

		CollisionBodyAttributes& CollisionBodyAttributes::stop() {
			details.linearVelocity = b2Vec2(0, 0);
			details.angularVelocity = 0;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetLinearVelocity(details.linearVelocity);
				parent.lock()->physicsBody->SetAngularVelocity(details.angularVelocity);
			}
			return *this;
		}

		bool CollisionBodyAttributes::isStatic() const {
			return details.type == b2_staticBody;
		}

		bool CollisionBodyAttributes::isDynamic() const {
			return details.type == b2_dynamicBody;
		}

		bool CollisionBodyAttributes::isKinematic() const {
			return details.type == b2_kinematicBody;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::makeStatic() {
			details.type = b2_staticBody;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetType(b2_staticBody);
			}
			return *this;
		}
		CollisionBodyAttributes& CollisionBodyAttributes::makeDynamic() {
			details.type = b2_dynamicBody;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetType(b2_dynamicBody);
			}
			return *this;
		}
		CollisionBodyAttributes& CollisionBodyAttributes::makeKinematic() {
			details.type = b2_kinematicBody;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetType(b2_kinematicBody);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::bullet(bool a_isBullet) {
			details.bullet = a_isBullet;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetBullet(a_isBullet);
			}
			return *this;
		}

		bool CollisionBodyAttributes::bullet() const {
			return details.bullet;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::position(const Point<> &a_position) {
			details.position = castToPhysics(a_position);
			if (!parent.expired()) {
				auto lockedParent = parent.lock();
				lockedParent->physicsBody->SetTransform(details.position, lockedParent->physicsBody->GetAngle());
				lockedParent->currentPosition = cast(details.position, lockedParent->owner()->position().z);
				lockedParent->previousPosition = lockedParent->currentPosition;
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::impulse(const Point<> &a_impulse) {
			if (!parent.expired()) {
				parent.lock()->physicsBody->ApplyLinearImpulse(castToPhysics(a_impulse), parent.lock()->physicsBody->GetPosition(), true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::force(const Point<> &a_force) {
			if (!parent.expired()) {
				parent.lock()->physicsBody->ApplyForceToCenter(castToPhysics(a_force), true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::impulse(const Point<> &a_impulse, const Point<> &a_position) {
			if (!parent.expired()) {
				parent.lock()->physicsBody->ApplyLinearImpulse(castToPhysics(a_impulse), castToPhysics(a_position), true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::force(const Point<> &a_force, const Point<> &a_position) {
			if (!parent.expired()) {
				parent.lock()->physicsBody->ApplyForce(castToPhysics(a_force), castToPhysics(a_position), true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::torque(PointPrecision a_amount) {
			if (!parent.expired()) {
				parent.lock()->physicsBody->ApplyTorque(a_amount, true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::angularImpulse(PointPrecision a_amount) {
			if (!parent.expired()) {
				parent.lock()->physicsBody->ApplyAngularImpulse(a_amount, true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::angle(PointPrecision a_newAngle) {
			details.angle = toRadians(a_newAngle);
			if (!parent.expired()) {
				auto lockedParent = parent.lock();
				lockedParent->physicsBody->SetTransform(lockedParent->physicsBody->GetPosition(), details.angle);
				lockedParent->currentAngle = details.angle;
				lockedParent->previousAngle = details.angle;
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::transform(const Point<> &a_position, PointPrecision a_newAngle) {
			details.position = castToPhysics(a_position);
			details.angle = toRadians(a_newAngle);
			if (!parent.expired()) {
				auto lockedParent = parent.lock();
				lockedParent->physicsBody->SetTransform(details.position, details.angle);
				lockedParent->currentPosition = cast(details.position, lockedParent->owner()->position().z);
				lockedParent->previousPosition = lockedParent->currentPosition;
				lockedParent->currentAngle = details.angle;
				lockedParent->previousAngle = details.angle;
			}
			return *this;
		}

		PointPrecision CollisionBodyAttributes::angle() const {
			if (!parent.expired()) {
				return toDegrees(parent.lock()->physicsBody->GetAngle());
			} else {
				return toDegrees(details.angle);
			}
		}

		Point<> CollisionBodyAttributes::position() const {
			if (!parent.expired()) {
				auto pysicsPosition = parent.lock()->physicsBody->GetPosition();
				return cast(pysicsPosition, parent.lock()->owner()->position().z);
			} else {
				return cast(details.position);
			}
		}

		CollisionBodyAttributes& CollisionBodyAttributes::gravityScale(PointPrecision a_newScale) {
			details.gravityScale = a_newScale;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetGravityScale(details.gravityScale);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::disableRotation() {
			details.fixedRotation = true;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetFixedRotation(true);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::enableRotation() {
			details.fixedRotation = false;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetFixedRotation(false);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::velocity(const Point<> &a_velocity) {
			details.linearVelocity = castToPhysics(a_velocity);
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetLinearVelocity(details.linearVelocity);
			}
			return *this;
		}

		Point<> CollisionBodyAttributes::velocity() const {
			if (!parent.expired()) {
				parent.lock()->physicsBody->GetLinearVelocity();
			}
			return cast(details.linearVelocity);
		}

		CollisionBodyAttributes& CollisionBodyAttributes::angularVelocity(PointPrecision a_value) {
			details.angularVelocity = a_value;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetAngularVelocity(details.angularVelocity);
			}
			return *this;
		}

		PointPrecision CollisionBodyAttributes::angularVelocity() const {
			if (!parent.expired()) {
				parent.lock()->physicsBody->GetAngularVelocity();
			}
			return details.angularVelocity;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::movementDamping(PointPrecision a_value) {
			details.linearDamping = a_value;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetLinearDamping(details.linearDamping);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::angularDamping(PointPrecision a_value) {
			details.angularDamping = a_value;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetAngularDamping(details.angularDamping);
			}
			return *this;
		}

		CollisionBodyAttributes& CollisionBodyAttributes::allowSleep() {
			details.allowSleep = true;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetSleepingAllowed(true);
			}
			return *this;
		}
		CollisionBodyAttributes& CollisionBodyAttributes::disallowSleep() {
			details.allowSleep = false;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetSleepingAllowed(false);
			}
			return *this;
		}

		void CollisionBodyAttributes::syncronize() {
			if (!parent.expired()) {
				b2Body* body = parent.lock()->physicsBody;
				details.position = body->GetPosition();
				details.angle = body->GetAngle();
				details.linearVelocity = body->GetLinearVelocity();
				details.angularVelocity = body->GetAngularVelocity();
				details.linearDamping = body->GetLinearDamping();
				details.angularDamping = body->GetAngularDamping();
				details.allowSleep = body->IsSleepingAllowed();
				details.awake = body->IsAwake();
				details.fixedRotation = body->IsFixedRotation();
				details.bullet = body->IsBullet();
				details.type = body->GetType();
				details.active = body->IsActive();
				details.gravityScale = body->GetGravityScale();
			}
		}

	}
}
