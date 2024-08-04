#include "CopilotConfig.h"

#include <meojson/json.hpp>

#include "TilePack.h"
#include "Utils/Logger.hpp"
#include "Utils/NoWarningCPR.h"

using namespace asst::battle;
using namespace asst::battle::copilot;

bool asst::CopilotConfig::parse_magic_code(const std::string& copilot_magic_code)
{
    if (copilot_magic_code.empty()) {
        Log.error("copilot_magic_code is empty");
        return false;
    }

    cpr::Response response =
        cpr::Get(cpr::Url("https://prts.maa.plus/copilot/get/" + copilot_magic_code), cpr::Timeout { 10000 });

    if (response.status_code != 200) {
        Log.error("copilot_magic_code request failed");
        return false;
    }

    auto json = json::parse(response.text);

    if (json && json->contains("status_code") && json->at("status_code").as_integer() == 200) {
        if (json->contains("data") && json->at("data").contains("content")) {
            auto content_str = json->at("data").at("content").as_string();
            auto content = json::parse(content_str);
            if (content) {
                return parse(*content);
            }
        }
    }

    Log.error("using copilot_code failed, response:", response.text);
    return false;
}

auto asst::CopilotConfig::dump_requires() const -> std::string
{
    if (m_data.requirements.empty()) {
        return std::string("这个作业没有干员需求");
    }

    static const std::array<std::string, static_cast<size_t>(Require::NodeType::Butt)> NodeTypeMapping = {
        "初始", "不要",     "或者", "并且",   "潜能",     "精英",   "等级",
        "技能", "技能等级", "模组", "信赖度", "血量上限", "攻击力", "防御力"
    };

    static const std::array<std::string, static_cast<size_t>(Require::Relation::Butt)> RelationNameMapping = {
        "等于", "不等于", "大于", "大于等于", "小于", "小于等于", "的范围是", "是", "像是"
    };

    std::ostringstream desc(std::ios::app);

    auto funcDumpNode = [&](auto&& _Self, Require::Node const* _Cur) {
        switch (_Cur->t) {
        case Require::NodeType::Not: {
            // 对于逻辑节点，如果下面没有内容，不需要转印
            if (_Cur->next.empty()) {
                return;
            }

            desc << NodeTypeMapping[static_cast<size_t>(_Cur->t)] << " (";
            _Self(_Self, _Cur->next.front().get());
            desc << ") ";
        } break;
        case Require::NodeType::Or:
        case Require::NodeType::And: {
            // 对于逻辑节点，如果下面没有内容，不需要转印
            if (_Cur->next.empty()) {
                return;
            }

            for (size_t i = 0, end = _Cur->next.size() - 1; i < end; ++i) {
                _Self(_Self, _Cur->next[i].get());
                desc << " " << NodeTypeMapping[static_cast<size_t>(_Cur->t)] << " ";
            }
            _Self(_Self, _Cur->next.back().get());

        } break;
        default:
            desc << NodeTypeMapping[static_cast<size_t>(_Cur->t)] << RelationNameMapping[static_cast<size_t>(_Cur->r)];

            switch (_Cur->r) {
            case Require::Relation::Between: {
                desc << " 从 " << _Cur->range.first << " 到 " << _Cur->range.second;
            } break;
            case Require::Relation::Is:
            case Require::Relation::Like: {
                desc << _Cur->code;
            } break;
            default:
                desc << _Cur->range.first;
                break;
            }

            break;
        }
    };

    desc << "这个作业有如下几个需求" << std::endl;
    for (size_t i = 0; i < m_data.requirements.size(); ++i) {
        auto& r = m_data.requirements[i];
        desc << "第" << i + 1 << "个需求，对于干员或者干员组（" << r.name << "）," << std::endl;
        funcDumpNode(funcDumpNode, &r.top);
        desc << std::endl;
        if (!r.tips.empty()) {
            desc << "还需要注意: " << std::endl;
            desc << r.tips << std::endl;
        }
    }

    return desc.str();
}

