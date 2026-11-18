#ifndef _WORKBENCH_TOOLTIPS_H_
#define _WORKBENCH_TOOLTIPS_H_

#include <memory>
#include <string>
#include "MV/Render/package.h"
#include "MV/Interface/tapDevice.h"
#include "theme.h"

namespace Workbench {

	// One small label beside the pointer, shown once the pointer has rested on a control that has a tip, and gone
	// when it leaves or presses. Controls reach it through the services (widgets.h: tooltip()).
	class Tooltips {
	public:
		Tooltips(MV::Services& a_services, const Theme& a_theme);

		// Where the label is drawn: the last node added to the UI, so it is above everything.
		void host(const std::shared_ptr<MV::Scene::Node>& a_host) { hostNode = a_host; }
		void attach(const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const std::string& a_text);
		void update(double a_dt);

		// The tip on screen; empty when there is none.
		const std::string& shown() const { return shownText; }
		// A label kept beside the pointer while something is dragged, saying what letting go there would do. It
		// shows at once and follows the pointer; empty text takes it away.
		void dragNote(const std::string& a_text);
		const std::string& dragNote() const { return noteText; }
		// How long the pointer rests before the tip shows.
		double restSeconds = 0.45;

	private:
		void show();
		void hide();
		std::shared_ptr<MV::Scene::Node> makePopup(const std::string& a_id, const std::string& a_text, MV::Size<>& a_size);
		void placeBesidePointer(const std::shared_ptr<MV::Scene::Node>& a_popup, const MV::Size<>& a_size);

		MV::Services& services;
		const Theme& theme;
		std::shared_ptr<MV::Scene::Node> hostNode;
		std::shared_ptr<MV::Scene::Node> popup;
		std::shared_ptr<MV::Scene::Node> notePopup;
		std::string noteText;
		MV::Size<> noteSize;
		std::weak_ptr<MV::Scene::Clickable> pending;
		std::string pendingText;
		std::string shownText;
		MV::Point<int> restedAt;
		double rested = 0.0;
	};

}

#endif
