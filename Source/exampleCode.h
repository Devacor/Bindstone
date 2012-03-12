/*/DISCLAIMER!
All of the code in exampleCode.h and exampleCode.cpp is hard-coded and
intended as a very basic example of how to use the rendering, animation, 
text display, and audio systems.

Please enjoy the rest of the project and forgive the hard-coded nature of
this setup code.

-Michael Hamilton
/*/
#include <SDL/SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include <string>
#include <ctime>

//Create the initial background text and position it.
void CreateTextScene(M2Rend::Scene &mainScene, M2Rend::TextLibrary &textLibrary);

//Twirl and change the color of the text.
void ManipulateText(M2Rend::Scene &mainScene);

//These two functions are hard-coded shortcuts to get the dog and fox shapes from the scene manager.
std::shared_ptr<M2Rend::DrawRectangle> GetDogShape(M2Rend::Scene &mainScene);
std::shared_ptr<M2Rend::DrawRectangle> GetFoxShape(M2Rend::Scene &mainScene);

//This loads all the textures for the example.
void LoadTexturesAndAnimations(M2Rend::TextureManager &textures, M2Rend::FrameSwapperRegister &animationLibrary);

//This sets up the Dog, Fox, and Ground objects
void CreateDogFoxScene(M2Rend::Scene &mainScene, M2Rend::TextureManager &textures);

//Update a shape's texture to the current frame of an animation.
void UpdateAnimation(M2Rend::Scene &mainScene, M2Rend::TextureManager &textures, M2Rend::DrawRectangle &shape, M2Rend::FrameSwapper &animation, bool flipAnimation = false);

//Update the sun and moon's rotation
void UpdateSky(M2Rend::Scene &mainScene);

//This is a really simple hard-coded jump sequence to provide a limited amount of interactivity in the demo.
class FoxJump{
public:
   FoxJump(M2Rend::FrameSwapperRegister *frameReg, M2Rend::FrameSwapper *foxAnim, std::shared_ptr<M2Rend::DrawRectangle> foxShape);
   ~FoxJump(){}

   //Call this to initiate a jump sequence (or do nothing if the fox is already jumping)
   void initiateJump();

   //Call each frame to update
   void updateJump();

   //Checks the direction the fox is moving and decides if the animation should be flipped
   bool isFlipped();
private:
   std::shared_ptr<M2Rend::DrawRectangle> foxBox;
   M2Rend::FrameSwapperRegister *frameRegister;
   M2Rend::FrameSwapper *foxSwapper;
   double directionX, directionY;
   double movedX, movedY;
   double distanceX, distanceY;
   bool inJump;

   const double jumpCompletionTime;
   M2Util::Stopwatch timer;
};