void asst::CopilotConfig::clear()
{
    m_data = decltype(m_data)();
}

bool asst::CopilotConfig::parse(const json::value& json)
{
    LogTraceFunction;

    clear();

    m_data.info = parse_basic_info(json);
    m_data.groups = parse_groups(json);
    m_data.actions = parse_actions(json);
    m_data.requirements = parse_requires(json);

    return true;
}

asst::battle::copilot::BasicInfo asst::CopilotConfig::parse_basic_info(const json::value& json)
{
    LogTraceFunction;

    battle::copilot::BasicInfo info;

    info.stage_name = json.at("stage_name").as_string();

    info.title = json.get("doc", "title", std::string());
    info.title_color = json.get("doc", "title_color", std::string());
    info.details = json.get("doc", "details", std::string());
    info.details_color = json.get("doc", "details_color", std::string());

    return info;
}

asst::battle::copilot::OperUsageGroups asst::CopilotConfig::parse_groups(const json::value& json)
{
    LogTraceFunction;

    battle::copilot::OperUsageGroups groups;

    if (auto opt = json.find<json::array>("groups")) {
        for (const auto& group_info : opt.value()) {
            std::string group_name = group_info.at("name").as_string();
            std::vector<OperUsage> oper_vec;
            for (const auto& oper_info : group_info.at("opers").as_array()) {
                OperUsage oper;
                oper.name = oper_info.at("name").as_string();
                oper.skill = oper_info.get("skill", 1);
                oper.skill_usage = static_cast<battle::SkillUsage>(oper_info.get("skill_usage", 0));
                oper.skill_times = oper_info.get("skill_times", 1); // 使用技能的次数，默认为 1，兼容曾经的作业
                oper_vec.emplace_back(std::move(oper));
            }
            groups.emplace(std::move(group_name), std::move(oper_vec));
        }
    }

    if (auto opt = json.find<json::array>("opers")) {
        for (const auto& oper_info : opt.value()) {
            OperUsage oper;
            oper.name = oper_info.at("name").as_string();
            oper.skill = oper_info.get("skill", 1);
            oper.skill_usage = static_cast<battle::SkillUsage>(oper_info.get("skill_usage", 0));
            oper.skill_times = oper_info.get("skill_times", 1); // 使用技能的次数，默认为 1，兼容曾经的作业

            // 单个干员的，干员名直接作为组名
            std::string group_name = oper.name;
            groups.emplace(std::move(group_name), std::vector { std::move(oper) });
        }
    }

    return groups;
}

TriggerInfo asst::CopilotConfig::parse_trigger(const json::value& json)
{
    TriggerInfo trigger;

    trigger.kills = json.get("kills", TriggerInfo::DEACTIVE_KILLS);
    trigger.costs = json.get("costs", TriggerInfo::DEACTIVE_COST);
    trigger.cost_changes = json.get("cost_changes", TriggerInfo::DEACTIVE_COST_CHANGES);
    trigger.cooling = json.get("cooling", TriggerInfo::DEACTIVE_COOLING);
    trigger.count = json.get("count", TriggerInfo::DEACTIVE_COUNT);
    trigger.timeout = json.get("timeout", TriggerInfo::DEACTIVE_TIMEOUT);

    if (auto category = json.find("category")) {
        trigger.category = TriggerInfo::loadCategoryFrom(category.value().as_string());
    }

    return trigger;
}

