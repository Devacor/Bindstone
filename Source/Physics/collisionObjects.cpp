#include "collisionObjects.h"

M2Rend::Point MakePoint(b2Vec2 a_box2DPoint, double a_z){
	return M2Rend::Point(static_cast<double>(a_box2DPoint.x), static_cast<double>(a_box2DPoint.y), a_z);
}
b2Vec2 MakePoint(M2Rend::Point a_M2RendPoint){
	return b2Vec2(static_cast<float32>(a_M2RendPoint.x), static_cast<float32>(a_M2RendPoint.y));
}

M2Rend::Point MakeScaledPoint(b2Vec2 a_box2DPoint, double a_z){
	return M2Rend::Point(static_cast<double>(a_box2DPoint.x) * CollisionScale, static_cast<double>(a_box2DPoint.y) * CollisionScale, a_z);
}
b2Vec2 MakeScaledPoint(M2Rend::Point a_M2RendPoint){
	return b2Vec2(static_cast<float32>(a_M2RendPoint.x / CollisionScale), static_cast<float32>(a_M2RendPoint.y / CollisionScale));
}

CollisionObject::CollisionObject( std::shared_ptr<CollisionWorld> a_world, std::shared_ptr<M2Rend::DrawShape> a_drawShape, b2BodyDef a_collisionAttributes /*= b2BodyDef()*/, ObjectType a_objectType /*= NORMAL*/ ) 
	:world(a_world),drawShape(a_drawShape),objectType(a_objectType),useBodyAngle(true)
{
	M2Rend::Point sceneLocation = previousLocation = currentLocation = a_drawShape->getRelativeLocation();
	a_collisionAttributes.position = MakeScaledPoint(sceneLocation);
	previousAngle = currentAngle = a_collisionAttributes.angle = static_cast<float32>(a_drawShape->getRotation().z);
	body = world->createBody(a_collisionAttributes);
	if(a_objectType == NORMAL){
		body->SetType(b2_dynamicBody);
	}else{
		body->SetType(b2_staticBody);
	}
	body->SetUserData((void *)this);
	initializeDefaultFixtureDefinition();
}

CollisionObject::~CollisionObject(){
	std::for_each(contacts.begin(), contacts.end(), [&](ContactMap::value_type contact){
		contact.first->deleteCollisionObject(this);
	});
}
