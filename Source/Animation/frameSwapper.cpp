#include "frameSwapper.h"

namespace M2Rend{
   void FrameSwapperDefinition::addFrame(const Frame &a_Frame){
      frameList.push_back(a_Frame);
   }

   FrameSwapper::FrameSwapper()
      :defaultDuration(100), previousFrame(0), framesListReference(0){
   }

   void FrameSwapper::setDefaultFrameDuration( int newDurationMilliseconds ){
      defaultDuration = newDurationMilliseconds;
   }

   void FrameSwapper::start(){
      timer.stop();
      timer.start();
      previousFrame = timer.check();
      frame = framesListReference->frameList.begin();
      nextFrame = getNextFrameTime();
   }

   void FrameSwapper::pause(){
      timer.pause();
   }

   void FrameSwapper::resume(){
      timer.resume();
   }

   bool FrameSwapper::getCurrentFrame( Frame &a_returnFrame ){
      bool hasWrapped = false;
      if(framesListReference->frameList.empty()){
         a_returnFrame = Frame("NULL", "NULL");
         return false;
      }
      if(nextFrame < timer.check()){
         frame++;
         if(frame == framesListReference->frameList.end()){
            frame = framesListReference->frameList.begin();
            hasWrapped = true;
         }
         nextFrame = getNextFrameTime();
         timer.stop(); timer.start();
      }
      a_returnFrame = *frame;
      return hasWrapped;
   }

   int FrameSwapper::getNextFrameTime(){
      return (frame->duration == -1)?defaultDuration:frame->duration;
   }

   void FrameSwapper::setFrameList( FrameSwapperDefinition *frameList ){
      framesListReference = frameList;
      frame = framesListReference->frameList.begin();
   }

   FrameSwapperRegister::FrameSwapperRegister(){
      FrameSwapperDefinition tmpSwap;
      tmpSwap.addFrame(Frame("NULL", "NULL"));
      definitions["NULL"] = tmpSwap;
   }

   void FrameSwapperRegister::newDefinition( const FrameSwapperDefinition &definition, const std::string &name ){
      definitions[name] = definition;
   }

   FrameSwapperDefinition* FrameSwapperRegister::getDefinition( const std::string &name ){
      std::map<std::string, FrameSwapperDefinition>::iterator cell = definitions.find(name);
      if(cell != definitions.end()){
         return &(cell->second);
      }
      return &(definitions["NULL"]);
   }

   void FrameSwapperRegister::save( const std::string &file ){
      using boost::property_tree::ptree;
      ptree outputStructure, definitionList;
      std::for_each(definitions.begin(), definitions.end(), [&](std::pair<const std::string, FrameSwapperDefinition> &current){
         definitionList.add_child(current.first, current.second.save());
      });

      outputStructure.add_child("FrameSwapperDefinitions", definitionList);
      write_json(file, outputStructure);
   }

   void FrameSwapperRegister::load( const std::string &file ){
      try{
         using boost::property_tree::ptree;
         ptree inputStructure;
         read_json(file, inputStructure);
         ptree definitionList = inputStructure.get_child("FrameSwapperDefinitions");
         std::for_each(definitionList.begin(), definitionList.end(), [&](boost::property_tree::ptree::value_type &current){
            definitions[current.first].load(current.second);
         });
      }catch(...){
         std::cerr << "Failed to load animation definition (" << file << ") is it correctly formed?" << std::endl;
      }
   }

}