bool asst::CopilotConfig::parse_action(const json::value& action_info, asst::battle::copilot::Action* _Out)
{
    LogTraceFunction;

    static const std::unordered_map<std::string, ActionType> ActionTypeMapping = {
        { "Deploy", ActionType::Deploy },
        { "DEPLOY", ActionType::Deploy },
        { "deploy", ActionType::Deploy },
        { "部署", ActionType::Deploy },

        { "Skill", ActionType::UseSkill },
        { "SKILL", ActionType::UseSkill },
        { "skill", ActionType::UseSkill },
        { "技能", ActionType::UseSkill },

        { "Retreat", ActionType::Retreat },
        { "RETREAT", ActionType::Retreat },
        { "retreat", ActionType::Retreat },
        { "撤退", ActionType::Retreat },

        { "SkillUsage", ActionType::SkillUsage },
        { "SKILLUSAGE", ActionType::SkillUsage },
        { "Skillusage", ActionType::SkillUsage },
        { "skillusage", ActionType::SkillUsage },
        { "技能用法", ActionType::SkillUsage },

        { "SpeedUp", ActionType::SwitchSpeed },
        { "SPEEDUP", ActionType::SwitchSpeed },
        { "Speedup", ActionType::SwitchSpeed },
        { "speedup", ActionType::SwitchSpeed },
        { "二倍速", ActionType::SwitchSpeed },

        { "BulletTime", ActionType::BulletTime },
        { "BULLETTIME", ActionType::BulletTime },
        { "Bullettime", ActionType::BulletTime },
        { "bullettime", ActionType::BulletTime },
        { "子弹时间", ActionType::BulletTime },

        { "Output", ActionType::Output },
        { "OUTPUT", ActionType::Output },
        { "output", ActionType::Output },
        { "输出", ActionType::Output },
        { "打印", ActionType::Output },

        { "SkillDaemon", ActionType::SkillDaemon },
        { "skilldaemon", ActionType::SkillDaemon },
        { "SKILLDAEMON", ActionType::SkillDaemon },
        { "Skilldaemon", ActionType::SkillDaemon },
        { "DoNothing", ActionType::SkillDaemon },
        { "摆完挂机", ActionType::SkillDaemon },
        { "开摆", ActionType::SkillDaemon },

        { "MoveCamera", ActionType::MoveCamera },
        { "movecamera", ActionType::MoveCamera },
        { "MOVECAMERA", ActionType::MoveCamera },
        { "Movecamera", ActionType::MoveCamera },
        { "移动镜头", ActionType::MoveCamera },

        { "DrawCard", ActionType::DrawCard },
        { "drawcard", ActionType::DrawCard },
        { "DRAWCARD", ActionType::DrawCard },
        { "Drawcard", ActionType::DrawCard },
        { "抽卡", ActionType::DrawCard },
        { "抽牌", ActionType::DrawCard },
        { "调配", ActionType::DrawCard },
        { "调配干员", ActionType::DrawCard },

        { "CheckIfStartOver", ActionType::CheckIfStartOver },
        { "Checkifstartover", ActionType::CheckIfStartOver },
        { "CHECKIFSTARTOVER", ActionType::CheckIfStartOver },
        { "checkifstartover", ActionType::CheckIfStartOver },
        { "检查重开", ActionType::CheckIfStartOver },

        { "Loop", ActionType::Loop },
        { "loop", ActionType::Loop },
        { "LOOP", ActionType::Loop },
        { "循环", ActionType::Loop },

        { "Case", ActionType::Case },
        { "case", ActionType::Case },
        { "CASE", ActionType::Case },
        { "派发", ActionType::Case },
        { "干员派发", ActionType::Case },

        { "Check", ActionType::Check },
        { "check", ActionType::Check },
        { "CHECK", ActionType::Check },
        { "确认", ActionType::Check },
        { "分支", ActionType::Check },

        { "Until", ActionType::Until },
        { "until", ActionType::Until },
        { "UNTIL", ActionType::Until },
        { "轮询", ActionType::Until },

        { "SavePoint", ActionType::SavePoint },
        { "savepoint", ActionType::SavePoint },
        { "SAVEPOINT", ActionType::SavePoint },
        { "锚点", ActionType::SavePoint },
        { "保存锚点", ActionType::SavePoint },

        { "SyncPoint", ActionType::SyncPoint },
        { "syncpoint", ActionType::SyncPoint },
        { "SYNCPOINT", ActionType::SyncPoint },
        { "同步锚点", ActionType::SyncPoint },

        { "CheckPoint", ActionType::CheckPoint },
        { "checkpoint", ActionType::CheckPoint },
        { "CHECKPOINT", ActionType::CheckPoint },
        { "检查锚点", ActionType::CheckPoint },
    };

    auto& action = (*_Out);

    std::string type_str = action_info.get("type", "Deploy");

    if (auto iter = ActionTypeMapping.find(type_str); iter != ActionTypeMapping.end()) {
        action.type = iter->second;
    }
    else {
        Log.warn("Unknown action type:", type_str);
        return false;
    }
    // 解析锚点编码，可选
    action.point_code = action_info.get("point_code", std::string());

    // 解析动作触发信息
    // 为了让action更加精简，可以把触发器选项放到 "trigger" 中
    // 同样地，为了保持兼容性，触发器条件可以放在第一级
    if (auto t = action_info.find("trigger")) {
        action.trigger = parse_trigger(t.value());
    }
    else {
        action.trigger = parse_trigger(action_info);
    }

    // 在每个动作进行等待时，如果等待超时，这里可以对命令过程中的特殊行为进行捕获
    // 这里先设定了一个超时异常之后的行动
    if (auto tExcept = action_info.find("except_action")) {
        if (auto tTimeout = tExcept.value().find("timeout")) {
            action.except_actions["timeout"] = parse_actions_ptr(tTimeout.value());
        }
    }

    // 解析前置条件满足之后的前后时延
    action.delay.pre_delay = action_info.get("pre_delay", 0);
    auto post_delay_opt = action_info.find<int>("post_delay");
    action.delay.post_delay = post_delay_opt ? *post_delay_opt : action_info.get("rear_delay", 0);

    // 解析行动的相关附加文本
    action.text.doc = action_info.get("doc", std::string());
    action.text.doc_color = action_info.get("doc_color", std::string());

    // 根据动作的类型解析载荷
    switch (action.type) {
    case ActionType::Deploy:
    case ActionType::UseSkill:
    case ActionType::Retreat:
    case ActionType::BulletTime:
    case ActionType::SkillUsage: {
        auto& avatar = action.payload.emplace<AvatarInfo>();
        avatar.name = action_info.get("name", std::string());
        avatar.location.x = action_info.get("location", 0, 0);
        avatar.location.y = action_info.get("location", 1, 0);
        avatar.direction = string_to_direction(action_info.get("direction", "Right"));

        avatar.modify_usage = static_cast<battle::SkillUsage>(action_info.get("skill_usage", 0));
        avatar.modify_times = action_info.get("skill_times", 1);
    } break;
    case ActionType::CheckIfStartOver: {
        auto& info = action.payload.emplace<CheckIfStartOverInfo>();

        info.name = action_info.get("name", std::string());
        if (auto tool_men = action_info.find("tool_men")) {
            info.role_counts = parse_role_counts(*tool_men);
        }
    } break;
    case ActionType::MoveCamera: {
        auto dist_arr = action_info.at("distance").as_array();
        action.payload.emplace<MoveCameraInfo>(
            std::pair<double, double>(dist_arr[0].as_double(), dist_arr[1].as_double()));
    } break;
    case ActionType::Loop: {
        auto& loop = action.payload.emplace<LoopInfo>();

        // 必选字段
        loop.end_info = parse_trigger(action_info.at("end"));
        if (loop.end_info.category == TriggerInfo::Category::None) {
            // 默认使用all策略，表示只有全部满足才算生效
            loop.end_info.category = TriggerInfo::Category::All;
        }

        // 可选字段
        if (auto t = action_info.find("continue")) {
            loop.continue_info = parse_trigger(t.value());

            if (loop.continue_info.category == TriggerInfo::Category::None) {
                // 默认使用all策略，表示只有全部满足才算生效
                loop.continue_info.category = TriggerInfo::Category::All;
            }
        }

        // 可选字段
        if (auto t = action_info.find("break")) {
            loop.break_info = parse_trigger(t.value());

            if (loop.break_info.category == TriggerInfo::Category::None) {
                // 默认使用all策略，表示只有全部满足才算生效
                loop.break_info.category = TriggerInfo::Category::All;
            }
        }

        // 可选字段
        if (auto t = action_info.find("loop_actions")) {
            loop.loop_actions = parse_actions_ptr(t.value());
        }

    } break;
    case ActionType::Case: {
        auto& case_info = action.payload.emplace<CaseInfo>();

        // 必选字段
        case_info.group_select = action_info.get("select", std::string());

        // 可选字段
        if (auto t = action_info.find("dispatch_actions")) {
            for (auto const& [name, batch] : t.value().as_object()) {
                case_info.dispatch_actions.emplace(name, parse_actions_ptr(batch));
            }
        }

        // 可选字段
        if (auto t = action_info.find("default_action")) {
            case_info.default_action = parse_actions_ptr(t.value());
        }
    } break;
    case ActionType::Check: {
        auto& check = action.payload.emplace<CheckInfo>();

        // 必选字段
        check.condition_info = parse_trigger(action_info.get("condition", "all"));

        if (check.condition_info.category == TriggerInfo::Category::None) {
            // 默认使用all策略，表示只有全部满足才算生效
            check.condition_info.category = TriggerInfo::Category::All;
        }

        // 可选字段
        if (auto t = action_info.find("then_actions")) {
            check.then_actions = parse_actions_ptr(t.value());
        }

        // 可选字段
        if (auto t = action_info.find("else_actions")) {
            check.else_actions = parse_actions_ptr(t.value());
        }
    } break;
    case ActionType::Until: {
        auto& until = action.payload.emplace<UntilInfo>();

        // 可选字段
        if (auto t = action_info.find("mode")) {
            until.mode = TriggerInfo::loadCategoryFrom(t.value().as_string());
        }

        switch (until.mode) {
        case TriggerInfo::Category::Any:
        case TriggerInfo::Category::All:
            break;
        default:
            until.mode = TriggerInfo::Category::All;
            break;
        }

        // 可选字段，缺省值为0
        if (auto t = action_info.find("limit")) {
            action.trigger.count = t.value().as_integer();
        }
        else if (action.trigger.count == TriggerInfo::DEACTIVE_COUNT) {
            action.trigger.count = 0;
        }

        // 可选字段
        if (auto t = action_info.find("candidate_actions")) {
            until.candidate_actions = parse_actions_ptr(t.value());
        }

        if (auto t = action_info.find("overflow_actions")) {
            until.overflow_actions = parse_actions_ptr(t.value());
        }
    } break;
    case ActionType::SyncPoint: {
        auto& point = action.payload.emplace<SyncPointInfo>();

        point.target_code = action_info.get("target_code", std::string());

        if (auto t = action_info.find("mode")) {
            point.mode = TriggerInfo::loadCategoryFrom(t.value().as_string());
        }
        switch (point.mode) {
        case TriggerInfo::Category::Any:
        case TriggerInfo::Category::All:
        case TriggerInfo::Category::Not:
        case TriggerInfo::Category::Succ:
            break;
        default:
            point.mode = TriggerInfo::Category::All;
            break;
        }

        if (auto t = action_info.find("kill_range")) {
            auto range = t.value().as_array();
            point.range.first.kills = range[0].as_integer();
            point.range.second.kills = range[1].as_integer();
        }

        if (auto t = action_info.find("cost_range")) {
            auto range = t.value().as_array();
            point.range.first.cost = range[0].as_integer();
            point.range.second.cost = range[1].as_integer();
        }

        if (auto t = action_info.find("cooling_range")) {
            auto range = t.value().as_array();
            point.range.first.cooling_count = range[0].as_integer();
            point.range.second.cooling_count = range[1].as_integer();
        }

        if (auto t = action_info.find("time_range")) {
            auto range = t.value().as_array();
            point.range.first.interval = range[0].as_integer();
            point.range.second.interval = range[1].as_integer();
        }

        if (auto t = action_info.find("then_actions")) {
            point.then_actions = parse_actions_ptr(t.value());
        }

        point.sync_timeout = action_info.get("sync_timeout", TriggerInfo::DEACTIVE_TIMEOUT);

        if (auto t = action_info.find("timeout_actions")) {
            point.timeout_actions = parse_actions_ptr(t.value());
        }
    } break;
    case ActionType::CheckPoint: {
        auto& point = action.payload.emplace<CheckPointInfo>();

        point.target_code = action_info.get("target_code", std::string());

        if (auto t = action_info.find("mode")) {
            point.mode = TriggerInfo::loadCategoryFrom(t.value().as_string());
        }
        switch (point.mode) {
        case TriggerInfo::Category::Any:
        case TriggerInfo::Category::All:
        case TriggerInfo::Category::Not:
        case TriggerInfo::Category::Succ:
            break;
        default:
            point.mode = TriggerInfo::Category::All;
            break;
        }

        if (auto t = action_info.find("kill_range")) {
            auto range = t.value().as_array();
            point.range.first.kills = range[0].as_integer();
            point.range.second.kills = range[1].as_integer();
        }

        if (auto t = action_info.find("cost_range")) {
            auto range = t.value().as_array();
            point.range.first.cost = range[0].as_integer();
            point.range.second.cost = range[1].as_integer();
        }

        if (auto t = action_info.find("cooling_range")) {
            auto range = t.value().as_array();
            point.range.first.cooling_count = range[0].as_integer();
            point.range.second.cooling_count = range[1].as_integer();
        }

        if (auto t = action_info.find("time_range")) {
            auto range = t.value().as_array();
            point.range.first.interval = range[0].as_integer();
            point.range.second.interval = range[1].as_integer();
        }

        if (auto t = action_info.find("then_actions")) {
            point.then_actions = parse_actions_ptr(t.value());
        }

        if (auto t = action_info.find("else_actions")) {
            point.else_actions = parse_actions_ptr(t.value());
        }

    } break;
    case ActionType::SwitchSpeed:
    case ActionType::Output:
    case ActionType::SkillDaemon:
    case ActionType::DrawCard:
    case ActionType::SavePoint:
        [[fallthrough]];
    default:
        break;
    }

    return true;
}

