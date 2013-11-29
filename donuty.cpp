/*----------------------------------------------------------------------------------*\
| Toroid "donut, the tastiest kind of nut" 3d rotation and mapping from a 2d surface |
|  by Michael Hamilton.  Feel free to use this for personal learning, but please     |
|  don't steal my code and call it your own, that's stupid and it lacks class.  If   |
|  you want to use this, please at least let me know, and give credit where it is due|
|  this code is not available for use in commercial products.  Feel free to send     |
|  Questions or comments to my e-mail at maxmike@gmail.com or icq me at 46753105.    |
|  Good luck on your own 3d endeavors.                                               |
|                         ~www.mutedvision.net~                                      |
|                                                                                    |
|                              controls:                                             |
|                 up/down arrows = yaxis shift                                       |
|                       home/end = zaxis shift                                       |
|              left/right arrows = x axis shift                                      |
|                                                                                    |
|                            a/d = yaw rotation                                      |
|                            w/s = pitch rotation                                    |
|                            q/e = roll rotation                                     |
|                                                                                    |
|                              z = continuous yaw increase                           |
|                              x = continuous pitch increase                         |
|                              c = continuous roll increase                          |
|                                                                                    |
|                          j/k/l = increment Red, Green, Blue values                 |
|                              g = grid frame                                        |
|                              h = horizontal lines only                             |
|                              v = vertical lines only                               |
|                              p = pixels only                                       |
\*----------------------------------------------------------------------------------*/

#include <iostream>
#include <math.h>
#include <time.h>
#include <SDL/SDL.h>
#include <stdlib.h>

#define positions 400      //change this depending on 360/st+1 (because it counts from 1 rather than 0...)
#define numofpoints 100   //Number of points to map to the toroid
#define xvectorrange 6    //x portion of the vector can be 0 to 6
#define yvectorrange 6    //y portion of the vector can be 0 to 6
#define pointoffset 8     //This is a lazy hack...  If you set pointoffset to 0, weird blinky dots go along the x axis... there seems to be
                          //an indirect correlation between a bigger x value and a bigger value here up until 8, at which point, it no longer is
                          //visible affected.  If you figure this out I'll give you a quarter, because It's getting late and I'm too lazy to figure
                          //this minor issue out.
using namespace std;

//Uint8 R, Uint8 G, Uint8 B refers to the color values from 1 to 256 (or so) and can be put in as integers
//ALL FUNCTIONS REGARDING YAW, PITCH, AND ROLL REQUIRE RADIANS TO BE PASSED, NOT DEGREES, the only exemption from this is the variable "st"


//CLeaRs the screen ex: cls(screen);
void clr(SDL_Surface *screen){SDL_FillRect(SDL_GetVideoSurface(),NULL,SDL_MapRGB(SDL_GetVideoSurface()->format, 0,0,0));}
//Line drawing algorithm ex: lin(screen, 5, 10, 10, 20, 100, 0, 100);
void lin(SDL_Surface *screen, double x1, double y1, double x2, double y2, Uint8 R, Uint8 G, Uint8 B);
//Pixel SET algorithm for plotting a pixel ex: pset(screen, 10, 10, 100, 0, 100);
void pset(SDL_Surface *screen, double x, double y, Uint8 R, Uint8 G, Uint8 B);

//Map is a function that requires alot of data, it does take all the stress out of mapping from 2d to 3d space though!
//x and y refer to 2d space co-ordinates you actually want to map, xmax and ymax refer to the max and min values of the 2d surface
//ysiz refers to the ysiz of the donut, and length refers to the distance from the center to the center of the donut's outer ring
//ytrans, xtrans, ztrans refer to the placement of the donut on the axis
//yaw, pitch and roll refer to the yaw, pitch, and roll of the donut
//focus is the focus placed on the donut (and, usually, the rest of the world)
//st is the "approximation" where st/360 determines the number of points on a circle calculated

