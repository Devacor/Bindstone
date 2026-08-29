#include "gameEditor.h"
#include "Editor/editorDefines.h"
#include "Editor/editorFactories.h"
#include <cstring>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <optional>
#include <functional>


void GameEditor::resumeTitleMusic() {
	//auto playlistGame = std::make_shared<MV::AudioPlayList>();
	//playlistGame->addSoundBack("title");
	//playlistGame->loopSounds(false);

	//game.managers().audio.setMusicPlayList(playlistGame);

	//playlistGame->beginPlaying();
}

void GameEditor::handleInput(){
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (!game.managers().renderer.handleEvent(event)) {
			switch (event.type) {
			case SDL_QUIT:
				done = true;
				break;
			case SDL_KEYDOWN:
				switch (event.key.keysym.sym) {
				case SDLK_ESCAPE:
					done = true;
					break;
				case SDLK_UP:

					break;
				case SDLK_LEFT:
					break;
				case SDLK_DOWN:

					break;
				case SDLK_SPACE:

					break;
				case SDLK_RIGHT:
					break;
				}
				break;
			case SDL_WINDOWEVENT:
				break;
			case SDL_MOUSEWHEEL:
				break;
			}
			mouse.updateTouch(event, managers.renderer.window().drawableSize());
		} else {
			auto scale = game.managers().renderer.window().uiScale();
			screenScaler->bounds({ MV::point(0.0f, 0.0f), game.managers().renderer.world().size() / scale });
			limbo->scale(scale);
		}
	}
	mouse.update();
}

