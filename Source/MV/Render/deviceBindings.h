#ifndef _MV_RENDER_DEVICE_BINDINGS_H_
#define _MV_RENDER_DEVICE_BINDINGS_H_

#include <cstddef>

namespace MV {
	namespace Render {
		class Device;

		// Every object holding GPU objects of a Device, so the device's owner frees them all while its context is
		// current, however long the objects themselves live (a script engine's globals, a static cache). A holder
		// tracks itself when it creates them, untracks in its destructor and gives DeviceBindings
		// releaseDeviceBindings(Device&): free them through the device and forget the handles.
		class DeviceBindings {
		public:
			template <class Holder>
			static void track(Holder* a_holder, Device* a_device) {
				trackHolder({ a_holder, &releaseHolder<Holder> }, a_device);
			}
			// The device a_holder's GPU objects live on; null when it made none or they were already released.
			template <class Holder>
			static Device* untrack(Holder* a_holder) {
				return untrackHolder({ a_holder, &releaseHolder<Holder> });
			}
			static void release(Device* a_device);
			static size_t holdersOn(Device* a_device);

			struct TrackedHolder {
				void* object;
				void (*releaseOn)(void*, Device&);
				bool operator==(const TrackedHolder& a_other) const { return object == a_other.object && releaseOn == a_other.releaseOn; }
			};

		private:
			template <class Holder>
			static void releaseHolder(void* a_holder, Device& a_device) {
				static_cast<Holder*>(a_holder)->releaseDeviceBindings(a_device);
			}
			static void trackHolder(const TrackedHolder& a_holder, Device* a_device);
			static Device* untrackHolder(const TrackedHolder& a_holder);
		};
	}
}

#endif
