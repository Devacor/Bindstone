#ifndef _PATHGRID_H_
#define _PATHGRID_H_
#include <vector>

namespace MV {

	class GridNode {
	public:
		GridNode():
			cost(0){
		}
		int cost;
		
	};

	class Grid {
	public:
		Grid(size_t a_x, size_t a_y):
			x(a_x),y(a_y),grid(x, std::vector<GridNode>(y)){
		}


	private:
		size_t x;
		size_t y;
		std::vector<std::vector<GridNode>> grid;
	};

}
#endif