std::vector<asst::battle::copilot::Action> asst::CopilotConfig::parse_actions(const json::value& json)
{
    LogTraceFunction;

    std::vector<battle::copilot::Action> actions_list;

    for (const auto& action_info : json.at("actions").as_array()) {
        battle::copilot::Action action;
        if (!parse_action(action_info, &action)) {
            continue;
        }

        actions_list.emplace_back(std::move(action));
    }

    return actions_list;
}

std::vector<asst::battle::copilot::ActionPtr> asst::CopilotConfig::parse_actions_ptr(const json::value& json)
{
    LogTraceFunction;

    std::vector<battle::copilot::ActionPtr> actions_list;

    for (const auto& action_info : json.as_array()) {
        battle::copilot::ActionPtr action = std::make_shared<battle::copilot::Action>();
        if (!parse_action(action_info, action.get())) {
            continue;
        }

        actions_list.emplace_back(action);
    }

    return actions_list;
}

void asst::CopilotConfig::parse_require_node(const json::value& json, asst::battle::copilot::Require::Node* _Node)
{
    LogTraceFunction;

    using Require = asst::battle::copilot::Require;

    static const std::unordered_map<std::string, Require::NodeType> NodeTypeMapping = {
        { "Not", Require::NodeType::Not },
        { "not", Require::NodeType::Not },
        { "NOT", Require::NodeType::Not },
        { "不要", Require::NodeType::Not },

        { "And", Require::NodeType::And },
        { "and", Require::NodeType::And },
        { "AND", Require::NodeType::And },
        { "并且", Require::NodeType::And },

        { "Or", Require::NodeType::Or },
        { "or", Require::NodeType::Or },
        { "OR", Require::NodeType::Or },
        { "或者", Require::NodeType::Or },

        { "Protentiality", Require::NodeType::Protentiality },
        { "protentiality", Require::NodeType::Protentiality },
        { "潜能", Require::NodeType::Protentiality },

        { "Elite", Require::NodeType::Elite },
        { "elite", Require::NodeType::Elite },
        { "精英化", Require::NodeType::Elite },

        { "Level", Require::NodeType::Level },
        { "level", Require::NodeType::Level },
        { "等级", Require::NodeType::Level },

        { "Skill", Require::NodeType::Skill },
        { "skill", Require::NodeType::Skill },
        { "技能", Require::NodeType::Skill },

        { "SkillLevel", Require::NodeType::Skill_level },
        { "skill_level", Require::NodeType::Skill_level },
        { "技能等级", Require::NodeType::Skill_level },

        { "Module", Require::NodeType::Module },
        { "module", Require::NodeType::Module },
        { "模组", Require::NodeType::Module },

        { "Trust", Require::NodeType::Trust },
        { "trust", Require::NodeType::Trust },
        { "信赖", Require::NodeType::Trust },

        { "Healthy", Require::NodeType::Healthy },
        { "healthy", Require::NodeType::Healthy },
        { "血量上限", Require::NodeType::Healthy },

        { "Attack", Require::NodeType::Attack },
        { "attack", Require::NodeType::Attack },
        { "攻击", Require::NodeType::Attack },

        { "Defence", Require::NodeType::Defence },
        { "defence", Require::NodeType::Defence },
        { "防御", Require::NodeType::Defence },
    };

    static const std::unordered_map<std::string, Require::Relation> RelationMapping = {
        { "Equal", Require::Relation::EqualTo },
        { "Equalto", Require::Relation::EqualTo },
        { "eq", Require::Relation::EqualTo },
        { "EQ", Require::Relation::EqualTo },
        { "等于", Require::Relation::EqualTo },

        { "NotEqual", Require::Relation::NotEqual },
        { "NotEqual", Require::Relation::NotEqual },
        { "neq", Require::Relation::NotEqual },
        { "NEQ", Require::Relation::NotEqual },
        { "不等于", Require::Relation::NotEqual },

        { "Greater", Require::Relation::Greater },
        { "greater", Require::Relation::Greater },
        { "GREATER", Require::Relation::Greater },
        { "gr", Require::Relation::Greater },
        { "GR", Require::Relation::Greater },
        { "多于", Require::Relation::Greater },
        { "大于", Require::Relation::Greater },

        { "GreaterThan", Require::Relation::GreaterThan },
        { "greater_than", Require::Relation::GreaterThan },
        { "GREATER_THAN", Require::Relation::GreaterThan },
        { "gt", Require::Relation::GreaterThan },
        { "GT", Require::Relation::GreaterThan },
        { "大于等于", Require::Relation::GreaterThan },

        { "Less", Require::Relation::Less },
        { "less", Require::Relation::Less },
        { "LESS", Require::Relation::Less },
        { "le", Require::Relation::Less },
        { "LE", Require::Relation::Less },
        { "少于", Require::Relation::Less },
        { "小于", Require::Relation::Less },

        { "LessThan", Require::Relation::LessThan },
        { "less_than", Require::Relation::LessThan },
        { "LESS_THAN", Require::Relation::LessThan },
        { "lt", Require::Relation::LessThan },
        { "LT", Require::Relation::LessThan },
        { "小于等于", Require::Relation::LessThan },

        { "Between", Require::Relation::Between },
        { "between", Require::Relation::Between },
        { "BETWEEM", Require::Relation::Between },
        { "bt", Require::Relation::Between },
        { "BT", Require::Relation::Between },
        { "范围", Require::Relation::Between },

        { "Is", Require::Relation::Is },
        { "is", Require::Relation::Is },
        { "IS", Require::Relation::Is },
        { "是", Require::Relation::Is },

        { "Like", Require::Relation::Like },
        { "like", Require::Relation::Like },
        { "LIKE", Require::Relation::Like },
        { "比如是", Require::Relation::Like },
        { "像是", Require::Relation::Like },
    };

    _Node->t = NodeTypeMapping.at(json.at("type").as_string());
    switch (_Node->t) {
    case Require::NodeType::Not: {
        auto p = Require::Node::create();
        parse_require_node(json.at("next"), p.get());
        _Node->next.emplace_back(p);
    } break;
    case Require::NodeType::And:
    case Require::NodeType::Or: {
        for (auto& n : json.at("next").as_array()) {
            auto p = Require::Node::create();
            parse_require_node(n, p.get());
            _Node->next.emplace_back(p);
        }
    } break;
    case Require::NodeType::Protentiality:
    case Require::NodeType::Elite:
    case Require::NodeType::Level:
    case Require::NodeType::Skill:
    case Require::NodeType::Skill_level:
    case Require::NodeType::Module:
    case Require::NodeType::Trust:
    case Require::NodeType::Healthy:
    case Require::NodeType::Attack:
    case Require::NodeType::Defence: {
        _Node->r = RelationMapping.at(json.at("relation").as_string());
        if (auto tBound = json.find("bound")) {
            _Node->range.first = tBound.value().as_integer();
        }
        else if (auto tRange = json.find("range")) {
            auto arr = tRange.value().as_array();
            _Node->range.first = arr[0].as_integer();
            _Node->range.second = arr[1].as_integer();
        }
    } break;
    default:
        break;
    }
}

