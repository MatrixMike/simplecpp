/*
 * preprocessor library by daniel marjamäki
 */

#include "simplecpp.h"

#include <cctype>
#include <list>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
#include <cstring>
#include <sstream>
#include <iostream>
#include <stack>

using namespace simplecpp;

static const TokenString DEFINE("define");
static const TokenString DEFINED("defined");
static const TokenString IF("if");
static const TokenString IFDEF("ifdef");
static const TokenString IFNDEF("ifndef");
static const TokenString ELSE("else");
static const TokenString ELIF("elif");
static const TokenString ENDIF("endif");

TokenList::TokenList() : first(nullptr), last(nullptr) {}

TokenList::TokenList(std::istringstream &istr) : first(nullptr), last(nullptr) {
    readfile(istr);
}

TokenList::TokenList(const TokenList &other) : first(nullptr), last(nullptr) {
    *this = other;
}

TokenList::~TokenList() {
    clear();
}

void TokenList::operator=(const TokenList &other) {
    if (this == &other)
        return;
    clear();
    for (const Token *tok = other.cbegin(); tok; tok = tok->next)
        push_back(new Token(tok->str, tok->location));
}

void TokenList::clear() {
    while (first) {
        Token *next = first->next;
        delete first;
        first = next;
    }
    last = nullptr;
}

void TokenList::push_back(Token *tok) {
    if (!first)
        first = tok;
    else
        last->next = tok;
    tok->previous = last;
    last = tok;
}

void TokenList::dump() const {
    for (const Token *tok = cbegin(); tok; tok = tok->next) {
        if (tok->previous && tok->previous->location.line != tok->location.line)
            std::cout << std::endl;
        else if (tok->previous)
            std::cout << ' ';
        std::cout << tok->str;
    }
    std::cout << std::endl;
}

void TokenList::readfile(std::istream &istr)
{
    Location location;
    location.file = 0U;
    location.line = 1U;
    location.col  = 0U;
    while (istr.good()) {
        unsigned char ch = (unsigned char)istr.get();
        if (!istr.good())
            break;
        location.col = (ch == '\t') ? ((location.col + 8) & (~7)) : (location.col + 1);

        if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && istr.peek() == '\n')
                istr.get();
            ++location.line;
            location.col = 0;
            continue;
        }

        if (std::isspace(ch))
            continue;

        TokenString currentToken;

        // number or name
        if (std::isalnum(ch) || ch == '_') {
            while (istr.good() && (std::isalnum(ch) || ch == '_')) {
                currentToken += ch;
                ch = (unsigned char)istr.get();
            }
            istr.unget();
        }

        // comment
        else if (ch == '/' && istr.peek() == '/') {
            while (istr.good() && ch != '\r' && ch != '\n') {
                currentToken += ch;
                ch = (unsigned char)istr.get();
            }
            istr.unget();
        }

        // comment
        else if (ch == '/' && istr.peek() == '*') {
            while (istr.good() && !(currentToken.size() > 2U && ch == '*' && istr.peek() == '/')) {
                currentToken += ch;
                ch = (unsigned char)istr.get();
            }
            istr.unget();
        }

        // string / char literal
        else if (ch == '\"' || ch == '\'') {
            do {
                currentToken += ch;
                ch = (unsigned char)istr.get();
                if (istr.good() && ch == '\\') {
                    currentToken += ch;
                    ch = (unsigned char)istr.get();
                    currentToken += ch;
                    ch = (unsigned char)istr.get();
                }
            } while (istr.good() && ch != '\"' && ch != '\'');
            currentToken += ch;
        }

        else {
            currentToken += ch;
        }

        push_back(new Token(currentToken, location));
        location.col += currentToken.size() - 1U;
    }

    combineOperators();
}

