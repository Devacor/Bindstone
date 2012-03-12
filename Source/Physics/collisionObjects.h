#ifndef _COLLISIONOBJECTS_H_
#define _COLLISIONOBJECTS_H_

#include "Render\package.h"
#include "Utility\package.h"
#include "Box2D\Box2D.h"
#include <list>
#include <map>

//Scales units down by 100 when dealing with collisions.  This way if we work in pixels 100 pixels is 1 unit.
//Box2D is tuned to deal with ranges 10 to .1 ideally, so this provides a good working scale from display size
//to world simulation size.  Be aware when dealing with raw response data that it needs to be scaled up by this
//value to translate properly.
const double CollisionScale = 100.0;
//This is the time step at which Box2D updates.
const double PhysicsTimeStep = 1.0 / 60.0;

M2Rend::Point MakePoint(b2Vec2 a_box2DPoint, double a_z = 0);
b2Vec2 MakePoint(M2Rend::Point a_M2RendPoint);

M2Rend::Point MakeScaledPoint(b2Vec2 a_box2DPoint, double a_z = 0);
b2Vec2 MakeScaledPoint(M2Rend::Point a_M2RendPoint);


class CollisionWorld;

class CollisionObject{
public:
   enum ObjectType{
      NORMAL,  //Regular dynamic collision response
      STATIC,  //Static object
      PLATFORM //Static object only collision responses from objects hitting the top
   };
   CollisionObject(std::shared_ptr<CollisionWorld> a_world, std::shared_ptr<M2Rend::DrawShape> a_drawShape, b2BodyDef a_collisionAttributes = b2BodyDef(), ObjectType a_objectType = NORMAL);

   virtual ~CollisionObject();

   std::shared_ptr<M2Rend::DrawShape> getDrawShape(){
      return drawShape;
   }

   void update(double a_percentOfStep, bool newPhysicsFrame){
      if(newPhysicsFrame){
         previousLocation = currentLocation;
         previousAngle = currentAngle;
         currentLocation = MakeScaledPoint(body->GetPosition(), drawShape->getDepth());
         if(useBodyAngle){
            currentAngle = M2Util::toDegrees(body->GetAngle());
         }else{
            currentAngle = getCurrentAngle();
         }
         newPhysicsFrameStartedHook();
      }
      a_percentOfStep = std::min(a_percentOfStep, 1.0);
      double z = currentLocation.z;
      M2Rend::Point interpolatedPoint = previousLocation + ((currentLocation - previousLocation) * a_percentOfStep);
      interpolatedPoint.z = z; //fix the z so it isn't scaled or modified;
      double interpolatedAngle = interpolateDrawAngle(a_percentOfStep);
      applyScenePositionUpdate(interpolatedPoint, interpolatedAngle);
      updateHook();
   }

   void setDefaultShapeDefinition(const b2FixtureDef &a_shapeAttributes){
      defaultFixtureDefinition = a_shapeAttributes;
   }

   void attachBoxShape(double a_width, double a_height, M2Rend::Point a_location = M2Rend::Point(), double a_rotation = 0.0, b2FixtureDef *a_shapeAttributes = nullptr){
      if(a_shapeAttributes == nullptr){a_shapeAttributes = getDefaultFixtureDefinition();}
      b2Vec2 centerPoint = MakeScaledPoint(a_location);
      a_width=(a_width/CollisionScale)/2.0;
      a_height=(a_height/CollisionScale)/2.0;

      b2PolygonShape shape;
      shape.SetAsBox(static_cast<float32>(a_width), static_cast<float32>(a_height), centerPoint, static_cast<float32>(M2Util::toRadians(a_rotation)));

      b2FixtureDef shapeDef;
      shapeDef.shape = &shape;
      applyShapeDefinition(shapeDef, *a_shapeAttributes);
      body->CreateFixture(&shapeDef);
   }

   void attachCircleShape(double a_diameter, M2Rend::Point a_location = M2Rend::Point(), b2FixtureDef *a_shapeAttributes = nullptr){
      if(a_shapeAttributes == nullptr){a_shapeAttributes = getDefaultFixtureDefinition();}
      b2CircleShape shape;
      shape.m_radius = static_cast<float32>((a_diameter/CollisionScale)/2.0);
      shape.m_p = MakeScaledPoint(a_location);

      b2FixtureDef shapeDef;
      shapeDef.shape = &shape;
      applyShapeDefinition(shapeDef, *a_shapeAttributes);
      body->CreateFixture(&shapeDef);
   }

   void addCollision(CollisionObject* a_collisionWith, const b2Vec2 &a_normal){
      contacts[a_collisionWith].normal = a_normal;
      if(++contacts[a_collisionWith].count == 1){
         startCollision(a_collisionWith, a_normal);
      }
   }

