#include "MV/Render/deviceBindings.h"

#include <functional>
#include <mutex>
#include <unordered_map>

namespace MV {
	namespace Render {
		namespace {
			struct TrackedHolderHash {
				size_t operator()(const DeviceBindings::TrackedHolder& a_holder) const { return std::hash<void*>()(a_holder.object); }
			};

			struct TrackedHolders {
				std::mutex lock;
				std::unordered_map<DeviceBindings::TrackedHolder, Device*, TrackedHolderHash> devices;
			};

			// Never destroyed: a holder can untrack from a static destructor in any translation unit.
			TrackedHolders& trackedHolders() {
				static TrackedHolders* result = new TrackedHolders();
				return *result;
			}
		}

		void DeviceBindings::trackHolder(const TrackedHolder& a_holder, Device* a_device) {
			auto& tracked = trackedHolders();
			std::scoped_lock guard(tracked.lock);
			tracked.devices[a_holder] = a_device;
		}

		Device* DeviceBindings::untrackHolder(const TrackedHolder& a_holder) {
			auto& tracked = trackedHolders();
			std::scoped_lock guard(tracked.lock);
			Device* result = nullptr;
			if (auto found = tracked.devices.find(a_holder); found != tracked.devices.end()) {
				result = found->second;
				tracked.devices.erase(found);
			}
			return result;
		}

		void DeviceBindings::release(Device* a_device) {
			if (!a_device) {
				return;
			}
			auto& tracked = trackedHolders();
			std::scoped_lock guard(tracked.lock);
			for (auto entry = tracked.devices.begin(); entry != tracked.devices.end();) {
				if (entry->second == a_device) {
					entry->first.releaseOn(entry->first.object, *a_device);
					entry = tracked.devices.erase(entry);
				} else {
					++entry;
				}
			}
		}

		size_t DeviceBindings::holdersOn(Device* a_device) {
			auto& tracked = trackedHolders();
			std::scoped_lock guard(tracked.lock);
			size_t result = 0;
			for (auto&& entry : tracked.devices) {
				if (entry.second == a_device) {
					++result;
				}
			}
			return result;
		}
	}
}
