#include "gameEditor.h"



GameEditor::GameEditor() :
	game(managers),
	editor(managers),
	limbo(MV::Scene::Node::make(managers.renderer))
{
	// 		auto stencilNode = limbo->make("StencilTestNode");
	// 		auto spineTestNode = stencilNode->make("SpineTest")->position({ 400.0f, 600.0f })->attach<MV::Scene::Spine>(MV::Scene::Spine::FileBundle("Assets/Spine/Tree/life.json", "Assets/Spine/Tree/life.atlas", 0.5f))->shader(MV::DEFAULT_ID)->animate("idle")->bindNode("effects", "tree_particle")->bindNode("effects", "simple")->owner();
	// 
	// 		stencilNode->attach<MV::Scene::Stencil>()->bounds({ MV::Point<>(), MV::Size<>(450.0f, 550.0f) });

	// 		spineTestNode->make("PaletteTest")->position({ -50.0f, -100.0f })->
	// 			attach<MV::Scene::Palette>(mouse)->bounds(MV::size(256.0f, 256.0f));
	// 		auto populateArchive = [&](cereal::JSONInputArchive& archive) {
	// 			archive.add(cereal::make_nvp("renderer", &managers.renderer));
	// 			archive.add(cereal::make_nvp("pool", &managers.pool));
	// 		};
	// 		spineTestNode->loadChild("simple.scene", populateArchive);
	// 		spineTestNode->loadChild("tree_particle.scene", populateArchive);

	screenScaler = limbo->attach<MV::Scene::Sprite>();
	screenScaler->color({ 1.0f, 1.0f, 1.0f, 0.0f });
	screenScaler->bounds({ MV::point(0.0f, 0.0f), game.getManager().renderer.world().size() });

//	auto textureSheet = MV::FileTextureDefinition::make("Assets/Images/slice.png");
// 	auto alignedSprite = limbo->make("repositionNode")->attach<MV::Scene::Sprite>();
// 	alignedSprite->texture(textureSheet->makeHandle());
// 	alignedSprite->anchors().anchor({ MV::point(0.5f, 0.5f), MV::point(0.5f, 0.5f) }).usePosition(true).parent(screenScaler.self());

// 	mouse.onLeftMouseDownEnd.connect("TEST", [&](MV::MouseState& a_mouse) {
// 		std::cout << "PRE: " << screenScaler->bounds() << std::endl;
// 		screenScaler->bounds({ screenScaler->owner()->localFromScreen(a_mouse.position()), screenScaler->bounds().maxPoint });
// 		std::cout << "POST: " << screenScaler->bounds() << std::endl;
// 	});
// 	mouse.onRightMouseDownEnd.connect("TEST", [&](MV::MouseState& a_mouse) {
// 		std::cout << "PRE: " << screenScaler->bounds() << std::endl;
// 		screenScaler->bounds({ screenScaler->bounds().minPoint, screenScaler->owner()->localFromScreen(a_mouse.position()) });
// 		std::cout << "POST: " << screenScaler->bounds() << std::endl;
// 	});

	auto grid = limbo->make("Grid")->position({ (static_cast<float>(game.getManager().renderer.window().width()) - 108.0f) / 2.0f, 200.0f })->
		attach<MV::Scene::Grid>()->columns(1)->padding({ 2.0f, 2.0f })->margin({ 4.0f, 4.0f })->color({ BOX_BACKGROUND })->owner();

	auto editorButton = makeButton(grid, game.getManager().textLibrary, mouse, "Editor", { 100.0f, 20.0f }, UTF_CHAR_STR("Editor"));
	editorButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		runEditor();
	});
	auto gameButton = makeButton(grid, game.getManager().textLibrary, mouse, "Game", { 100.0f, 20.0f }, UTF_CHAR_STR("Game"));
	gameButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		runGame();
	});
	auto quitButton = makeButton(grid, game.getManager().textLibrary, mouse, "Quit", { 100.0f, 20.0f }, UTF_CHAR_STR("Quit"));
	quitButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		done = true;
	});

	auto serverButton = makeButton(grid, game.getManager().textLibrary, mouse, "Server", { 100.0f, 20.0f }, UTF_CHAR_STR("Server"));
	serverButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		std::cout << "serving" << std::endl;
		server = std::make_shared<LobbyServer>(managers);
	});

	auto clientButton = makeButton(grid, game.getManager().textLibrary, mouse, "Client", { 100.0f, 20.0f }, UTF_CHAR_STR("Client"));
	clientButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		//client = MV::Client::make(MV::Url{ "http://ec2-54-218-22-3.us-west-2.compute.amazonaws.com:22325" }, [=](const std::string &a_message) {
		client = MV::Client::make(MV::Url{ "http://96.229.120.252:22325" }, [=](const std::string &a_message) {
			auto value = MV::fromBinaryString<std::shared_ptr<ClientAction>>(a_message);
			value->execute();
		}, [](const std::string &a_dcreason) {
			std::cout << "Disconnected: " << a_dcreason << std::endl;
		}, [=] {});
		//client->send("UUUUH!");
	});

	auto sendButton = makeButton(grid, game.getManager().textLibrary, mouse, "Send", { 100.0f, 20.0f }, UTF_CHAR_STR("Send"));
	sendButton->onAccept.connect("Swap", [&](const std::shared_ptr<MV::Scene::Clickable>&) {
		if (client) {
			client->send(MV::toBinaryStringCast<ServerAction>(std::make_shared<CreatePlayer>("maxmike@gmail.com", "M2tM", "SuperTinker123")));
		}
	});

	grid->component<MV::Scene::Grid>()->anchors().anchor({ MV::point(0.5f, 0.5f), MV::point(0.5f, 0.5f) }).usePosition(true).parent(screenScaler.self(), MV::Scene::Anchors::BoundsToOffset::Apply);

	if (MV::RUNNING_IN_HEADLESS) {
		serverButton->press();
	}
}