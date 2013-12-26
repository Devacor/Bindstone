#ifndef _ACTIONSEQUENCE_H_
#define _ACTIONSEQUENCE_H_

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace MV {

class ActionSequence;

class ActionSequence : public std::enable_shared_from_this<ActionSequence> {
public:
	typedef std::function<void (std::shared_ptr<ActionSequence>)> ActionSequenceCallback;
	
	ActionSequence(const std::string &a_ourId = "", bool a_neverDie = false);
	virtual ~ActionSequence();
	
	//return true on complete
	bool passTime(double a_dt);
	bool done() const; //Action is done running
	bool shouldDie() const; //Action could be removed
	
	void addParallelAction( std::shared_ptr<ActionSequence> a_action );
	
	void addSequentialAction(std::shared_ptr<ActionSequence> a_action);
	void addSequentialActionAfter(std::shared_ptr<ActionSequence> a_action, std::string a_matchId);
	void addSequentialActionBefore(std::shared_ptr<ActionSequence> a_action, std::string a_matchId);
	void addSequentialActionAfter(std::shared_ptr<ActionSequence> a_action, std::shared_ptr<ActionSequence> a_matchId);
	void addSequentialActionBefore(std::shared_ptr<ActionSequence> a_action, std::shared_ptr<ActionSequence> a_matchId);
	
	void addNonBlockingSequentialAction(std::shared_ptr<ActionSequence> a_action);
	void addNonBlockingSequentialActionAfter(std::shared_ptr<ActionSequence> a_action, std::string a_matchId);
	void addNonBlockingSequentialActionBefore(std::shared_ptr<ActionSequence> a_action, std::string a_matchId);
	void addNonBlockingSequentialActionAfter(std::shared_ptr<ActionSequence> a_action, std::shared_ptr<ActionSequence> a_matchId);
	void addNonBlockingSequentialActionBefore(std::shared_ptr<ActionSequence> a_action, std::shared_ptr<ActionSequence> a_matchId);
	
	void replaceParallelAction(std::string name, std::shared_ptr<ActionSequence> a_action);
	void replaceSequentialAction(std::string name, std::shared_ptr<ActionSequence> a_action);
	
	std::shared_ptr<ActionSequence> getActionSequence(const std::string a_idToFind) const;
	bool hasChildren() { return parallelOnCompleteActions.size() > 0 || sequentialOnCompleteActions.size() > 0; }
	
	std::string id() const;
	void addCompletionCallback(ActionSequenceCallback a_callback);
	bool empty();
	void forceComplete();
	
	bool forceCompleted() const{
		return wasForceCompleted;
	}
	
	bool blocking() const;
	void unblock();
	void block();
	
	void setParent(std::shared_ptr<ActionSequence> a_newParent){
		ourParent = a_newParent;
	}
	std::shared_ptr<ActionSequence> parent() const{
		return ourParent;
	}
protected:

	typedef std::vector< std::shared_ptr<ActionSequence> > ActionSequenceList;
	typedef std::deque< std::shared_ptr<ActionSequence> > ActionSequenceQueue;
	ActionSequenceList parallelOnCompleteActions;
	ActionSequenceQueue sequentialOnCompleteActions;
    
private:
	void forceCompleteInternal();
	void doCallbacks();
	bool runAndCompleteChildActions(double dt);
	void runAndCompleteSequentialChildActions(double dt);
	void runAndCompleteParallelChildActions(double dt);
	
	virtual void onBlock() {}
	virtual void onUnblock() {}
	virtual void onForceComplete() {}
	virtual void onAddParallelAction( std::shared_ptr<ActionSequence> action ){}
	virtual void onAddSequentialAction( std::shared_ptr<ActionSequence> action ){}
	virtual void onCompleteParallelAction( std::shared_ptr<ActionSequence> action ){}
	virtual void onCompleteSequentialAction( std::shared_ptr<ActionSequence> action ){}

	virtual void onBeginThisAction() {}
	virtual void onCompleteThisAction() {}
	virtual bool passTimeAction(double dt){
		return true;
	}
	
	typedef std::vector< ActionSequenceCallback > ActionSequenceCallbackList;
	ActionSequenceCallbackList callbacks;
	
	std::shared_ptr<ActionSequence> ourParent;
	bool started;
	bool isDone;
	bool neverDie;
	bool wasForceCompleted;
	bool isBlocking;
	std::string actionId;
};

bool operator==(const ActionSequence &action, const std::string &matchId);
bool operator==(const std::string &matchId, const ActionSequence &action);

bool operator==(const std::string &matchId, std::shared_ptr<ActionSequence> action);
bool operator==(std::shared_ptr<ActionSequence> action, const std::string &matchId);

}

#endif