   void removeCollision(CollisionObject* a_collisionWith){
      if(--contacts[a_collisionWith].count <= 0){
         contacts.erase(a_collisionWith);
         endCollision(a_collisionWith, false);
      }
   }

   ObjectType getType(){
      return objectType;
   }

   void placeAt(const M2Rend::Point &a_location, double a_angle=0, bool a_stopMovement = true){
      body->SetTransform(MakeScaledPoint(a_location), static_cast<float32>(a_angle));
      if(a_stopMovement){
         body->SetLinearVelocity(b2Vec2(0, 0));
         body->SetAngularVelocity(0);
      }
   }

   void impulse(const M2Rend::Point &a_impulse){
      body->ApplyLinearImpulse(MakeScaledPoint(a_impulse), body->GetPosition());
   }
   void force(const M2Rend::Point &a_force){
      body->ApplyForce(MakeScaledPoint(a_force), body->GetPosition());
   }
   void setVelocity(const M2Rend::Point &a_velocity){
      body->SetLinearVelocity(MakeScaledPoint(a_velocity));
   }
   M2Rend::Point getVelocity(){
      return MakeScaledPoint(body->GetLinearVelocity());
   }

   void spinImpulse(double a_angle){
      body->ApplyAngularImpulse(static_cast<float32>(M2Util::toRadians(a_angle)));
   }
   void setAngularVelocity(double a_angle){
      body->SetAngularVelocity(static_cast<float32>(M2Util::toRadians(a_angle)));
   }
   double getAngularVelocity(){
      return static_cast<double>(body->GetAngularVelocity());
   }

   //lock or unlock the drawing angle to the physical body
   void lockDrawAngle(){
      useBodyAngle = true;
   }
   void unlockDrawAngle(){
      useBodyAngle = false;
   }
protected:
   //derived collision response should happen here.
   virtual void startCollision(CollisionObject* a_collisionWith, const b2Vec2 &a_normal) {};
   virtual void endCollision(CollisionObject* a_collisionWith, bool a_objectDestroyed) {};

   b2FixtureDef defaultFixtureDefinition;

   //used for overriding the default behavior
   virtual double getCurrentAngle(){return drawShape->getRotation().z;}

   struct ContactInformation{
      ContactInformation():count(0){}
      b2Vec2 normal;
      unsigned int count;
   };
   typedef std::map<CollisionObject*, ContactInformation> ContactMap;
   ContactMap contacts;
   b2Body *body;

   std::shared_ptr<CollisionWorld> world;
   std::shared_ptr<M2Rend::DrawShape> drawShape;

   virtual void newPhysicsFrameStartedHook(){}
   virtual void updateHook(){}
private:
   void initializeDefaultFixtureDefinition(){
      defaultFixtureDefinition.restitution = .3f;
      defaultFixtureDefinition.friction = .3f;
      defaultFixtureDefinition.density = 1.0f;
      defaultFixtureDefinition.isSensor = false;
   }

   b2FixtureDef* getDefaultFixtureDefinition(){
      return &defaultFixtureDefinition;
   }

   void applyScenePositionUpdate(M2Rend::Point a_location, double a_angle){
      drawShape->placeAt(a_location);
      drawShape->setRotate(M2Rend::AxisAngles(0.0, 0.0, a_angle));
   }

   double interpolateDrawAngle(double a_percentOfStep){
      if(currentAngle == previousAngle){return currentAngle;}
      bool wrapped;
      double rotation = M2Util::getWrappingDistance(currentAngle, previousAngle, 0.0, 360.0, &wrapped);
      if(wrapped){
         if(previousAngle > currentAngle){
            rotation = previousAngle + ((rotation) * a_percentOfStep);
         }else{
            rotation = previousAngle - ((rotation) * a_percentOfStep);
         }
      }else{
         if(previousAngle > currentAngle){
            rotation = previousAngle - ((rotation) * a_percentOfStep);
         }else{
            rotation = previousAngle + ((rotation) * a_percentOfStep);
         }
      }
      return M2Util::boundBetween(rotation, 0.0, 360.0);
   }

   void deleteCollisionObject(CollisionObject* a_collisionWith){
      contacts.erase(a_collisionWith);
      endCollision(a_collisionWith, true);
   }

   void applyShapeDefinition(b2FixtureDef &a_applyTo, const b2FixtureDef &a_applicator){
      a_applyTo.isSensor = a_applicator.isSensor;
      a_applyTo.density = (objectType==NORMAL)?a_applicator.density:0.0f;
      a_applyTo.restitution = a_applicator.restitution;
      a_applyTo.filter = a_applicator.filter;
      a_applyTo.userData = ((void *)this);
   }