void TokenList::constFold() {
    while (begin()) {
        // goto last '('
        Token *tok = end();
        while (tok && tok->op != '(')
            tok = tok->previous;

        // no '(', goto first token
        if (!tok)
            tok = begin();

        // Constant fold expression
        constFoldNot(tok);
        constFoldMulDivRem(tok);
        constFoldAddSub(tok);
        constFoldComparison(tok);
        constFoldLogicalOp(tok);

        // If there is no '(' we are done with the constant folding
        if (tok->op != '(')
            break;

        if (!tok->next || !tok->next->next || tok->next->next->op != ')')
            break;

        tok = tok->next;
        deleteToken(tok->previous);
        deleteToken(tok->next);
    }
}

void TokenList::combineOperators() {
    for (Token *tok = begin(); tok; tok = tok->next) {
        if (tok->op == '\0' || !tok->next || tok->next->op == '\0')
            continue;
        if (std::strchr("=!<>", tok->op) && tok->next->op == '=') {
            tok->setstr(tok->str + "=");
            deleteToken(tok->next);
        } else if ((tok->op == '|' || tok->op == '&') && tok->op == tok->next->op) {
            tok->setstr(tok->str + tok->next->str);
            deleteToken(tok->next);
        }
    }
}

void TokenList::constFoldNot(Token *tok) {
    for (; tok && tok->op != ')'; tok = tok->next) {
        if (tok->op == '!' && tok->next && tok->next->number) {
            tok->setstr(tok->next->str == "0" ? "1" : "0");
            deleteToken(tok->next);
        }
    }
}

void TokenList::constFoldMulDivRem(Token *tok) {
    for (; tok && tok->op != ')'; tok = tok->next) {
        if (!tok->previous || !tok->previous->number)
            continue;
        if (!tok->next || !tok->next->number)
            continue;

        long long result;
        if (tok->op == '*')
            result = (std::stoll(tok->previous->str) * std::stoll(tok->next->str));
        else if (tok->op == '/')
            result = (std::stoll(tok->previous->str) / std::stoll(tok->next->str));
        else if (tok->op == '%')
            result = (std::stoll(tok->previous->str) % std::stoll(tok->next->str));
        else
            continue;

        tok->setstr(std::to_string(result));
        deleteToken(tok->previous);
        deleteToken(tok->next);
    }
}

void TokenList::constFoldAddSub(Token *tok) {
    for (; tok && tok->op != ')'; tok = tok->next) {
        if (!tok->previous || !tok->previous->number)
            continue;
        if (!tok->next || !tok->next->number)
            continue;

        long long result;
        if (tok->op == '+')
            result = (std::stoll(tok->previous->str) + std::stoll(tok->next->str));
        else if (tok->op == '-')
            result = (std::stoll(tok->previous->str) - std::stoll(tok->next->str));
        else
            continue;

        tok->setstr(std::to_string(result));
        deleteToken(tok->previous);
        deleteToken(tok->next);
    }
}

void TokenList::constFoldComparison(Token *tok) {
    for (; tok && tok->op != ')'; tok = tok->next) {
        if (!std::strchr("<>=!", tok->str[0]))
            continue;
        if (!tok->previous || !tok->previous->number)
            continue;
        if (!tok->next || !tok->next->number)
            continue;

        int result;
        if (tok->str == "==")
            result = (std::stoll(tok->previous->str) == std::stoll(tok->next->str));
        else if (tok->str == "!=")
            result = (std::stoll(tok->previous->str) != std::stoll(tok->next->str));
        else if (tok->str == ">")
            result = (std::stoll(tok->previous->str) > std::stoll(tok->next->str));
        else if (tok->str == ">=")
            result = (std::stoll(tok->previous->str) >= std::stoll(tok->next->str));
        else if (tok->str == "<")
            result = (std::stoll(tok->previous->str) < std::stoll(tok->next->str));
        else if (tok->str == "<=")
            result = (std::stoll(tok->previous->str) <= std::stoll(tok->next->str));
        else
            continue;

        tok->setstr(std::to_string(result));
        deleteToken(tok->previous);
        deleteToken(tok->next);
    }
}

