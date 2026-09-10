/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 * Released under GNU GPL v2 or later; see COPYING.
 */

#ifndef PLAYERBOTS_BODY_TRAVEL_H
#define PLAYERBOTS_BODY_TRAVEL_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>
#include <optional>

// Progress along a committed path, independent of distance to the final destination. A road can lead away
// from that destination. All points are owned values, and elapsed combat/offline time is not navigation work.
class BodyTravel
{
public:
    struct Point
    {
        float x = 0, y = 0, z = 0;
        bool operator==(Point const&) const = default;
        bool Valid() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
        float Distance(Point const& other) const
        {
            return std::hypot(std::hypot(x - other.x, y - other.y), z - other.z);
        }
    };

    bool Commit(std::vector<Point> points, uint32_t now)
    {
        if (points.size() < 2 || points.size() > 512
            || std::any_of(points.begin(), points.end(), [](auto const& point) { return !point.Valid(); }))
            return false;
        if (points.front().Distance(points.back()) < 3)
            return false;
        _points = std::move(points);
        _cursor = 1;
        _nearest = _points.front().Distance(_points[1]);
        _lastSample = now;
        _stalledMs = 0;
        _paused = false;
        return true;
    }

    void Observe(Point position, uint32_t now)
    {
        uint32_t const elapsed = now - _lastSample;
        _lastSample = now;
        if (_points.empty() || !position.Valid())
            return;
        if (!_paused && elapsed <= 5000)
            _stalledMs += elapsed;
        _paused = false;
        // A fast mover can pass several short spline segments between samples. Search a bounded local window,
        // with earlier points winning ties, so we do not jump across a distant crossing of the route.
        auto nearestIndex = _cursor;
        float nearestDistance = position.Distance(_points[_cursor]);
        for (auto index = _cursor + 1; index < std::min(_points.size(), _cursor + 16); ++index)
            if (float const distance = position.Distance(_points[index]); distance < nearestDistance)
            {
                nearestIndex = index;
                nearestDistance = distance;
            }
        if (nearestIndex > _cursor && nearestDistance < 6)
        {
            _cursor = nearestIndex;
            _nearest = nearestDistance;
            _stalledMs = 0;
            ++_advances;
        }
        while (_cursor + 1 < _points.size() && position.Distance(_points[_cursor]) < 5)
        {
            ++_cursor;
            _nearest = position.Distance(_points[_cursor]);
            _stalledMs = 0;
            ++_advances;
        }
        float const distance = position.Distance(_points[_cursor]);
        if (distance + 2 < _nearest)
        {
            _nearest = distance;
            _stalledMs = 0;
            ++_advances;
        }
    }

    void Pause(uint32_t now) { _lastSample = now; _paused = true; }
    bool Stalled() const { return _stalledMs >= 30000; }
    bool HasPath() const { return !_points.empty(); }
    Point End() const { return _points.empty() ? Point{} : _points.back(); }
    uint32_t StalledMs() const { return _stalledMs; }
    uint32_t Advances() const { return _advances; }
    std::size_t Waypoint() const { return _cursor; }
    void ClearPath() { _points.clear(); _stalledMs = 0; }

    // Remember partial-path endpoints across retries to reject A -> B -> A loops, including during combat.
    bool Tried(Point point) const
    {
        return std::any_of(_endpoints.begin(), _endpoints.end(), [point](auto const& old)
            { return point.Distance(old) < 5; });
    }
    void Remember(Point point)
    {
        if (_endpoints.size() == 64)
            _endpoints.erase(_endpoints.begin());
        _endpoints.push_back(point);
    }
    uint32_t failures = 0;
    uint32_t recoveries = 0;
    uint32_t nextAttempt = 0;

private:
    std::vector<Point> _points;
    std::vector<Point> _endpoints;
    std::size_t _cursor = 0;
    float _nearest = 0;
    uint32_t _lastSample = 0;
    uint32_t _stalledMs = 0;
    uint32_t _advances = 0;
    bool _paused = true;
};

// Bounded route policy from the owning brain. The tactical executor still supplies the actual goal;
// a policy for another incarnation, objective, map or destination cannot redirect that executor.
class BodyRoutePolicy
{
public:
    using Point = BodyTravel::Point;

    bool Install(uint64_t generation, uint64_t attachment, uint64_t objective, uint64_t revision,
        uint32_t map, Point goal, std::vector<Point> stops)
    {
        if (!generation || !attachment || !objective || !revision || !goal.Valid()
            || stops.empty() || stops.size() > 3 || stops.back().Distance(goal) >= 5
            || std::any_of(stops.begin(), stops.end(), [](Point point) { return !point.Valid(); })
            || (_generation == generation && _attachment == attachment && _objective == objective
                && revision < _revision))
            return false;
        for (std::size_t i = 0; i < stops.size(); ++i)
            for (std::size_t j = i + 1; j < stops.size(); ++j)
                if (stops[i].Distance(stops[j]) < 5)
                    return false;
        _generation = generation;
        _attachment = attachment;
        _objective = objective;
        _revision = revision;
        _map = map;
        _goal = goal;
        _stops = std::move(stops);
        _cursor = 0;
        return true;
    }

    bool Matches(uint64_t generation, uint64_t attachment, uint64_t objective, uint32_t map, Point goal) const
    {
        return !_stops.empty() && _generation == generation && _attachment == attachment
            && _objective == objective && _map == map && goal.Valid() && _goal.Distance(goal) < 5;
    }

    std::optional<Point> Next(uint64_t generation, uint64_t attachment, uint64_t objective,
        uint32_t map, Point goal, Point position)
    {
        if (!Matches(generation, attachment, objective, map, goal) || !position.Valid())
            return std::nullopt;
        while (_cursor + 1 < _stops.size() && position.Distance(_stops[_cursor]) < 5)
            ++_cursor;
        return _stops[_cursor];
    }

    std::size_t Stops() const { return _stops.size(); }
    std::size_t Cursor() const { return _cursor; }
    uint64_t Revision() const { return _revision; }

private:
    uint64_t _generation = 0, _attachment = 0, _objective = 0, _revision = 0;
    uint32_t _map = 0;
    Point _goal;
    std::vector<Point> _stops;
    std::size_t _cursor = 0;
};

#endif
