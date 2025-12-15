class Point {
    float x = 0.0;
    float y = 0.0;

    Point(float px, float py) {
        x = px;
        y = py;
    }

    float distanceTo(Point other) {
        auto dx = x - other.x;
        auto dy = y - other.y;
        return sqrt(dx * dx + dy * dy);
    }
}

auto p1 = Point(3.0, 4.0);
auto p2 = Point(0.0, 0.0);
p1.distanceTo(p2);