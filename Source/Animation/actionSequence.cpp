#include "ActionSequence.h"

#include <iostream>

namespace MV{

ActionSequence::ActionSequence(const std::string &a_ourId, bool a_neverDie) :
	actionId(a_ourId),
	isDone(false),
	neverDie(a_neverDie),
	wasForceCompleted(false),
	started(false),
	isBlocking(true){
	
}
ActionSequence::~ActionSequence(){
}

bool ActionSequence::done() const {
	if(!isDone){
		return false;
	}
	for(ActionSequenceQueue::const_iterator i = sequentialOnCompleteActions.begin();i != sequentialOnCompleteActions.end();++i){
		if(!(*i)->done()){
			return false;
		}
	}
	for(ActionSequenceList::const_iterator i = parallelOnCompleteActions.begin();i != parallelOnCompleteActions.end();++i){
		if(!(*i)->done()){
			return false;
		}
	}
	return true;
}

bool ActionSequence::shouldDie() const{
	if(neverDie || !isDone){
		return false;
	}
	for(ActionSequenceQueue::const_iterator i = sequentialOnCompleteActions.begin();i != sequentialOnCompleteActions.end();++i){
		if(!(*i)->shouldDie()){
			return false;
		}
	}
	for(ActionSequenceList::const_iterator i = parallelOnCompleteActions.begin();i != parallelOnCompleteActions.end();++i){
		if(!(*i)->shouldDie()){
			return false;
		}
	}
	return true;
}

std::string ActionSequence::id() const{
	return actionId;
}
void ActionSequence::addCompletionCallback(const ActionSequenceCallback &a_callback){
	callbacks.push_back(a_callback);
}
bool ActionSequence::empty(){
	return parallelOnCompleteActions.empty() && sequentialOnCompleteActions.empty();
}

void ActionSequence::forceComplete(){
	onForceComplete();
	forceCompleteInternal();
	passTime(0);
	wasForceCompleted = true;
}

void ActionSequence::forceCompleteInternal(){
	isDone = true;
	wasForceCompleted = true;
	for(ActionSequenceList::iterator it = parallelOnCompleteActions.begin(); it != parallelOnCompleteActions.end(); ++it){
		(*it)->forceCompleteInternal();
	}
	for(ActionSequenceQueue::iterator it = sequentialOnCompleteActions.begin(); it != sequentialOnCompleteActions.end(); ++it){
		(*it)->forceCompleteInternal();
	}
}

bool ActionSequence::blocking() const{
	return isBlocking;
}
void ActionSequence::unblock(){
	isBlocking = false;
	onUnblock();
}
void ActionSequence::block(){
	isBlocking = true;
	onBlock();
}

std::shared_ptr<ActionSequence> ActionSequence::getActionSequence(const std::string &a_matchId) const{
	ActionSequenceQueue::const_iterator found1 = std::find(sequentialOnCompleteActions.begin(), sequentialOnCompleteActions.end(), a_matchId);
	if(found1 != sequentialOnCompleteActions.end()){
		return *found1;
	}
	ActionSequenceList::const_iterator found2 = std::find(parallelOnCompleteActions.begin(), parallelOnCompleteActions.end(), a_matchId);
	if(found2 != parallelOnCompleteActions.end()){
		return *found2;
	}
	
	std::cerr << "Missing Action: " << a_matchId << std::endl;
	throw(std::out_of_range("ActionSequence::getActionSequence supplied an id not in its list of actions!"));
}

void ActionSequence::replaceParallelAction(const std::string &a_matchId, std::shared_ptr<ActionSequence> a_action){
	ActionSequenceList::iterator found = std::find(parallelOnCompleteActions.begin(), parallelOnCompleteActions.end(), a_matchId);
	if(found != parallelOnCompleteActions.end()){
		parallelOnCompleteActions.erase(found);
	}
	parallelOnCompleteActions.push_back(a_action);
}

void ActionSequence::replaceSequentialAction(const std::string &a_matchId, std::shared_ptr<ActionSequence> a_action){
	ActionSequenceQueue::iterator found = std::find(sequentialOnCompleteActions.begin(), sequentialOnCompleteActions.end(), a_matchId);
	if(found != sequentialOnCompleteActions.end()){
		sequentialOnCompleteActions.insert(found, a_action);
		sequentialOnCompleteActions.erase(found);
	}else{
		sequentialOnCompleteActions.push_back(a_action);
	}
}

void ActionSequence::addParallelAction(std::shared_ptr<ActionSequence> a_action){
	a_action->setParent(shared_from_this());
	parallelOnCompleteActions.push_back(a_action);
	onAddParallelAction(a_action);
}

void ActionSequence::addNonBlockingSequentialAction(std::shared_ptr<ActionSequence> a_action){
	a_action->setParent(shared_from_this());
	a_action->unblock();
	sequentialOnCompleteActions.push_back(a_action);
	onAddSequentialAction(a_action);
}
void ActionSequence::addNonBlockingSequentialActionAfter(std::shared_ptr<ActionSequence> a_action, const std::string &a_matchId){
	a_action->setParent(shared_from_this());
	ActionSequenceQueue::iterator cell = std::find(sequentialOnCompleteActions.begin(), sequentialOnCompleteActions.end(), a_matchId);
	if(cell!=sequentialOnCompleteActions.end()){
		++cell;
	}
	a_action->unblock();
	sequentialOnCompleteActions.insert(cell, a_action);
	onAddSequentialAction(a_action);
}
void ActionSequence::addNonBlockingSequentialActionBefore(std::shared_ptr<ActionSequence> a_action, const std::string &a_matchId){
	a_action->setParent(shared_from_this());
	ActionSequenceQueue::iterator cell = std::find(sequentialOnCompleteActions.begin(), sequentialOnCompleteActions.end(), a_matchId);
	a_action->unblock();
	sequentialOnCompleteActions.insert(cell, a_action);
	onAddSequentialAction(a_action);
}
void ActionSequence::addNonBlockingSequentialActionAfter(std::shared_ptr<ActionSequence> a_action, std::shared_ptr<ActionSequence> a_matchId){
	a_action->setParent(shared_from_this());
	ActionSequenceQueue::iterator cell = std::find(sequentialOnCompleteActions.begin(), sequentialOnCompleteActions.end(), a_matchId);
	if(cell!=sequentialOnCompleteActions.end()){
		++cell;
	}
	a_action->unblock();
	sequentialOnCompleteActions.insert(cell, a_action);
	onAddSequentialAction(a_action);
}
void ActionSequence::addNonBlockingSequentialActionBefore(std::shared_ptr<ActionSequence> a_action, std::shared_ptr<ActionSequence> a_matchId){
	a_action->setParent(shared_from_this());
	ActionSequenceQueue::iterator cell = std::find(sequentialOnCompleteActions.begin(), sequentialOnCompleteActions.end(), a_matchId);
	a_action->unblock();
	sequentialOnCompleteActions.insert(cell, a_action);
	onAddSequentialAction(a_action);
}

void ActionSequence::addSequentialAction(std::shared_ptr<ActionSequence> a_action){
	a_action->setParent(shared_from_this());
	sequentialOnCompleteActions.push_back(a_action);
	onAddSequentialAction(a_action);
}
void ActionSequence::addSequentialActionAfter(std::shared_ptr<ActionSequence> a_action, const std::string &a_matchId){
	a_action->setParent(shared_from_this());
	ActionSequenceQueue::iterator cell = std::find(sequentialOnCompleteActions.begin(), sequentialOnCompleteActions.end(), a_matchId);
	if(cell!=sequentialOnCompleteActions.end()){
		++cell;
	}
	sequentialOnCompleteActions.insert(cell, a_action);
	onAddSequentialAction(a_action);
}
void ActionSequence::addSequentialActionBefore(std::shared_ptr<ActionSequence> a_action, const std::string &a_matchId){
	a_action->setParent(shared_from_this());
	ActionSequenceQueue::iterator cell = std::find(sequentialOnCompleteActions.begin(), sequentialOnCompleteActions.end(), a_matchId);
	sequentialOnCompleteActions.insert(cell, a_action);
	onAddSequentialAction(a_action);
}
void ActionSequence::addSequentialActionAfter(std::shared_ptr<ActionSequence> a_action, std::shared_ptr<ActionSequence> a_matchId){
	a_action->setParent(shared_from_this());
	ActionSequenceQueue::iterator cell = std::find(sequentialOnCompleteActions.begin(), sequentialOnCompleteActions.end(), a_matchId);
	if(cell!=sequentialOnCompleteActions.end()){
		++cell;
	}
	sequentialOnCompleteActions.insert(cell, a_action);
	onAddSequentialAction(a_action);
}
void ActionSequence::addSequentialActionBefore(std::shared_ptr<ActionSequence> a_action, std::shared_ptr<ActionSequence> a_matchId){
	a_action->setParent(shared_from_this());
	ActionSequenceQueue::iterator cell = std::find(sequentialOnCompleteActions.begin(), sequentialOnCompleteActions.end(), a_matchId);
	sequentialOnCompleteActions.insert(cell, a_action);
	onAddSequentialAction(a_action);
}

void ActionSequence::doCallbacks(){
	ActionSequenceCallbackList buffer(callbacks); //allow callbacks to add callbacks.
	callbacks.clear();
	
	for(ActionSequenceCallbackList::iterator i = buffer.begin();i != buffer.end();++i){
		(*i)(shared_from_this());
	}
}

void ActionSequence::runAndCompleteSequentialChildActions(double a_dt){
	while(!sequentialOnCompleteActions.empty() && !sequentialOnCompleteActions.front()->blocking()){
		addParallelAction(sequentialOnCompleteActions.front());
		sequentialOnCompleteActions.pop_front();
	}
	if(!sequentialOnCompleteActions.empty()){
		bool sequentialActionWasDone = sequentialOnCompleteActions.front()->done();
		std::shared_ptr<ActionSequence> toRemove = sequentialOnCompleteActions.front();
		if (sequentialOnCompleteActions.front()->passTime(a_dt)){
			//need to seek in case passTime messes with our sequentialOnCompleteActions order, used to just pop_front.
			auto found = std::find(sequentialOnCompleteActions.begin(), sequentialOnCompleteActions.end(), toRemove);
			if(found != sequentialOnCompleteActions.end()){
				sequentialOnCompleteActions.erase(found);
			}
			if(!toRemove->shouldDie()){ //push to the back so we don't block!
				sequentialOnCompleteActions.push_back(toRemove);
			}
			if(!sequentialActionWasDone){ //probably an unkillable sequential action.
				onCompleteSequentialAction(toRemove);
			}
		}
	}
}

void ActionSequence::runAndCompleteParallelChildActions(double a_dt){
	for(ActionSequenceList::iterator i = parallelOnCompleteActions.begin();i != parallelOnCompleteActions.end();){
		bool parallelActionWasDone = (*i)->done() && !(*i)->forceCompleted();
		if((*i)->passTime(a_dt)){
			std::shared_ptr<ActionSequence> toRemove = *i;
			if(toRemove->shouldDie()){
				i = parallelOnCompleteActions.erase(i);
			}else{
				++i;
			}
			if(!parallelActionWasDone){
				onCompleteParallelAction(toRemove);
			}
		}else{
			++i;
		}
	}
}

bool ActionSequence::runAndCompleteChildActions(double a_dt){
	runAndCompleteSequentialChildActions(a_dt);
	runAndCompleteParallelChildActions(a_dt);
	return done();
}

bool ActionSequence::passTime(double a_dt){
	wasForceCompleted = false;
	if(!isDone){
		if(!started){
			onBeginThisAction();
			started = true;
		}
		isDone = passTimeAction(a_dt);
	}
	if(isDone){
		if(!started){
			onBeginThisAction(); //we force completed before starting
		}
		if(runAndCompleteChildActions(a_dt)){
			doCallbacks();
			onCompleteThisAction();
			return true;
		}
	}
	return false;
}

bool operator==(const ActionSequence &a_action, const std::string &a_matchId){
	return a_action.id() == a_matchId;
}
bool operator==(const std::string &a_matchId, const ActionSequence &a_action){
	return a_action.id() == a_matchId;
}
bool operator==(const std::string &a_matchId, std::shared_ptr<ActionSequence> a_action){
	return a_action && (a_action->id() == a_matchId);
}
bool operator==(std::shared_ptr<ActionSequence> a_action, const std::string &a_matchId){
	return a_action && (a_action->id() == a_matchId);
}

}