GameEditor::GameEditor(std::string a_username, std::string a_password, bool a_autoStart) :
	managers({ a_username, a_password, a_autoStart }),
	game(managers),
	autoStartGame(a_autoStart && !a_username.empty() && !a_password.empty()),
	editor(managers),
	limbo(MV::Scene::Node::make(managers.renderer))
{
	//managers.audio.loadMusic("Audio/TitleTheme.ogg", "title");
	//managers.audio.loadMusic("Audio/FieldIntro.ogg", "gameintro");
	//managers.audio.loadMusic("Audio/FieldLoopNeutral.ogg", "gameloop");

	resumeTitleMusic();

	// 		auto stencilNode = limbo->make("StencilTestNode");
	// 		auto spineTestNode = stencilNode->make("SpineTest")->position({ 400.0f, 600.0f })->attach<MV::Scene::Spine>(MV::Scene::Spine::FileBundle("Spine/Tree/life.json", "Spine/Tree/life.atlas", 0.5f))->shader(MV::DEFAULT_ID)->animate("idle")->bindNode("effects", "tree_particle")->bindNode("effects", "simple")->owner();
	// 
	// 		stencilNode->attach<MV::Scene::Stencil>()->bounds({ MV::Point<>(), MV::Size<>(450.0f, 550.0f) });

	// 		spineTestNode->make("PaletteTest")->position({ -50.0f, -100.0f })->
	// 			attach<MV::Scene::Palette>(mouse)->bounds(MV::size(256.0f, 256.0f));
	screenScaler = limbo->attach<MV::Scene::Sprite>();
	screenScaler->hide()->id("ScreenScaler");

	auto scale = game.managers().renderer.window().uiScale();
	screenScaler->bounds({ MV::point(0.0f, 0.0f), game.managers().renderer.world().size() / scale });
	limbo->scale(scale);

	auto child = limbo->make("child");
	auto screenBounds = screenScaler->worldBounds();

	// 	auto alignedSprite = limbo->make("repositionNode")->attach<MV::Scene::Sprite>();
	// 	alignedSprite->texture(textureSheet->makeHandle());
	// 	alignedSprite->anchors().anchor({ MV::point(0.5f, 0.5f), MV::point(0.5f, 0.5f) }).usePosition(true).parent(screenScaler.self());

	// 	mouse.onLeftMouseDownEnd.connect("TEST", [&](MV::TapDevice& a_mouse) {
	// 		std::cout << "PRE: " << screenScaler->bounds() << std::endl;
	// 		screenScaler->bounds({ screenScaler->owner()->localFromScreen(a_mouse.position()), screenScaler->bounds().maxPoint });
	// 		std::cout << "POST: " << screenScaler->bounds() << std::endl;
	// 	});
	// 	mouse.onRightMouseDownEnd.connect("TEST", [&](MV::TapDevice& a_mouse) {
	// 		std::cout << "PRE: " << screenScaler->bounds() << std::endl;
	// 		screenScaler->bounds({ screenScaler->bounds().minPoint, screenScaler->owner()->localFromScreen(a_mouse.position()) });
	// 		std::cout << "POST: " << screenScaler->bounds() << std::endl;
	// 	});

	MV::Point worldCenter{ screenScaler->worldBounds().width() / 2.0f, screenScaler->worldBounds().height() / 2.0f };
	MV::Point localCenter{ screenScaler->bounds().width() / 2.0f, screenScaler->bounds().height() / 2.0f };
	MV::Point halfSize{ screenScaler->worldBounds().width() / 2.0f - 10.0f, screenScaler->worldBounds().height() / 2.0f - 10.0f };

	auto grid = limbo->make("Grid")->position({ screenScaler->bounds().width() / 2.0f, screenScaler->bounds().height() / 2.0f })->
		attach<MV::Scene::Grid>()->columns(1)->padding({ 2.0f, 2.0f })->margin({ 4.0f, 4.0f })->color({ BOX_BACKGROUND })->owner();

	auto editorButton = makeButton(grid, game.managers().textLibrary, mouse, "Editor", { 100.0f, 20.0f }, U8_STR("Editor"));
	editorButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		runEditor();
		resumeTitleMusic();
		});
	auto gameButton = makeButton(grid, game.managers().textLibrary, mouse, "Game", { 100.0f, 20.0f }, U8_STR("Game"));
	gameButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		runGame();
		resumeTitleMusic();
		});
	auto quitButton = makeButton(grid, game.managers().textLibrary, mouse, "Quit", { 100.0f, 20.0f }, U8_STR("Quit"));
	quitButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		done = true;
		});

	grid->position(grid->position() - MV::toPoint(grid->bounds().size()) / 2.0f);
	
	auto scrollerWindow = limbo->make("ScrollerWindow")->position({ 100.0f, 100.0f })->
		attach<MV::Scene::Sprite>()->color(MV::Color(128, 128, 128, 128))->bounds({ MV::Size{400.0f, 400.0f} })->owner();
	auto childWindow = scrollerWindow->make("ChildView")->position({ 0.0f, 20.0f })->
		attach<MV::Scene::Sprite>()->color(MV::Color(128, 128, 128, 128))->bounds({ MV::Size{400.0f, 380.0f} })->owner();

	auto moveState = std::make_shared<bool>(true);
	auto toggleBut = makeButton(limbo, game.managers().textLibrary, mouse, "Quit", MV::Size(200.0f, 20.0f), "Toggle");
	toggleBut->owner()->position({ 15.0f, 15.0f });
	toggleBut->onAccept.connect("Toggle", [=](const std::shared_ptr<MV::Scene::Clickable>& a_clickable) {
		*moveState = !*moveState;
		});

	scrollerWindow->rotation({ 0.0f, 0.0f, 45.0f });
	scrollerWindow->attach<MV::Scene::Clickable>(mouse)->color(MV::Color(32, 23, 32, 128))->show()->bounds({ MV::Size{400.0f, 20.0f} })->
		onDrag.connect("Drag", [moveState,childWindow,weakScrollerWindow=std::weak_ptr<MV::Scene::Node>(scrollerWindow)](const std::shared_ptr<MV::Scene::Clickable> &a_handle, const MV::Point<int>& startPosition, const MV::Point<int>& deltaPosition) {
			if (auto scrollerWindow = weakScrollerWindow.lock()) {
				if (*moveState) {
					scrollerWindow->translate(scrollerWindow->parent()->localFromScreen(deltaPosition) - scrollerWindow->parent()->localFromScreen(MV::Point<int>()));
				} else {
					childWindow->translate(childWindow->parent()->localFromScreen(deltaPosition) - childWindow->parent()->localFromScreen(MV::Point<int>()));
				}
			}
		});

