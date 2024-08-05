#include "CopilotConfig.h"

#include <regex>

#include <meojson/json.hpp>

#include "TilePack.h"
#include "Utils/Logger.hpp"
#include "Utils/NoWarningCPR.h"

using namespace asst::battle;
using namespace asst::battle::copilot;

namespace requirement_expression
{

bool starts_with(const std::wstring& str, const std::wstring& prefix)
{
    if (str.length() < prefix.length()) {
        // 如果str的长度小于prefix的长度，则str不可能以prefix开始
        return false;
    }

    return str.substr(0, prefix.length()) == prefix;
}

class Variable
{
public:
    Variable(int _LineSeq, std::wstring _Name) :
        m_iLineNum__(_LineSeq),
        m_strName__(_Name)
    {
    }

    friend std::wistream& operator>>(std::wistream& in, Variable& a);
    friend std::wostream& operator<<(std::wostream& os, Variable& a);

    int m_iLineNum__;
    std::wstring m_strName__;
};

inline std::wistream& operator>>(std::wistream& in, Variable& a)
{
    in >> a.m_iLineNum__ >> a.m_strName__;
    return in;
}

inline std::wostream& operator<<(std::wostream& os, Variable& a)
{
    os << typeid(a).name() << std::endl << a.m_iLineNum__ << std::endl << a.m_strName__ << std::endl;
    return os;
}

// 语法基类
class Statement
{
public:
    using ptr = std::shared_ptr<Statement>;
    virtual ~Statement() = default;
};

// 源码树
class SourceCode
{
public:
    SourceCode() = default;

    SourceCode(int _LineNum, std::vector<Statement::ptr> _States) :
        m_iLineNum__(_LineNum),
        m_vecStatements__(_States)
    {
    }

    friend std::istream& operator>>(std::istream& in, SourceCode& a);
    friend std::ostream& operator<<(std::ostream& os, SourceCode& a);

    int m_iLineNum__;
    std::vector<Statement::ptr> m_vecStatements__;
};

inline std::istream& operator>>(std::istream& in, SourceCode& a)
{
    in >> a.m_iLineNum__;
    return in;
}

inline std::ostream& operator<<(std::ostream& os, SourceCode& a)
{
    os << typeid(a).name() << std::endl << a.m_iLineNum__ << std::endl;
    return os;
}

// 词法解析
class lexer_exception : public std::exception
{
public:
    using exception::exception;
};

enum class token_type
{
    E_TOKEN_EOF = 0,     // 文件的结束符
    E_TOKEN_LEFT_PAREN,  // 左括号
    E_TOKEN_RIGHT_PAREN, // 右括号
    E_TOKEN_EQUAL,
    E_TOKEN_NO_EQUAL,
    E_TOKEN_GREAT,
    E_TOKEN_GREAT_THAN,
    E_TOKEN_LESS,
    E_TOKEN_LESS_THAN,
    E_TOKEN_QUOTE,    // 单引号
    E_TOKEN_DUOQUOTE, // 双引号
    E_TOKEN_NAME,
    E_TOKEN_IGNORED,

    E_TOKEN_BUTT
};

class token_info
{
public:
    token_info() :
        m_iLineNum__(-1),
        m_enTypeToken__(token_type::E_TOKEN_BUTT)
    {
    }

    token_info(int _line, token_type _type, std::string _token) :
        m_iLineNum__(_line),
        m_enTypeToken__(_type),
        m_strToken__(_token)
    {
    }

    token_info(token_info&& _Other) noexcept :
        m_iLineNum__(_Other.m_iLineNum__),
        m_enTypeToken__(_Other.m_enTypeToken__),
        m_strToken__(std::move(_Other.m_strToken__))
    {
        _Other.reset();
    }

    token_info(token_info const& _Other) :
        m_iLineNum__(_Other.m_iLineNum__),
        m_enTypeToken__(_Other.m_enTypeToken__),
        m_strToken__(_Other.m_strToken__)
    {
    }

