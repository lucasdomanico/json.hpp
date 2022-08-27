//      ____. _________________    _______   
//     |    |/   _____/\_____  \   \      \  
//     |    |\_____  \  /   |   \  /   |   \ 
// /\__|    |/        \/    |    \/    |    \
// \________/_______  /\_______  /\____|__  /
//                  \/         \/         \/ 
// a toy Parsing Expression Grammar JSON encoder and decoder for hobby projects
// Copyright: 2022 Lucas Domanico <https://github.com/lucasdomanico/json.hpp>
// License: MIT

// TODO: char32_t strings

#ifndef JSON_HPP
#define JSON_HPP

#include <string>
#include <vector>
#include <memory>

// -----------------------------------
//             PUBLIC API             
// -----------------------------------

namespace json {
    template <typename T>
    class hash { // an std::map-like container with insertion order
        std::vector<std::pair<std::string, T>> v;
        public:
            hash();
            hash(std::initializer_list<std::pair<std::string, T>> il);
            std::vector<std::pair<std::string, T>>& vector();
            bool has(const std::string& key) const;
            T& operator [] (const std::string& key);
    };
    struct value {
        std::string type;
        bool boolean;
        double number;
        std::string string;
        std::vector<std::shared_ptr<value>> array;
        hash<std::shared_ptr<value>> object;
    };
    std::shared_ptr<value> boolean(bool boolean);
    std::shared_ptr<value> number(double number);
    std::shared_ptr<value> string(const std::string& string);
    std::shared_ptr<value> array(const std::vector<std::shared_ptr<value>>& array);
    std::shared_ptr<value> object(const hash<std::shared_ptr<value>>& object);
    struct decoded {
        int error;
        std::shared_ptr<value> value;
    };
    decoded decode(const std::string& s);
    std::string encode(const std::shared_ptr<value>& v);
};

// --------------------------------------------------------
// ---------------- PRIVATE IMPLEMENTATION ----------------
// --------------------------------------------------------

namespace json {
    template <typename T>
    hash<T>::hash() {}
    template <typename T>
    hash<T>::hash(std::initializer_list<std::pair<std::string, T>> il) {
        for(auto& e : il) {
            (*this)[e.first] = e.second;
        }
    }
    template <typename T>
    std::vector<std::pair<std::string, T>>& hash<T>::vector() { return v; }
    template <typename T>
    bool hash<T>::has(const std::string& key) const {
        for(auto& e : v) {
            if(e.first == key) {
                return true;
            }
        }
        return false;
    }
    template <typename T>
    T& hash<T>::operator [] (const std::string& key) {
        for(auto& e : v) {
            if(e.first == key) {
                return e.second;
            }
        }
        v.push_back(std::pair<std::string, T>(key, T()));
        return v[v.size() - 1].second;
    }

    std::shared_ptr<value> boolean(bool boolean) {
        std::shared_ptr<value> v = std::shared_ptr<value>(new value());
        v->type = "boolean";
        v->boolean = boolean;
        return v;
    }
    std::shared_ptr<value> number(double number) {
        std::shared_ptr<value> v = std::shared_ptr<value>(new value());
        v->type = "number";
        v->number = number;
        return v;
    }
    std::shared_ptr<value> string(const std::string& string) {
        std::shared_ptr<value> v = std::shared_ptr<value>(new value());
        v->type = "string";
        v->string = string;
        return v;
    }
    std::shared_ptr<value> array(const std::vector<std::shared_ptr<value>>& array) {
        std::shared_ptr<value> v = std::shared_ptr<value>(new value());
        v->type = "array";
        v->array = array;
        return v;
    }
    std::shared_ptr<value> object(const hash<std::shared_ptr<value>>& object) {
        std::shared_ptr<value> v = std::shared_ptr<value>(new value());
        v->type = "object";
        v->object = object;
        return v;
    }
};

#include <iostream>
#include <functional>
#include <regex>
#include <sstream>

namespace json_internals {
    typedef std::string str;
    template<typename T> using vector = std::vector<T>;
    template<typename T> using fun =    std::function<T>;
    template<typename T> using ptr =    std::shared_ptr<T>;