// 	auto sendButton = makeButton(grid, game.managers().textLibrary, mouse, "Send", { 100.0f, 20.0f }, UTF_CHAR_STR("Send"));
// 	sendButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
// 		if (client) {
// 			client->send(MV::toBinaryStringCast<ServerAction>(std::make_shared<CreatePlayer>("maxmike@gmail.com", "M2tM", "SuperTinker123")));
// 		}
// 	});

	grid->component<MV::Scene::Grid>()->anchors().anchor({ MV::point(0.5f, 0.5f), MV::point(0.5f, 0.5f) }).parent(screenScaler.self(), true);
}

int GameEditor::runGameFrames(int a_frameLimit, const std::string& a_capturePath) {
	game.returnFromBackground();
	managers.timer.start();
	managers.timer.delta("tick");
	int frame = 0;
	for (; (a_frameLimit < 0 || frame < a_frameLimit) && game.update(managers.timer.delta("tick")); ++frame) {
		game.handleInput();
		if (!a_capturePath.empty() && frame == a_frameLimit - 1) {
			managers.renderer.clearScreen();
			if (game.instance()) {
				game.instance()->scene()->draw();
			}
			game.root()->draw();
			captureScreenshot(a_capturePath);
			managers.renderer.updateScreen();
		} else {
			game.render();
		}
		std::this_thread::yield();
	}
	return frame;
}

