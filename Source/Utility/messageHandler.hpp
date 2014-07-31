#ifndef _MV_MESSAGEHANDLER_H_
#define _MV_MESSAGEHANDLER_H_
#include <memory>
#include "Utility/signal.hpp"
namespace MV {
	struct MessageHandlerBase{
		virtual ~MessageHandlerBase(){}
	};

	struct Message;
	//MessageHandler has begin and end to allow for nested message handling.  You may only need begin in your case.
	template<typename MessageType>
	struct MessageHandler : public virtual MessageHandlerBase {
	private:
		Slot<void(std::shared_ptr<MessageType>)> customHandleBeginSlot;
		Slot<void(std::shared_ptr<MessageType>)> customHandleEndSlot;

	public:
		MessageHandler():
			customHandleBegin(customHandleBeginSlot),
			customHandleEnd(customHandleEndSlot){
		}

		SlotRegister<void(std::shared_ptr<MessageType>)> customHandleBegin;
		SlotRegister<void(std::shared_ptr<MessageType>)> customHandleEnd;

		virtual bool handleBegin(std::shared_ptr<MessageType>) = 0;
		virtual void handleEnd(std::shared_ptr<MessageType>) = 0;

	private:
		friend Message;
		bool handleBeginInterface(std::shared_ptr<MessageType> a_self){
			customHandleBeginSlot(a_self);
			return handleBegin(a_self);
		}

		void handleEndInterface(std::shared_ptr<MessageType> a_self){
			customHandleEndSlot(a_self);
			handleEnd(a_self);
		}
	};

	struct Message{
		template<typename MessageType>
		bool tryToHandleBegin(MessageHandlerBase* a_handler, std::shared_ptr<MessageType> a_self){
			auto* castHandler = dynamic_cast<MessageHandler<MessageType>*>(a_handler);
			if(castHandler != nullptr){
				return castHandler->handleBeginInterface(a_self);
			}
			return true;
		}

		template<typename MessageType>
		void tryToHandleEnd(MessageHandlerBase* a_handler, std::shared_ptr<MessageType> a_self){
			auto* castHandler = dynamic_cast<MessageHandler<MessageType>*>(a_handler);
			if(castHandler != nullptr){
				castHandler->handleEndInterface(a_self);
			}
		}
	};
}
#endif
