/*/DISCLAIMER!
All of the code in exampleCode.h and exampleCode.cpp is hard-coded and
intended as a very basic example of how to use the rendering, animation, 
text display, and audio systems.

Please enjoy the rest of the project and forgive the hard-coded nature of
this setup code.

-Michael Hamilton
/*/
#include <SDL.h>
#include "Utility/package.h"
#include "Render/package.h"
#include "Audio/package.h"
#include "Animation/package.h"
#include <string>
#include <ctime>

//Create the initial background text and position it.
void CreateTextScene(std::shared_ptr<MV::Scene::Node> mainScene, MV::TextLibrary &textLibrary);

//Twirl and change the color of the text.
void ManipulateText(std::shared_ptr<MV::Scene::Node> mainScene);

//These two functions are hard-coded shortcuts to get the dog and fox shapes from the scene manager.
std::shared_ptr<MV::Scene::Rectangle> GetDogShape(std::shared_ptr<MV::Scene::Node> mainScene);
std::shared_ptr<MV::Scene::Rectangle> GetFoxShape(std::shared_ptr<MV::Scene::Node> mainScene);

//This loads all the textures for the example.
void LoadTexturesAndAnimations(MV::FrameSwapperRegister &animationLibrary);

//This sets up the Dog, Fox, and Ground objects
void CreateDogFoxScene(std::shared_ptr<MV::Scene::Node> mainScene);

//Update a shape's texture to the current frame of an animation.
void UpdateAnimation(std::shared_ptr<MV::Scene::Node> mainScene, std::shared_ptr<MV::Scene::Rectangle> shape, MV::FrameSwapper &animation, bool flipAnimation = false);

//Update the sun and moon's rotation
void UpdateSky(std::shared_ptr<MV::Scene::Node> mainScene);

//This is a really simple hard-coded jump sequence to provide a limited amount of interactivity in the demo.
class FoxJump{
public:
	FoxJump(MV::FrameSwapperRegister *frameReg, MV::FrameSwapper *foxAnim, std::shared_ptr<MV::Scene::Rectangle> foxShape);
	~FoxJump(){}

	//Call this to initiate a jump sequence (or do nothing if the fox is already jumping)
	void initiateJump();

	//Call each frame to update
	void updateJump();

	//Checks the direction the fox is moving and decides if the animation should be flipped
	bool isFlipped();
private:
	std::shared_ptr<MV::Scene::Rectangle> foxBox;
	MV::FrameSwapperRegister *frameRegister;
	MV::FrameSwapper *foxSwapper;
	double directionX, directionY;
	double movedX, movedY;
	double distanceX, distanceY;
	bool inJump;

	const double jumpCompletionTime;
	MV::Stopwatch timer;
};