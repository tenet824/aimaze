#include "algorithm.h"

#include <queue>
#include <cmath>
#include <limits>

namespace
{
    // DirectionInfo（方向信息结构体）输入形式 无 输入含义 无 输出形式 DirectionInfo 输出含义 保存方向、视野坐标和坐标增量
    struct DirectionInfo
    {
        Direction direction;
        int viewX;   // 在 3x3 视野中的行索引
        int viewY;   // 在 3x3 视野中的列索引
        int dx;      // 在地图中的行偏移
        int dy;      // 在地图中的列偏移
    };

    // 四个移动方向：顺序影响同分时的选择
    const vector<DirectionInfo> kDirections = {
        {Direction::RIGHT, 1, 2, 0, 1},
        {Direction::DOWN, 2, 1, 1, 0},
        {Direction::LEFT, 1, 0, 0, -1},
        {Direction::UP, 0, 1, -1, 0}};

    // makeMove（构造移动动作）输入形式 Direction direction 输入含义 移动方向 输出形式 Action 输出含义 MOVE 类型动作
    Action makeMove(Direction direction)
    {
        Action action;
        action.type = ActionType::MOVE;
        action.direction = direction;
        return action;
    }

    // isWalkable（判断格子是否可通行）输入形式 char cell 输入含义 地图格子字符 输出形式 bool 输出含义 true 表示不是墙
    bool isWalkable(char cell)
    {
        return cell != '#';
    }

    // nextPosition（计算相邻坐标）输入形式 Position p, const DirectionInfo& d 输入含义 当前坐标、方向信息 输出形式 Position 输出含义 相邻目标坐标
    Position nextPosition(Position position, const DirectionInfo& direction)
    {
        return {position.x + direction.dx, position.y + direction.dy};
    }

    // cellWeight（Dijkstra 加权边的权重函数）输入形式 char cell 输入含义 格子类型 输出形式 double 输出含义 通过该格的代价值
    // 优化后的权重设计（基于经济价值校准）：
    //   金币 G = +50 金币 → 权重 0.05（极力吸引路径经过）
    //   陷阱 T = -30 金币 → 权重 2.0（适度惩罚，不再完全阻断：1陷阱+1金币净赚20金币）
    //   空地/终点/Boss → 权重 1.0
    // 旧权重（陷阱 8.0）导致算法遇到陷阱就绕路，错失陷阱后的金币；新权重使算法愿意"冒险"
    double cellWeight(char cell)
    {
        switch (cell)
        {
        case 'G': // 金币：极低权重，强力吸引
            return 0.05;
        case 'T': // 陷阱：适度惩罚（≈2个空格的代价），允许穿越以获取后方金币
            return 2.0;
        case ' ': // 空地
        case 'E': // 终点
        case 'B': // Boss 格
            return 1.0;
        default: // 未知、墙壁等
            return 1.0;
        }
    }

    // countNearbyGolds（统计陷阱周围已知未访问金币数）输入形式 const Position& pos, const map<Position, CellMemory>& memory, int range
    // 输入含义 陷阱坐标、AI记忆、搜索半径 输出形式 int 输出含义 范围内已知未访问金币数量
    // 用于动态评估：陷阱附近金币越多，穿越陷阱越"划算"
    int countNearbyGolds(const Position& pos, const map<Position, CellMemory>& memory, int range = 3)
    {
        int count = 0;
        for (int dx = -range; dx <= range; ++dx)
        {
            for (int dy = -range; dy <= range; ++dy)
            {
                if (dx == 0 && dy == 0)
                {
                    continue;
                }
                Position check{pos.x + dx, pos.y + dy};
                auto it = memory.find(check);
                if (it != memory.end() && it->second.type == 'G' && it->second.visitCount == 0)
                {
                    ++count;
                }
            }
        }
        return count;
    }

    // adjustedTrapWeight（动态陷阱权重）输入形式 const Position& pos, const map<Position, CellMemory>& memory
    // 输入含义 陷阱坐标、AI记忆 输出形式 double 输出含义 根据附近金币数量动态调整后的陷阱代价值
    // 附近 0 个金币 → 权重 2.5（仍比空地差，会尽量避免但不再完全阻断）
    // 附近 1 个金币 → 权重 1.7（值得考虑穿越）
    // 附近 2+个金币 → 权重 1.2（几乎等价于空地，极力鼓励穿越捡金币）
    double adjustedTrapWeight(const Position& pos, const map<Position, CellMemory>& memory)
    {
        int nearbyGolds = countNearbyGolds(pos, memory);
        double weight = 2.5 - nearbyGolds * 0.8;
        return max(1.2, weight);
    }