    token_info& operator=(token_info&& _Other) noexcept
    {
        m_iLineNum__ = _Other.m_iLineNum__;
        m_enTypeToken__ = _Other.m_enTypeToken__;
        m_strToken__ = std::move(_Other.m_strToken__);
        _Other.reset();

        return *this;
    }

    token_info& operator=(token_info const& _Other)
    {
        if (this == &_Other) {
            return *this;
        }
        m_iLineNum__ = _Other.m_iLineNum__;
        m_enTypeToken__ = _Other.m_enTypeToken__;
        m_strToken__ = _Other.m_strToken__;

        return *this;
    }

    void show()
    {
        std::cout << "m_iLineNum__:" << m_iLineNum__ << " m_enTypeToken__:" << (int)m_enTypeToken__
                  << " m_strToken__:" << m_strToken__ << std::endl;
    }

    bool empty() const { return m_iLineNum__ == -1; }

    void reset()
    {
        m_iLineNum__ = -1;
        m_enTypeToken__ = token_type::E_TOKEN_BUTT;
        m_strToken__.clear();
    }

    bool matchType(token_type _Type) const { return m_enTypeToken__ == _Type; }

    bool matchType(token_info&& _Rhs) const { return m_enTypeToken__ == _Rhs.m_enTypeToken__; }

    bool matchType(token_info const& _Rhs) const { return m_enTypeToken__ == _Rhs.m_enTypeToken__; }

    token_type type() const { return m_enTypeToken__; }

    int num() const { return m_iLineNum__; }

    std::string token() { return m_strToken__; }

private:
    int m_iLineNum__;
    token_type m_enTypeToken__;
    std::string m_strToken__;
};

class Lexer
{
public:
    using String_T = std::wstring;

    Lexer(String_T const& _Source) :
        source(_Source),
        idx_head(0)
    {
    }

    bool nextSource(String_T const& _Prefix) const
    {
        if (_Prefix.empty()) {
            return source.empty() ? true : false;
        }

        return starts_with(source, _Prefix);
    }

    bool finished() const { return idx_head >= source.size(); }

    auto source_now() const -> String_T { return source.substr(idx_head); }

    auto scan_pattern(std::regex&& _Pattern) -> String_T
    {
        std::wsmatch resMatch;
        auto result = std::regex_search(source.substr(idx_head), resMatch, _Pattern, std::regex_constants::match_any);
        if (!result) {
            throw lexer_exception("returned unexpected result");
        }

        return resMatch[0];
    }

    std::wstring scanName() { return scan_pattern(std::regex("^[_a-zA-Z][_a-zA-Z0-9]*")); }

    std::wstring scanIgnored() { return scan_pattern(std::regex("^[\t\n\v\f\r ]+")); }

    std::string scanBeforeToken(std::string _token)
    {
        auto res = StringUtil::split(sourceNow(), _token);
        if (res.empty()) {
            throw lexer_exception("miss token");
        }
        m_iHead += res[0].size();
        processNewLine(res[0]);
        return res[0];
    }

    void processNewLine(std::wstring _Ignored)
    {
        std::size_t i = 0;
        while (i < _Ignored.size()) {
            if (_Ignored.substr(i, 2) == L"\r\n" || _Ignored.substr(i, 2) == L"\n\r") {
                i += 2;
                idx_cursor++;
            }
            else {
                switch (_Ignored[i]) {
                case '\r':
                case '\n':
                    idx_cursor++;
                    break;
                default:
                    break;
                }
                i++;
            }
        }
    }