//ex: map(screen, 50, 50, 640, 480, 50, ysiz*2, 100, 100, 100, yaw, pitch, roll, 70, 20, 100, 0, 100);

void map(SDL_Surface *screen, double x, double y, double xmax, double ymax, double ysiz, double length, double ytrans, double xtrans, double ztrans, double yaw, double pitch, double roll, double focus, double st, Uint8 R, Uint8 G, Uint8 B);

//This is map overloaded to use the coefficient method of rotation.  It becomes worth it after more than (but not equal to) 5 points are rotated with
//the same yaw, pitch, and roll.  72 multiplications with the first map function, 70 multiplications with the second map function.
//We assume you already have coefficients figured out, as you must pass them into the function
void map(SDL_Surface *screen, double x, double y, double xmax, double ymax, double ysiz, double length, double ytrans, double xtrans, double ztrans, double yaw, double pitch, double roll, double focus, double st, Uint8 R, Uint8 G, Uint8 B, double coefficient[]);
//slock will lock the screen if it needs to for drawing, slock must be called before attempting to draw with sdl
void slock(SDL_Surface *screen);
//sulock should be called to unlock the screen so that you can display it, don't forget to SDL_Flip(screen); the thing afterwards
void sulock(SDL_Surface *screen);

//rotpoint is a painless way of rotating a point on the screen, just plug and play.  xtmp, ytmp, and ztmp are passed by refrence
//and the rotated points are stored in those values.  This generates a flat (12 * pointsrotated) number of multiplications
//ex: rotpoint(yaw, pitch, roll, 10, 10, 10, xtmp, ytmp, ztmp);
void rotpoint(double yaw, double pitch, double roll, double x, double y, double z, double &xtmp, double &ytmp, double &ztmp);

//These rotation functions gather a coefficient (ie, plug in the yaw, pitch, and roll to get the coefficients) then, after that,
//call the rotpoint with those coefficients already calculated any number of times you like for (9 * pointsrotated + 16) multiplications
//ex: coefficients(yaw, pitch, roll, coefficient);
//    for(i = 1;i<100;i++){rotpoint(x[i], y[i], z[i], xtmp, ytmp, ztmp, coefficient);}
void coefficients(double yaw, double pitch, double roll, double coefficient[]);
void rotpoint(double x, double y, double z, double &xtmp, double &ytmp, double &ztmp, double coefficient[]);


