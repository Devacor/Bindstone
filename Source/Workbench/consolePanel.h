#ifndef _WORKBENCH_CONSOLEPANEL_H_
#define _WORKBENCH_CONSOLEPANEL_H_

#include <memory>
#include <string>
#include <vector>
#include "dock.h"
#include "messageLog.h"

namespace Workbench {

	// The message log as a list, newest first, each line in its level's colour, with a Clear button above it.
	class ConsolePanel : public Panel {
	public:
		explicit ConsolePanel(std::shared_ptr<MessageLog> a_log) : Panel("Console"), log(std::move(a_log)) {}

		// The log changed: the list is rebuilt once, at the next update.
		void logChanged() { stale = true; }

		void resized(const MV::Size<>& a_size) override;
		void update(double a_dt) override;

		size_t rowCount() const { return rowTexts.size(); }
		// A row as listed, without its colour: "selftest failure (x2)".
		const std::string& rowText(size_t a_row) const { return rowTexts[a_row]; }
		MV::Point<int> clearButtonScreenPoint() const;

		// The colour markup a level is written in.
		static std::string levelMarkup(MessageLevel a_level);

	protected:
		void build() override;

	private:
		void rebuildRows();

		std::shared_ptr<MessageLog> log;
		std::shared_ptr<MV::Scene::Node> rowHost;
		std::shared_ptr<MV::Scene::Node> clearNode;
		std::vector<std::string> rowTexts;
		float width = 0.0f;
		bool stale = true;
	};

}

#endif
