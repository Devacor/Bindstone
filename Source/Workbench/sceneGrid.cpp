#include "sceneGrid.h"
#include <algorithm>
#include <cmath>

namespace Workbench {

	namespace {
		const MV::Color minorLine(1.0f, 1.0f, 1.0f, 0.045f);
		const MV::Color majorLine(1.0f, 1.0f, 1.0f, 0.10f);
		const MV::Color horizontalAxis(0.86f, 0.30f, 0.30f, 0.55f);
		const MV::Color verticalAxis(0.35f, 0.75f, 0.35f, 0.55f);
	}

	SceneGrid::SceneGrid(const std::shared_ptr<MV::Scene::Node>& a_host, const Theme& a_theme) :
		host(a_host),
		theme(a_theme) {
	}

	void SceneGrid::visible(bool a_visible) {
		isVisible = a_visible;
		if (isVisible) {
			host->show();
		} else {
			host->hide();
		}
		laidOutZoom = 0.0f;
	}

	void SceneGrid::spacing(const MV::Size<>& a_worldUnits) {
		worldSpacing = MV::size(std::max(0.001f, a_worldUnits.width), std::max(0.001f, a_worldUnits.height));
	}

	std::shared_ptr<MV::Scene::Sprite> SceneGrid::line(size_t a_index) {
		while (lines.size() <= a_index) {
			lines.push_back(host->make(MV::guid("line_"))->attach<MV::Scene::Sprite>().self());
		}
		return lines[a_index];
	}

	void SceneGrid::update(const MV::Point<>& a_cameraPosition, float a_zoom, const MV::BoxAABB<>& a_view) {
		if (!isVisible || a_zoom <= 0.0f) {
			return;
		}
		if (a_cameraPosition == laidOutPosition && a_zoom == laidOutZoom && a_view == laidOutView && worldSpacing.width == laidOutSpacing.width && worldSpacing.height == laidOutSpacing.height) {
			return;
		}
		laidOutPosition = a_cameraPosition;
		laidOutZoom = a_zoom;
		laidOutView = a_view;
		laidOutSpacing = worldSpacing;

		auto widened = [&](float a_step) {
			while (a_step * a_zoom < theme.px(12.0f)) {
				a_step *= 10.0f;
			}
			return a_step;
		};
		const float stepAcross = widened(worldSpacing.width);
		const float stepDown = widened(worldSpacing.height);
		const float thickness = std::max(1.0f, theme.px(0.5f));
		size_t used = 0;
		auto layOut = [&](bool a_vertical) {
			const float viewStart = a_vertical ? a_view.minPoint.x : a_view.minPoint.y;
			const float viewEnd = a_vertical ? a_view.maxPoint.x : a_view.maxPoint.y;
			const float cameraOffset = a_vertical ? a_cameraPosition.x : a_cameraPosition.y;
			const float step = a_vertical ? stepAcross : stepDown;
			const auto firstIndex = static_cast<long long>(std::ceil((viewStart - cameraOffset) / (step * a_zoom)));
			for (long long index = firstIndex; ; ++index) {
				const float screen = std::round(cameraOffset + static_cast<float>(index) * step * a_zoom);
				if (screen > viewEnd) {
					break;
				}
				auto sprite = line(used++);
				sprite->owner()->show();
				sprite->owner()->position(a_vertical ? MV::point(screen, a_view.minPoint.y) : MV::point(a_view.minPoint.x, screen));
				sprite->bounds(a_vertical ? MV::size(thickness, a_view.maxPoint.y - a_view.minPoint.y) : MV::size(a_view.maxPoint.x - a_view.minPoint.x, thickness));
				sprite->color(index == 0 ? (a_vertical ? verticalAxis : horizontalAxis) : index % 10 == 0 ? majorLine : minorLine);
			}
		};
		layOut(true);
		layOut(false);
		for (size_t index = used; index < lines.size(); ++index) {
			lines[index]->owner()->hide();
		}
	}

}
