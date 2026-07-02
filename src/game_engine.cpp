#include "game_engine.h"

namespace
{
constexpr char WALL_CELL = '#';
constexpr char ROAD_CELL = ' ';
constexpr char GOLD_CELL = 'G';
constexpr char TRAP_CELL = 'T';
constexpr char BOSS_CELL = 'B';
constexpr char END_CELL = 'E';
constexpr int GOLD_VALUE = 50;
constexpr int TRAP_VALUE = -30;

// makeEvent（创建事件）输入形式 EventType type, Position position 输入含义 事件类型、事件发生坐标 输出形式 Event 输出含义 填好类型和坐标的事件对象
Event makeEvent(EventType type, Position position)
{
    Event event;
    event.type = type;
    event.position = position;
    return event;
}
}

// GameEngine（默认构造函数）输入形式 无 输入含义 无 输出形式 GameEngine对象 输出含义 创建空裁判层对象
GameEngine::GameEngine() = default;

// GameEngine（初始化游戏）输入形式 const MapData& mapData, const GameConfig& gameConfig, const PlayerState& playerState, const BossState& bossState 输入含义 地图数据、游戏配置、玩家初始状态、Boss初始状态 输出形式 GameEngine对象 输出含义 创建可运行的裁判层对象
GameEngine::GameEngine(const MapData& mapData,
                       const GameConfig& gameConfig,
                       const PlayerState& playerState,
                       const BossState& bossState)
    : map(mapData),
      config(gameConfig),
      player(playerState),
      boss(bossState),
      initialBossHpList(bossState.hpList)
{
}

// getState（获取当前游戏状态）输入形式 无 输入含义 无 输出形式 GameState 输出含义 玩家状态、Boss状态、3x3视野和战斗配置
GameState GameEngine::getState() const
{
    GameState state;
    state.status = status;
    state.player = player;
    state.boss.currentBoss = boss.currentBoss;
    state.boss.currentRound = boss.currentRound;
    if (boss.currentBoss >= 0 &&
        boss.currentBoss < static_cast<int>(boss.hpList.size()))
    {
        state.boss.currentBossHp = boss.hpList[boss.currentBoss];
    }
    state.vision = getVision();
    if (config.finishOnNoAction)
    {
        state.knownMap = map.maze;
    }
    state.inBattle = inBattle;
    state.finishOnNoAction = config.finishOnNoAction;
    state.minRounds = config.minRounds;
    state.coinConsumption = config.coinConsumption;
    return state;
}

// step（执行玩家动作）输入形式 const Action& action 输入含义 AIPlayer提交的动作 输出形式 StepRecord 输出含义 动作前后状态和本步事件
StepRecord GameEngine::step(const Action& action)
{
    StepRecord record;
    record.before = getState();
    record.action = action;

    if (status != GameStatus::RUNNING)
    {
        record.after = getState();
        return record;
    }

    if (inBattle && action.type != ActionType::USE_SKILL)
    {
        Event failed = makeEvent(EventType::USE_SKILL_FAILED, player.position);
        failed.skillIndex = action.skillIndex;
        record.events.push_back(failed);
        ++boss.currentRound;
        updateSkillCooldown();
        handleBattleRoundLimit(record.events);
    }
    else if (action.type == ActionType::MOVE)
    {
        record.events = handleMove(action);
    }
    else if (action.type == ActionType::USE_SKILL)
    {
        record.events = handleSkill(action);
    }
    else if (action.type == ActionType::NONE && config.finishOnNoAction)
    {
        status = GameStatus::WIN;
        record.events.push_back(makeEvent(EventType::GAME_WIN, player.position));
    }

    record.after = getState();
    return record;
}

// isGameOver（判断游戏是否结束）输入形式 无 输入含义 无 输出形式 bool 输出含义 true表示游戏已胜利或失败
bool GameEngine::isGameOver() const
{
    return status != GameStatus::RUNNING;
}