//////////main
int main(int argc, char *argv[])
{
/*----------*\
|Initializing|
\*----------*/
    srand( time(NULL) ); //seed randomizer
  SDL_Surface *screen;  //setting up a screen pointer to draw to for SDL
  if( SDL_Init(SDL_INIT_VIDEO) <0 )
  {
    cout << "Unable to init SDL" << SDL_GetError(); //couts will save in a text file, not display on screen... this is an SDL thing
    return 1;
  }

  screen=SDL_SetVideoMode(640,480,32,SDL_HWSURFACE|SDL_DOUBLEBUF|SDL_FULLSCREEN);

  if ( screen == NULL )
  {
    cout << "Unable to set 640x480 video:" << SDL_GetError();
    exit(1);
  }

//atexit(SDL_Quit);
////////////////////////
/*Setting up variables*/   // Most are double to simplify math and avoid mis-matched conversions
double xtmp, ytmp, ztmp; //these values are the saved values of the rotpoint function
int tempar;          //temp variable only used once

double mapx[numofpoints+pointoffset][2], mapy[numofpoints+pointoffset][2];

for(tempar = pointoffset; tempar < numofpoints+pointoffset; tempar++){  //setting all the points we want to map for display purposes, this isn't required in the finished program
 mapx[tempar][0] = tempar;
 mapy[tempar][0] = tempar;
 mapx[tempar][1] = int(xvectorrange * rand()/(RAND_MAX+1.0));
 mapy[tempar][1] = int(yvectorrange * rand()/(RAND_MAX+1.0));
}
int i, j;



double ysiz = 60; //ysiz = the size of the thing
double focus = 100, length = ysiz * 2; //focus is the focus on an object during 2d-3d translation,
                                      //length is the distance from the centre of the circle to the axis

//////////////////////////////////////////////////////// SHAPE MODIFIER
int circle = 0; // 1 = circle, 0 = donut
////////////////////////////////////////////////////////

double x[positions][positions], y[positions][positions], z[positions][positions];           //point storage
double xrot[positions][positions], yrot[positions][positions], zrot[positions][positions];  //rotated point storage

double racopy;

double xmax=640, ymax=480; //the max sizes of the screen, this is used in the map function
int yadd = 1, padd = 1, radd = 1;
//Rembember, these are radians, not degrees.  Degree to radians: (deg / 180)*pi
double yaw = 0;   //gamma
double pitch = 0; //beta
double roll = 0;  //alpha

double st = 20; //step value in degrees for the circle (ex: st = 20, every 20 degrees, there is a point)
bool change = 0;
int ptoff = 0; //Turn point mapping on and off
int yconst = 0, pconst = 0, rconst = 0;
int red=255, green=255, blue=255;
bool rdown = 1, gdown = 1, bdown = 1;

double ytrans = 240; //translational values to offset the object by, change these to move it around
double xtrans = 320;
double ztrans = 300;

int done = 0, leftst=0, rightst=0, upst=0, downst=0, deepst = 0, shallowst = 0; //user control variables to test whether a key is on or off
int yawpos = 1, yawneg = 0, pitchpos = 1, pitchneg = 0, rollpos = 1, rollneg =0;
int rot;

double x2tmp = 0, y2tmp = 0; //x and y temporary variables
int grid = 0;       //grid = 0: pixel display grid = 1: grid display grid = 2: vertical line display grid = 3: horizontal line display
double ra, ra1, ra2, ra3; //radians to rotate by
int h =1, b=1, a =1, k = 0; //All temporary variables
double c=0;
double coefficient[9];
////////////////////
/*Setting up donut*/
if(circle == 1){length = 0;}
for(i = 1; i <= 360; i+=int(st)){       //first point series.  This is made up of a plain circle offset from the y axis
  j = j + 1;
  ra = (double(i) / double(180)) * double(3.14159265);
  y[1][j] = double(cos(ra)) * ysiz;          //remember trig? (of course ;) all of everything in this program is based on a unit circle.
  x[1][j] = double(sin(ra)) * ysiz + length; //x is incremented by length to offset the points from the axis
  z[1][j] = 0; //we just want a flat circle, 0 is a good value because it stays centered on the axis for rotation later.
}

a=1;

for(rot = int(st);rot <= 360;rot+= int(st)){                       //Rotating and saving base circle
  ra = (double(rot) / double(180)) * double(3.14159265); //rotate ra radians
    coefficients(ra, 0, 0, coefficient);
   h = h + 1; //move to the next circle value
  for(b = 1; b <= (360/st); b++){                        //saving points on that base circle
  // rotpoint(ra, 0, 0, x[1][b], y[1][b], z[1][b], xtmp, ytmp, ztmp);
  rotpoint(x[1][b], y[1][b], z[1][b], xtmp, ytmp, ztmp, coefficient);
   y[h][b] = ytmp;  //actual co-ordinate
   x[h][b] = xtmp;
   z[h][b] = ztmp;
  }
}


/////////////////////////////////////////////
/*Drawing donut... The tastiest kind of nut*/
rot = 0;
SDL_Event event;
int jack;
while (done != 1){

    while ( SDL_PollEvent(&event) ){
      // If SDL quits somehow then quit the loop
       if ( event.type == SDL_QUIT )  {  done = 1;  }


            switch(event.type){
               case SDL_KEYDOWN:       //if keydown, turn the damn thing on ;)
                switch( event.key.keysym.sym ){
                    case SDLK_ESCAPE:
                        done = 1;
                        break;
                    case SDLK_LEFT:
                        leftst = -1;
                        break;
                    case SDLK_RIGHT:
                        rightst =  1;
                        break;
                    case SDLK_UP:
                        upst = -1;
                        break;
                    case SDLK_DOWN:
                        downst =  1;
                        break;
                    case SDLK_HOME:
                        deepst = 1;
                        break;
                    case SDLK_END:
                        shallowst = -1;
                        break;
                    case SDLK_d:
                        yconst = 0;
                        yawpos = 1;
                        break;
                    case SDLK_a:
                        yconst = 0;
                        yawneg = -1;
                        break;
                    case SDLK_w:
                        pconst = 0;
                        pitchpos = 1;
                        break;
                    case SDLK_s:
                        pconst = 0;
                        pitchneg = -1;
                        break;
                    case SDLK_q:
                        rconst = 0;
                        rollpos = 1;
                        break;
                    case SDLK_e:
                        rconst = 0;
                        rollneg = -1;
                        break;
                    case SDLK_g:
                        grid = 1;
                        break;
                    case SDLK_p:
                        grid = 0;
                        break;
                    case SDLK_h:
                        grid = 2;
                        break;
                    case SDLK_v:
                        grid = 3;
                        break;
                    case SDLK_j:
                        rdown = 1;
                        break;
                    case SDLK_k:
                        gdown = 1;
                        break;
                    case SDLK_l:
                        bdown = 1;
                        break;
                    case SDLK_z:
                        yconst = 1;
                        break;
                    case SDLK_x:
                        pconst = 1;
                        break;
                    case SDLK_c:
                        rconst = 1;
                        break;
                    case SDLK_t:
                        ptoff = 1;
                        break;
                    default:
                        break;
                }
            break;
            case SDL_KEYUP:             //if keyup, turn the damn thing off ;)
                switch( event.key.keysym.sym ){
                    case SDLK_LEFT:
                        leftst = 0;
                        break;
                    case SDLK_RIGHT:
                        rightst = 0;
                        break;
                    case SDLK_UP:
                        upst = 0;
                        break;
                    case SDLK_DOWN:
                        downst = 0;
                        break;
                    case SDLK_HOME:
                        deepst = 0;
                        break;
                    case SDLK_END:
                        shallowst = 0;
                        break;
                    case SDLK_d:
                        yawpos = 0;
                        break;
                    case SDLK_a:
                        yawneg = 0;
                        break;
                    case SDLK_w:
                        pitchpos = 0;
                        break;
                    case SDLK_s:
                        pitchneg = 0;
                        break;
                    case SDLK_q:
                        rollpos = 0;
                        break;
                    case SDLK_e:
                        rollneg = 0;
                        break;
                    case SDLK_y:
                        ptoff = 0;
                        break;
                    case SDLK_j:
                        rdown = 0;
                        break;
                    case SDLK_k:
                        gdown = 0;
                        break;
                    case SDLK_l:
                        bdown = 0;
                        break;
                    default:
                        break;
                }
             break;
            }
       }

/////////
 xtrans += rightst+leftst;              //Depending on what controls are active, we do different things:
 ytrans += upst+downst;
 ztrans += deepst+shallowst;
// if(ztrans < ysiz*2.5){ztrans = ysiz*2.5;}
 if(yconst == 1){yaw++;}
 if(yaw > 179){yaw = -179;}
 if(yaw < -179){yaw = 179;}
 if(pconst == 1){pitch++;}
 if(pitch > 179){pitch = -179;}
 if(pitch < -179){pitch = 179;}
 if(rconst == 1){roll++;}
 if(roll > 179){roll = -179;}
 if(roll < -179){roll = 179;}
 yaw += yawpos+yawneg;
 pitch += pitchpos+pitchneg;
 roll += rollpos+rollneg;

 if(rdown == 1){
  red++;
  if(red > 255){red = 0;}
 }
 if(gdown == 1){
  green++;
  if(green > 255){green = 0;}
 }
 if(bdown == 1){
  red++;
  if(red > 255){blue = 0;}
 }


 clr(screen);
 slock(screen);       //Locking for drawing purposes
 for(j = 1; j <= (360/st); j++){  //taking all the circles around the center point
  for(i = 1; i <= (360/st); i++){ //and all the points around the circles (easier to get now, huh?)
   xtmp = int((double(xrot[j][i]) / double(zrot[j][i] + ztrans)) * focus + xtrans); //This is the 2d to 3d translation formula
   ytmp = int((double(yrot[j][i]) / double(zrot[j][i] + ztrans)) * focus + ytrans);


//GRID MODE DISPLAYS

    if((grid == 1 || grid == 2) && j > 1){
     x2tmp = int((double(xrot[j-1][i]) / double(zrot[j-1][i] + ztrans)) * focus + xtrans);
     y2tmp = int((double(yrot[j-1][i]) / double(zrot[j-1][i] + ztrans)) * focus + ytrans);
      lin(screen, xtmp, ytmp, x2tmp, y2tmp, red, green, blue);
    }
    if((grid == 1 || grid == 3) && i > 1){
     x2tmp = int((double(xrot[j][i-1]) / double(zrot[j][i-1] + ztrans)) * focus + xtrans);
     y2tmp = int((double(yrot[j][i-1]) / double(zrot[j][i-1] + ztrans)) * focus + ytrans);
     lin(screen, xtmp, ytmp, x2tmp, y2tmp, red, green, blue);
    }
    if((grid == 1 || grid == 2) && j == 1){
     x2tmp = int((double(xrot[int(360/st)][i]) / double(zrot[int(360/st)][i] + ztrans)) * focus + xtrans);
     y2tmp = int((double(yrot[int(360/st)][i]) / double(zrot[int(360/st)][i] + ztrans)) * focus + ytrans);
    lin(screen, xtmp, ytmp, x2tmp, y2tmp, red, green, blue);
    }
    if((grid == 1 || grid == 3) && i == 1){
     x2tmp = int((double(xrot[j][int(360/st)]) / double(zrot[j][int(360/st)] + ztrans)) * focus + xtrans);
     y2tmp = int((double(yrot[j][int(360/st)]) / double(zrot[j][int(360/st)] + ztrans)) * focus + ytrans);
      lin(screen, xtmp, ytmp, x2tmp, y2tmp, red, green, blue);
    }
    if(grid == 0){
     pset(screen, xtmp, ytmp, red, green, blue);
    }

//GRID MODES DONE
  }
 }
  ra1 = (double(yaw) / double(180)) * double(3.14159265); //yaw rotation

  ra2 = (double(pitch) / double(180)) * double(3.14159265); //pitch rotation

  ra3 = (double(roll) / double(180)) * double(3.14159265); //you probably know what this is.

if(circle == 1){
 racopy = ra1;
 ra1 = ra2;
 ra2 = racopy;
}

 coefficients(ra1, ra2, ra3, coefficient);            //Figuring out the coefficients

if(ptoff == 0){
 for(tempar = pointoffset; tempar < numofpoints+pointoffset; tempar++){
 //This is simply to move the 2d points around their 2d plane
  mapx[tempar][0] += mapx[tempar][1];
  mapy[tempar][0] += mapy[tempar][1];
  if(mapx[tempar][0] > 639){mapx[tempar][0] = 0;}
  if(mapy[tempar][0] > 479){mapy[tempar][0] = 0;}
  if(mapx[tempar][0] < 1){mapx[tempar][0] = 0;}
  if(mapy[tempar][0] < 1){mapy[tempar][0] = 0;}
  //Done that
 pset(screen, mapx[tempar][0], mapy[tempar][0], 0, 200, 200); //displaying 2d points
  //below, displaying 3d points around the tastiest of nuts
     // coefficients(ra1, ra2, ra3, coefficient);
  map(screen, mapx[tempar][0], mapy[tempar][0], xmax, ymax, ysiz, length, ytrans, xtrans, ztrans, yaw, pitch, roll, focus, st, 200, 200, 0, coefficient);
 // map(screen, mapx[tempar][0], mapy[tempar][0], xmax, ymax, ysiz, length, ytrans, xtrans, ztrans, ra1, ra2, ra3, focus, st, 200, 200, 0);   //inefficient way
 }
}
  sulock(screen); //re-lock the screen
 SDL_Flip(screen); //flip the whole thing into display


//Here's where the rotation happens (like magic, only more sexy)

  for(a = 1;a <= (360/st); a++){ //for all the circles around the point
   for(b = 1;b <= (360/st); b++){ //and all the points around the circles
    //rotpoint(ra1, ra2, ra3, x[int(a)][int(b)], y[int(a)][int(b)], z[int(a)][int(b)], xtmp, ytmp, ztmp); //ROTATE INEFFICIENT

   rotpoint(x[int(a)][int(b)], y[int(a)][int(b)], z[int(a)][int(b)], xtmp, ytmp, ztmp, coefficient); //Woo, coefficients

    yrot[int(a)][int(b)] = ytmp; //Save the effects of the rotation
    xrot[int(a)][int(b)] = xtmp;
    zrot[int(a)][int(b)] = ztmp;
   }
  }
}
//done rotation


  return 0;
}
//////////end of main


