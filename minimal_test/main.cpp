#include <iostream>
#include <queue>
#include <vector>
#include <array>
#include <unordered_map>
#include <algorithm>
#include <limits>

struct Point { int x; int y; };

struct Map {
    int width, height;
    std::vector<std::vector<bool>> blocked;
    Map(int w, int h) : width(w), height(h), blocked(w, std::vector<bool>(h,false)){}
    bool inBounds(Point p) const { return p.x>=0 && p.y>=0 && p.x<width && p.y<height; }
    bool isBlocked(Point p) const { return blocked[p.x][p.y]; }
};

struct Agent {
    Point pos;
    Point goal;
    std::vector<Point> path;
};

std::vector<Point> neighbors(Point p){
    return {{p.x+1,p.y},{p.x-1,p.y},{p.x,p.y+1},{p.x,p.y-1}};
}

std::vector<Point> findPath(const Map& map, Point start, Point goal){
    std::queue<Point> q; q.push(start);
    std::unordered_map<int,int> prev;
    auto key=[&](Point p){return p.x*map.height+p.y;};
    prev[key(start)] = -1;
    while(!q.empty()){
        Point cur=q.front(); q.pop();
        if(cur.x==goal.x && cur.y==goal.y) break;
        for(auto n:neighbors(cur)) if(map.inBounds(n)&&!map.isBlocked(n)&&!prev.count(key(n))){
            prev[key(n)]=key(cur); q.push(n);
        }
    }
    std::vector<Point> path; Point cur=goal; if(!prev.count(key(goal))) return path;
    while(key(cur)!=key(start)){ path.push_back(cur); cur={prev[key(cur)]/map.height, prev[key(cur)]%map.height}; }
    std::reverse(path.begin(),path.end());
    return path;
}

int main(){
    Map map(3,1);
    Agent a1{{0,0},{1,0}};
    Agent a2{{2,0},{1,0}};
    a1.path=findPath(map,a1.pos,a1.goal);
    a2.path=findPath(map,a2.pos,a2.goal);
    for(int step=0; step<10; ++step){
        if(step<(int)a1.path.size()) a1.pos=a1.path[step];
        if(step<(int)a2.path.size()) a2.pos=a2.path[step];
        std::cout<<"Step"<<step<<" A1("<<a1.pos.x<<","<<a1.pos.y<<") A2("<<a2.pos.x<<","<<a2.pos.y<<")\n";
        if(a1.pos.x==a2.pos.x && a1.pos.y==a2.pos.y){
            std::cout<<"Agents overlapped at step "<<step<<"\n";
            return 1;
        }
    }
    return 0;
}
