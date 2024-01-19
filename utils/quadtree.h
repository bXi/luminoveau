#pragma once

#include <cmath>

#include "configuration/configuration.h"

#include "colors.h"
#include "rectangles.h"

#include "render2d/render2dhandler.h"

class QuadTree
{
public:

    struct qtPoint
    {
        float x;
        float y;

        void* entity = nullptr;

    };

    struct AABB
    {
        float _top;
        float _left;
        float _width;
        float _height;

        float getLeft()
        {
            return _left;
        }
        float getRight()
        {
            return _left + _width;
        }
        float getTop()
        {
            return _top;
        }
        float getBottom()
        {
            return _top + _height;
        }

        AABB(float left, float top, float width, float height)
        {
            _top = top;
            _left = left;
            _width = width;
            _height = height;
        }
        bool containsPoint(const qtPoint& point)
        {
            return (
                getLeft() <= point.x &&
                point.x <= getRight() &&
                getTop() <= point.y &&
                point.y <= getBottom()
                );
        }
        bool intersectsAABB(const AABB& other)
        {
            return (
                _left < other._left + other._width &&
                _left + _width > other._left &&
                _top < other._top + other._height &&
                _top + _height > other._top
                );
        }

        rectf getRectangle()
        {
            rectf rect = {
                getLeft(),
                getTop(),
                _width,
                _height,
            };

            return rect;

        }
    };

    struct AABBCircle {
        float _x;
        float _y;
        float _r;

        AABBCircle(float x, float y, float r) {
            _x = x;
            _y = y;
            _r = r;
        }

        bool containsPoint(const qtPoint& point) {
            const double d = pow(point.x - _x, 2) + pow(point.y - _y, 2);
            return d <= static_cast<double>(_r) * static_cast<double>(_r);
        }

        bool intersectsAABB(const AABB& range) {
            const float recCenterX = (range._left + range._width / 2.0f);
            const float recCenterY = (range._top + range._height / 2.0f);

            const float dx = fabsf(_x - recCenterX);
            const float dy = fabsf(_y - recCenterY);

            if (dx > (range._width / 2.0f + _r)) { return false; }
            if (dy > (range._height / 2.0f + _r)) { return false; }

            if (dx <= (range._width / 2.0f)) { return true; }
            if (dy <= (range._height / 2.0f)) { return true; }

            const float cornerDistanceSq = (dx - range._width / 2.0f) * (dx - range._width / 2.0f) +
                (dy - range._height / 2.0f) * (dy - range._height / 2.0f);
            return (cornerDistanceSq <= (_r * _r));

        }
    };

private:

    std::vector<qtPoint> points;


    AABB _boundary = {0.0f, 0.0f, 0.0f, 0.0f};


    QuadTree *northWest = NULL;
    QuadTree *northEast = NULL;
    QuadTree *southWest = NULL;
    QuadTree *southEast = NULL;
    unsigned int QT_NODE_CAPACITY = 3;
public:



    QuadTree(rectf boundary)
    {

        const AABB aabbboundary = AABB(boundary.x, boundary.y, boundary.width, boundary.height);
        _boundary = aabbboundary;
    };

    void subdivide()
    {

        const rectf nwRect = {_boundary._left                           , _boundary._top,                            _boundary._width / 2.0f, _boundary._height / 2.0f};
        const rectf neRect = {_boundary._left + _boundary._width / 2.0f , _boundary._top,                            _boundary._width / 2.0f, _boundary._height / 2.0f};
        const rectf swRect = {_boundary._left                           , _boundary._top + _boundary._height / 2.0f, _boundary._width / 2.0f, _boundary._height / 2.0f};
        const rectf seRect = {_boundary._left + _boundary._width / 2.0f , _boundary._top + _boundary._height / 2.0f, _boundary._width / 2.0f, _boundary._height / 2.0f};

        northWest = new QuadTree(nwRect);
        northEast = new QuadTree(neRect);
        southWest = new QuadTree(swRect);
        southEast = new QuadTree(seRect);
    }

    bool insert(const qtPoint &point)
    {
        if (!_boundary.containsPoint(point))
            return false; // object cannot be added

        if (points.size() < QT_NODE_CAPACITY && northWest == nullptr) {
            points.push_back(point);
            return true;
        }

        if (northWest == nullptr)
            subdivide();
       
        if (northWest->insert(point)) return true;
        if (northEast->insert(point)) return true;
        if (southEast->insert(point)) return true;
        if (southWest->insert(point)) return true;

        return false; // this should never happen.
    }

    void draw(Color col)
    {

        rectf screenBoundary = _boundary.getRectangle();

        screenBoundary.width *= Configuration::tileWidth;
        screenBoundary.height *= Configuration::tileHeight;

        screenBoundary.x *= Configuration::tileWidth;
        screenBoundary.y *= Configuration::tileHeight;

        Render2D::DrawRectangle({screenBoundary.x, screenBoundary.y}, {screenBoundary.width, screenBoundary.height}, col);

        if (northWest != NULL) {
            northWest->draw(RED);
            northEast->draw(GREEN);
            southWest->draw(YELLOW);
            southEast->draw(BLUE);
        }
    }

    void draw(int x, int y, Color col)
    {
        
        rectf screenBoundary = _boundary.getRectangle();

        screenBoundary.x += x;
        screenBoundary.y += y;

        Render2D::DrawRectangle({screenBoundary.x, screenBoundary.y}, {screenBoundary.width, screenBoundary.height}, col);

        if (northWest != NULL) {
            northWest->draw(x, y, RED);
            northEast->draw(x, y, GREEN);
            southWest->draw(x, y, YELLOW);
            southEast->draw(x, y, BLUE);
        }
    }



    void draw()
    {
        draw({255,255,255,255});
    }

    void query(AABB range, std::vector<void *> *found)
    {

        if (!range.intersectsAABB(_boundary)) {
            return;
        }

        for (auto &p : points) {
            if (range.containsPoint(p)) {
                found->push_back(p.entity);
            }
        }
        if (northWest != NULL) {
            northWest->query(range, found);
            northEast->query(range, found);
            southWest->query(range, found);
            southEast->query(range, found);
        }

        return;
    }

    void query(AABBCircle range, std::vector<void*>* found)
    {

        if (!range.intersectsAABB(_boundary)) {
            return;
        }

        for (auto& p : points) {
            if (range.containsPoint(p)) {
                found->push_back(p.entity);
            }
        }
        if (northWest != NULL) {
            northWest->query(range, found);
            northEast->query(range, found);
            southWest->query(range, found);
            southEast->query(range, found);
        }

        return;
    }


    void reset()
    {
        if (northWest != NULL) {
            northWest->reset();
            northEast->reset();
            southWest->reset();
            southEast->reset();

            delete northWest;
            delete northEast;
            delete southWest;
            delete southEast;
        }

        points.clear();

    }

};