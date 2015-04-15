#include "exampleCode.h"
#include "Render/package.h"

void CreateTextScene(std::shared_ptr<MV::Scene::Node> mainScene, MV::TextLibrary &textLibrary){
	//load the font
	textLibrary.loadFont("annabel", "Assets/Fonts/AnnabelScript.ttf", 20);
	textLibrary.loadFont("bluehighway1", "Assets/Fonts/bluehigh.ttf", 12);
	textLibrary.loadFont("bluehighway2", "Assets/Fonts/bluehigh.ttf", 24);
	textLibrary.loadFont("bluehighway3", "Assets/Fonts/bluehigh.ttf", 42);

	mainScene->renderer().backgroundColor(MV::Color(0, 0, 0, 0));
	mainScene->renderer().clearScreen();
	mainScene->renderer().updateScreen();
	//Add basic instructions
	//mainScene->add("instruction", textLibrary.composeScene(MV::parseTextStateList("annabel", UTF_CHAR_STR("Press Space to Jump"))));

	//Add background text
	//auto text = mainScene->add("text", textLibrary.composeScene(MV::parseTextStateList("bluehighway1", UTF_CHAR_STR("A quick brown fox jumped over the lazy dog."))));
	//Prepare text to rotate around its center.
	//text->translate(MV::Point<>(150, 250));
	//text->centerRotationOrigin();
}

void ManipulateText(std::shared_ptr<MV::Scene::Node> mainScene){
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

	mainScene->get("text")->addRotation(MV::Point<>(0.0f, 0.0f, .05f * direction));
	mainScene->get("text")->component<MV::Scene::Text>()->color(MV::Color(red, blue, green));
}

std::shared_ptr<MV::Scene::Sprite> GetDogShape(std::shared_ptr<MV::Scene::Node> mainScene){
	return mainScene->get("DogFox")->get("dog")->component<MV::Scene::Sprite>().self();
}

std::shared_ptr<MV::Scene::Sprite> GetFoxShape(std::shared_ptr<MV::Scene::Node> mainScene){
	return mainScene->get("DogFox")->get("fox")->component<MV::Scene::Sprite>().self();
}

void LoadTexturesAndAnimations(MV::FrameSwapperRegister &animationLibrary){
	animationLibrary.load("Assets/Animations.json");
}

void CreateDogFoxScene(std::shared_ptr<MV::Scene::Node> mainScene){
	auto sunAndMoon = mainScene->make("SunAndMoon");
	
	auto sun = sunAndMoon->make("sun")->attach<MV::Scene::Sprite>();
	sun->bounds({MV::Point<>(350, 0, 1), MV::Point<>(450, 100, 1)});
	auto textureSheet = MV::FileTextureDefinition::make("Assets/Images/dogfox.png");
	sun->texture(textureSheet->makeHandle({MV::Point<int>(0, 384), MV::Size<int>(128, 128)}));

	auto moon = sunAndMoon->make("moon")->attach<MV::Scene::Sprite>();
	moon->bounds({MV::Point<>(350, 500, 1), MV::Point<>(450, 600, 1)});
	moon->texture(textureSheet->makeHandle({MV::Point<int>(128, 384), MV::Size<int>(128, 128)}));

	//sunAndMoon->rotationOrigin(MV::Point<>(400, 300, 1));

	auto dogFoxScene = mainScene->make("DogFox");
	//These values are hard coded, I could have substituted the numbers for named constants
	//but the scope of this example is incredibly limited.
	auto dog = dogFoxScene->make("dog")->attach<MV::Scene::Sprite>();
	dog->bounds({MV::Point<>(350, 250, 2), MV::Point<>(450, 350, 2)});

	auto fox = dogFoxScene->make("fox")->attach<MV::Scene::Sprite>();
	fox->bounds({MV::Point<>(290, 250, 3), MV::Point<>(390, 350, 3)});

	auto ground = dogFoxScene->make("ground")->attach<MV::Scene::Sprite>();
	ground->bounds({MV::Point<>(300, 325, 1), MV::Point<>(500, 425, 1)});

	ground->texture(textureSheet->makeHandle({MV::Point<int>(256, 384), MV::Size<int>(256, 128)}));
}

void UpdateSky(std::shared_ptr<MV::Scene::Node> mainScene){
	mainScene->get("SunAndMoon")->addRotation(MV::point(0.0f, 0.0f, -0.2f));
	MV::Point<> position = mainScene->get("SunAndMoon")->rotation();
	float distance = (float)abs(MV::wrap(position.z, 0.0f, 360.0f) - 180.0);
	float brightness = distance / 180.0f;
	if(brightness < .1f){brightness = .1f;}

	mainScene->renderer().backgroundColor(MV::Color(0.25f*brightness, 0.45f*brightness, 0.65f*brightness));
}

void UpdateAnimation(std::shared_ptr<MV::Scene::Node> mainScene, std::shared_ptr<MV::Scene::Sprite> shape, MV::FrameSwapper &animation, bool flipAnimation){
	MV::Frame frame;
	animation.getCurrentFrame(frame);
	//AssignTextureToRectangle(shape, textures.getSubTexture(frame.mainTexture, frame.subTexture), flipAnimation);
}

FoxJump::FoxJump( MV::FrameSwapperRegister *frameReg, MV::FrameSwapper *foxAnim, std::shared_ptr<MV::Scene::Sprite> foxShape ) :
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
		MV::PointPrecision timeElapsed = static_cast<MV::PointPrecision>(timer.delta("jumped"));
		movedX += directionX * (distanceX * (timeElapsed / jumpCompletionTime));
		movedY += directionY * (distanceY * (timeElapsed / (jumpCompletionTime / 2.0f)));
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
		foxBox->owner()->position(MV::point(movedX, movedY, 3.0f));
	}
}

bool FoxJump::isFlipped(){
	return directionX == 1;
}