void TokenList::constFoldLogicalOp(Token *tok) {
    for (; tok && tok->op != ')'; tok = tok->next) {
        if (tok->str != "&&" && tok->str != "||")
            continue;
        if (!tok->previous || !tok->previous->number)
            continue;
        if (!tok->next || !tok->next->number)
            continue;

        int result;
        if (tok->str == "||")
            result = (std::stoll(tok->previous->str) || std::stoll(tok->next->str));
        else if (tok->str == "&&")
            result = (std::stoll(tok->previous->str) && std::stoll(tok->next->str));
        else
            continue;

        tok->setstr(std::to_string(result));
        deleteToken(tok->previous);
        deleteToken(tok->next);
    }
}


namespace simplecpp {
class Macro {
public:
    Macro() : nameToken(nullptr) {}

    explicit Macro(const Token *tok) : nameToken(nullptr) {
        if (tok->previous && tok->previous->location.line == tok->location.line)
            throw std::runtime_error("bad macro syntax");
        if (tok->op != '#')
            throw std::runtime_error("bad macro syntax");
        tok = tok->next;
        if (!tok || tok->str != DEFINE)
            throw std::runtime_error("bad macro syntax");
        tok = tok->next;
        if (!tok || !tok->name)
            throw std::runtime_error("bad macro syntax");
        parseDefine(tok);
    }

    explicit Macro(const std::string &name, const std::string &value) : nameToken(nullptr) {
        const std::string def(name + ' ' + value);
        std::istringstream istr(def);
        tokenListDefine.readfile(istr);
        parseDefine(tokenListDefine.cbegin());
    }

    Macro(const Macro &macro) {
        *this = macro;
    }

    void operator=(const Macro &macro) {
        if (this != &macro) {
            if (macro.tokenListDefine.empty())
                parseDefine(macro.nameToken);
            else {
                tokenListDefine = macro.tokenListDefine;
                parseDefine(tokenListDefine.cbegin());
            }
        }
    }

    const Token * expand(TokenList * const output, const Location &loc, const Token * const nameToken, const std::map<TokenString,Macro> &macros, std::set<TokenString> expandedmacros) const {
        const std::set<TokenString> expandedmacros1(expandedmacros);
        expandedmacros.insert(nameToken->str);

        if (args.empty()) {
            Token * const token1 = output->end();
            for (const Token *macro = valueToken; macro != endToken;) {
                const std::map<TokenString, Macro>::const_iterator it = macros.find(macro->str);
                if (it != macros.end() && expandedmacros.find(macro->str) == expandedmacros.end()) {
                    macro = it->second.expand(output, loc, macro, macros, expandedmacros);
                } else {
                    output->push_back(newMacroToken(macro->str, loc, false));
                    macro = macro->next;
                }
            }
            setMacroName(output, token1, expandedmacros1);
            return nameToken->next;
        }

        // Parse macro-call
        const std::vector<const Token*> parametertokens(getMacroParameters(nameToken));
        if (parametertokens.size() != args.size() + 1U) {
            // wrong number of parameters => don't expand
            output->push_back(newMacroToken(nameToken->str, loc, false));
            return nameToken->next;
        }

        // expand
        for (const Token *tok = valueToken; tok != endToken;) {
            if (tok->op != '#') {
                tok = expandToken(output, loc, tok, macros, expandedmacros1, expandedmacros, parametertokens);
                continue;
            }

            tok = tok->next;
            if (tok->op == '#') {
                // A##B => AB
                Token *A = output->end();
                if (!A)
                    throw std::runtime_error("invalid ##");
                tok = expandToken(output, loc, tok->next, macros, expandedmacros1, expandedmacros, parametertokens);
                Token *next = A->next;
                if (!next)
                    throw std::runtime_error("invalid ##");
                A->setstr(A->str + A->next->str);
                A->flags();
                output->deleteToken(A->next);
            } else {
                // #123 => "123"
                TokenList tokenListHash;
                tok = expandToken(&tokenListHash, loc, tok, macros, expandedmacros1, expandedmacros, parametertokens);
                std::string s;
                for (const Token *hashtok = tokenListHash.cbegin(); hashtok; hashtok = hashtok->next)
                    s += hashtok->str;
                output->push_back(newMacroToken('\"' + s + '\"', loc, expandedmacros1.empty()));
            }
        }

        return parametertokens[args.size()]->next;
    }

