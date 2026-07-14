#include "widgets.h"
#include "MV/Utility/generalUtility.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Workbench {

	namespace {
		std::string formatNumber(float a_value) {
			std::ostringstream out;
			out << a_value;
			return out.str();
		}

		MV::Color axisTint(const std::string& a_axis) {
			if (a_axis == "X" || a_axis == "R") { return MV::Color(0x9c4a4a); }
			if (a_axis == "Y" || a_axis == "G") { return MV::Color(0x4a9c5a); }
			if (a_axis == "Z" || a_axis == "B") { return MV::Color(0x4a6a9c); }
			return MV::Color(0x6a6a72);
		}

		// White rounded-box coverage mask (Inigo Quilez SDF, 1px AA edge). STRAIGHT alpha: the
		// default premultiply shader multiplies rgb by alpha at the end. Power-of-two content so
		// convertToPowerOfTwoSurface never pads (padding would shrink/blur the slice corners).
		std::shared_ptr<MV::OwnedSurface> makeRoundedRectSurface(int a_radius) {
			const int contentSize = MV::roundUpPowerOfTwo(2 * a_radius + 2);
			int bitsPerPixel = 0;
			Uint32 redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
			SDL_PixelFormatEnumToMasks(SDL_PIXELFORMAT_ABGR8888, &bitsPerPixel, &redMask, &greenMask, &blueMask, &alphaMask);
			SDL_Surface* surface = SDL_CreateRGBSurface(0, contentSize, contentSize, bitsPerPixel, redMask, greenMask, blueMask, alphaMask);
			SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_NONE);

			const float radius = static_cast<float>(a_radius);
			const float half = contentSize * 0.5f;
			auto* pixels = static_cast<uint32_t*>(surface->pixels);
			const int pitch = surface->pitch / 4;
			for (int y = 0; y < contentSize; ++y) {
				for (int x = 0; x < contentSize; ++x) {
					const float qx = std::abs((x + 0.5f) - half) - half + radius;
					const float qy = std::abs((y + 0.5f) - half) - half + radius;
					const float mx = std::max(qx, 0.0f);
					const float my = std::max(qy, 0.0f);
					const float distance = std::sqrt(mx * mx + my * my) + std::min(std::max(qx, qy), 0.0f) - radius;
					const float coverage = std::clamp(0.5f - distance, 0.0f, 1.0f);
					const auto alpha = static_cast<uint8_t>(coverage * 255.0f + 0.5f);
					pixels[y * pitch + x] = (uint32_t(alpha) << 24) | 0x00FFFFFFu;
				}
			}
			return MV::OwnedSurface::make(surface);
		}
	}

	std::shared_ptr<MV::TextureHandle> roundedRectHandle(MV::Services& a_services, const Theme& a_theme) {
		const int radius = std::max(2, static_cast<int>(a_theme.cornerRadius + 0.5f));
		auto definition = a_services.get<MV::SharedTextures>()->surface("workbenchRounded_r" + std::to_string(radius), [radius] {
			return makeRoundedRectSurface(radius);
		});
		auto handle = definition->makeHandle();
		const int contentSize = handle->texture()->size().width;
		handle->slice(MV::BoxAABB<int>(MV::point(radius, radius), MV::point(contentSize - radius, contentSize - radius)));
		return handle;
	}

	std::shared_ptr<MV::Scene::Sprite> attachRoundedRect(const std::shared_ptr<MV::Scene::Node>& a_owner, MV::Services& a_services, const Theme& a_theme, const MV::Size<>& a_size, const MV::Color& a_color) {
		return a_owner->attach<MV::Scene::Sprite>()->bounds(a_size)->texture(roundedRectHandle(a_services, a_theme))->color(a_color);
	}

	std::shared_ptr<MV::Scene::Text> makeLabel(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_text) {
		auto& library = *a_services.get<MV::TextLibrary>();
		auto text = a_parent->make(a_id)->attach<MV::Scene::Text>(library, a_theme.labelFont);
		text->bounds({ MV::Point<>(), a_size });
		text->justification(MV::TextJustification::LEFT)->wrapping(MV::TextWrapMethod::NONE, a_size.width)->minimumLineHeight(a_size.height)->text(a_text);
		return text.self();
	}

	std::shared_ptr<MV::Scene::Button> makeButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_text, std::function<void()> a_onClick) {
		auto& library = *a_services.get<MV::TextLibrary>();
		auto& mouse = *a_services.get<MV::TapDevice>();
		auto button = a_parent->make(a_id)->attach<MV::Scene::Button>(mouse)->bounds(a_size);

		auto activeScene = attachRoundedRect(button->owner()->make("active"), a_services, a_theme, a_size, a_theme.buttonActive())->owner();
		auto idleScene = attachRoundedRect(button->owner()->make("idle"), a_services, a_theme, a_size, a_theme.buttonIdle())->owner();
		for (auto&& stateScene : { activeScene, idleScene }) {
			auto stateText = stateScene->attach<MV::Scene::Text>(library, a_theme.buttonFont);
			stateText->bounds({ MV::Point<>(), a_size });
			stateText->justification(MV::TextJustification::CENTER)->wrapping(MV::TextWrapMethod::NONE, a_size.width)->minimumLineHeight(a_size.height)->text(a_text);
		}
		button->activeNode(activeScene);
		button->idleNode(idleScene);
		if (a_onClick) {
			button->onAccept.connect("click", [a_onClick](const std::shared_ptr<MV::Scene::Clickable>&) {
				a_onClick();
			});
		}
		return button;
	}

	std::shared_ptr<MV::Scene::Text> makeTextField(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, const std::string& a_id, const MV::Size<>& a_size, const std::string& a_initial, std::function<void(const std::string&)> a_onCommit) {
		auto& library = *a_services.get<MV::TextLibrary>();
		auto& mouse = *a_services.get<MV::TapDevice>();
		auto box = attachRoundedRect(a_parent->make(a_id), a_services, a_theme, a_size, a_theme.fieldBg())->owner();
		auto text = box->attach<MV::Scene::Text>(library, a_theme.labelFont);
		text->bounds({ MV::Point<>(), a_size });
		text->justification(MV::TextJustification::LEFT)->wrapping(MV::TextWrapMethod::NONE, a_size.width)->minimumLineHeight(a_size.height)->text(a_initial);

		auto clickable = box->attach<MV::Scene::Clickable>(mouse)->bounds(a_size);
		std::weak_ptr<MV::Scene::Text> weakText = text.self();
		FocusRouter* focus = &a_focus;
		auto commit = [weakText, a_onCommit]() {
			if (auto locked = weakText.lock()) {
				if (a_onCommit) {
					a_onCommit(locked->text());
				}
			}
		};
		clickable->onAccept.connect("focus", [weakText, focus, commit](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (auto locked = weakText.lock()) {
				focus->activate(locked, commit);
			}
		});
		text->onEnter.connect("commit", [focus](std::shared_ptr<MV::Scene::Text>) {
			focus->deactivate();
		});
		return text.self();
	}

	std::shared_ptr<MV::Scene::Node> makeToggle(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, bool a_initial, std::function<void(bool)> a_onChange) {
		auto& mouse = *a_services.get<MV::TapDevice>();
		MV::Size<> boxSize(a_theme.fieldHeight, a_theme.fieldHeight);
		auto node = a_parent->make(a_id);
		auto mark = attachRoundedRect(node->make("mark"), a_services, a_theme, boxSize / 2.0f, a_theme.accent())->owner();
		mark->position(MV::toPoint(boxSize / 4.0f));
		if (!a_initial) {
			mark->hide();
		}
		auto toggleClick = node->attach<MV::Scene::Clickable>(mouse);
		toggleClick->bounds(boxSize);
		toggleClick->texture(roundedRectHandle(a_services, a_theme));
		toggleClick->color(a_theme.fieldBg());
		toggleClick->show();
		toggleClick->onAccept.connect("toggle", [mark, a_onChange](const std::shared_ptr<MV::Scene::Clickable>&) {
			bool nowOn = !mark->visible();
			mark->visible(nowOn);
			if (a_onChange) {
				a_onChange(nowOn);
			}
		});
		return node;
	}

	std::shared_ptr<MV::Scene::Text> makeScrubField(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, const std::string& a_id, const MV::Size<>& a_size, float a_value, std::function<void(float)> a_onChange, std::function<void(float, float)> a_onHistory) {
		auto& library = *a_services.get<MV::TextLibrary>();
		auto& mouse = *a_services.get<MV::TapDevice>();
		auto box = attachRoundedRect(a_parent->make(a_id), a_services, a_theme, a_size, a_theme.fieldBg())->owner();
		auto text = box->attach<MV::Scene::Text>(library, a_theme.labelFont);
		text->bounds({ MV::Point<>(), a_size });
		text->justification(MV::TextJustification::LEFT)->wrapping(MV::TextWrapMethod::NONE, a_size.width)->minimumLineHeight(a_size.height)->text(formatNumber(a_value));

		auto clickable = box->attach<MV::Scene::Clickable>(mouse)->bounds(a_size);
		std::weak_ptr<MV::Scene::Text> weakText = text.self();
		FocusRouter* focus = &a_focus;

		struct ScrubState {
			bool started = false;
			bool suppressAccept = false;
			float committed = 0.0f;
		};
		auto state = std::make_shared<ScrubState>();
		state->committed = a_value;

		auto commitFromText = [weakText, state, a_onChange, a_onHistory]() {
			if (auto locked = weakText.lock()) {
				try {
					float value = std::stof(locked->text());
					if (a_onChange) {
						a_onChange(value);
					}
					if (a_onHistory && value != state->committed) {
						a_onHistory(state->committed, value);
					}
					state->committed = value;
				} catch (...) {
				}
			}
		};

		clickable->onDrag.connect("scrub", [weakText, state, a_onChange](const std::shared_ptr<MV::Scene::Clickable>& a_clickable, const MV::Point<int>& a_start, const MV::Point<int>&) {
			auto locked = weakText.lock();
			if (!locked) {
				return;
			}
			int totalDelta = a_clickable->mouse().position().x - a_start.x;
			if (!state->started && std::abs(totalDelta) < 3) {
				return;
			}
			state->started = true;
			state->suppressAccept = true;
			float step = std::max(0.05f, std::abs(state->committed) * 0.01f);
			float value = state->committed + static_cast<float>(totalDelta) * step;
			locked->text(formatNumber(value));
			if (a_onChange) {
				a_onChange(value);
			}
		});
		clickable->onRelease.connect("scrubEnd", [weakText, state, a_onHistory](const std::shared_ptr<MV::Scene::Clickable>&, const MV::Point<MV::PointPrecision>&) {
			if (!state->started) {
				return;
			}
			state->started = false;
			if (auto locked = weakText.lock()) {
				try {
					float value = std::stof(locked->text());
					if (a_onHistory && value != state->committed) {
						a_onHistory(state->committed, value);
					}
					state->committed = value;
				} catch (...) {
				}
			}
		});
		clickable->onAccept.connect("focus", [weakText, focus, state, commitFromText](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (state->suppressAccept) {
				state->suppressAccept = false;
				return;
			}
			if (auto locked = weakText.lock()) {
				focus->activate(locked, commitFromText);
			}
		});
		text->onEnter.connect("commit", [focus](std::shared_ptr<MV::Scene::Text>) {
			focus->deactivate();
		});
		return text.self();
	}

	void makeVectorRow(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, FocusRouter& a_focus, float a_totalWidth, const std::vector<std::pair<std::string, float>>& a_axes, std::function<void(size_t, float)> a_onChange, std::function<void(size_t, float, float)> a_onHistory) {
		auto& library = *a_services.get<MV::TextLibrary>();
		float axisLabelWidth = a_theme.fieldHeight * 0.75f;
		size_t axisCount = std::max<size_t>(1, a_axes.size());
		float fieldWidth = std::max(24.0f, (a_totalWidth - axisCount * (axisLabelWidth + a_theme.pad)) / axisCount);
		float x = 0.0f;
		for (size_t i = 0; i < a_axes.size(); ++i) {
			auto axisHost = a_parent->make(MV::guid("axis_"));
			axisHost->position(MV::point(x, 0.0f));
			axisHost->attach<MV::Scene::Sprite>()->bounds(MV::size(axisLabelWidth, a_theme.fieldHeight))->color(axisTint(a_axes[i].first));
			auto axisLabel = axisHost->attach<MV::Scene::Text>(library, a_theme.labelFont);
			axisLabel->bounds({ MV::Point<>(), MV::size(axisLabelWidth, a_theme.fieldHeight) });
			axisLabel->justification(MV::TextJustification::CENTER)->wrapping(MV::TextWrapMethod::NONE, axisLabelWidth)->minimumLineHeight(a_theme.fieldHeight)->text(a_axes[i].first);
			x += axisLabelWidth;

			auto fieldHost = a_parent->make(MV::guid("axisField_"));
			fieldHost->position(MV::point(x, 0.0f));
			size_t axisIndex = i;
			makeScrubField(fieldHost, a_services, a_theme, a_focus, "value", MV::size(fieldWidth, a_theme.fieldHeight), a_axes[i].second,
				[a_onChange, axisIndex](float a_value) {
					if (a_onChange) {
						a_onChange(axisIndex, a_value);
					}
				},
				[a_onHistory, axisIndex](float a_oldValue, float a_newValue) {
					if (a_onHistory) {
						a_onHistory(axisIndex, a_oldValue, a_newValue);
					}
				});
			x += fieldWidth + a_theme.pad;
		}
	}

	std::shared_ptr<MV::Scene::Button> makeCycleButton(const std::shared_ptr<MV::Scene::Node>& a_parent, MV::Services& a_services, const Theme& a_theme, const std::string& a_id, const MV::Size<>& a_size, std::vector<std::string> a_labels, int a_initialIndex, std::function<void(int)> a_onChange) {
		int startIndex = a_labels.empty() ? 0 : std::max(0, std::min<int>(a_initialIndex, static_cast<int>(a_labels.size()) - 1));
		auto button = makeButton(a_parent, a_services, a_theme, a_id, a_size, a_labels.empty() ? std::string("--") : a_labels[startIndex], nullptr);
		auto index = std::make_shared<int>(startIndex);
		std::weak_ptr<MV::Scene::Button> weakButton = button;
		button->onAccept.connect("cycle", [weakButton, index, a_labels, a_onChange](const std::shared_ptr<MV::Scene::Clickable>&) {
			if (a_labels.empty()) {
				return;
			}
			*index = (*index + 1) % static_cast<int>(a_labels.size());
			if (auto locked = weakButton.lock()) {
				locked->text(a_labels[*index]);
			}
			if (a_onChange) {
				a_onChange(*index);
			}
		});
		return button;
	}

}