// Verbs (one per line, # comments):
//   wait S | move X Y | move node ID | down | up | click X Y | click node ID | drag X1 Y1 X2 Y2 [STEPS]
//   scroll AMOUNT [X Y] | type TEXT | key NAME (SDL key name: Return, Escape, Backspace, Tab...)
//   shot FILE.bmp | dump ID | tree [ID] [DEPTH] | waitfor ID [SECONDS=15] | waitgone ID [SECONDS=15] | print TEXT | quit
// Mouse position/buttons are injected into the game's TapDevice; wheel and keys are real SDL events.
// Live mode tails the file: append lines while the game runs and they execute as they arrive.
int GameEditor::runDriver(const std::string& a_scriptPath, bool a_live) {
	struct Step { std::string op; std::vector<std::string> args; int remaining = 0; };
	std::vector<Step> steps;
	std::streamoff consumed = 0;
	auto readNewLines = [&]() {
		std::ifstream in(a_scriptPath, std::ios::binary);
		if (!in) { return false; }
		in.seekg(0, std::ios::end);
		auto size = static_cast<std::streamoff>(in.tellg());
		if (size <= consumed) { return false; }
		in.seekg(consumed);
		std::string pending(static_cast<size_t>(size - consumed), '\0');
		in.read(pending.data(), size - consumed);
		auto lastNewline = pending.find_last_of('\n');
		if (lastNewline == std::string::npos) { return false; }   // a partial line is still being written
		pending.resize(lastNewline + 1);
		consumed += static_cast<std::streamoff>(pending.size());
		std::istringstream lines(pending);
		std::string line;
		while (std::getline(lines, line)) {
			if (!line.empty() && line.back() == '\r') { line.pop_back(); }
			auto hash = line.find('#');
			if (hash == 0) { continue; }
			std::istringstream words(line);
			Step step;
			if (!(words >> step.op)) { continue; }
			if (step.op == "type" || step.op == "print") {
				std::string rest;
				std::getline(words, rest);
				step.args.push_back(rest.empty() ? rest : rest.substr(1));
			} else {
				std::string word;
				while (words >> word) { step.args.push_back(word); }
			}
			auto push = [&](const std::string& a_op, std::vector<std::string> a_args = {}) { steps.push_back({ a_op, std::move(a_args) }); };
			if (step.op == "click") {
				push("move", step.args); push("down"); push("wait", { "0.05" }); push("up"); push("wait", { "0.05" });
			} else if (step.op == "drag" && step.args.size() >= 4) {
				push("move", { step.args[0], step.args[1] }); push("down"); push("wait", { "0.05" });
				push("moveto", { step.args[2], step.args[3], step.args.size() > 4 ? step.args[4] : "12" });
				push("wait", { "0.05" }); push("up"); push("wait", { "0.05" });
			} else {
				steps.push_back(step);
			}
		}
		return true;
	};
	{
		std::ifstream probe(a_scriptPath);
		if (!probe) { std::cerr << "[drive] cannot open " << a_scriptPath << std::endl; return 2; }
	}
	readNewLines();

	auto findNode = [&](const std::string& a_id) -> std::shared_ptr<MV::Scene::Node> {
		auto node = game.gui().root()->get(a_id, false);
		if (!node && game.instance()) { node = game.instance()->scene()->get(a_id, false); }
		return node;
	};
	// A node's own box includes its origin; a clickable's hit rectangle is what a person aims at
	auto hitBox = [](const std::shared_ptr<MV::Scene::Node>& a_node) {
		auto clickable = a_node->component<MV::Scene::Clickable>(false, false);
		return clickable ? a_node->screenFromLocal(clickable->bounds()) : a_node->screenBounds();
	};
	auto center = [&](const std::shared_ptr<MV::Scene::Node>& a_node) {
		auto bounds = hitBox(a_node);
		return MV::Point<int>((bounds.minPoint.x + bounds.maxPoint.x) / 2, (bounds.minPoint.y + bounds.maxPoint.y) / 2);
	};
	auto target = [&](const std::vector<std::string>& a_args) -> std::optional<MV::Point<int>> {
		if (a_args.size() >= 2 && a_args[0] == "node") {
			auto node = findNode(a_args[1]);
			if (!node) { std::cout << "[drive] no node " << a_args[1] << std::endl; return std::nullopt; }
			return center(node);
		}
		if (a_args.size() >= 2) { return MV::Point<int>(std::atoi(a_args[0].c_str()), std::atoi(a_args[1].c_str())); }
		return std::nullopt;
	};
	auto pushKey = [](SDL_Keycode a_key, bool a_down) {
		SDL_Event event{};
		event.type = a_down ? SDL_KEYDOWN : SDL_KEYUP;
		event.key.state = a_down ? SDL_PRESSED : SDL_RELEASED;
		event.key.keysym.sym = a_key;
		event.key.keysym.scancode = SDL_GetScancodeFromKey(a_key);
		SDL_PushEvent(&event);
	};

	std::function<void(const std::shared_ptr<MV::Scene::Node>&, int, int)> printTree = [&](const std::shared_ptr<MV::Scene::Node>& a_node, int a_depth, int a_maxDepth) {
		auto hit = hitBox(a_node);
		bool clickable = a_node->component<MV::Scene::Clickable>(false, false) != nullptr;
		std::cout << "[drive] " << std::string(static_cast<size_t>(a_depth) * 2, ' ') << (a_node->id().empty() ? "(unnamed)" : a_node->id())
			<< (clickable ? " [clickable]" : "") << (a_node->visible() ? "" : " [hidden]")
			<< " (" << hit.minPoint.x << "," << hit.minPoint.y << ")-(" << hit.maxPoint.x << "," << hit.maxPoint.y << ")" << std::endl;
		if (a_depth < a_maxDepth) {
			for (auto& child : *a_node) { printTree(child, a_depth + 1, a_maxDepth); }
		}
	};

	auto& mouse = game.mouse();
	MV::Point<int> cursor(managers.renderer.window().drawableSize().width / 2, managers.renderer.window().drawableSize().height / 2);
	bool left = false;
	double waitRemaining = 0.0;
	double pollRemaining = 0.0;
	size_t next = 0;
	std::string capture;
	bool quit = false;

	game.returnFromBackground();
	managers.timer.start();
	managers.timer.delta("tick");
	mouse.simulate(cursor, left);
	while (!quit) {
		double dt = managers.timer.delta("tick");
		waitRemaining -= dt;
		while (!quit && next < steps.size() && waitRemaining <= 0.0) {
			auto& step = steps[next];
			bool advance = true;
			if (step.op == "wait") { waitRemaining = step.args.empty() ? 0.0 : std::atof(step.args[0].c_str()); }
			else if (step.op == "move") { if (auto to = target(step.args)) { cursor = *to; } }
			else if (step.op == "moveto") {
				if (step.remaining == 0) { step.remaining = std::max(1, std::atoi(step.args.size() > 2 ? step.args[2].c_str() : "12")); }
				if (auto to = target(step.args)) {
					cursor += (*to - cursor) / step.remaining;
					if (--step.remaining > 0) { advance = false; }
					else { cursor = *to; }
				}
				waitRemaining = 1.0 / 60.0;
			}
			else if (step.op == "waitfor") {
				auto node = step.args.empty() ? nullptr : findNode(step.args[0]);
				auto box = node ? hitBox(node) : MV::BoxAABB<int>();
				bool up = node && node->visible() && node->active() && box.width() > 0 && box.height() > 0;
				// a page that just appeared is not clickable on its first frames (its buttons hook the
				// mouse on their first update), so a node counts as up after a dozen settled frames
				if (up && step.remaining <= -12) { std::cout << "[drive] " << step.args[0] << " is up" << std::endl; }
				else if (up) { step.remaining = std::min(step.remaining, 0) - 1; advance = false; waitRemaining = 1.0 / 60.0; }
				else {
					if (step.remaining == 0) { step.remaining = static_cast<int>(std::atof(step.args.size() > 1 ? step.args[1].c_str() : "15") * 60.0) + 1; }
					if (--step.remaining > 0) { advance = false; waitRemaining = 1.0 / 60.0; }
					else { std::cout << "[drive] waitfor " << (step.args.empty() ? "" : step.args[0]) << " timed out" << std::endl; }
				}
			}
			else if (step.op == "waitgone") {
				auto node = step.args.empty() ? nullptr : findNode(step.args[0]);
				bool gone = !node || !node->visible();
				if (gone) { std::cout << "[drive] " << (step.args.empty() ? "" : step.args[0]) << " is gone" << std::endl; }
				else {
					if (step.remaining == 0) { step.remaining = static_cast<int>(std::atof(step.args.size() > 1 ? step.args[1].c_str() : "15") * 60.0) + 1; }
					if (--step.remaining > 0) { advance = false; waitRemaining = 1.0 / 60.0; }
					else { std::cout << "[drive] waitgone " << step.args[0] << " timed out" << std::endl; }
				}
			}
			else if (step.op == "down") { left = true; waitRemaining = 1.0 / 60.0; }
			else if (step.op == "up") { left = false; waitRemaining = 1.0 / 60.0; }
			else if (step.op == "scroll") {
				if (step.args.size() >= 3) { cursor = MV::Point<int>(std::atoi(step.args[1].c_str()), std::atoi(step.args[2].c_str())); }
				SDL_Event event{};
				event.type = SDL_MOUSEWHEEL;
				event.wheel.y = step.args.empty() ? 1 : std::atoi(step.args[0].c_str());
				SDL_PushEvent(&event);
				waitRemaining = 1.0 / 60.0;
			}
			else if (step.op == "type") {
				const std::string& text = step.args.empty() ? std::string() : step.args[0];
				for (size_t i = 0; i < text.size(); i += 31) {
					SDL_Event event{};
					event.type = SDL_TEXTINPUT;
					auto chunk = text.substr(i, 31);
					std::memcpy(event.text.text, chunk.c_str(), chunk.size() + 1);
					SDL_PushEvent(&event);
				}
			}
			else if (step.op == "key") {
				auto key = step.args.empty() ? SDLK_UNKNOWN : SDL_GetKeyFromName(step.args[0].c_str());
				if (key == SDLK_UNKNOWN) { std::cout << "[drive] unknown key " << (step.args.empty() ? "" : step.args[0]) << std::endl; }
				else { pushKey(key, true); pushKey(key, false); }
			}
			else if (step.op == "shot") { capture = step.args.empty() ? "drive_shot.bmp" : step.args[0]; }
			else if (step.op == "dump") {
				auto node = step.args.empty() ? nullptr : findNode(step.args[0]);
				if (!node) { std::cout << "[drive] dump: no node " << (step.args.empty() ? "" : step.args[0]) << std::endl; }
				else {
					auto bounds = node->screenBounds();
					auto hit = hitBox(node);
					std::cout << "[drive] " << step.args[0] << " visible " << node->visible() << " screen (" << bounds.minPoint.x << "," << bounds.minPoint.y << ")-(" << bounds.maxPoint.x << "," << bounds.maxPoint.y << ") hit (" << hit.minPoint.x << "," << hit.minPoint.y << ")-(" << hit.maxPoint.x << "," << hit.maxPoint.y << ") children " << node->size() << " active " << node->active() << std::endl;
				}
			}
			else if (step.op == "tree") {
				std::shared_ptr<MV::Scene::Node> root = step.args.empty() ? game.gui().root() : findNode(step.args[0]);
				int depth = step.args.size() > 1 ? std::atoi(step.args[1].c_str()) : 4;
				if (root) { printTree(root, 0, depth); } else { std::cout << "[drive] tree: no node " << step.args[0] << std::endl; }
			}
			else if (step.op == "print") { std::cout << "[drive] " << (step.args.empty() ? "" : step.args[0]) << std::endl; }
			else if (step.op == "quit") { quit = true; }
			else { std::cout << "[drive] unknown verb " << step.op << std::endl; }
			if (advance) { ++next; }
		}
		if (a_live && next >= steps.size()) {
			pollRemaining -= dt;
			if (pollRemaining <= 0.0) { pollRemaining = 0.25; readNewLines(); }
		}
		mouse.simulate(cursor, left);
		if (!game.update(dt)) { break; }
		game.handleInput();
		if (!capture.empty()) {
			managers.renderer.clearScreen();
			if (game.instance()) { game.instance()->scene()->draw(); }
			game.root()->draw();
			captureScreenshot(capture);
			managers.renderer.updateScreen();
			capture.clear();
		} else {
			game.render();
		}
		std::this_thread::yield();
	}
	mouse.stopSimulating();
	std::cout << "[drive] done (" << next << "/" << steps.size() << " steps)" << std::endl;
	return next == steps.size() ? 0 : 1;
}

