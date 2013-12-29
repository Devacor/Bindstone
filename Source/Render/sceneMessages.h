#ifndef _MV_SCENEMESSAGES_H_
#define _MV_SCENEMESSAGES_H_

#include <memory>
#include "Utility/messageHandler.hpp"

namespace MV{
	namespace Scene{
		class Node;

		struct PushMatrix : public MV::Message {
			static std::shared_ptr<PushMatrix> make(std::shared_ptr<Node> a_sender){ return std::make_shared<PushMatrix>(a_sender); }
			PushMatrix(std::shared_ptr<Node> a_sender):sender(a_sender){}
			std::shared_ptr<Node> sender;
		};
		struct PopMatrix : public MV::Message {
			static std::shared_ptr<PopMatrix> make(std::shared_ptr<Node> a_sender){ return std::make_shared<PopMatrix>(a_sender); }
			PopMatrix(std::shared_ptr<Node> a_sender):sender(a_sender){}
			std::shared_ptr<Node> sender;
		};

		struct VisualChange : public MV::Message {
			static std::shared_ptr<VisualChange> make(std::shared_ptr<Node> a_sender){ return std::make_shared<VisualChange>(a_sender); }
			VisualChange(std::shared_ptr<Node> a_sender):sender(a_sender){}
			std::shared_ptr<Node> sender;
		};

		struct ChildAdded : public MV::Message {
			static std::shared_ptr<ChildAdded> make(std::shared_ptr<Node> a_sender, std::shared_ptr<Node> a_child){
				return std::make_shared<ChildAdded>(a_sender, a_child); 
			}
			ChildAdded(std::shared_ptr<Node> a_sender, std::shared_ptr<Node> a_child):sender(a_sender), child(a_child){}
			std::shared_ptr<Node> sender;
			std::shared_ptr<Node> child;
		};
		struct ChildRemoved : public MV::Message {
			static std::shared_ptr<ChildRemoved> make(std::shared_ptr<Node> a_sender, std::shared_ptr<Node> a_child){
				return std::make_shared<ChildRemoved>(a_sender, a_child);
			}
			ChildRemoved(std::shared_ptr<Node> a_sender, std::shared_ptr<Node> a_child):sender(a_sender), child(a_child){}
			std::shared_ptr<Node> sender;
			std::shared_ptr<Node> child;
		};
	}
}

#endif
