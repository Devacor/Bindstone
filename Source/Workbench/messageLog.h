#ifndef _WORKBENCH_MESSAGELOG_H_
#define _WORKBENCH_MESSAGELOG_H_

#include <array>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace Workbench {

	enum class MessageLevel { INFO, WARNING, FAILURE };

	struct Message {
		MessageLevel level = MessageLevel::INFO;
		std::string text;
		// The same message again in a row counts here instead of filling the log.
		int repeats = 1;
	};

	// What the editor and the engine have reported, newest last. The Console panel lists it and the status bar shows
	// its latest line and its counts.
	class MessageLog {
	public:
		void add(MessageLevel a_level, std::string a_text) {
			for (auto&& character : a_text) {
				if (character == '\n' || character == '\r' || character == '\t') {
					character = ' ';
				}
			}
			++counts[static_cast<size_t>(a_level)];
			if (!entries.empty() && entries.back().level == a_level && entries.back().text == a_text) {
				++entries.back().repeats;
			} else {
				entries.push_back({ a_level, std::move(a_text), 1 });
				if (entries.size() > maximumEntries) {
					entries.pop_front();
				}
			}
			changed();
		}

		// From any thread: joins the log at the next drain().
		void post(MessageLevel a_level, std::string a_text) {
			std::lock_guard<std::mutex> guard(postedLock);
			posted.push_back({ a_level, std::move(a_text), 1 });
		}

		void drain() {
			std::vector<Message> arrived;
			{
				std::lock_guard<std::mutex> guard(postedLock);
				arrived.swap(posted);
			}
			for (auto&& message : arrived) {
				add(message.level, std::move(message.text));
			}
		}

		void clear() {
			entries.clear();
			counts = {};
			changed();
		}

		const std::deque<Message>& messages() const { return entries; }
		// Reported since the last clear, repeats included.
		size_t count(MessageLevel a_level) const { return counts[static_cast<size_t>(a_level)]; }

		std::function<void()> onChange;

	private:
		void changed() {
			if (onChange) {
				onChange();
			}
		}

		static constexpr size_t maximumEntries = 1000;
		std::deque<Message> entries;
		std::array<size_t, 3> counts {};
		std::mutex postedLock;
		std::vector<Message> posted;
	};

}

#endif