    TokenString name() const {
        return nameToken->str;
    }

private:
    Token *newMacroToken(const TokenString &str, const Location &loc, bool rawCode) const {
        Token *tok = new Token(str,loc);
        if (!rawCode)
            tok->macro = nameToken->str;
        return tok;
    }

    void setMacroName(TokenList *output, Token *token1, const std::set<std::string> &expandedmacros1) const {
        if (!expandedmacros1.empty())
            return;
        for (Token *tok = token1 ? token1->next : output->begin(); tok; tok = tok->next) {
            if (!tok->macro.empty())
                tok->macro = nameToken->str;
        }
    }

    void parseDefine(const Token *nametoken) {
        nameToken = nametoken;
        if (!nameToken) {
            valueToken = endToken = nullptr;
            args.clear();
            return;
        }

        // function like macro..
        if (nameToken->next &&
                nameToken->next->op == '(' &&
                nameToken->location.line == nameToken->next->location.line &&
                nameToken->next->location.col == nameToken->location.col + nameToken->str.size()) {
            args.clear();
            const Token *argtok = nameToken->next->next;
            while (argtok && argtok->op != ')') {
                if (argtok->op != ',')
                    args.push_back(argtok->str);
                argtok = argtok->next;
            }
            valueToken = argtok->next;
        } else {
            args.clear();
            valueToken = nameToken->next;
        }

        if (valueToken && valueToken->location.line != nameToken->location.line)
            valueToken = nullptr;
        endToken = valueToken;
        while (endToken && endToken->location.line == nameToken->location.line)
            endToken = endToken->next;
    }

    unsigned int getArgNum(const TokenString &str) const {
        unsigned int par = 0;
        while (par < args.size()) {
            if (str == args[par])
                return par;
            par++;
        }
        return ~0U;
    }

    std::vector<const Token *> getMacroParameters(const Token *nameToken) const {
        if (!nameToken->next || nameToken->next->op != '(')
            return std::vector<const Token *>();

        std::vector<const Token *> parametertokens;
        parametertokens.push_back(nameToken->next);
        unsigned int par = 0U;
        for (const Token *tok = nameToken->next->next; tok; tok = tok->next) {
            if (tok->op == '(')
                ++par;
            else if (tok->op == ')') {
                if (par == 0U) {
                    parametertokens.push_back(tok);
                    break;
                }
                --par;
            }
            else if (par == 0U && tok->op == ',')
                parametertokens.push_back(tok);
        }
        return parametertokens;
    }

    const Token *expandToken(TokenList *output, const Location &loc, const Token *tok, const std::map<TokenString,Macro> &macros, std::set<TokenString> expandedmacros1, std::set<TokenString> expandedmacros, const std::vector<const Token*> &parametertokens) const {
        // Not name..
        if (!tok->name) {
            output->push_back(newMacroToken(tok->str, loc, false));
            return tok->next;
        }

        // Not macro parameter..
        const unsigned int par = getArgNum(tok->str);
        if (par >= args.size()) {
            // Macro..
            const std::map<TokenString, Macro>::const_iterator it = macros.find(tok->str);
            if (it != macros.end() && expandedmacros1.find(tok->str) == expandedmacros1.end())
                return it->second.expand(output, loc, tok, macros, expandedmacros);

            output->push_back(newMacroToken(tok->str, loc, false));
            return tok->next;
        }

        // Expand parameter..
        for (const Token *partok = parametertokens[par]->next; partok != parametertokens[par+1];) {
            const std::map<TokenString, Macro>::const_iterator it = macros.find(partok->str);
            if (it != macros.end() && expandedmacros1.find(partok->str) == expandedmacros1.end())
                partok = it->second.expand(output, loc, partok, macros, expandedmacros);
            else {
                output->push_back(newMacroToken(partok->str, loc, expandedmacros1.empty()));
                partok = partok->next;
            }
        }

        return tok->next;
    }

