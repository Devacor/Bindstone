#ifndef _MV_SCENE_MESSAGES_H_
#define _MV_SCENE_MESSAGES_H_

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

		struct SetRenderer : public MV::Message {
			static std::shared_ptr<SetRenderer> make(std::shared_ptr<Node> a_sender, MV::Draw2D *renderer){ return std::make_shared<SetRenderer>(a_sender, renderer); }
			SetRenderer(std::shared_ptr<Node> a_sender, MV::Draw2D *renderer):sender(a_sender), renderer(renderer){}
			std::shared_ptr<Node> sender;
			MV::Draw2D *renderer;
		};

		struct VisualChange : public MV::Message {
			static std::shared_ptr<VisualChange> make(std::shared_ptr<Node> a_sender){ return std::make_shared<VisualChange>(a_sender); }
			VisualChange(std::shared_ptr<Node> a_sender):sender(a_sender){}
			std::shared_ptr<Node> sender;
		};

		struct SetShader : public MV::Message {
			static std::shared_ptr<SetShader> make(const std::string &a_id){ return std::make_shared<SetShader>(a_id); }
			SetShader(const std::string &a_id){shaderId = a_id;}
			std::string shaderId;
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

		template <typename T>
		class ScopedDepthChangeNote{
		public:
			ScopedDepthChangeNote(T* a_target, bool visualChangeRegardless = true):
				target(a_target),
				startDepth(a_target->getDepth()){
			}

			~ScopedDepthChangeNote(){
				if(startDepth != target->getDepth()){
					target->depthChanged();
				} else if(visualChangeRegardless){
					target->alertParent(VisualChange::make(target->shared_from_this()));
				}
			}
		private:
			T* target;
			double startDepth;
			bool visualChangeRegardless;
		};

		template <typename T>
		std::unique_ptr<ScopedDepthChangeNote<T>> makeScopedDepthChangeNote(T* a_target, bool a_visualChangeRegardless = true){
			return std::move(std::make_unique<ScopedDepthChangeNote<T>>(a_target, a_visualChangeRegardless));
		}
	}
}

#endif
