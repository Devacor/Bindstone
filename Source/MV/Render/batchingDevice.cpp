#include "MV/Render/batchingDevice.h"

namespace MV {
	namespace Render {

		BatchingDevice::BatchingDevice(Device& a_backend) :
			wrapped(a_backend) {
		}

		BatchingDevice::~BatchingDevice() {
		}

		void BatchingDevice::draw(const DrawItem& a_item) {
			wrapped.draw(a_item);
			++currentFrame.draws;
		}

		void BatchingDevice::endFrame() {
			wrapped.endFrame();
			previousFrame = currentFrame;
			currentFrame = DrawStatistics{};
		}

	}
}