    void setMacro(Token *tok) const {
        while (tok) {
            if (!tok->macro.empty())
                tok->macro = nameToken->str;
            tok = tok->next;
        }
    }

    const Token *nameToken;
    std::vector<TokenString> args;
    const Token *valueToken;
    const Token *endToken;
    TokenList tokenListDefine;
};
}

static bool sameline(const Token *tok1, const Token *tok2) {
    return (tok1 && tok2 && tok1->location.line == tok2->location.line);
}

static void simplifySizeof(TokenList &expr) {
    for (Token *tok = expr.begin(); tok; tok = tok->next) {
        if (tok->str != "sizeof")
            continue;
        Token *tok1 = tok->next;
        Token *tok2 = tok1->next;
        if (tok1->op == '(') {
            while (tok2->op != ')')
                tok2 = tok2->next;
            tok2 = tok2->next;
        }

        unsigned int sz = 0;
        for (Token *typeToken = tok1; typeToken != tok2; typeToken = typeToken->next) {
            if (typeToken->str == "char")
                sz = sizeof(char);
            if (typeToken->str == "short")
                sz = sizeof(short);
            if (typeToken->str == "int")
                sz = sizeof(int);
            if (typeToken->str == "long")
                sz = sizeof(long);
            if (typeToken->str == "float")
                sz = sizeof(float);
            if (typeToken->str == "double")
                sz = sizeof(double);
        }

        tok->setstr(std::to_string(sz));

        while (tok->next != tok2)
            expr.deleteToken(tok->next);
    }
}

static void simplifyName(TokenList &expr) {
    for (Token *tok = expr.begin(); tok; tok = tok->next) {
        if (tok->name)
            tok->setstr("0");
    }
}

static void simplifyNumbers(TokenList &expr) {
    for (Token *tok = expr.begin(); tok; tok = tok->next) {
        if (tok->str.size() == 1U)
            continue;
        if (tok->str.compare(0,2,"0x") == 0)
            tok->setstr(std::to_string(std::stoll(tok->str.substr(2), nullptr, 16)));
        else if (tok->str[0] == '\'')
            tok->setstr(std::to_string((unsigned char)tok->str[1]));
    }
}


static int evaluate(TokenList expr) {
    simplifySizeof(expr);
    simplifyName(expr);
    simplifyNumbers(expr);
    expr.constFold();
    return expr.cbegin() ? std::stoi(expr.cbegin()->str) : 0;
}

static const Token *gotoNextLine(const Token *tok) {
    const unsigned int line = tok->location.line;
    while (tok && tok->location.line == line)
        tok = tok->next;
    return tok;
}

