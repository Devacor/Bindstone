#ifndef _MV_MESSAGEHANDLER_H_
#define _MV_MESSAGEHANDLER_H_
#include <memory>
namespace MV {
	struct MessageHandlerBase{
		virtual ~MessageHandlerBase(){}
	};

	//MessageHandler has begin and end to allow for nested message handling.  You may only need begin in your case.
	template<typename MessageType>
	struct MessageHandler : public virtual MessageHandlerBase {
		virtual void handleBegin(std::shared_ptr<MessageType>) = 0;
		virtual void handleEnd(std::shared_ptr<MessageType>) = 0;
	};

	struct Message{
		template<typename MessageType>
		void tryToHandleBegin(MessageHandlerBase* a_handler, std::shared_ptr<MessageType> a_self){
			auto* castHandler = dynamic_cast<MessageHandler<MessageType>*>(a_handler);
			if(castHandler != nullptr){
				castHandler->handleBegin(a_self);
			}
		}

		template<typename MessageType>
		void tryToHandleEnd(MessageHandlerBase* a_handler, std::shared_ptr<MessageType> a_self){
			auto* castHandler = dynamic_cast<MessageHandler<MessageType>*>(a_handler);
			if(castHandler != nullptr){
				castHandler->handleEnd(a_self);
			}
		}
	};
}
#endif
