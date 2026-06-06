/**********************************************************\
| Michael Hamilton (maxmike@gmail.com) www.mutedvision.net |
|----------------------------------------------------------|
\**********************************************************/

// Generation-checked slot table mapping an opaque Handle<Tag> to a backend Payload. The
// generation detects use-after-free of a recycled slot; index 0 is reserved so Handle{0,0}
// is null. Backend-agnostic (no GL/Vulkan/Metal types).

#ifndef _MV_RENDER_HANDLEPOOL_H_
#define _MV_RENDER_HANDLEPOOL_H_

#include "MV/Render/device.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace MV {
	namespace Render {

		template <class HandleT, class Payload>
		class HandlePool {
		public:
			HandleT create(Payload a_payload) {
				uint32_t slot;
				if (!freeSlots.empty()) {
					slot = freeSlots.back();
					freeSlots.pop_back();
				} else {
					slot = static_cast<uint32_t>(slots.size());
					slots.emplace_back();
				}
				Slot &s = slots[slot];
				s.generation += 1;
				if (s.generation == 0) { s.generation = 1; } // 32-bit wrap must never yield the null generation
				s.payload = std::move(a_payload);
				s.live = true;
				return HandleT{ slot + 1u, s.generation };
			}

			bool valid(HandleT a_handle) const {
				if (a_handle.index == 0 || a_handle.generation == 0) { return false; }
				const uint32_t slot = a_handle.index - 1u;
				return slot < slots.size() && slots[slot].live && slots[slot].generation == a_handle.generation;
			}

			Payload* get(HandleT a_handle) {
				return valid(a_handle) ? &slots[a_handle.index - 1u].payload : nullptr;
			}
			const Payload* get(HandleT a_handle) const {
				return valid(a_handle) ? &slots[a_handle.index - 1u].payload : nullptr;
			}

			// Moves the payload out so the caller can release the native object; false (no-op) on a
			// stale/null handle, so double-destroy is safe.
			bool remove(HandleT a_handle, Payload &a_releasedPayload) {
				if (!valid(a_handle)) { return false; }
				const uint32_t slot = a_handle.index - 1u;
				a_releasedPayload = std::move(slots[slot].payload);
				slots[slot].payload = Payload{};
				slots[slot].live = false;
				slots[slot].generation += 1;
				freeSlots.push_back(slot);
				return true;
			}

			template <class Fn>
			void forEachLive(Fn &&a_fn) {
				for (auto &s : slots) {
					if (s.live) { a_fn(s.payload); }
				}
			}

			void clear() {
				slots.clear();
				freeSlots.clear();
			}

		private:
			struct Slot {
				Payload payload{};
				uint32_t generation = 0;
				bool live = false;
			};
			std::vector<Slot> slots;
			std::vector<uint32_t> freeSlots;
		};

	} // namespace Render
} // namespace MV

#endif
