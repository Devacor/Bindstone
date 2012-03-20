#ifndef _FRAMESWAPPER_H_
#define _FRAMESWAPPER_H_

#include <list>
#include "Utility/package.h"

#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace MV{
   struct Frame{
      Frame(){duration = -1;}
      Frame(std::string a_mainTexture, std::string a_subTexture, int a_duration = -1)
         :duration(a_duration), mainTexture(a_mainTexture), subTexture(a_subTexture){}
      Frame(boost::property_tree::ptree &frameInput){
         duration = frameInput.get<int>("Duration", -1);
         mainTexture = frameInput.get<std::string>("MainTexture", "");
         subTexture = frameInput.get<std::string>("SubTexture", "");
      }

      boost::property_tree::ptree save() const{
         boost::property_tree::ptree outputStructure;
         outputStructure.put("Duration", duration);
         outputStructure.put("MainTexture", mainTexture);
         outputStructure.put("SubTexture", subTexture);
         return outputStructure;
      }

      int duration;
      std::string mainTexture;
      std::string subTexture;
   };

   class FrameSwapperDefinition{
   public:
      void addFrame(const Frame &a_Frame);
      void clear(){frameList.clear();}
      boost::property_tree::ptree save() const{
         boost::property_tree::ptree outputStructure;
         int i = 0;
         std::for_each(frameList.begin(), frameList.end(), [&](const Frame &current){
            outputStructure.add_child(boost::lexical_cast<std::string>(i++), current.save());
         });
         return outputStructure;
      }
      void load(boost::property_tree::ptree frameListInput){
         std::for_each(frameListInput.begin(), frameListInput.end(), [&](boost::property_tree::ptree::value_type &current){
            frameList.push_back(Frame(current.second));
         });
      }
      std::list<Frame> frameList;
   };

   class FrameSwapper {
   public:
      FrameSwapper();

      void setFrameList(FrameSwapperDefinition *frameList);

      void setDefaultFrameDuration(int newDurationMilliseconds);

      void start();
      void pause();
      void resume();

      //returns true if the frame has wrapped around to the first one again, and a_returnFrame
      bool getCurrentFrame(Frame &a_returnFrame);
   private:
      int getNextFrameTime();

      FrameSwapperDefinition *framesListReference;

      int defaultDuration;
      Stopwatch timer;
      Int64 previousFrame, nextFrame;

      std::list<Frame>::iterator frame;
   };

   class FrameSwapperRegister{
   public:
      FrameSwapperRegister();

      void newDefinition(const FrameSwapperDefinition &definition, const std::string &name);
      FrameSwapperDefinition* getDefinition(const std::string &name);

      void save(const std::string &file);
      void load(const std::string &file);

      void clear(){
         definitions.clear();
      }

   private:
      std::map<std::string, FrameSwapperDefinition> definitions;
   };
}
#endif
