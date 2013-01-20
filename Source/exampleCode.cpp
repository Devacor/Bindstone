#include "exampleCode.h"
#include "Render/package.h"

void CreateTextScene(MV::Scene &mainScene, MV::TextLibrary &textLibrary){
	//load the font
	textLibrary.loadFont("annabel", 20, "Assets/Fonts/AnnabelScript.ttf");
	textLibrary.loadFont("bluehighway1", 12, "Assets/Fonts/bluehigh.ttf");
	textLibrary.loadFont("bluehighway2", 24, "Assets/Fonts/bluehigh.ttf");
	textLibrary.loadFont("bluehighway3", 42, "Assets/Fonts/bluehigh.ttf");

	mainScene.getRenderer()->setBackgroundColor(MV::Color(0, 0, 0, 0));
	mainScene.getRenderer()->clearScreen();
	mainScene.getRenderer()->updateScreen();
	//Add basic instructions
	mainScene.add(textLibrary.composeScene(MV::parseTextStateList("annabel", UTF_CHAR_STR("Press Space to Jump"))), "instruction");

	//Add background text
	auto text = mainScene.add(textLibrary.composeScene(MV::parseTextStateList("bluehighway1", UTF_CHAR_STR("A quick brown fox jumped over the lazy dog."))), "text");
	//Prepare text to rotate around its center.
	text->translate(MV::Point(150, 250, 0));
	text->centerRotateOrigin();
}

void ManipulateText(MV::Scene &mainScene){
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

	mainScene.get<MV::Scene>("text")->incrementRotate(MV::Point(0.0, 0.0, .05 * direction));
	mainScene.get<MV::Scene>("text")->setColor(MV::Color(red, blue, green));
}

std::shared_ptr<MV::DrawRectangle> GetDogShape(MV::Scene &mainScene){
	return mainScene.get<MV::Scene>("DogFox")->get<MV::DrawRectangle>("dog");
}

std::shared_ptr<MV::DrawRectangle> GetFoxShape(MV::Scene &mainScene){
	return mainScene.get<MV::Scene>("DogFox")->get<MV::DrawRectangle>("fox");
}

void LoadTexturesAndAnimations(MV::TextureManager &textures, MV::FrameSwapperRegister &animationLibrary){
	textures.load("Assets/Images/TextureMap.json");
	animationLibrary.load("Assets/Animations.json");
}

void CreateDogFoxScene(MV::Scene &mainScene, MV::TextureManager &textures){
	auto sunAndMoon = mainScene.make<MV::Scene>("SunAndMoon");
	
	auto sun = sunAndMoon->make<MV::DrawRectangle>("sun");
	sun->setTwoCorners(MV::Point(350, 0, 1), MV::Point(450, 100, 1));
	MV::AssignTextureToRectangle(*sun, textures.getSubTexture("DogAndFox", "Sun"));

	auto moon = sunAndMoon->make<MV::DrawRectangle>("moon");
	moon->setTwoCorners(MV::Point(350, 500, 1), MV::Point(450, 600, 1));
	MV::AssignTextureToRectangle(*moon, textures.getSubTexture("DogAndFox", "Moon"));

	sunAndMoon->setRotateOrigin(MV::Point(400, 300, 1));

	auto dogFoxScene = mainScene.make<MV::Scene>("DogFox");
	//These values are hard coded, I could have substituted the numbers for named constants
	//but the scope of this example is incredibly limited.
	auto dog = dogFoxScene->make<MV::DrawRectangle>("dog");
	dog->setTwoCorners(MV::Point(350, 250, 2), MV::Point(450, 350, 2));

	auto fox = dogFoxScene->make<MV::DrawRectangle>("fox");
	fox->setTwoCorners(MV::Point(290, 250, 3), MV::Point(390, 350, 3));

	auto ground = dogFoxScene->make<MV::DrawRectangle>("ground");
	ground->setTwoCorners(MV::Point(300, 325, 1), MV::Point(500, 425, 1));
	ground->getDepth();
	MV::AssignTextureToRectangle(*ground, textures.getSubTexture("DogAndFox", "Ground"));
}

void UpdateSky(MV::Scene &mainScene){
	mainScene.get<MV::Scene>("SunAndMoon")->incrementRotate(MV::Point(0.0, 0.0, -0.2));
	MV::Point position = mainScene.get<MV::Scene>("SunAndMoon")->getRotation();
	float distance = (float)abs(MV::boundBetween(position.z, 0.0, 360.0) - 180.0);
	float brightness = distance / 180.0f;
	if(brightness < .1f){brightness = .1f;}

	mainScene.getRenderer()->setBackgroundColor(MV::Color(0.25f*brightness, 0.45f*brightness, 0.65f*brightness));
}

void UpdateAnimation(MV::Scene &mainScene, MV::TextureManager &textures, MV::DrawRectangle &shape, MV::FrameSwapper &animation, bool flipAnimation){
	MV::Frame frame;
	animation.getCurrentFrame(frame);
	AssignTextureToRectangle(shape, textures.getSubTexture(frame.mainTexture, frame.subTexture), flipAnimation);
}

FoxJump::FoxJump( MV::FrameSwapperRegister *frameReg, MV::FrameSwapper *foxAnim, std::shared_ptr<MV::DrawRectangle> foxShape ) :
	foxBox(foxShape),
	foxSwapper(foxAnim),
	frameRegister(frameReg),
	inJump(false),
	movedX(0),
	directionX(1),
	distanceX(100),distanceY(100),
	jumpCompletionTime(1.0){}

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
		foxBox->placeAt(MV::Point(movedX, movedY, 3));
	}
}

bool FoxJump::isFlipped(){
	return directionX == 1;
}