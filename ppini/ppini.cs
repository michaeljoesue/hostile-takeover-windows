// ppini - a small C preprocessor for Hostile Takeover's data files.
//
// The data build runs the game's INI sources (forms.ini.pp, game.ini.pp,
// GobTemplates.ini.pp and every exported level) through "cpp -P" so that
// constant names such as kidcOk or kifntButton from game/res.h are replaced by
// their numbers. That needs only a small part of the C preprocessor:
// #include, object-like #define/#undef, #ifdef/#ifndef/#else/#endif guards,
// -D, -I and -imacros. Shipping GCC's preprocessor on Windows would add ~40MB,
// so this tool reproduces "cpp -P" output for those features, including its
// spacing rules, so the built data is identical.
//
// Usage (same as the cpp options the makefile uses):
//   ppini [-P] [-Wxxx] [-DNAME[=VALUE]] [-Idir] [-imacros file] input [output]

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace PpIni {

enum TokKind { Name, Number, String, Char, Punct, Other }

class Token {
    public TokKind Kind;
    public string Text;
    public bool PrevWhite;
    public Token(TokKind kind, string text, bool prevWhite) {
        Kind = kind; Text = text; PrevWhite = prevWhite;
    }
}

class Macro {
    public string Name;
    public List<Token> Body = new List<Token>();
}

class Preprocessor {
    // Latin-1 maps every byte to one char and back, so non-ASCII bytes in the
    // data files pass through unchanged.
    public static readonly Encoding Bytes = Encoding.GetEncoding(28591);
    Dictionary<string, Macro> macros_ = new Dictionary<string, Macro>();
    List<string> includeDirs_ = new List<string>();
    bool inComment_;
    TextWriter warnings_ = Console.Error;

    static readonly string[] Puncts = {
        "%:%:", "...", "<<=", ">>=", "->*",
        "##", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
        "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "->", "::", ".*",
        "<:", ":>", "<%", "%>", "%:",
    };

    public void AddIncludeDir(string dir) { includeDirs_.Add(dir); }

    public void Define(string spec) {
        string name = spec, value = "1";
        int eq = spec.IndexOf('=');
        if (eq >= 0) { name = spec.Substring(0, eq); value = spec.Substring(eq + 1); }
        DefineFromLine(name + " " + value, "<command line>");
    }

    // ---- lexing -------------------------------------------------------

    static bool IsIdStart(char c) { return char.IsLetter(c) || c == '_' || c == '$'; }
    static bool IsIdChar(char c) { return char.IsLetterOrDigit(c) || c == '_' || c == '$'; }

    List<Token> Lex(string line, string where, out int leading) {
        var toks = new List<Token>();
        int i = 0, n = line.Length;
        bool white = false;
        leading = 0;
        bool atStart = true;
        while (i < n) {
            char c = line[i];
            if (inComment_) {
                int end = line.IndexOf("*/", i, StringComparison.Ordinal);
                if (end < 0) { i = n; break; }
                i = end + 2;
                inComment_ = false;
                white = true;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\f' || c == '\v' || c == '\r') {
                if (atStart) leading++;
                white = true;
                i++;
                continue;
            }
            atStart = false;
            if (c == '/' && i + 1 < n && line[i + 1] == '/') break;
            if (c == '/' && i + 1 < n && line[i + 1] == '*') {
                inComment_ = true;
                i += 2;
                continue;
            }
            int start = i;
            if (IsIdStart(c)) {
                while (i < n && IsIdChar(line[i])) i++;
                // String/char prefixes like L"..." are treated as plain names
                toks.Add(new Token(TokKind.Name, line.Substring(start, i - start), white));
            } else if (char.IsDigit(c) || (c == '.' && i + 1 < n && char.IsDigit(line[i + 1]))) {
                i++;
                while (i < n) {
                    char d = line[i];
                    if ((d == '+' || d == '-') && "eEpP".IndexOf(line[i - 1]) >= 0) { i++; continue; }
                    if (IsIdChar(d) || d == '.') { i++; continue; }
                    break;
                }
                toks.Add(new Token(TokKind.Number, line.Substring(start, i - start), white));
            } else if (c == '"' || c == '\'') {
                i++;
                bool closed = false;
                while (i < n) {
                    if (line[i] == '\\' && i + 1 < n) { i += 2; continue; }
                    if (line[i] == c) { i++; closed = true; break; }
                    i++;
                }
                if (!closed) {
                    // Like GCC: warn, and keep the rest of the line verbatim
                    warnings_.WriteLine("{0}: warning: missing terminating {1} character", where, c);
                    toks.Add(new Token(TokKind.Other, line.Substring(start).TrimEnd(), white));
                    i = n;
                } else {
                    toks.Add(new Token(c == '"' ? TokKind.String : TokKind.Char,
                            line.Substring(start, i - start), white));
                }
            } else {
                string p = null;
                foreach (string cand in Puncts) {
                    if (string.CompareOrdinal(line, i, cand, 0, cand.Length) == 0) { p = cand; break; }
                }
                if (p == null) p = line.Substring(i, 1);
                i += p.Length;
                toks.Add(new Token(TokKind.Punct, p, white));
            }
            white = false;
        }
        return toks;
    }

    // ---- directives ---------------------------------------------------

    void DefineFromLine(string rest, string where) {
        int leading;
        var toks = Lex(rest, where, out leading);
        if (toks.Count == 0 || toks[0].Kind != TokKind.Name) {
            throw new Exception(where + ": bad #define");
        }
        var m = new Macro { Name = toks[0].Text };
        if (toks.Count > 1 && toks[1].Text == "(" && !toks[1].PrevWhite) {
            throw new Exception(where + ": function-like macros are not supported: " + m.Name);
        }
        for (int k = 1; k < toks.Count; k++) m.Body.Add(toks[k]);
        if (m.Body.Count > 0) m.Body[0].PrevWhite = false;
        macros_[m.Name] = m;
    }

    string ResolveInclude(string name, bool angle, string curDir) {
        if (!angle) {
            string p = Path.Combine(curDir, name);
            if (File.Exists(p)) return p;
        }
        foreach (string dir in includeDirs_) {
            string p = Path.Combine(dir, name);
            if (File.Exists(p)) return p;
        }
        return null;
    }

    // ---- expansion ----------------------------------------------------

    void Expand(List<Token> toks, int idx, HashSet<string> hide, List<KeyValuePair<Token, bool>> output) {
        // output: token + "came next to a macro boundary" (GCC's avoid_paste)
        Token t = toks[idx];
        Macro m;
        if (t.Kind == TokKind.Name && !hide.Contains(t.Text) && macros_.TryGetValue(t.Text, out m)) {
            var hide2 = new HashSet<string>(hide);
            hide2.Add(t.Name());
            var body = new List<Token>();
            for (int k = 0; k < m.Body.Count; k++) {
                Token b = m.Body[k];
                body.Add(new Token(b.Kind, b.Text, k == 0 ? t.PrevWhite : b.PrevWhite));
            }
            int before = output.Count;
            for (int k = 0; k < body.Count; k++) {
                Expand(body, k, hide2, output);
            }
            // Mark the boundaries: first token of the expansion and the token
            // that follows it may need a separating space.
            if (output.Count > before) {
                output[before] = new KeyValuePair<Token, bool>(output[before].Key, true);
            }
            pendingBoundary_ = true;
            return;
        }
        output.Add(new KeyValuePair<Token, bool>(t, pendingBoundary_));
        pendingBoundary_ = false;
    }
    bool pendingBoundary_;

    static bool AvoidPaste(Token a, Token b) {
        if (a == null) return false;
        if (a.Kind == TokKind.Punct && b.Kind == TokKind.Punct) {
            string s = a.Text + b.Text;
            foreach (string p in Puncts) {
                if (p.StartsWith(s, StringComparison.Ordinal) || s.StartsWith(p, StringComparison.Ordinal) && p.Length > a.Text.Length) return true;
            }
            // Comment openers
            if (a.Text == "/" && (b.Text == "/" || b.Text == "*")) return true;
            return false;
        }
        if (a.Kind == TokKind.Name && (b.Kind == TokKind.Name || b.Kind == TokKind.Number ||
                b.Kind == TokKind.Char || b.Kind == TokKind.String)) return true;
        if (a.Kind == TokKind.Number && (b.Kind == TokKind.Name || b.Kind == TokKind.Number ||
                b.Text == "." || b.Text == "+" || b.Text == "-")) return true;
        if (a.Text == "." && b.Kind == TokKind.Number) return true;
        if ((a.Text == "+" || a.Text == "-") && b.Kind == TokKind.Number) return false;
        return false;
    }

    // ---- driver -------------------------------------------------------

    public void ProcessFile(string path, TextWriter output, int depth) {
        if (depth > 64) throw new Exception("#include nested too deeply: " + path);
        string curDir = Path.GetDirectoryName(Path.GetFullPath(path));
        var skip = new Stack<bool>();   // true = skipping this section
        var taken = new Stack<bool>();
        int lineno = 0;
        foreach (string raw in File.ReadAllLines(path, Bytes)) {
            lineno++;
            string where = path + ":" + lineno;
            bool skipping = skip.Count > 0 && skip.Peek();
            string trimmed = raw.TrimStart(' ', '\t');
            if (!inComment_ && trimmed.StartsWith("#")) {
                string d = trimmed.Substring(1).TrimStart(' ', '\t');
                string word = d;
                int sp = d.IndexOfAny(new[] { ' ', '\t' });
                string rest = "";
                if (sp >= 0) { word = d.Substring(0, sp); rest = d.Substring(sp + 1).Trim(); }
                rest = StripComment(rest);
                switch (word) {
                case "ifdef":
                case "ifndef": {
                    bool def = macros_.ContainsKey(rest.Trim());
                    bool cond = word == "ifdef" ? def : !def;
                    skip.Push(skipping || !cond);
                    taken.Push(cond);
                    continue;
                }
                case "else":
                    if (skip.Count == 0) throw new Exception(where + ": #else without #if");
                    skip.Pop();
                    bool parentSkip = skip.Count > 0 && skip.Peek();
                    skip.Push(parentSkip || taken.Peek());
                    continue;
                case "endif":
                    if (skip.Count == 0) throw new Exception(where + ": #endif without #if");
                    skip.Pop();
                    taken.Pop();
                    continue;
                }
                if (skipping) continue;
                switch (word) {
                case "define":
                    DefineFromLine(rest, where);
                    continue;
                case "undef":
                    macros_.Remove(rest.Trim());
                    continue;
                case "include": {
                    string r = rest.Trim();
                    bool angle = r.StartsWith("<");
                    string name = r.Substring(1, r.Length - 2);
                    string inc = ResolveInclude(name, angle, curDir);
                    if (inc == null) throw new Exception(where + ": cannot find include file " + name);
                    ProcessFile(inc, output, depth + 1);
                    continue;
                }
                case "":
                case "pragma":
                    continue;
                default:
                    throw new Exception(where + ": unsupported directive #" + word);
                }
            }
            if (skipping) continue;

            int leading;
            var toks = Lex(raw, where, out leading);
            if (toks.Count == 0) continue;
            var outToks = new List<KeyValuePair<Token, bool>>();
            pendingBoundary_ = false;
            for (int k = 0; k < toks.Count; k++) Expand(toks, k, new HashSet<string>(), outToks);
            if (outToks.Count == 0) continue;

            var sb = new StringBuilder();
            // GCC -P keeps one space per leading whitespace character
            sb.Append(' ', leading);
            Token prev = null;
            foreach (var kv in outToks) {
                Token t = kv.Key;
                if (prev != null) {
                    if (t.PrevWhite) sb.Append(' ');
                    else if (kv.Value && AvoidPaste(prev, t)) sb.Append(' ');
                }
                sb.Append(t.Text);
                prev = t;
            }
            output.Write(sb.ToString().TrimEnd());
            output.Write('\n');
        }
        if (skip.Count != 0) throw new Exception(path + ": unterminated #if");
    }

    static string StripComment(string s) {
        int i = s.IndexOf("//", StringComparison.Ordinal);
        if (i >= 0) s = s.Substring(0, i);
        int j = s.IndexOf("/*", StringComparison.Ordinal);
        if (j >= 0) {
            int k = s.IndexOf("*/", j + 2, StringComparison.Ordinal);
            s = k >= 0 ? s.Substring(0, j) + " " + s.Substring(k + 2) : s.Substring(0, j);
        }
        return s;
    }
}

static class TokenExt {
    public static string Name(this Token t) { return t.Text; }
}

class Program {
    static int Main(string[] args) {
        var pp = new Preprocessor();
        var imacros = new List<string>();
        var files = new List<string>();
        try {
            for (int i = 0; i < args.Length; i++) {
                string a = args[i];
                if (a == "-P" || a.StartsWith("-W")) continue;
                if (a.StartsWith("-D")) { pp.Define(a.Length > 2 ? a.Substring(2) : args[++i]); continue; }
                if (a.StartsWith("-I")) { pp.AddIncludeDir(a.Length > 2 ? a.Substring(2) : args[++i]); continue; }
                if (a == "-imacros") { imacros.Add(args[++i]); continue; }
                if (a.StartsWith("-") && a != "-") throw new Exception("unknown option " + a);
                files.Add(a);
            }
            if (files.Count < 1 || files.Count > 2) {
                Console.Error.WriteLine("usage: ppini [-P] [-DNAME[=VALUE]] [-Idir] [-imacros file] input [output]");
                return 2;
            }
            foreach (string f in imacros) pp.ProcessFile(f, TextWriter.Null, 0);
            if (files.Count == 2) {
                using (var w = new StreamWriter(files[1], false, Preprocessor.Bytes)) {
                    w.NewLine = "\n";
                    pp.ProcessFile(files[0], w, 0);
                }
            } else {
                var stdout = new StreamWriter(Console.OpenStandardOutput(), Preprocessor.Bytes);
                stdout.NewLine = "\n";
                pp.ProcessFile(files[0], stdout, 0);
                stdout.Flush();
            }
            return 0;
        } catch (Exception ex) {
            Console.Error.WriteLine("ppini: " + ex.Message);
            return 1;
        }
    }
}

}