bool asst::CopilotConfig::parse_require(const json::value& json, asst::battle::copilot::Require* _Req)
try {
    LogTraceFunction;

    using Require = asst::battle::copilot::Require;

    _Req->name = json.at("name").as_string();
    _Req->tips = json.get("tips", std::string());

    parse_require_node(json.at("need"), &_Req->top);

    return true;
}
catch (std::out_of_range const& _Ex) {
    Log.warn("failed to parse requires with ", _Ex.what());
    return false;
}

std::vector<asst::battle::copilot::Require> asst::CopilotConfig::parse_requires(const json::value& json)
{
    LogTraceFunction;

    std::vector<battle::copilot::Require> requires_list;

    for (const auto& action_info : json.at("requires").as_array()) {
        battle::copilot::Require r;
        if (!parse_require(action_info, &r)) {
            continue;
        }

        requires_list.emplace_back(std::move(r));
    }

    return requires_list;
}

asst::battle::RoleCounts asst::CopilotConfig::parse_role_counts(const json::value& json)
{
    battle::RoleCounts counts;
    for (const auto& [role_name, count] : json.as_object()) {
        auto role = get_role_type(role_name);
        if (role == Role::Unknown) {
            Log.error("Unknown role name: ", role_name);
            throw std::runtime_error("Unknown role name: " + role_name);
        }
        counts.emplace(role, count.as_integer());
    }
    return counts;
}

asst::battle::DeployDirection asst::CopilotConfig::string_to_direction(const std::string& str)
{
    static const std::unordered_map<std::string, DeployDirection> DeployDirectionMapping = {
        { "Right", DeployDirection::Right }, { "RIGHT", DeployDirection::Right },
        { "right", DeployDirection::Right }, { "右", DeployDirection::Right },

        { "Left", DeployDirection::Left },   { "LEFT", DeployDirection::Left },
        { "left", DeployDirection::Left },   { "左", DeployDirection::Left },

        { "Up", DeployDirection::Up },       { "UP", DeployDirection::Up },
        { "up", DeployDirection::Up },       { "上", DeployDirection::Up },

        { "Down", DeployDirection::Down },   { "DOWN", DeployDirection::Down },
        { "down", DeployDirection::Down },   { "下", DeployDirection::Down },

        { "None", DeployDirection::None },   { "NONE", DeployDirection::None },
        { "none", DeployDirection::None },   { "无", DeployDirection::None },
    };

    if (auto iter = DeployDirectionMapping.find(str); iter != DeployDirectionMapping.end()) {
        return iter->second;
    }
    else {
        return DeployDirection::Right;
    }
}
