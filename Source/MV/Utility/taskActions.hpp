#ifndef _MV_TASK_ACTIONS_H_
#define _MV_TASK_ACTIONS_H_

#include "task.h"

namespace MV {
	class BlockForSeconds : public ActionBase {
		friend ::cereal::access;
	public:
		virtual std::string name() const override { return "BlockForSeconds(" + std::to_string(seconds) + ")"; }
		
		BlockForSeconds(double a_seconds = 0.0) : seconds(a_seconds) {}

		virtual bool update(Task& a_self, double) override { return a_self.localElapsed() < seconds; }

	protected:
		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/) {
			archive(CEREAL_NVP(seconds), cereal::make_nvp("ActionBase", cereal::base_class<ActionBase>(this)));
		}

	private:

		double seconds;
	};

	class BlockForFrames : ActionBase {
		virtual std::string name() const override { return "BlockForFrames(" + std::to_string(targetFrames) + ")"; }

		BlockForFrames(int a_targetFrames = 1) : targetFrames(a_targetFrames) { }

		bool update(Task&, double) override { return totalFrames++ < targetFrames; }
	protected:
		template <class Archive>
		void serialize(Archive & archive, std::uint32_t const /*version*/) {
			archive(CEREAL_NVP(targetFrames), CEREAL_NVP(totalFrames), cereal::make_nvp("ActionBase", cereal::base_class<ActionBase>(this)));
		}

	private:
		int targetFrames = 0;
		int totalFrames = 0;
	};
}

CEREAL_REGISTER_TYPE(MV::BlockForSeconds);
CEREAL_REGISTER_TYPE(MV::BlockForFrames);

#endif