//it's a mouthful, but it works
void rotpoint(double x, double y, double z, double &xtmp, double &ytmp, double &ztmp, double coefficient[]){
 //Optomized rotation if more than 5 points are rotated with yaw, cos, and tan being constant
 xtmp =(int)(x * coefficient[0] + y * coefficient[1] + z * coefficient[2]);
 ytmp =(int)(x * coefficient[3] + y * coefficient[4] + z * coefficient[5]);
 ztmp =(int)(x * coefficient[6] + y * coefficient[7] + z * coefficient[8]);
}


//unoptimized at more than 5 points being rotated with the same yaw, cos, and tan
void rotpoint(double yaw, double pitch, double roll, double x, double y, double z, double &xtmp, double &ytmp, double &ztmp){
 xtmp = (z * sin(yaw)) + (x * cos(yaw));          //yaw
 ztmp = (z * cos(yaw)) - (x * sin(yaw));

 ytmp = (y * cos(pitch)) - (ztmp * sin(pitch));  //pitch
 ztmp = (y * sin(pitch)) + (ztmp * cos(pitch));

 double xrot1 = xtmp;

 xtmp = (ytmp * sin(roll)) + (xtmp * cos(roll)); //roll
 ytmp = (ytmp * cos(roll)) - (xrot1 * sin(roll));
}

