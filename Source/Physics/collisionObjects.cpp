#include "collisionObjects.h"
#include "Utility/generalUtility.h"

namespace MV {
	namespace Scene {
		Point<> cast(b2Vec2 a_box2DPoint, PointPrecision a_z) {
			return Point<>(static_cast<PointPrecision>(a_box2DPoint.x) * CollisionScale, static_cast<PointPrecision>(a_box2DPoint.y), a_z);
		}

		Collider::Collider(std::shared_ptr<Environment> a_world, CollisionBodyAttributes a_collisionAttributes)
			:world(a_world)
		{
			Point sceneLocation = previousLocation = currentLocation = a_drawShape->getRelativeLocation();
			a_collisionAttributes.position = castToPhysics(sceneLocation);
			previousAngle = currentAngle = a_collisionAttributes.angle = static_cast<float32>(a_drawShape->getRotation().z);
			physicsBody = world->createBody(a_collisionAttributes);

			if (a_objectType == NORMAL) {
				physicsBody->SetType(b2_dynamicBody);
			}
			else {
				physicsBody->SetType(b2_staticBody);
			}
			physicsBody->SetUserData((void *)this);
			initializeDefaultFixtureDefinition();
		}

		Collider::~Collider() {
			std::for_each(contacts.begin(), contacts.end(), [&](ContactMap::value_type contact) {
				contact.first->deleteCollisionObject(this);
			});
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
				lockedParent->currentLocation = cast(details.position, lockedParent->owner()->position().z);
				lockedParent->previousLocation = lockedParent->currentLocation;
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
				lockedParent->currentLocation = cast(details.position, lockedParent->owner()->position().z);
				lockedParent->previousLocation = lockedParent->currentLocation;
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
				return cast(parent.lock()->physicsBody->GetPosition(), parent.lock()->owner()->position().z);
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

		CollisionBodyAttributes& CollisionBodyAttributes::angularVelocity(PointPrecision a_value) {
			details.angularVelocity = a_value;
			if (!parent.expired()) {
				parent.lock()->physicsBody->SetAngularVelocity(details.angularVelocity);
			}
			return *this;
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
