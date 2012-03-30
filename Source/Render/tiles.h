#ifndef __TILES_H__
#define __TILES_H__

#include "drawShapes.h"

namespace MV {

   class Tiles {
   public:
      Tiles(std::shared_ptr<Scene> a_mainScene, const SubTexture& a_initTexture, size_t a_x, size_t a_y, double a_tileWidth, double a_tileHeight):
         tileCountX(a_x),
         tileCountY(a_y),
         tileWidth(a_tileWidth),
         tileHeight(a_tileHeight),
         decorationZOffset(static_cast<double>(a_y+2)*a_tileHeight),
         mainScene(a_mainScene){
         initializeScene();
         createBlankTiles(a_initTexture);
      }

      std::shared_ptr<Scene> decorations(){
         return mainScene->get<Scene>("decorations");
      }
      std::shared_ptr<Scene> scene(){
         return mainScene;
      }
      std::shared_ptr<DrawRectangle> tile(unsigned int a_x, unsigned int a_y){
         mainScene->get<Scene>("tiles")->get<DrawRectangle>(getChildId(a_x, a_y));
      }

      //returns bottom left point for a tile
      Point pointForTile(unsigned int a_x, unsigned int a_y){
         return Point(static_cast<double>(a_x)*tileWidth, static_cast<double>(a_y+1)*tileHeight, a_y+decorationZOffset);
      }
      Point pointForTile(unsigned int a_x, unsigned int a_y, double a_zOffsetOverride){
         return Point(static_cast<double>(a_x)*tileWidth, static_cast<double>(a_y+1)*tileHeight, a_y+a_zOffsetOverride);
      }
   private:
      void initializeScene(){
         mainScene->make<Scene>("decorations");
         mainScene->make<Scene>("tiles");
      }

      void createBlankTiles(const SubTexture &a_initTexture) {
         mainScene->clear();
         auto tiles = mainScene->make<Scene>("tiles");
         for(size_t y = 0;y < tileCountY;++y){
            for(size_t x = 0;x < tileCountX;++x){
               auto rect = mainScene->make<DrawRectangle>(getChildId(x, y));
               rect->setSizeAndLocation(pointForTile(x, y, 0), tileWidth, tileHeight);
               AssignTextureToRectangle(*rect, &a_initTexture);
            }
         }
      }

      std::string getChildId(unsigned int a_x, unsigned int a_y){
         return boost::lexical_cast<std::string>(a_x)+"_"+boost::lexical_cast<std::string>(a_y);
      }

      typedef std::vector<std::string> TileRow;

      std::shared_ptr<Scene> mainScene;
      double decorationZOffset;
      double tileWidth, tileHeight;
      size_t tileCountX, tileCountY;
   };
}

#endif