    // isFrontier（判断是否为探索前沿）输入形式 const Position& p, const map<Position, CellMemory>& m 输入含义 待判断坐标、AI记忆 输出形式 bool 输出含义 true表示该格可走且邻接未探索区域
    bool isFrontier(const Position& position, const map<Position, CellMemory>& memory)
    {
        auto current = memory.find(position);
        if (current == memory.end() || !isWalkable(current->second.type))
        {
            return false;
        }

        for (const DirectionInfo& direction : kDirections)
        {
            Position neighbor = nextPosition(position, direction);
            if (memory.find(neighbor) == memory.end())
            {
                return true; // 存在未探索的邻居
            }
        }
        return false;
    }

    // hasAnyFrontier（检查是否存在未探索区域）输入形式 const map<Position, CellMemory>& memory 输入含义 AI记忆 输出形式 bool 输出含义 true表示还有可探索的前沿格
    // 用于"延迟出终点"策略：即使终点已知，只要还有未探索区域，就继续探索以发现更多金币
    bool hasAnyFrontier(const map<Position, CellMemory>& memory)
    {
        for (const auto& item : memory)
        {
            if (isFrontier(item.first, memory))
            {
                return true;
            }
        }
        return false;
    }

    // DistCompare（优先队列比较器）输入形式 两个 pair<double, Position> 输入含义 加权距离和坐标 输出形式 bool 输出含义 true 表示 a 排在 b 后面，实现小顶堆
    struct DistCompare
    {
        bool operator()(const pair<double, Position>& a, const pair<double, Position>& b) const
        {
            return a.first > b.first;
        }
    };

    // dijkstraFirstStep（加权 Dijkstra 寻路，返回通往目标的第一步）输入形式 Position start, Position target, const map<Position, CellMemory>& memory
    // 输入含义 起点、目标、AI已知地图记忆 输出形式 Action 输出含义 通往目标的第一步 MOVE 动作，找不到则为 NONE
    Action dijkstraFirstStep(Position start, Position target, const map<Position, CellMemory>& memory)
    {
        if (start == target)
        {
            return Action{};
        }

        map<Position, double> distance;                          // 从起点到各位置的当前已知最短距离
        map<Position, Direction> firstDirection;                  // 从起点到达各位置的第一步方向
        priority_queue<pair<double, Position>, vector<pair<double, Position>>, DistCompare> pq;

        distance[start] = 0.0;
        pq.push({0.0, start});

        while (!pq.empty())
        {
            auto [dist, current] = pq.top();
            pq.pop();

            // 跳过已过时的记录（同一位置可能有多次入队）
            auto distIter = distance.find(current);
            if (distIter == distance.end() || distIter->second < dist)
            {
                continue;
            }

            // 到达目标，返回第一步动作
            if (current == target)
            {
                return makeMove(firstDirection[current]);
            }

            for (const DirectionInfo& direction : kDirections)
            {
                Position next = nextPosition(current, direction);

                auto cell = memory.find(next);
                if (cell == memory.end() || !isWalkable(cell->second.type))
                {
                    continue;
                }

                double newDist = dist + cellWeight(cell->second.type);
                auto nextDistIter = distance.find(next);

                if (nextDistIter == distance.end() || newDist < nextDistIter->second)
                {
                    distance[next] = newDist;
                    // 起点的邻居记录自己的方向，其余继承第一步方向
                    firstDirection[next] = (current == start) ? direction.direction : firstDirection[current];
                    pq.push({newDist, next});
                }
            }
        }

        return Action{}; // 无可达路径
    }