   bool isStatic;
   ObjectType objectType;

   M2Rend::Point currentLocation, previousLocation;
   double currentAngle, previousAngle;
   bool useBodyAngle;
};

class ContactListener : public b2ContactListener {
public:
   virtual void BeginContact(b2Contact* contact){
      CollisionObject *fixtureAObject = static_cast<CollisionObject*>(contact->GetFixtureA()->GetUserData());
      CollisionObject *fixtureBObject = static_cast<CollisionObject*>(contact->GetFixtureB()->GetUserData());
      b2WorldManifold worldManifold;
      contact->GetWorldManifold(&worldManifold);
      fixtureAObject->addCollision(fixtureBObject, worldManifold.normal);
      fixtureBObject->addCollision(fixtureAObject, -worldManifold.normal);
   }
   virtual void EndContact(b2Contact* contact){
      CollisionObject *fixtureAObject = static_cast<CollisionObject*>(contact->GetFixtureA()->GetUserData());
      CollisionObject *fixtureBObject = static_cast<CollisionObject*>(contact->GetFixtureB()->GetUserData());
      b2WorldManifold worldManifold;
      contact->GetWorldManifold(&worldManifold);
      fixtureAObject->removeCollision(fixtureBObject);
      fixtureBObject->removeCollision(fixtureAObject);
   }
   virtual void PreSolve(b2Contact* contact, const b2Manifold* oldManifold){
      int multiply = 0;
      if(static_cast<CollisionObject*>(contact->GetFixtureA()->GetUserData())->getType() == CollisionObject::PLATFORM){
         b2WorldManifold worldManifold;
         contact->GetWorldManifold(&worldManifold);

         if ((worldManifold.normal.y) > 0.5f){
            contact->SetEnabled(false);
         }
      }else if(static_cast<CollisionObject*>(contact->GetFixtureB()->GetUserData())->getType() == CollisionObject::PLATFORM){
         b2WorldManifold worldManifold;
         contact->GetWorldManifold(&worldManifold);

         if ((worldManifold.normal.y) < -0.5f){
            contact->SetEnabled(false);
         }
      }
   }
   virtual void PostSolve(b2Contact* contact, const b2Manifold* oldManifold){}
};

class CollisionWorld : public std::enable_shared_from_this<CollisionWorld> {
public:
   static std::shared_ptr<CollisionWorld> makeCollisionWorld(const M2Rend::Point &a_gravity){
      return std::shared_ptr<CollisionWorld>(new CollisionWorld(a_gravity));
   }

   template<typename CollisionObjectType>
   std::shared_ptr<CollisionObjectType> makeCollisionObject(std::shared_ptr<M2Rend::DrawShape> a_drawShape, const b2BodyDef &a_collisionAttributes = b2BodyDef(), CollisionObject::ObjectType a_objectType = CollisionObject::NORMAL){
      auto thisShared = shared_from_this();
      auto newObject = std::make_shared<CollisionObjectType>(thisShared, a_drawShape, a_collisionAttributes, a_objectType);
      collisionObjects.push_back(newObject);
      return newObject;
   }

   b2Body* createBody(const b2BodyDef& a_bodyDef = b2BodyDef()){
      return world.CreateBody(&a_bodyDef);
   }

   void update(){
      double boxStepSeconds = double(timer.check()) / 1000.0;
      unsigned int stepsDone = 0;
      while(boxStepSeconds > PhysicsTimeStep){
         world.Step(float32(PhysicsTimeStep), Box2dVelocityIterations, Box2dPositionIterations);
         boxStepSeconds-=PhysicsTimeStep;
         stepsDone++;
      }
      if(stepsDone>0){
         timer.start();
         timer.setTimeOffset(int(boxStepSeconds*1000.0)); //carryover remaining time.
      }
      std::for_each(collisionObjects.begin(), collisionObjects.end(), [&](CollisionObjectList::value_type collisionObject){
         collisionObject->update(boxStepSeconds/PhysicsTimeStep, stepsDone!=0);
      });
   }
private:
   CollisionWorld(const M2Rend::Point &a_gravity):world(b2Vec2(static_cast<float32>(a_gravity.x), static_cast<float32>(a_gravity.y)), true){
      world.SetContactListener(&listener);
   }

   typedef std::vector<std::shared_ptr<CollisionObject>> CollisionObjectList;
   CollisionObjectList collisionObjects;
   M2Util::Stopwatch timer;
   ContactListener listener;
   b2World world;

   static const int Box2dVelocityIterations = 10;
   static const int Box2dPositionIterations = 4;
};

#endif