TokenList Preprocessor::preprocess(const TokenList &rawtokens, const std::map<std::string,std::string> &defines)
{
    std::map<TokenString, Macro> macros;
    for (std::map<std::string,std::string>::const_iterator it = defines.begin(); it != defines.end(); ++it) {
        const Macro macro(it->first, it->second.empty() ? std::string("1") : it->second);
        macros[macro.name()] = macro;
    }

    // TRUE => code in current #if block should be kept
    // ELSE_IS_TRUE => code in current #if block should be dropped. the code in the #else should be kept.
    // ALWAYS_FALSE => drop all code in #if and #else
    enum IfState { TRUE, ELSE_IS_TRUE, ALWAYS_FALSE };
    std::stack<IfState> ifstates;
    ifstates.push(TRUE);

    TokenList output;
    for (const Token *rawtok = rawtokens.cbegin(); rawtok;) {
        if (rawtok->op == '#' && !sameline(rawtok->previous, rawtok)) {
            rawtok = rawtok->next;
            if (!rawtok || !rawtok->name)
                continue;

            if (rawtok->str == DEFINE) {
                if (ifstates.top() != TRUE)
                    continue;
                try {
                    const Macro &macro = Macro(rawtok->previous);
                    macros[macro.name()] = macro;
                } catch (const std::runtime_error &) {
                }
            } else if (rawtok->str == IF || rawtok->str == IFDEF || rawtok->str == IFNDEF || rawtok->str == ELIF) {
                bool conditionIsTrue;
                if (ifstates.top() == ALWAYS_FALSE)
                    conditionIsTrue = false;
                else if (rawtok->str == IFDEF)
                    conditionIsTrue = (macros.find(rawtok->next->str) != macros.end());
                else if (rawtok->str == IFNDEF)
                    conditionIsTrue = (macros.find(rawtok->next->str) == macros.end());
                else if (rawtok->str == IF || rawtok->str == ELIF) {
                    TokenList expr;
                    const Token * const endToken = gotoNextLine(rawtok);
                    for (const Token *tok = rawtok->next; tok != endToken; tok = tok->next) {
                        if (!tok->name) {
                            expr.push_back(new Token(tok->str,tok->location));
                            continue;
                        }

                        if (tok->str == DEFINED) {
                            tok = tok->next;
                            const bool par = (tok && tok->op == '(');
                            if (par)
                                tok = tok->next;
                            if (!tok)
                                break;
                            if (macros.find(tok->str) != macros.end())
                                expr.push_back(new Token("1", tok->location));
                            else
                                expr.push_back(new Token("0", tok->location));
                            if (tok && par)
                                tok = tok->next;
                            continue;
                        }

                        const std::map<std::string,Macro>::const_iterator it = macros.find(tok->str);
                        if (it != macros.end()) {
                            TokenList value;
                            std::set<TokenString> expandedmacros;
                            it->second.expand(&value, tok->location, tok, macros, expandedmacros);
                            for (const Token *tok2 = value.cbegin(); tok2; tok2 = tok2->next)
                                expr.push_back(new Token(tok2->str, tok->location));
                        } else {
                            expr.push_back(new Token(tok->str, tok->location));
                        }
                    }
                    conditionIsTrue = evaluate(expr);
                }

                if (rawtok->str != ELIF) {
                    // push a new ifstate..
                    if (ifstates.top() != TRUE)
                        ifstates.push(ALWAYS_FALSE);
                    else
                        ifstates.push(conditionIsTrue ? TRUE : ELSE_IS_TRUE);
                } else if (ifstates.top() == TRUE) {
                    ifstates.top() = ALWAYS_FALSE;
                } else if (ifstates.top() == ELSE_IS_TRUE && conditionIsTrue) {
                    ifstates.top() = TRUE;
                }
            } else if (rawtok->str == ELSE) {
                ifstates.top() = (ifstates.top() == ELSE_IS_TRUE) ? TRUE : ALWAYS_FALSE;
            } else if (rawtok->str == ENDIF) {
                if (ifstates.size() > 1U)
                    ifstates.pop();
            }
            rawtok = gotoNextLine(rawtok);
            if (!rawtok)
                break;
            continue;
        }

        if (ifstates.top() != TRUE) {
            // drop code
            rawtok = gotoNextLine(rawtok);
            continue;
        }

        if (macros.find(rawtok->str) != macros.end()) {
            std::map<TokenString,Macro>::const_iterator macro = macros.find(rawtok->str);
            if (macro != macros.end()) {
                std::set<TokenString> expandedmacros;
                rawtok = macro->second.expand(&output,rawtok->location,rawtok,macros,expandedmacros);
                continue;
            }
        }

        output.push_back(new Token(*rawtok));
        rawtok = rawtok->next;
    }
    return output;
}