    // dijkstraToNearestGold（加权 Dijkstra 寻路到最近已知未访问金币）输入形式 Position start, const map<Position, CellMemory>& memory
    // 输入含义 起点、AI已知地图记忆 输出形式 Action 输出含义 通往最近金币的第一步 MOVE 动作，找不到则为 NONE
    // 该函数是优化的核心：主动寻找已知金币，而非等待随机探索发现
    // 对陷阱使用动态权重（adjustedTrapWeight），根据附近金币数量降低陷阱代价
    Action dijkstraToNearestGold(Position start, const map<Position, CellMemory>& memory)
    {
        map<Position, double> distance;
        map<Position, Direction> firstDirection;
        priority_queue<pair<double, Position>, vector<pair<double, Position>>, DistCompare> pq;

        distance[start] = 0.0;
        pq.push({0.0, start});

        // 用于记录找到的金币，选择距离最近的
        Position bestGoldPos = start;
        double bestGoldDist = numeric_limits<double>::max();
        bool goldFound = false;

        while (!pq.empty())
        {
            auto [dist, current] = pq.top();
            pq.pop();

            auto distIter = distance.find(current);
            if (distIter == distance.end() || distIter->second < dist)
            {
                continue;
            }

            // 检查当前格是否为未访问的金币
            auto cellIter = memory.find(current);
            if (cellIter != memory.end() &&
                cellIter->second.type == 'G' &&
                cellIter->second.visitCount == 0 &&
                dist < bestGoldDist)
            {
                bestGoldDist = dist;
                bestGoldPos = current;
                goldFound = true;
                // 不立即返回，继续搜索确保找到真正最近的金币（Dijkstra保证第一个出队的最近）
                // 但由于后续节点dist只会更大，第一个金币就是最近的
                return makeMove(firstDirection[current]);
            }

            // 如果已经找到金币且当前距离已超过最佳距离，提前退出
            if (goldFound && dist >= bestGoldDist)
            {
                break;
            }

            for (const DirectionInfo& direction : kDirections)
            {
                Position next = nextPosition(current, direction);

                auto cell = memory.find(next);
                if (cell == memory.end() || !isWalkable(cell->second.type))
                {
                    continue;
                }

                // 对陷阱使用动态权重：附近金币越多，穿越陷阱代价越低
                double weight = (cell->second.type == 'T')
                    ? adjustedTrapWeight(next, memory)
                    : cellWeight(cell->second.type);

                double newDist = dist + weight;
                auto nextDistIter = distance.find(next);

                if (nextDistIter == distance.end() || newDist < nextDistIter->second)
                {
                    distance[next] = newDist;
                    firstDirection[next] = (current == start) ? direction.direction : firstDirection[current];
                    pq.push({newDist, next});
                }
            }
        }

        return Action{}; // 无可达金币
    }

    // chooseBestNeighbor（在 3x3 视野中选择最优相邻格）输入形式 const GameState& state 输入含义 当前状态 输出形式 Action 输出含义 最优方向 MOVE 动作
    Action chooseBestNeighbor(const GameState& state)
    {
        bool found = false;
        double bestWeight = 0.0;
        Direction bestDirection = Direction::UP;

        for (const DirectionInfo& direction : kDirections)
        {
            char cell = state.vision[direction.viewX][direction.viewY];
            if (!isWalkable(cell))
            {
                continue;
            }

            double weight = cellWeight(cell);
            if (!found || weight < bestWeight)
            {
                found = true;
                bestWeight = weight;
                bestDirection = direction.direction;
            }
        }

        return found ? makeMove(bestDirection) : Action{};
    }
}