//Grabbing the coefficients in 16 multiplications and 6 sin and cos evaluations!
void coefficients(double yaw, double pitch, double roll, double *coefficient){
//Remember that:
//yaw = gamma
//pitch = beta
//roll = alpha
 double cosalpha, sinalpha, cosbeta, sinbeta, cosgamma, singamma;
 cosalpha = cos(roll); cosbeta = cos(pitch);
 sinalpha = sin(roll); sinbeta = sin(pitch);
 cosgamma = cos(yaw); singamma = sin(yaw);

 *coefficient = singamma*sinbeta*sinalpha + cosgamma*cosalpha;   //When we calculate these, it's so that we don't need to repeat the same multiplications
 coefficient++;                                         //of sin and cos for each point because the sin and cos of one particular angle is always
 *coefficient = cosbeta*sinalpha;                       //the same, only the point changes.
 coefficient++;
 *coefficient = singamma*cosalpha - cosgamma*sinbeta*sinalpha;
 coefficient++;
 *coefficient = singamma*sinbeta*cosalpha - cosgamma*sinalpha;
 coefficient++;
 *coefficient = cosbeta*cosalpha;
 coefficient++;
 *coefficient = -cosgamma*sinbeta*cosalpha - singamma*sinalpha;
 coefficient++;
 *coefficient = -singamma*cosbeta;;
 coefficient++;
 *coefficient = sinbeta;
 coefficient++;
 *coefficient = cosgamma*cosbeta;
}