    struct ast {
        int pos;
        int length;
        int error;
        str tag;
        vector<ptr<ast>> data;
        str text;
        ast(int pos, int length, int error, str tag, const vector<ptr<ast>>& data, str text) {
            this->pos = pos;
            this->length = length;
            this->error = error;
            this->tag = tag;
            this->data = data;
            this->text = text;
        }
    };
    ptr<ast> fail(int pos, int error, str tag) {
        return ptr<ast>(new ast(pos, -1, error, tag, {}, ""));
    }
    bool isfail(ptr<ast> ast) {
        return ast->length == -1;
    }

    typedef fun<ptr<ast>(const char* src, int pos)> rule;

    rule token(const str& t, const str& tag = "") {
        return [=](auto s, auto i) {
            s += i;
            str cmp = str(s, 0, t.size());
            if(t.compare(cmp) == 0) {
                return ptr<ast>(new ast(i, t.size(), i, tag, {}, t));
            }
            return fail(i, i, tag);
        };
    }
    rule regex(const str& re, const str& tag = "") { // too slow
        std::regex regexp(re);
        return [=](auto s, auto i) {
            s += i;
            std::cmatch m;
            if(regex_search(s, m, regexp)) {
                str r = m.str(0);
                return ptr<ast>(new ast(i, r.size(), i, tag, {}, r));
            }
            return fail(i, i, tag);
        };
    }
    rule all(const vector<rule>& rules, const str& tag = "") {
        return [=](auto s, auto i) {
            int pos = i;
            int error = i;
            vector<ptr<ast>> data;
            for(auto& p : rules) {
                ptr<ast> a = p(s, i);
                if(isfail(a)) {
                    data.push_back(a); // fixed
                    for(auto& e : data) {
                        if(e->error > error) error = e->error;
                    } // end fixed
                    return fail(a->pos, error, a->tag);
                }
                i += a->length;
                data.push_back(a);
            }
            return ptr<ast>(new ast(pos, i - pos, pos, tag, data, ""));
        };
    }
    rule many(const rule& rule, const str& tag = "") {
        return [=](auto s, auto i) {
            int size = strlen(s);
            int pos = i;
            vector<ptr<ast>> data;
            for(;;) { // dead lock ***
                ptr<ast> a = rule(s, i);
                if(i + a->length >= size || isfail(a)) {
                // if(isfail(a)) {
                    return ptr<ast>(new ast(pos, i - pos, a->error, tag, data, "")); // fixed
                }
                i += a->length;
                data.push_back(a);
            }
            // never
        };
    }
    rule cases(const vector<rule>& rules, const str& tag = "") {
        return [=](auto s, auto i) {
            int error = i;
            for(auto& r : rules) {
                ptr<ast> a = r(s, i);
                if(!isfail(a)) {
                    return ptr<ast>(new ast(i, a->length, i, tag, { a }, ""));
                }
                else {
                    if(a->error > error) error = a->error;
                }
            }
            return fail(i, error, tag);
        };
    }
    rule lazy(const fun<rule()>& rule) { // forward definitions
        return [=](auto s, auto i) {
            return rule()(s, i);
        };
    }

    // manual regex because it is too slow
    ptr<ast> wsmatch(const char* s, int i) {
        str tag = "ws";
        for(int c = i; ; c++) {
            char e = s[c];
            if(e == ' ' || e == '\t' || e == '\r' || e == '\n' || e == ',' ) {
                continue;
            }
            return ptr<ast>(new ast(i, c - i, c, tag, {}, ""));
        }
    }
    ptr<ast> nummatch(const char *s, int i) {
        str tag = "number";
        for(int c = i; ; c++) {
            char e = s[c];
            if(e >= '0' && e <= '9') continue;
            if(e == '.') continue;
            if(c == i) return fail(i, i, tag);
            return ptr<ast>(new ast(i, c - i, c, tag, {}, str(s, i, c - i)));
        }
    }
    ptr<ast> strmatch(const char* s, int i) {
        str tag = "string";
        if(s[i] != '"') return fail(i, i, tag);
        for(int c = i + 1; ; c++) {
            if(s[c] == '\\') { c++; continue; }
            if(s[c] == 0) return fail(i, c, tag);
            if(s[c] == '"') return ptr<ast>(new ast(i, c + 1 - i, c, tag, {}, str(s, i, c + 1 - i)));
        }
        // never
    }