// Dijkstra（加权 Dijkstra 迷宫探索算法）输入形式 const GameState& state, map<Position, CellMemory>& memory
// 输入含义 当前游戏状态、AI 已知地图记忆 输出形式 Action 输出含义 Dijkstra 策略给出的下一步动作
//
// 五层策略（优化后）：
//   第一层（直接目标）：G/B 在 3x3 视野中 → 直接走过去；E 仅在无剩余金币时走
//   第二层（Dijkstra 到终点）：终点已知且无剩余金币 → 寻路到终点
//   第三层（Dijkstra 到金币）：主动寻路到已知未访问金币 → 动态陷阱权重
//   第四层（Dijkstra 探索前沿）：无已知金币 → 探索最近前沿
//   第五层（回退策略）：前沿不可达 → 回退到终点（若已知）或选择最优相邻格
Action Algorithm::Dijkstra(const GameState& state, map<Position, CellMemory>& memory)
{
    // ===== 第一层：直接目标处理 =====
    // G（金币）和 B（Boss）在 3x3 视野内 → 直接走过去
    // E（终点）暂不处理：优先收集完所有金币后再走出出口
    for (const DirectionInfo& direction : kDirections)
    {
        char cell = state.vision[direction.viewX][direction.viewY];
        if (cell == 'G' || cell == 'B')
        {
            return makeMove(direction.direction);
        }
    }

    // 检查 AI 记忆中是否还有已知但尚未访问的金币
    // 这是"延迟出终点"策略的核心：只要还有金币可捡，就不急于走向终点
    bool hasKnownGold = false;
    for (const auto& item : memory)
    {
        if (item.second.type == 'G' && item.second.visitCount == 0)
        {
            hasKnownGold = true;
            break;
        }
    }

    // ===== 第一层补充：3x3 视野内有终点 E =====
    // ★优化：只有当没有已知金币且没有未探索前沿时，才允许走向终点
    // 这样即使终点在眼前，也会先去探索迷宫的其他区域以发现更多金币
    if (!hasKnownGold)
    {
        bool hasFrontiers = hasAnyFrontier(memory);
        if (!hasFrontiers)
        {
            for (const DirectionInfo& direction : kDirections)
            {
                char cell = state.vision[direction.viewX][direction.viewY];
                if (cell == 'E')
                {
                    return makeMove(direction.direction);
                }
            }
        }
    }

    // ===== 第二层：若已知终点位置 =====
    // ★优化：即使终点已知，只要还有金币可收集，就优先收集金币
    // 如果金币已收完但还有未探索前沿，继续探索（可能发现更多金币）
    // 只有当所有金币都收集完毕且没有未探索区域时，才走向终点
    Position endPos;
    bool endKnown = false;
    for (const auto& item : memory)
    {
        if (item.second.type == 'E')
        {
            endPos = item.first;
            endKnown = true;
            break;
        }
    }

    if (endKnown)
    {
        bool hasFrontiers = hasAnyFrontier(memory);

        if (!hasKnownGold && !hasFrontiers)
        {
            // 地图已完全探索且无剩余金币 → 直接走向终点
            Action action = dijkstraFirstStep(state.player.position, endPos, memory);
            if (action.type != ActionType::NONE)
            {
                return action;
            }
        }
        // 否则：还有金币或前沿 → 不走向终点，交给后续层处理
    }

    // ===== 第三层：主动寻路到已知未访问金币 =====
    // 无论是探索阶段还是终点已知阶段，优先收集已知金币
    // 使用动态陷阱权重：陷阱附近金币越多，穿越代价越低
    {
        Action goldAction = dijkstraToNearestGold(state.player.position, memory);
        if (goldAction.type != ActionType::NONE)
        {
            return goldAction;
        }
    }

    // ===== 第三层补充：金币收集完毕后的收尾 =====
    // 如果终点已知、所有已知金币已收集、且无可探索前沿 → 走向终点
    // 否则继续探索未探索区域以发现更多金币
    if (endKnown)
    {
        bool hasFrontiers = hasAnyFrontier(memory);
        if (!hasFrontiers)
        {
            Action action = dijkstraFirstStep(state.player.position, endPos, memory);
            if (action.type != ActionType::NONE)
            {
                return action;
            }
        }
    }

    // ===== 第四层：加权 Dijkstra 探索到最近前沿 =====
    // 终点未知且无已知金币可收集时，以探索未知区域为目标
    // 对路径中的陷阱使用动态权重，鼓励穿越陷阱探索可能含金币的未知区域
    map<Position, double> distance;
    map<Position, Direction> firstDirection;
    priority_queue<pair<double, Position>, vector<pair<double, Position>>, DistCompare> pq;

    distance[state.player.position] = 0.0;
    pq.push({0.0, state.player.position});

    while (!pq.empty())
    {
        auto [dist, current] = pq.top();
        pq.pop();

        // 跳过已过时的记录
        auto distIter = distance.find(current);
        if (distIter == distance.end() || distIter->second < dist)
        {
            continue;
        }

        // 检查是否到达探索前沿（排除起点自身）
        if (!(current == state.player.position) && isFrontier(current, memory))
        {
            return makeMove(firstDirection[current]);
        }

        for (const DirectionInfo& direction : kDirections)
        {
            Position next = nextPosition(current, direction);

            auto cell = memory.find(next);
            if (cell == memory.end() || !isWalkable(cell->second.type))
            {
                continue;
            }

            // 探索时对陷阱使用动态权重，鼓励穿越陷阱探索未知区域
            double weight = (cell->second.type == 'T')
                ? adjustedTrapWeight(next, memory)
                : cellWeight(cell->second.type);

            double newDist = dist + weight;
            auto nextDistIter = distance.find(next);

            if (nextDistIter == distance.end() || newDist < nextDistIter->second)
            {
                distance[next] = newDist;
                firstDirection[next] = (current == state.player.position) ? direction.direction : firstDirection[current];
                pq.push({newDist, next});
            }
        }
    }

    // ===== 第五层：回退策略 =====
    // 如果所有已知可通行区域都无法通向任何前沿：
    //   优先回退到终点（若已知），其次在 3x3 视野中选择最优相邻格
    if (endKnown)
    {
        Action action = dijkstraFirstStep(state.player.position, endPos, memory);
        if (action.type != ActionType::NONE)
        {
            return action;
        }
    }

    return chooseBestNeighbor(state);
}