//whoa, more of a mouthful, but again, it works so quit complaining
void map(SDL_Surface *screen, double x, double y, double xmax, double ymax, double ysiz, double length, double ytrans, double xtrans, double ztrans, double yaw, double pitch, double roll, double focus, double st, Uint8 R, Uint8 G, Uint8 B){
 double tmpx, tmpy, tmpz, conx, cony, ra, z = 0, ra1, ra2, ra3;
 conx = 360/xmax;
 cony = 360/ymax;

 tmpy = y*cony;

 ra = (tmpy / double(180)) * 3.14159265;

 tmpy = (cos(ra) * ysiz);

 tmpx = (sin(ra) * ysiz + length);
 ra = (conx*x)/((((360+3.14159265)/20))*3.14159265);


 rotpoint(ra, 0, 0, tmpx, tmpy, 0, x, y, z);
 tmpx = x; tmpy = y; tmpz = z;
  ra1 = (double(yaw) / double(180)) * double(3.14159265);
  ra2 = (double(pitch) / double(180)) * double(3.14159265);
  ra3 = (double(roll) / double(180)) * double(3.14159265);
 rotpoint(ra1, ra2, ra3, tmpx, tmpy, tmpz, x, y, z);

 tmpx = int((double(x) / double(z + ztrans)) * focus + xtrans);
 tmpy = int((double(y) / double(z + ztrans)) * focus + ytrans);

 pset(screen, tmpx, tmpy, R, G, B);
}

