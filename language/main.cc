#include "culebra.h"
#include "linenoise.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace culebra;
using namespace peglib;
using namespace std;

bool read_file(const char* path, vector<char>& buff)
{
    ifstream ifs(path, ios::in|ios::binary);
    if (ifs.fail()) {
        return false;
    }

    auto size = static_cast<unsigned int>(ifs.seekg(0, ios::end).tellg());

    if (size > 0) {
        buff.resize(size);
        ifs.seekg(0, ios::beg).read(&buff[0], static_cast<streamsize>(buff.size()));
    }

    return true;
}

struct CommandLineDebugger
{
    void operator()(const Ast& ast, Environment& env, bool force_to_break) {
        if (quit) {
            return;
        }

        if ((command_ == "n" && env.level <= level_) ||
            (command_ == "s") ||
            (command_ == "o" && env.level < level_)) {
            force_to_break = true;
        }

        if (force_to_break) {
            show_lines(ast);

            for (;;) {
                cout << "debug> ";
                string s;
                std::getline(cin, s);

                istringstream is(s);
                is >> command_;

                if (command_ == "h") {
                    cout << "(c)ontinue, (n)ext, (s)tep in, step (o)out, (p)ring, (l)ist, (q)uit" << endl;
                } else if (command_ == "l") {
                    is >> display_lines_;
                    show_lines(ast);
                } else if (command_ == "p") {
                    string symbol;
                    is >> symbol;
                    print(ast, env, symbol);
                } else if (command_ == "c") {
                    break;
                } else if (command_ == "n") {
                    break;
                } else if (command_ == "s") {
                    break;
                } else if (command_ == "o") {
                    break;
                } else if (command_ == "q") {
                    quit = true;
                    break;
                }
            }
            level_ = env.level;;
        }
    }

    void show_lines(const Ast& ast) {
        prepare_cache(ast.path);

        cout << "break in " << ast.path << ":" << ast.line << endl;

        auto count = get_line_count(ast.path);

        auto lines_ahead = (size_t)((display_lines_ - .5) / 2);
        auto start = (size_t)max((int)ast.line - (int)lines_ahead, 1);
        auto end = min(start + display_lines_, count);

        auto needed_digits = to_string(count).length();

        for (auto l = start; l < end; l++) {
            auto s = get_line(ast.path, l);
            if (l == ast.line) {
                cout << "> ";
            } else {
                cout << "  ";
            }
            cout << setw(needed_digits) << l << " " << s << endl;
        }
    }

    shared_ptr<Ast> find_function_node(const Ast& ast) {
        auto node = ast.parent_node;
        while (node->parent_node && node->tag != "FUNCTION"_) {
            node = node->parent_node;
        }
        return node;
    }

    void enum_identifiers(const Ast& ast, set<string>& references) {
        for (auto node: ast.nodes) {
            switch (node->tag) {
            case "IDENTIFIER"_:
                references.insert(node->token);
                break;
            case "FUNCTION"_:
                break;
            default:
                enum_identifiers(*node, references);
                break;
            }
        }
    }

    void print(const Ast& ast, Environment& env, const string& symbol) {
        if (symbol.empty()) {
            print_all(ast, env);
        } else if (env.has(symbol)) {
            cout << symbol << ": " << env.get(symbol).str() << endl;
        } else {
            cout << "'" << symbol << "'" << "is not undefined." << endl;
        }
    }

    void print_all(const Ast& ast, Environment& env) {
        auto node = find_function_node(ast);
        set<string> references;
        enum_identifiers(*node, references);
        for (const auto& symbol: references) {
            const auto& val = env.get(symbol);
            cout << symbol << ": " << val.str() << endl;
        }
    }

    size_t get_line_count(const string& path) {
        return sources_[path].size();
    }

    string get_line(const string& path, size_t line) {
        const auto& positions = sources_[path];
        auto idx = line - 1;
        auto first = idx > 0 ? positions[idx - 1] : 0;
        auto last = positions[idx];
        auto size = last - first;

        string s(size, 0);
        ifstream ifs(path, ios::in | ios::binary);
        ifs.seekg(first, ios::beg).read((char*)s.data(), static_cast<streamsize>(s.size()));

        size_t count = 0;
        auto rit = s.rbegin();
        while (rit != s.rend()) {
            if (*rit == '\n') {
                count++;
            }
            ++rit;
        }

        s = s.substr(0, s.size() - count);

        return s;
    }

    void prepare_cache(const string& path) {
        auto it = sources_.find(path);
        if (it == sources_.end()) {
            vector<char> buff;
            read_file(path.c_str(), buff);

            auto& positions = sources_[path];

            auto i = 0u;
            for (; i < buff.size(); i++) {
                if (buff[i] == '\n') {
                    positions.push_back(i + 1);
                }
            }
            positions.push_back(i);
        }
    }

    bool   quit = false;
    string command_;
    size_t level_ = 0;
    size_t display_lines_ = 4;
    map<string, vector<size_t>> sources_;
};

int repl(shared_ptr<Environment> env, bool print_ast)
{
    for (;;) {
        auto line = linenoise::Readline("cul> ");

        if (line == "exit" || line == "quit") {
            break;
        }

        if (!line.empty()) {
            Value           val;
            vector<string>  msgs;
            shared_ptr<Ast> ast;
            auto ret = run("(repl)", env, line.c_str(), line.size(), val, msgs, ast);
            if (ret) {
                if (print_ast) {
                    ast->print();
                }
                cout << val << endl;
                linenoise::AddHistory(line.c_str());
            } else if (!msgs.empty()) {
                for (const auto& msg: msgs) {
                    cout << msg << endl;;
                }
            }
        }
    }

    return 0;
}

int main(int argc, const char** argv)
{
    auto print_ast = false;
    auto shell = false;
    auto debug = false;
    vector<const char*> path_list;

    int argi = 1;
    while (argi < argc) {
        auto arg = argv[argi++];
        if (string("--shell") == arg) {
            shell = true;
        } else if (string("--ast") == arg) {
            print_ast = true;
        } else if (string("--debug") == arg) {
            debug = true;
        } else {
            path_list.push_back(arg);
        }
    }

    if (!shell) {
        shell = path_list.empty();
    }

    try {
        auto env = make_shared<Environment>();
        setup_built_in_functions(*env);

        for (auto path: path_list) {
            vector<char> buff;
            if (!read_file(path, buff)) {
                cerr << "can't open '" << path << "'." << endl;
                return -1;
            }

            Value           val;
            vector<string>  msgs;
            shared_ptr<Ast> ast;
            Debugger        dbg;

            CommandLineDebugger debugger;
            if (debug) {
                dbg = debugger;
            }

            auto ret = run(path, env, buff.data(), buff.size(), val, msgs, ast, dbg);

            if (!ret) {
                for (const auto& msg: msgs) {
                    cerr << msg << endl;
                }
                return -1;
            } else if (print_ast) {
                ast->print();
            }
        }

        if (shell) {
            repl(env, print_ast);
        }
    } catch (exception& e) {
        cerr << e.what() << endl;
        return -1;
    }

    return 0;
}

// vim: et ts=4 sw=4 cin cino={1s ff=unix