    namespace parser {
        rule element_ptr();
        // rule ws      = regex("^([ \\t\\r\\n,])*", "ws");
        // rule number  = regex("^((0[xX][0-9a-fA-F]+)|(\\d+(\\.\\d+)?))", "number");
        // rule string  = regex("^([\"'])((\\\\(\\1|\\\\))|.)*?\\1", "string");
        rule ws      = wsmatch;
        rule boolean = cases({ token("true"), token("false") }, "boolean");
        rule number  = nummatch;
        rule string  = strmatch;
        rule member  = all({ string, ws, token(":"), ws, lazy(element_ptr) });
        rule array   = all({ token("["), ws, many(all({ lazy(element_ptr), ws })), ws, token("]") }, "array");
        rule object  = all({ token("{"), ws, many(all({ member,            ws })), ws, token("}") }, "object");
        rule element = cases({ array, object, string, boolean, number }, "element");
        rule element_ptr() { return element; }
    };

    namespace decoder {
        using namespace json;
        ptr<value> element(const ptr<ast>& ast);
        ptr<value> boolean(const ptr<ast>& ast) {
            return json::boolean(ast->data[0]->text == "true");
        }
        ptr<value> number(const ptr<ast>& ast) {
            return json::number(std::stod(ast->text));
        }
        ptr<value> string(const ptr<ast>& ast) {
            return json::string(ast->text.substr(1, ast->text.size() - 2));
        }
        ptr<value> array(const ptr<ast>& ast) {
            ptr<value> v = json::array({});
            for(auto& e : ast->data[2]->data) {
                v->array.push_back(element(e->data[0]));
            }
            return v;
        }
        ptr<value> object(const ptr<ast>& ast) {
            ptr<value> v = json::object({});
            for(auto& e : ast->data[2]->data) {
                str key = e->data[0]->data[0]->text;
                v->object.vector().push_back({ key.substr(1, key.size() - 2), element(e->data[0]->data[4]) });
            }
            return v;
        }
        ptr<value> element(const ptr<ast>& ast) {
            if(ast->data[0]->tag == "boolean") return boolean(ast->data[0]);
            if(ast->data[0]->tag == "number")  return number(ast->data[0]);
            if(ast->data[0]->tag == "string")  return string(ast->data[0]);
            if(ast->data[0]->tag == "array")   return array(ast->data[0]);
            if(ast->data[0]->tag == "object")  return object(ast->data[0]);
            abort();
        }
    };

    namespace encoder {
        using namespace json;
        str comma(bool c) {
            return c? ",\n" : "\n";
        }
        void encodeos(const ptr<value>& v, const str& tab, std::ostream& os, bool c) {
            if(v->type == "boolean") os << tab << (v->boolean? "true" : "false") << comma(c);
            if(v->type == "number")  os << tab << v->number << comma(c);
            if(v->type == "string")  os << tab << "\"" << v->string << "\"" << comma(c);
            if(v->type == "array") {
                os << tab << "[" << std::endl;
                int i = 1;
                for(auto& e : v->array) {
                    encodeos(e, tab + "    ", os, i < v->array.size());
                    i++;
                }
                os << tab << "]" << comma(c);
            }
            if(v->type == "object") {
                os << tab << "{" << std::endl;
                int i = 1;
                for(auto& e : v->object.vector()) {
                    os << tab << "    \"" << e.first << "\":" << std::endl;
                    encodeos(e.second, tab + "        ", os, i < v->object.vector().size());
                    i++;
                }
                os << tab << "}" << comma(c);
            }
        }
    };
};

namespace json {
    using namespace json_internals;
    decoded decode(const std::string& s) {
        decoded r;
        ptr<ast> ast = parser::element(s.c_str(), 0);
        if(isfail(ast)) {
            r.error = ast->error;
            return r;
        }
        r.error = -1;
        r.value = decoder::element(ast);
        return r;
    }
    std::string encode(const std::shared_ptr<value>& v) {
        std::ostringstream s;
        // s.precision(20);
        encoder::encodeos(v, "", s, false);
        return s.str();
    }
};

#endif