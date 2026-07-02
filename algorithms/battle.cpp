#include "algorithm.h"

#include <algorithm>
#include <limits>

namespace
{
// makeSkillAction（构造技能动作）输入形式 int skillIndex 输入含义 技能下标 输出形式 Action 输出含义 USE_SKILL类型动作
Action makeSkillAction(int skillIndex)
{
    Action action;
    action.type = ActionType::USE_SKILL;
    action.skillIndex = skillIndex;
    return action;
}

// cooldownAfterUse（模拟技能冷却）输入形式 const vector<int>& currentCooldowns, const vector<Skill>& skills, int usedSkill 输入含义 当前冷却数组、技能列表、本回合使用技能下标 输出形式 vector<int> 输出含义 使用技能并经过一回合后的冷却数组
vector<int> cooldownAfterUse(const vector<int>& currentCooldowns,
                             const vector<Skill>& skills,
                             int usedSkill)
{
    vector<int> nextCooldowns = currentCooldowns;
    nextCooldowns[usedSkill] = skills[usedSkill].cooldown + 1;
    for (int& cooldown : nextCooldowns)
    {
        if (cooldown > 0)
        {
            --cooldown;
        }
    }
    return nextCooldowns;
}

// searchBestSequence（搜索已知Boss最短胜利技能序列）输入形式 const vector<int>& knownBossHp, int bossOffset, int hp, int roundsLeft, const vector<Skill>& skills, const vector<int>& cooldowns, vector<int>& current, vector<int>& best 输入含义 已知Boss血量序列、当前序列下标、当前Boss血量、剩余回合、技能列表、当前冷却、当前序列、最佳序列 输出形式 bool 输出含义 true表示找到可击败已知Boss序列的技能序列
bool searchBestSequence(const vector<int>& knownBossHp,
                        int bossOffset,
                        int hp,
                        int roundsLeft,
                        const vector<Skill>& skills,
                        const vector<int>& cooldowns,
                        vector<int>& current,
                        vector<int>& best)
{
    if (bossOffset >= static_cast<int>(knownBossHp.size()))
    {
        if (best.empty() || current.size() < best.size())
        {
            best = current;
        }
        return true;
    }

    if (hp <= 0)
    {
        int nextOffset = bossOffset + 1;
        int nextHp = nextOffset < static_cast<int>(knownBossHp.size()) ? knownBossHp[nextOffset] : 0;
        return searchBestSequence(knownBossHp,
                                  nextOffset,
                                  nextHp,
                                  roundsLeft,
                                  skills,
                                  cooldowns,
                                  current,
                                  best);
    }
    if (roundsLeft <= 0)
    {
        return false;
    }
    if (!best.empty() && current.size() >= best.size())
    {
        return false;
    }

    bool found = false;
    vector<int> order(skills.size());
    for (int i = 0; i < static_cast<int>(skills.size()); ++i)
    {
        order[i] = i;
    }
    sort(order.begin(), order.end(), [&](int a, int b) {
        return skills[a].damage > skills[b].damage;
    });

    for (int index : order)
    {
        if (cooldowns[index] > 0)
        {
            continue;
        }

        current.push_back(index);
        vector<int> nextCooldowns = cooldownAfterUse(cooldowns, skills, index);
        if (searchBestSequence(knownBossHp,
                               bossOffset,
                               hp - skills[index].damage,
                               roundsLeft - 1,
                               skills,
                               nextCooldowns,
                               current,
                               best))
        {
            found = true;
        }
        current.pop_back();
    }

    return found;
}

// chooseHighestDamageReadySkill（选择最高伤害可用技能）输入形式 const vector<Skill>& skills 输入含义 玩家技能列表 输出形式 int 输出含义 当前冷却为0且伤害最高的技能下标，找不到返回-1
int chooseHighestDamageReadySkill(const vector<Skill>& skills)
{
    int bestIndex = -1;
    int bestDamage = numeric_limits<int>::min();
    for (int i = 0; i < static_cast<int>(skills.size()); ++i)
    {
        if (skills[i].currentCD == 0 && skills[i].damage > bestDamage)
        {
            bestDamage = skills[i].damage;
            bestIndex = i;
        }
    }
    return bestIndex;
}

// buildKnownBossHp（构造已知Boss序列）输入形式 const GameState& state, const BossMemory& bossMemory 输入含义 当前可见Boss状态、AI已知Boss记忆 输出形式 vector<int> 输出含义 从当前Boss开始的连续已知血量序列
vector<int> buildKnownBossHp(const GameState& state, const BossMemory& bossMemory)
{
    vector<int> knownBossHp;
    knownBossHp.push_back(state.boss.currentBossHp);

    for (int bossIndex = state.boss.currentBoss + 1;; ++bossIndex)
    {
        auto iter = bossMemory.knownHp.find(bossIndex);
        if (iter == bossMemory.knownHp.end())
        {
            break;
        }
        knownBossHp.push_back(iter->second);
    }

    return knownBossHp;
}
}

// Battle（Boss战算法）输入形式 const GameState& state, const BossMemory& bossMemory 输入含义 当前Boss战可见状态、AI已知Boss记忆 输出形式 Action 输出含义 搜索到的技能序列第一步动作，无法释放技能则为NONE
Action Algorithm::Battle(const GameState& state, const BossMemory& bossMemory)
{
    if (state.boss.currentBoss < 0 || state.boss.currentBossHp <= 0)
    {
        return Action{};
    }

    int roundsLeft = state.minRounds - state.boss.currentRound;
    if (roundsLeft <= 0)
    {
        roundsLeft = state.minRounds > 0 ? state.minRounds : 1;
    }

    vector<int> cooldowns;
    for (const Skill& skill : state.player.skills)
    {
        cooldowns.push_back(skill.currentCD);
    }

    vector<int> current;
    vector<int> best;
    vector<int> knownBossHp = buildKnownBossHp(state, bossMemory);
    searchBestSequence(knownBossHp,
                       0,
                       knownBossHp.front(),
                       roundsLeft,
                       state.player.skills,
                       cooldowns,
                       current,
                       best);
    if (!best.empty())
    {
        return makeSkillAction(best.front());
    }

    int fallback = chooseHighestDamageReadySkill(state.player.skills);
    if (fallback >= 0)
    {
        return makeSkillAction(fallback);
    }

    return Action{};
}
