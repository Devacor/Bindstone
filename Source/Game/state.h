#ifndef __MV_GAMESTATE_H__
#define __MV_GAMESTATE_H__

struct Catalogs {
	std::unique_ptr<BuildingCatalog> buildings;
};

class LocalData {
public:
	std::shared_ptr<Player> player() const {
		return localPlayer;
	}

	BuildingData& building(const std::string &a_id) const {

	}
private:
	std::shared_ptr<Player> localPlayer;
	std::vector<BuildingData> buildings;
};

struct Constants {
	int startHealth = 20;
};

#endif