void map(SDL_Surface *screen, double x, double y, double xmax, double ymax, double ysiz, double length, double ytrans, double xtrans, double ztrans, double yaw, double pitch, double roll, double focus, double st, Uint8 R, Uint8 G, Uint8 B, double coefficient[]){
 double tmpx, tmpy, tmpz, conx, cony, ra, z = 0, ra1, ra2, ra3;
 conx = 360/xmax;
 cony = 360/ymax;

 tmpy = y*cony;

 ra = (tmpy / double(180)) * 3.14159265;

 tmpy = (cos(ra) * ysiz);

 tmpx = (sin(ra) * ysiz + length);
 ra = (conx*x)/((((360+3.14159265)/20))*3.14159265);


 rotpoint(ra, 0, 0, tmpx, tmpy, 0, x, y, z);
 tmpx = x; tmpy = y; tmpz = z;

 rotpoint(tmpx, tmpy, tmpz, x, y, z, coefficient);

 tmpx = int((double(x) / double(z + ztrans)) * focus + xtrans);
 tmpy = int((double(y) / double(z + ztrans)) * focus + ytrans);

 pset(screen, tmpx, tmpy, R, G, B);
}


///////////////////SDL SPECIFIC DRAWING FUNCTIONS
void slock(SDL_Surface *screen)      //SDL specific code, not mine: locks the screen for writing to
{
  if ( SDL_MUSTLOCK(screen) )
  {
    if ( SDL_LockSurface(screen) < 0 )  //Not my code
    {
      return;
    }
  }
}

void sulock(SDL_Surface *screen)  //SDL specific code, not mine: unlocks the screen for writing to
{
  if ( SDL_MUSTLOCK(screen) )
  {
    SDL_UnlockSurface(screen);
  }
}

