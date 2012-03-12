#include "exampleCode.h"
#include "Render/package.h"

void CreateTextScene(M2Rend::Scene &mainScene, M2Rend::TextLibrary &textLibrary){
   //load the font
   textLibrary.loadFont("annabel", 20, "Assets/Fonts/AnnabelScript.ttf");
   textLibrary.loadFont("bluehighway1", 12, "Assets/Fonts/bluehigh.ttf");
   textLibrary.loadFont("bluehighway2", 24, "Assets/Fonts/bluehigh.ttf");
   textLibrary.loadFont("bluehighway3", 42, "Assets/Fonts/bluehigh.ttf");

   //Add basic instructions
   mainScene.add(textLibrary.composeScene(M2Rend::parseTextStateList("annabel", UTF_CHAR_STR("Press Space to Jump"))), "instruction");

   //Add background text
   auto text = mainScene.add(textLibrary.composeScene(M2Rend::parseTextStateList("bluehighway1", UTF_CHAR_STR("A quick brown fox jumped over the lazy dog."))), "text");
   //Prepare text to rotate around its center.
   text->translate(M2Rend::Point(150, 250, 0));
   text->centerRotateOrigin();
}

void ManipulateText(M2Rend::Scene &mainScene){
   static float red = 1.0f, green = 0.0f, blue = 0.5f;
   static float redInc = -.005f, greenInc = .010f, blueInc = .001f;
   static int direction = 1, calls = 25, callsNeeded = 50;
   calls++;
   if(calls > callsNeeded){calls = 0; direction*=-1;}
   //cycle the colors
   red+=redInc; green+=greenInc; blue+=blueInc;
   if(red < 0.0){red = 1.0;}
   if(blue > 1.0){blue = 0.0;}
   if(green > 1.0){green = 0.0;}

   mainScene.get<M2Rend::Scene>("text")->incrementRotate(M2Rend::Point(0.0, 0.0, .05 * direction));
   mainScene.get<M2Rend::Scene>("text")->setColor(M2Rend::Color(red, blue, green));
}

std::shared_ptr<M2Rend::DrawRectangle> GetDogShape(M2Rend::Scene &mainScene){
   return mainScene.get<M2Rend::Scene>("DogFox")->get<M2Rend::DrawRectangle>("dog");
}

std::shared_ptr<M2Rend::DrawRectangle> GetFoxShape(M2Rend::Scene &mainScene){
   return mainScene.get<M2Rend::Scene>("DogFox")->get<M2Rend::DrawRectangle>("fox");
}

void LoadTexturesAndAnimations(M2Rend::TextureManager &textures, M2Rend::FrameSwapperRegister &animationLibrary){
   textures.load("Assets/Images/TextureMap.json");
   animationLibrary.load("Assets/Animations.json");
}

void CreateDogFoxScene(M2Rend::Scene &mainScene, M2Rend::TextureManager &textures){
   auto sunAndMoon = mainScene.make<M2Rend::Scene>("SunAndMoon");
   
   auto sun = sunAndMoon->make<M2Rend::DrawRectangle>("sun");
   sun->setTwoCorners(M2Rend::Point(350, 0, 1), M2Rend::Point(450, 100, 1));
   M2Rend::AssignTextureToRectangle(*sun, textures.getSubTexture("DogAndFox", "Sun"));

   auto moon = sunAndMoon->make<M2Rend::DrawRectangle>("moon");
   moon->setTwoCorners(M2Rend::Point(350, 500, 1), M2Rend::Point(450, 600, 1));
   M2Rend::AssignTextureToRectangle(*moon, textures.getSubTexture("DogAndFox", "Moon"));

   sunAndMoon->setRotateOrigin(M2Rend::Point(400, 300, 1));

   auto dogFoxScene = mainScene.make<M2Rend::Scene>("DogFox");
   //These values are hard coded, I could have substituted the numbers for named constants
   //but the scope of this example is incredibly limited.
   auto dog = dogFoxScene->make<M2Rend::DrawRectangle>("dog");
   dog->setTwoCorners(M2Rend::Point(350, 250, 2), M2Rend::Point(450, 350, 2));

   auto fox = dogFoxScene->make<M2Rend::DrawRectangle>("fox");
   fox->setTwoCorners(M2Rend::Point(290, 250, 3), M2Rend::Point(390, 350, 3));

   auto ground = dogFoxScene->make<M2Rend::DrawRectangle>("ground");
   ground->setTwoCorners(M2Rend::Point(300, 325, 1), M2Rend::Point(500, 425, 1));
   ground->getDepth();
   M2Rend::AssignTextureToRectangle(*ground, textures.getSubTexture("DogAndFox", "Ground"));
}

void UpdateSky(M2Rend::Scene &mainScene){
   mainScene.get<M2Rend::Scene>("SunAndMoon")->incrementRotate(M2Rend::Point(0.0, 0.0, -0.2));
   M2Rend::Point position = mainScene.get<M2Rend::Scene>("SunAndMoon")->getRotation();
   float distance = (float)abs(M2Util::boundBetween(position.z, 0.0, 360.0) - 180.0);
   float brightness = distance / 180.0f;
   if(brightness < .1f){brightness = .1f;}

   mainScene.getRenderer()->setBackgroundColor(M2Rend::Color(0.25f*brightness, 0.45f*brightness, 0.65f*brightness));
}

void UpdateAnimation(M2Rend::Scene &mainScene, M2Rend::TextureManager &textures, M2Rend::DrawRectangle &shape, M2Rend::FrameSwapper &animation, bool flipAnimation){
   M2Rend::Frame frame;
   animation.getCurrentFrame(frame);
   AssignTextureToRectangle(shape, textures.getSubTexture(frame.mainTexture, frame.subTexture), flipAnimation);
}

FoxJump::FoxJump( M2Rend::FrameSwapperRegister *frameReg, M2Rend::FrameSwapper *foxAnim, std::shared_ptr<M2Rend::DrawRectangle> foxShape ) :
   foxBox(foxShape),
   foxSwapper(foxAnim),
   frameRegister(frameReg),
   inJump(false),
   movedX(0),
   directionX(1),
   distanceX(100),distanceY(100),
   jumpCompletionTime(1000){}

void FoxJump::initiateJump(){
   if(!inJump){
      inJump = true;
      movedY = 0;
      directionY = -1;
      foxSwapper->setFrameList(frameRegister->getDefinition("FoxBounce"));
      timer.start();
      timer.delta("jumped");
   }
}

void FoxJump::updateJump(){
   if(inJump){
      double timeElapsed = static_cast<double>(timer.delta("jumped"));
      movedX += directionX * (distanceX * (timeElapsed / jumpCompletionTime));
      movedY += directionY * (distanceY * (timeElapsed / (jumpCompletionTime / 2.0)));
      if(movedY < -distanceY){directionY = 1; movedY = -distanceY;}
      if(movedX > distanceX || movedX < 0){
         movedY = 0;
         if(movedX > distanceX){
            movedX = distanceX;
         }else if(movedX < 0){
            movedX = 0;
         }
         directionX*=-1;
         inJump = false;
         foxSwapper->setFrameList(frameRegister->getDefinition("FoxStand"));
      }
      foxBox->placeAt(M2Rend::Point(movedX, movedY, 3));
   }
}

bool FoxJump::isFlipped(){
   return directionX == 1;
}