// getVision（生成3x3视野）输入形式 无 输入含义 使用当前玩家位置 输出形式 vector<vector<char>> 输出含义 玩家周围3x3格子
vector<vector<char>> GameEngine::getVision() const
{
    vector<vector<char>> vision(3, vector<char>(3, WALL_CELL));
    for (int dx = -1; dx <= 1; ++dx)
    {
        for (int dy = -1; dy <= 1; ++dy)
        {
            Position pos{player.position.x + dx, player.position.y + dy};
            int vx = dx + 1;
            int vy = dy + 1;
            if (pos.x >= 0 && pos.x < static_cast<int>(map.maze.size()) &&
                pos.y >= 0 && pos.y < static_cast<int>(map.maze[pos.x].size()))
            {
                vision[vx][vy] = map.maze[pos.x][pos.y];
            }
        }
    }
    return vision;
}

// isWalkable（判断坐标是否可通行）输入形式 const Position& pos 输入含义 待判断坐标 输出形式 bool 输出含义 true表示在地图内且不是墙
bool GameEngine::isWalkable(const Position& pos) const
{
    if (pos.x < 0 || pos.x >= static_cast<int>(map.maze.size()))
    {
        return false;
    }
    if (pos.y < 0 || pos.y >= static_cast<int>(map.maze[pos.x].size()))
    {
        return false;
    }
    return map.maze[pos.x][pos.y] != WALL_CELL;
}

// getNextPosition（计算下一坐标）输入形式 Direction direction 输入含义 移动方向 输出形式 Position 输出含义 玩家按该方向移动后的目标坐标
Position GameEngine::getNextPosition(Direction direction) const
{
    Position next = player.position;
    if (direction == Direction::UP)
    {
        --next.x;
    }
    else if (direction == Direction::DOWN)
    {
        ++next.x;
    }
    else if (direction == Direction::LEFT)
    {
        --next.y;
    }
    else if (direction == Direction::RIGHT)
    {
        ++next.y;
    }
    return next;
}

// handleMove（处理移动动作）输入形式 const Action& action 输入含义 MOVE类型动作 输出形式 vector<Event> 输出含义 移动产生的事件列表
vector<Event> GameEngine::handleMove(const Action& action)
{
    vector<Event> events;
    Position next = getNextPosition(action.direction);

    if (!isWalkable(next))
    {
        events.push_back(makeEvent(EventType::MOVE_BLOCKED, next));
        return events;
    }

    player.position = next;
    ++player.steps;
    events.push_back(makeEvent(EventType::MOVE_SUCCESS, next));

    vector<Event> cellEvents = updateCellEffect(next);
    events.insert(events.end(), cellEvents.begin(), cellEvents.end());
    return events;
}

// handleSkill（处理技能动作）输入形式 const Action& action 输入含义 USE_SKILL类型动作 输出形式 vector<Event> 输出含义 技能释放和Boss战产生的事件列表
vector<Event> GameEngine::handleSkill(const Action& action)
{
    vector<Event> events;
    if (!inBattle ||
        action.skillIndex < 0 ||
        action.skillIndex >= static_cast<int>(player.skills.size()) ||
        player.skills[action.skillIndex].currentCD > 0 ||
        boss.currentBoss >= static_cast<int>(boss.hpList.size()))
    {
        Event failed = makeEvent(EventType::USE_SKILL_FAILED, player.position);
        failed.skillIndex = action.skillIndex;
        events.push_back(failed);
        return events;
    }

    Skill& skill = player.skills[action.skillIndex];
    Event useSkill = makeEvent(EventType::USE_SKILL_SUCCESS, player.position);
    useSkill.skillIndex = action.skillIndex;
    events.push_back(useSkill);

    boss.hpList[boss.currentBoss] -= skill.damage;
    Event damaged = makeEvent(EventType::BOSS_DAMAGED, player.position);
    damaged.hpDelta = -skill.damage;
    damaged.skillIndex = action.skillIndex;
    damaged.bossIndex = boss.currentBoss;
    events.push_back(damaged);

    skill.currentCD = skill.cooldown + 1;
    ++boss.currentRound;

    if (boss.hpList[boss.currentBoss] <= 0)
    {
        Event defeated = makeEvent(EventType::BOSS_DEFEATED, player.position);
        defeated.bossIndex = boss.currentBoss;
        events.push_back(defeated);
        ++boss.currentBoss;
        if (boss.currentBoss >= static_cast<int>(boss.hpList.size()))
        {
            inBattle = false;
            boss.currentRound = 0;
            if (map.maze[player.position.x][player.position.y] == BOSS_CELL)
            {
                map.maze[player.position.x][player.position.y] = ROAD_CELL;
            }
        }
    }
    handleBattleRoundLimit(events);

    updateSkillCooldown();
    return events;
}