/*DRAWING A SIMPLE LINE USING the H or L pixel case method: refer to the line section in my notes*/
void lin(SDL_Surface *screen, double x1, double y1, double x2, double y2, Uint8 R, Uint8 G, Uint8 B)
{
if((x1 > 0 || x1 < 640) && (x2 > 0 || x2 < 640) && (y1 > 0 || y1 < 480) && (y2 > 0 || y2 < 480)){
/* ^ that statement checks to see if the line is on the screen before continuing, if it isn't then
  that would be a lot of wasted time to draw all those points, and it can cause errors on top of that
  This is our second last line of defense, the next falls directly on the pixel algorithm            */
  double dx, dy, longd, shortd;
  double d, dh, dl;
  double xh,yh, xl, yl;
/* DETERMINING RANGES */
 dx = x2 - x1; dy = y2 - y1;                   //Ranges for the pixels
if (dx < 0){
  dx = dx * -1;                                //makes sure dx and dy are greater than 0
  xh = -1;                                     //then adjusts the increment values
  xl = -1;                                     //so that the line flows in the right
}else{                                         //direction and no errors occur
  xh = 1;
  xl = 1;
}

if (dy < 0){
  dy = dy * -1;                                //same comment as above applying to the
  yh = -1;                                     //y case
  yl = -1;
} else {
  yh = 1;
  yl = 1;
}

if (dx > dy){
  longd = dx;                                 //adjusting the values so that the loop
  shortd = dy;                                //goes based on x or y depending on wheater
  yl = 0;                                     //dx is bigger than or less than dy
} else {                                      //this helps finish the checking so that
  longd = dy;                                 //the loop can through assuming this:
  shortd = dx;                                // x1 < x2 and y1 < y2
  xl = 0;
}
/*DONE DETERMINING RANGES*/
/*Initializing constants for the loop*/
d = 2 * shortd - longd;                       // initial value of d
dl = 2 * shortd;                              // d adjustment for H case
dh = 2 * shortd - 2 * longd;                  // d adjustment for L case
/*Done initializing everything*/

/*Rasterization loop*/
for (int I = 0; I <= longd;I++){
  slock(screen);

   pset(screen, x1,y1,R,G,B);         //Plots the pixel after locking the screen

  sulock(screen);

 if (d >= 0) {
  y1 = y1 + yh;            //The higher of the two pixels (aka H case
  x1 = x1 + xh;            //This code is executed if the previous p)
  d = d + dh;
 } else {
  x1 = x1 + xl;            //This code is executed if the previous pixel was
  y1 = y1 + yl;            // the lower of the two pixels (aka L case)
  d = d + dl;
 }
}
}
//SDL_Flip(screen);  //It should be noted that we may not want to flip the screen yet
}

void pset(SDL_Surface *screen, double x1, double y1, Uint8 R, Uint8 G, Uint8 B) //SDL code for drawing a pixel
{
 int x=int(x1), y=int(y1);
 if(x>0  &&  y>0  &&  x<640  &&  y<480){  /*Rasterization Clipping.*/
  Uint32 color = SDL_MapRGB(screen->format, R, G, B);
  switch (screen->format->BytesPerPixel)
  {
    case 1: // Assuming 8-bpp
      {
        Uint8 *bufp;
        bufp = (Uint8 *)screen->pixels + y*screen->pitch + x;
        *bufp = color;
      }
      break;
    case 2: // Probably 15-bpp or 16-bpp
      {
        Uint16 *bufp;
        bufp = (Uint16 *)screen->pixels + y*screen->pitch/2 + x;
        *bufp = color;
      }
      break;
    case 3: // Slow 24-bpp mode, usually not used
      {
        Uint8 *bufp;
        bufp = (Uint8 *)screen->pixels + y*screen->pitch + x * 3;
        if(SDL_BYTEORDER == SDL_LIL_ENDIAN)
        {
          bufp[0] = color;
          bufp[1] = color >> 8;
          bufp[2] = color >> 16;
        } else {
          bufp[2] = color;
          bufp[1] = color >> 8;
          bufp[0] = color >> 16;
        }
      }
      break;
    case 4: // Probably 32-bpp
      {
        Uint32 *bufp;
        bufp = (Uint32 *)screen->pixels + y*screen->pitch/4 + x;
        *bufp = color;
      }
      break;
  }
 }
}