    auto get_next_token() -> token_info
    {
        static std::map<std::wstring, token_type> s_mapKeyWordsTbl = { { "print", token_type::E_TOKEN_PRINT } };

        if (!token_next.empty()) {
            return std::move(token_next);
        }

        if (finished()) {
            return token_info(0, token_type::E_TOKEN_EOF, "EOF");
        }

        auto cNext = source_now()[0];

        switch (cNext) {
        case L'$':
            m_iHead++;
            return token_info(m_iLineNum, token_type::E_TOKEN_VAR_PREFIX, "$");
        case L'(':
            m_iHead++;
            return token_info(m_iLineNum, token_type::E_TOKEN_LEFT_PAREN, "(");
        case L')':
            m_iHead++;
            return token_info(m_iLineNum, token_type::E_TOKEN_RIGHT_PAREN, ")");
        case L'=':
            m_iHead++;
            return token_info(m_iLineNum, token_type::E_TOKEN_EQUAL, "=");
        case L'\"':
            if (nextSource("\"\"")) {
                m_iHead += 2;
                return token_info(m_iLineNum, token_type::E_TOKEN_DUOQUOTE, "\"\"");
            }
            m_iHead++;
            return token_info(m_iLineNum, token_type::E_TOKEN_QUOTE, "\"");
        case L'\t':
        case L'\n':
        case L'\v':
        case L'\f':
        case L'\r':
        case L' ': {
            auto strIgnored = scanIgnored();
            auto oldLine = m_iLineNum;
            m_iHead += strIgnored.size();
            processNewLine(strIgnored);
            return token_info(oldLine, token_type::E_TOKEN_IGNORED, strIgnored);
        }

        default:
            break;
        }

        if (cNext == '_' || std::isalpha(cNext)) {
            auto strName = scanName();
            auto itFind = s_mapKeyWordsTbl.find(strName);
            if (itFind != s_mapKeyWordsTbl.end()) {
                m_iHead += strName.size();
                return token_info(m_iLineNum, itFind->second, strName);
            }

            m_iHead += strName.size();
            return token_info(m_iLineNum, token_type::E_TOKEN_NAME, strName);
        }

        throw lexer_exception("unexpected symbol");
    }

    token_info nextToken(token_type _Guess)
    {
        auto myInfo = get_next_token();
        if (!myInfo.matchType(_Guess)) {
            throw lexer_exception("syntax error near , expecting ");
        }

        return myInfo;
    }

    token_type lookAhead()
    {
        if (m_instNextToken__.empty()) {
            m_instNextToken__ = getNextToken();
        }
        return m_instNextToken__.type();
    }

    std::wstring source;
    size_t idx_head = 0;
    size_t idx_cursor = 0;
    token_info token_next;
};

class parser_exception : public std::exception
{
public:
    using exception::exception;
};

class Parser
{
public:
    Parser(Lexer&& _Lex) :
        m_istLex__(_Lex)
    {
    }

    Parser(Lexer& _Lex) :
        m_istLex__(_Lex)
    {
    }

    SourceCode parse()
    {
        std::vector<Statement::ptr> vecStates;
        auto iLineNum = m_istLex__.m_iLineNum;
        while (m_istLex__.lookAhead() != token_type::E_TOKEN_EOF) {
            vecStates.push_back(__parseStatement());
        }

        return SourceCode(iLineNum, vecStates);
    }

private:
    void __parseIgnored()
    {
        if (m_istLex__.lookAhead() == token_type::E_TOKEN_IGNORED) {
            m_istLex__.nextToken(token_type::E_TOKEN_IGNORED);
        }
    }

    // $ + xxx
    Variable __parseVariable()
    {
        auto numLine = m_istLex__.nextToken(token_type::E_TOKEN_VAR_PREFIX).num();
        auto strName = m_istLex__.nextToken(token_type::E_TOKEN_NAME).token();
        __parseIgnored();

        return Variable(numLine, strName);
    }

    // " + xxx + "
    std::string __parseString()
    {
        if (m_istLex__.lookAhead() == token_type::E_TOKEN_DUOQUOTE) {
            m_istLex__.nextToken(token_type::E_TOKEN_DUOQUOTE);
            return "";
        }

        m_istLex__.nextToken(token_type::E_TOKEN_QUOTE);
        auto strStr = m_istLex__.scanBeforeToken("\"");
        m_istLex__.nextToken(token_type::E_TOKEN_QUOTE);
        return strStr;
    }