// updateCellEffect（处理进入格子的效果）输入形式 const Position& pos 输入含义 玩家进入的坐标 输出形式 vector<Event> 输出含义 金币、陷阱、Boss、终点等事件列表
vector<Event> GameEngine::updateCellEffect(const Position& pos)
{
    vector<Event> events;
    char& cell = map.maze[pos.x][pos.y];

    if (cell == GOLD_CELL)
    {
        player.coins += GOLD_VALUE;
        Event event = makeEvent(EventType::PICK_GOLD, pos);
        event.coinDelta = GOLD_VALUE;
        events.push_back(event);
        cell = ROAD_CELL;
    }
    else if (cell == TRAP_CELL)
    {
        player.coins += TRAP_VALUE;
        Event event = makeEvent(EventType::TRIGGER_TRAP, pos);
        event.coinDelta = TRAP_VALUE;
        events.push_back(event);
        cell = ROAD_CELL;
        if (player.coins < 0)
        {
            status = GameStatus::LOSE;
            events.push_back(makeEvent(EventType::GAME_LOSE, pos));
        }
    }
    else if (cell == BOSS_CELL)
    {
        inBattle = true;
        boss.hpList = initialBossHpList;
        boss.currentBoss = 0;
        boss.currentRound = 0;
        events.push_back(makeEvent(EventType::ENTER_BOSS, pos));
    }
    else if (cell == END_CELL)
    {
        events.push_back(makeEvent(EventType::REACH_END, pos));
        if (boss.currentBoss >= static_cast<int>(boss.hpList.size()))
        {
            status = GameStatus::WIN;
            events.push_back(makeEvent(EventType::GAME_WIN, pos));
        }
        else
        {
            status = GameStatus::LOSE;
            events.push_back(makeEvent(EventType::GAME_LOSE, pos));
        }
    }

    return events;
}

// updateSkillCooldown（更新技能冷却）输入形式 无 输入含义 使用player.skills当前冷却状态 输出形式 void 输出含义 无返回值，直接修改技能冷却
void GameEngine::updateSkillCooldown()
{
    for (Skill& skill : player.skills)
    {
        if (skill.currentCD > 0)
        {
            --skill.currentCD;
        }
    }
}

// handleBattleRoundLimit（处理Boss战回合限制）输入形式 vector<Event>& events 输入含义 本回合事件列表 输出形式 void 输出含义 无返回值，可能追加复活或失败事件
void GameEngine::handleBattleRoundLimit(vector<Event>& events)
{
    if (!inBattle ||
        boss.currentBoss >= static_cast<int>(boss.hpList.size()) ||
        config.minRounds <= 0 ||
        boss.currentRound < config.minRounds)
    {
        return;
    }

    if (player.coins >= config.coinConsumption)
    {
        player.coins -= config.coinConsumption;
        Event revive = makeEvent(EventType::REVIVE, player.position);
        revive.coinDelta = -config.coinConsumption;
        revive.bossIndex = boss.currentBoss;
        events.push_back(revive);

        boss.hpList = initialBossHpList;
        boss.currentBoss = 0;
        boss.currentRound = 0;
        for (Skill& playerSkill : player.skills)
        {
            playerSkill.currentCD = 0;
        }
    }
    else
    {
        status = GameStatus::LOSE;
        events.push_back(makeEvent(EventType::GAME_LOSE, player.position));
    }
}

// isAtBoss（判断玩家是否在Boss格）输入形式 无 输入含义 使用当前玩家位置 输出形式 bool 输出含义 true表示玩家位于Boss坐标
bool GameEngine::isAtBoss() const
{
    return player.position == map.boss;
}

// isAtEnd（判断玩家是否在终点）输入形式 无 输入含义 使用当前玩家位置 输出形式 bool 输出含义 true表示玩家位于终点坐标
bool GameEngine::isAtEnd() const
{
    return player.position == map.end;
}