void GameEditor::dumpSlicedSprites() {
	for (auto&& entry : game.gui().root()->componentsInChildren<MV::Scene::Sprite>(false, true)) {
		auto sprite = std::get<0>(entry).self();
		if (!sprite || sprite->pointSize() != 16 || !sprite->texture()) { continue; }
		auto handle = sprite->texture();
		auto percent = handle->rawPercent();
		auto slice = handle->slice();
		auto logical = handle->logicalSize();
		std::cout << "[slice] " << sprite->owner()->id() << "." << sprite->id() << " handle=" << handle->name()
			<< " percent=(" << percent.minPoint.x << "," << percent.minPoint.y << ")-(" << percent.maxPoint.x << "," << percent.maxPoint.y << ")"
			<< " slice=(" << slice.minPoint.x << "," << slice.minPoint.y << ")-(" << slice.maxPoint.x << "," << slice.maxPoint.y << ")"
			<< " logical=" << logical.width << "x" << logical.height << " texture=" << handle->texture()->size().width << "x" << handle->texture()->size().height
			<< " visible=" << sprite->owner()->visible() << std::endl;
		for (size_t i = 0; i < 16; ++i) {
			auto& p = sprite->point(i);
			std::cout << "    p" << i << "=(" << p.x << "," << p.y << ") uv=(" << p.textureX << "," << p.textureY << ")" << std::endl;
		}
	}
}

int GameEditor::runFrames(int a_frameCount, const std::string& a_capturePath) {
	int frames = runGameFrames(a_frameCount, a_capturePath);
	dumpSlicedSprites();
	std::cout << "Game frames completed (" << frames << "/" << a_frameCount << ")" << std::endl;
	return frames == a_frameCount ? 0 : 1;
}

void GameEditor::captureScreenshot(const std::string& a_path) {
	auto size = managers.renderer.window().drawableSize();
	SDL_Surface* surface = SDL_CreateRGBSurface(0, size.width, size.height, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
	std::vector<uint8_t> pixels(static_cast<size_t>(size.width) * size.height * 4);
	glReadPixels(0, 0, size.width, size.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	auto* target = static_cast<uint8_t*>(surface->pixels);
	for (int y = 0; y < size.height; ++y) {
		std::memcpy(target + static_cast<size_t>(y) * surface->pitch,
			pixels.data() + static_cast<size_t>(size.height - 1 - y) * size.width * 4,
			static_cast<size_t>(size.width) * 4);
	}
	SDL_SaveBMP(surface, a_path.c_str());
	SDL_FreeSurface(surface);
	std::cout << "Game screenshot: " << a_path << std::endl;
}