    // var + ig + = + ig + string + ig
    Assignment::ptr __parseAssignment()
    {
        auto var = __parseVariable();
        __parseIgnored();
        m_istLex__.nextToken(token_type::E_TOKEN_EQUAL);
        __parseIgnored();
        auto strStr = __parseString();
        __parseIgnored();

        return std::make_shared<Assignment>(var.m_iLineNum__, var, strStr);
    }

    // print + ( + ig + var + ) + ig
    Print::ptr __parsePrint()
    {
        auto line_num = m_istLex__.nextToken(token_type::E_TOKEN_PRINT).num();
        m_istLex__.nextToken(token_type::E_TOKEN_LEFT_PAREN);
        __parseIgnored();
        auto variable = __parseVariable();
        __parseIgnored();
        m_istLex__.nextToken(token_type::E_TOKEN_RIGHT_PAREN);
        __parseIgnored();

        return std::make_shared<Print>(line_num, variable);
    }

    Statement::ptr __parseStatement()
    {
        if (m_istLex__.lookAhead() == token_type::E_TOKEN_PRINT) {
            return __parsePrint();
        }

        if (m_istLex__.lookAhead() == token_type::E_TOKEN_VAR_PREFIX) {
            return __parseAssignment();
        }

        throw parser_exception("unexpected token");
    }

    Lexer& m_istLex__;
};

// 解释器
class interpreter_exception : public std::runtime_error
{
public:
    using runtime_error::runtime_error;
};

class Interpreter
{
public:
    Interpreter() = default;

    Interpreter(std::string _Source) :
        m_ast(Parser(Lexer(_Source)).parse())
    {
    }

    void execute() { __resolveSourceCode(); }

    void execute(std::string const& _Source)
    {
        m_ast = SourceCode(Parser(Lexer(_Source)).parse());
        m_variables.clear();

        execute();
    }

    bool hadVar(std::string _name) { return m_variables.find(_name) != m_variables.end(); }

    bool getVar(std::string const _name, std::string& _Var)
    {
        auto itFind = m_variables.find(_name);
        if (itFind == m_variables.end()) {
            return false;
        }

        _Var = itFind->second;
        return true;
    }

private:
    void __resolvePrint(Print::ptr _Statement) { std::cout << m_variables[_Statement->m_istVar__.m_strName__]; }

    void __resolveAssignment(Assignment::ptr _Statement)
    {
        m_variables[_Statement->m_istVar__.m_strName__] = _Statement->m_strStr__;
    }

    void __resolveStatement(Statement::ptr _Statement)
    {
        using std::static_pointer_cast;
        if (dynamic_cast<Print*>(_Statement.get())) {
            __resolvePrint(static_pointer_cast<Print>(_Statement));
        }
        else if (dynamic_cast<Assignment*>(_Statement.get())) {
            __resolveAssignment(static_pointer_cast<Assignment>(_Statement));
        }
        else {
            throw interpreter_exception(": unexpected statement type");
        }
    }

    void __resolveSourceCode()
    {
        for (auto state : m_ast.m_vecStatements__) {
            __resolveStatement(state);
        }
    }

    SourceCode m_ast;
    std::map<std::string, std::string> m_variables;
};

class Generator
{
public:
    Generator() :
        m_ossResult__(std::ios::app)
    {
    }

    explicit Generator(std::string const& _Src) :
        m_ossResult__(_Src, std::ios::app)
    {
    }

    Generator(std::string&& _Src) :
        Generator(_Src)
    {
    }

    Generator& setVar(std::string _Name, std::string _Var)
    {
        m_ossResult__ << '$' << _Name << " = \"" << _Var << "\" ";

        return *this;
    }

    Generator& setPrint(std::string _Name)
    {
        m_ossResult__ << " print( " << _Name << ')';

        return *this;
    }

    std::string getResult()
    {
        auto res = m_ossResult__.str();
        clr();
        return res;
    }

    void clr() { m_ossResult__.str(""); }

private:
    std::ostringstream m_ossResult__;
};
}

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
