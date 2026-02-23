/**
 * DOCUNATION.C - TRIPLE A FAMILY HOLDINGS Documentation Generator
 * 
 * Walks C source trees and generates documentation.
 * No external dependencies (pure C).
 * Usage:
 *     docunation <file.c>              # Document a single file
 *     docunation -j <file.c>           # Output JSON
 *     docunation -h <file.c>           # Output HTML
 *     docunation -a <num>              # Show axiom documentation
 * 
 * Extracts:
 *     - Functions (signatures, docstrings from preceding comments)
 *     - Structs, Unions, Enums
 *     - Typedefs
 *     - Macros (#define)
 *     - Global variables
 * 
 * Standards:
 *     - ISO C17
 *     - IEEE 754 (numeric constants)
 * 
 * Build:
 *     cc -o docunation docunation.c -O2
 * 
 * (c) 2026 Triple A Family Holdings LLC
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════ */

#define DOCUNATION_VERSION "1.0.0"
#define MAX_LINE 4096
#define MAX_NAME 256
#define MAX_DOC 8192
#define MAX_SIG 8192
#define MAX_NODES 2048
#define MAX_PARAMS 32
#define MAX_PATH_LEN 8192

/* Safe node increment macro */
#define ADD_NODE(p) do { \
    if ((p)->doc->node_count >= MAX_NODES - 1) { \
        fprintf(stderr, "Warning: max nodes (%d) reached, truncating\n", MAX_NODES); \
        return; \
    } \
    (p)->doc->node_count++; \
} while(0)

/* ═══════════════════════════════════════════════════════════════════════════
 * ANSI COLORS
 * ═══════════════════════════════════════════════════════════════════════════ */

#define COL_RESET   "\033[0m"
#define COL_BOLD    "\033[1m"
#define COL_RED     "\033[91m"
#define COL_GREEN   "\033[92m"
#define COL_YELLOW  "\033[93m"
#define COL_BLUE    "\033[94m"
#define COL_MAGENTA "\033[95m"
#define COL_CYAN    "\033[96m"

static int use_color = 1;

#define C(code) (use_color ? code : "")

/* ═══════════════════════════════════════════════════════════════════════════
 * NODE TYPES
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    NODE_FUNCTION,
    NODE_STRUCT,
    NODE_UNION,
    NODE_ENUM,
    NODE_TYPEDEF,
    NODE_MACRO,
    NODE_VARIABLE,
    NODE_INCLUDE
} NodeType;

static const char *node_type_names[] = {
    "function", "struct", "union", "enum", "typedef", "macro", "variable", "include"
};

/* ═══════════════════════════════════════════════════════════════════════════
 * DOC NODE STRUCTURE
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char name[MAX_NAME];
    NodeType type;
    int line;
    char docstring[MAX_DOC];
    char signature[MAX_SIG];
    char return_type[MAX_NAME];
    int is_static;
    int is_inline;
    int is_extern;
} DocNode;

typedef struct {
    char filepath[MAX_LINE];
    char module_name[MAX_NAME];
    char docstring[MAX_DOC];
    DocNode nodes[MAX_NODES];
    int node_count;
    char timestamp[64];
} DOCUNATION;

/* ═══════════════════════════════════════════════════════════════════════════
 * STRING UTILITIES
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Trim leading whitespace */
static char *ltrim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    return s;
}

/* Trim trailing whitespace */
static char *rtrim(char *s) {
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

/* Trim both ends */
static char *trim(char *s) {
    return rtrim(ltrim(s));
}

/* Safe string copy */
static void safe_strcpy(char *dest, const char *src, size_t size) {
    if (size == 0) return;
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

static void safe_append(char *dest, const char *src, size_t size) {
    if (size == 0 || !src) return;
    size_t dest_len = strlen(dest);
    if (dest_len >= size - 1) return;
    size_t remaining = size - dest_len - 1;
    size_t copy_len = strlen(src);
    if (copy_len > remaining) copy_len = remaining;
    memcpy(dest + dest_len, src, copy_len);
    dest[dest_len + copy_len] = '\0';
}

/* Check if string starts with prefix */
static int starts_with(const char *str, const char *prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

/* Check if character is identifier char */
static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

/* Extract module name from filepath */
static void extract_module_name(const char *filepath, char *name, size_t size) {
    const char *base = strrchr(filepath, '/');
    if (!base) base = strrchr(filepath, '\\');
    base = base ? base + 1 : filepath;
    
    safe_strcpy(name, base, size);
    
    /* Remove extension */
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
}

static int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t lenstr = strlen(str);
    size_t lensuf = strlen(suffix);
    if (lensuf > lenstr) return 0;
    return strncmp(str + lenstr - lensuf, suffix, lensuf) == 0;
}

static void sanitize_rel_path(const char *rel, char *safe, size_t size) {
    size_t idx = 0;
    if (!rel) {
        if (size > 0) safe[0] = '\0';
        return;
    }
    for (const char *p = rel; *p && idx + 1 < size; p++) {
        if (*p == '/' || *p == '\\') {
            if (idx + 2 >= size) break;
            safe[idx++] = '_';
            safe[idx++] = '_';
        } else if (*p == ' ') {
            safe[idx++] = '_';
        } else {
            safe[idx++] = *p;
        }
    }
    safe[idx] = '\0';
}

static int ensure_dir(const char *path) {
    if (!path || !*path) return -1;
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        fprintf(stderr, "Error: %s exists and is not a directory\n", path);
        return -1;
    }
    if (mkdir(path, 0777) == 0) return 0;
    if (errno == EEXIST) return 0;
    fprintf(stderr, "Error: cannot create directory '%s': %s\n", path, strerror(errno));
    return -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * C PARSER - Type Definition (needed early for forward decl)
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct Parser {
    FILE *fp;
    char line[MAX_LINE];
    int line_num;
    char pending_comment[MAX_DOC];
    int pending_comment_line;
    DOCUNATION *doc;
} Parser;

static void output_text(DOCUNATION *doc, FILE *out);
static void output_json(DOCUNATION *doc, FILE *out);
static void output_html(DOCUNATION *doc, FILE *out);

/* ═══════════════════════════════════════════════════════════════════════════
 * COMMENT PARSING
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Clean a comment block, removing star prefixes and delimiters */
static void clean_comment(const char *raw, char *cleaned, size_t size) {
    const char *p = raw;
    char *out = cleaned;
    char *end = cleaned + size - 1;
    int at_line_start = 1;
    
    /* Skip opening delimiter */
    if (starts_with(p, "/**")) p += 3;
    else if (starts_with(p, "/*")) p += 2;
    else if (starts_with(p, "//")) p += 2;
    
    while (*p && out < end) {
        /* Skip closing delimiter */
        if (starts_with(p, "*/")) {
            p += 2;
            continue;
        }
        
        /* Handle newlines */
        if (*p == '\n') {
            *out++ = '\n';
            p++;
            at_line_start = 1;
            continue;
        }
        
        /* Skip leading whitespace and * at line start */
        if (at_line_start) {
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '*' && *(p+1) != '/') {
                p++;
                if (*p == ' ') p++;
            }
            at_line_start = 0;
            continue;
        }
        
        *out++ = *p++;
    }
    *out = '\0';
    
    /* Trim result */
    trim(cleaned);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * C PARSER - Functions
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Read next non-empty line */
static int next_line(Parser *p) {
    while (fgets(p->line, MAX_LINE, p->fp)) {
        p->line_num++;
        char *trimmed = trim(p->line);
        if (strlen(trimmed) > 0) {
            return 1;
        }
    }
    return 0;
}

/* Parse a block comment */
static void parse_block_comment(Parser *p) {
    char buf[MAX_DOC] = {0};
    
    /* Copy current line */
    safe_append(buf, p->line, MAX_DOC);
    
    /* Check if comment ends on same line */
    if (strstr(p->line, "*/")) {
        clean_comment(buf, p->pending_comment, MAX_DOC);
        p->pending_comment_line = p->line_num;
        return;
    }
    
    /* Read until end of comment */
    while (fgets(p->line, MAX_LINE, p->fp)) {
        p->line_num++;
        if (strlen(buf) + strlen(p->line) < MAX_DOC - 1) {
            safe_append(buf, p->line, MAX_DOC);
        }
        if (strstr(p->line, "*/")) break;
    }
    
    clean_comment(buf, p->pending_comment, MAX_DOC);
    p->pending_comment_line = p->line_num;
}

/* Parse a function declaration/definition */
static void parse_function(Parser *p, const char *start, int is_static, int is_inline, int is_extern) {
    (void)start;
    DocNode *node = &p->doc->nodes[p->doc->node_count];
    memset(node, 0, sizeof(DocNode));
    
    node->type = NODE_FUNCTION;
    node->line = p->line_num;
    node->is_static = is_static;
    node->is_inline = is_inline;
    node->is_extern = is_extern;
    
    /* Build full signature */
    char sig[MAX_SIG] = {0};
    safe_append(sig, p->line, MAX_SIG);
    
    /* If line doesn't end with { or ;, read more */
    while (!strchr(sig, '{') && !strchr(sig, ';')) {
        if (!fgets(p->line, MAX_LINE, p->fp)) break;
        p->line_num++;
        char *trimmed = trim(p->line);
        safe_append(sig, " ", MAX_SIG);
        safe_append(sig, trimmed, MAX_SIG);
    }
    
    /* Clean up signature - remove body */
    char *brace = strchr(sig, '{');
    if (brace) *brace = '\0';
    char *semi = strchr(sig, ';');
    if (semi) *semi = '\0';
    
    /* Trim and save signature */
    safe_strcpy(node->signature, trim(sig), MAX_SIG);
    
    /* Extract function name (last identifier before '(') */
    char *paren = strchr(sig, '(');
    if (paren) {
        *paren = '\0';
        /* Walk backwards to find name */
        char *name_end = paren - 1;
        while (name_end > sig && isspace((unsigned char)*name_end)) name_end--;
        char *name_start = name_end;
        while (name_start > sig && is_ident_char(*(name_start-1))) name_start--;
        
        int len = name_end - name_start + 1;
        if (len > 0 && len < MAX_NAME) {
            strncpy(node->name, name_start, len);
            node->name[len] = '\0';
        }
        
        /* Extract return type */
        *name_start = '\0';
        safe_strcpy(node->return_type, trim(sig), MAX_NAME);
    }
    
    /* Copy docstring if comment was on previous line */
    if (p->pending_comment_line == node->line - 1 || 
        p->pending_comment_line == node->line) {
        safe_strcpy(node->docstring, p->pending_comment, MAX_DOC);
        p->pending_comment[0] = '\0';
    }
    
    ADD_NODE(p);
}

/* Parse a struct/union/enum */
static void parse_aggregate(Parser *p, NodeType type) {
    DocNode *node = &p->doc->nodes[p->doc->node_count];
    memset(node, 0, sizeof(DocNode));
    
    node->type = type;
    node->line = p->line_num;
    
    /* Extract name */
    const char *keyword = (type == NODE_STRUCT) ? "struct" :
                          (type == NODE_UNION) ? "union" : "enum";
    char *kw_pos = strstr(p->line, keyword);
    if (kw_pos) {
        char *name_start = kw_pos + strlen(keyword);
        while (*name_start && isspace((unsigned char)*name_start)) name_start++;
        
        char *name_end = name_start;
        while (*name_end && is_ident_char(*name_end)) name_end++;
        
        int len = name_end - name_start;
        if (len > 0 && len < MAX_NAME) {
            strncpy(node->name, name_start, len);
            node->name[len] = '\0';
        }
    }
    
    /* If anonymous, use placeholder */
    if (node->name[0] == '\0') {
        snprintf(node->name, MAX_NAME, "(anonymous %s)", keyword);
    }
    
    /* Build signature */
    safe_strcpy(node->signature, trim(p->line), MAX_SIG);
    
    /* Copy docstring */
    if (p->pending_comment_line == node->line - 1) {
        safe_strcpy(node->docstring, p->pending_comment, MAX_DOC);
        p->pending_comment[0] = '\0';
    }
    
    ADD_NODE(p);
}

/* Parse a typedef */
static void parse_typedef(Parser *p) {
    DocNode *node = &p->doc->nodes[p->doc->node_count];
    memset(node, 0, sizeof(DocNode));
    
    node->type = NODE_TYPEDEF;
    node->line = p->line_num;
    
    /* Build full typedef (may span lines) */
    char sig[MAX_SIG] = {0};
    safe_append(sig, p->line, MAX_SIG);
    
    while (!strchr(sig, ';')) {
        if (!fgets(p->line, MAX_LINE, p->fp)) break;
        p->line_num++;
        char *trimmed = trim(p->line);
        safe_append(sig, " ", MAX_SIG);
        safe_append(sig, trimmed, MAX_SIG);
    }
    
    /* Remove trailing semicolon */
    char *semi = strchr(sig, ';');
    if (semi) *semi = '\0';
    
    /* Extract name (last identifier before ;) */
    char *end = sig + strlen(sig) - 1;
    while (end > sig && isspace((unsigned char)*end)) end--;
    char *name_start = end;
    while (name_start > sig && is_ident_char(*(name_start-1))) name_start--;
    
    int len = end - name_start + 1;
    if (len > 0 && len < MAX_NAME) {
        strncpy(node->name, name_start, len);
        node->name[len] = '\0';
    }
    
    safe_strcpy(node->signature, trim(sig), MAX_SIG);
    
    /* Copy docstring */
    if (p->pending_comment_line == node->line - 1) {
        safe_strcpy(node->docstring, p->pending_comment, MAX_DOC);
        p->pending_comment[0] = '\0';
    }
    
    ADD_NODE(p);
}

/* Parse a #define macro */
static void parse_macro(Parser *p) {
    DocNode *node = &p->doc->nodes[p->doc->node_count];
    memset(node, 0, sizeof(DocNode));
    
    node->type = NODE_MACRO;
    node->line = p->line_num;
    
    /* Build full macro (may have line continuations) */
    /* Find actual #define in line (may have leading spaces) */
    char *def = strstr(p->line, "#define");
    if (!def) return;
    
    char sig[MAX_SIG] = {0};
    safe_strcpy(sig, def, MAX_SIG);
    
    while (1) {
        size_t sig_len = strlen(sig);
        int has_backslash = (sig_len > 0 && sig[sig_len - 1] == '\\');
        int penultimate_backslash = (sig_len > 1 && sig[sig_len - 2] == '\\');
        if (!has_backslash && !penultimate_backslash) {
            break;
        }
        if (has_backslash) {
            sig[sig_len - 1] = ' ';
        }
        if (!fgets(p->line, MAX_LINE, p->fp)) break;
        p->line_num++;
        char *trimmed = trim(p->line);
        if (strlen(sig) + strlen(trimmed) < MAX_SIG - 1) {
            safe_append(sig, trimmed, MAX_SIG);
        }
    }
    
    /* Extract name */
    char *name_start = sig + 7;  /* Skip "#define" */
    while (*name_start && isspace((unsigned char)*name_start)) name_start++;
    char *name_end = name_start;
    while (*name_end && (is_ident_char(*name_end) || *name_end == '(')) {
        if (*name_end == '(') break;  /* Function-like macro */
        name_end++;
    }
    
    int len = name_end - name_start;
    if (len > 0 && len < MAX_NAME) {
        strncpy(node->name, name_start, len);
        node->name[len] = '\0';
    }
    
    safe_strcpy(node->signature, trim(sig), MAX_SIG);
    
    /* Copy docstring */
    if (p->pending_comment_line == node->line - 1) {
        safe_strcpy(node->docstring, p->pending_comment, MAX_DOC);
        p->pending_comment[0] = '\0';
    }
    
    ADD_NODE(p);
}

/* Parse an #include */
static void parse_include(Parser *p) {
    DocNode *node = &p->doc->nodes[p->doc->node_count];
    memset(node, 0, sizeof(DocNode));
    
    node->type = NODE_INCLUDE;
    node->line = p->line_num;
    
    /* Extract filename */
    char *start = strchr(p->line, '<');
    char *end = NULL;
    if (start) {
        start++;
        end = strchr(start, '>');
    } else {
        start = strchr(p->line, '"');
        if (start) {
            start++;
            end = strchr(start, '"');
        }
    }
    
    if (start && end && end > start) {
        int len = end - start;
        if (len < MAX_NAME) {
            strncpy(node->name, start, len);
            node->name[len] = '\0';
        }
    }
    
    safe_strcpy(node->signature, trim(p->line), MAX_SIG);
    ADD_NODE(p);
}

/* Parse a static/const variable or constant */
static void parse_variable(Parser *p, const char *line, int is_static) {
    DocNode *node = &p->doc->nodes[p->doc->node_count];
    memset(node, 0, sizeof(DocNode));
    
    node->type = NODE_VARIABLE;
    node->line = p->line_num;
    node->is_static = is_static;
    
    /* Build full declaration (may span multiple lines) */
    char sig[MAX_SIG] = {0};
    safe_strcpy(sig, line, MAX_SIG);
    
    /* If we have an array initializer, read until } or ; */
    if (strchr(sig, '{') && !strchr(sig, '}')) {
        while (fgets(p->line, MAX_LINE, p->fp)) {
            p->line_num++;
            if (strlen(sig) + strlen(p->line) < MAX_SIG - 10) {
                safe_append(sig, " ", MAX_SIG);
            }
            if (strchr(p->line, '}') || strchr(p->line, ';')) {
                break;
            }
        }
    }
    
    /* Extract variable name:
     * Look for identifier before [ or = */
    const char *s = line;
    
    /* Skip leading type keywords */
    if (strncmp(s, "static ", 7) == 0) s += 7;
    while (*s && isspace((unsigned char)*s)) s++;
    if (strncmp(s, "const ", 6) == 0) s += 6;
    while (*s && isspace((unsigned char)*s)) s++;
    
    /* Skip the type (int, char, struct, etc.) */
    while (*s && (is_ident_char(*s) || *s == ' ' || *s == '*')) {
        /* Stop at = or [ */
        if (*s == '=' || *s == '[') break;
        /* Look for last identifier before = or [ */
        if (isspace((unsigned char)*s) && *(s+1) && is_ident_char(*(s+1))) {
            s++;
            const char *name_start = s;
            const char *name_end = s;
            while (*name_end && is_ident_char(*name_end)) name_end++;
            
            /* Check if next non-space char is [ or = */
            const char *c = name_end;
            while (*c && isspace((unsigned char)*c)) c++;
            if (*c == '[' || *c == '=' || *c == ';') {
                int len = name_end - name_start;
                if (len > 0 && len < MAX_NAME) {
                    strncpy(node->name, name_start, len);
                    node->name[len] = '\0';
                }
                break;
            }
        }
        s++;
    }
    
    /* Simplified signature (just the declaration, not the value) */
    char *eq = strstr(sig, " = ");
    char *br = strchr(sig, '{');
    if (eq) *eq = '\0';
    else if (br) *br = '\0';
    
    safe_strcpy(node->signature, trim(sig), MAX_SIG);
    
    /* Copy docstring */
    if (p->pending_comment_line == node->line - 1) {
        safe_strcpy(node->docstring, p->pending_comment, MAX_DOC);
        p->pending_comment[0] = '\0';
    }
    
    ADD_NODE(p);
}

/* Main parser loop */
static void parse_file(Parser *p) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(p->doc->timestamp, sizeof(p->doc->timestamp), 
             "%Y-%m-%dT%H:%M:%S", t);
    
    while (next_line(p)) {
        char *line = ltrim(p->line);
        
        /* Block comment */
        if (starts_with(line, "/*")) {
            parse_block_comment(p);
            
            /* Check if this is file-level doc (first comment) */
            if (p->doc->node_count == 0 && p->doc->docstring[0] == '\0') {
                safe_strcpy(p->doc->docstring, p->pending_comment, MAX_DOC);
            }
            continue;
        }
        
        /* Line comment */
        if (starts_with(line, "//")) {
            clean_comment(line, p->pending_comment, MAX_DOC);
            p->pending_comment_line = p->line_num;
            continue;
        }
        
        /* Preprocessor */
        if (line[0] == '#') {
            if (starts_with(line, "#include")) {
                parse_include(p);
            } else if (starts_with(line, "#define")) {
                parse_macro(p);
            }
            continue;
        }
        
        /* Check for keywords */
        int is_static = (strstr(line, "static ") != NULL);
        int is_inline = (strstr(line, "inline ") != NULL);
        int is_extern = (strstr(line, "extern ") != NULL);
        
        /* Struct/Union/Enum */
        if (strstr(line, "struct ") && !strstr(line, "typedef")) {
            parse_aggregate(p, NODE_STRUCT);
            continue;
        }
        if (strstr(line, "union ") && !strstr(line, "typedef")) {
            parse_aggregate(p, NODE_UNION);
            continue;
        }
        if (strstr(line, "enum ") && !strstr(line, "typedef")) {
            parse_aggregate(p, NODE_ENUM);
            continue;
        }
        
        /* Typedef */
        if (starts_with(line, "typedef")) {
            parse_typedef(p);
            continue;
        }
        
        /* Function (has parentheses but not control statements) 
         * Must start with a type or be static/inline/extern */
        if (strchr(line, '(') && 
            !starts_with(line, "if") &&
            !starts_with(line, "while") &&
            !starts_with(line, "for") &&
            !starts_with(line, "switch") &&
            !starts_with(line, "return") &&
            !strstr(line, "sizeof") &&
            !strstr(line, "= ") &&
            !strstr(line, "->") &&
            !strstr(line, ".") &&
            (is_static || is_inline || is_extern ||
             starts_with(line, "void") ||
             starts_with(line, "int") ||
             starts_with(line, "char") ||
             starts_with(line, "long") ||
             starts_with(line, "short") ||
             starts_with(line, "unsigned") ||
             starts_with(line, "signed") ||
             starts_with(line, "float") ||
             starts_with(line, "double") ||
             starts_with(line, "size_t") ||
             starts_with(line, "const") ||
             strstr(line, "* ") ||   /* pointer return */
             strstr(line, "*\t"))) { /* pointer return */
            parse_function(p, line, is_static, is_inline, is_extern);
            continue;
        }
        
        /* Static/const variables (not functions - no parens or has = sign) 
         * Must be at file scope - require static/extern or no leading whitespace */
        int at_file_scope = (p->line[0] && !isspace((unsigned char)p->line[0])) || 
                            is_static || is_extern;
        if (at_file_scope &&
            (is_static || starts_with(line, "const ")) && 
            !strchr(line, '(') &&
            !strstr(line, "->") &&
            (strstr(line, "=") || strstr(line, "["))) {
            parse_variable(p, line, is_static);
            continue;
        }
        
        /* Clear pending comment if not consumed */
        if (p->pending_comment_line < p->line_num - 1) {
            p->pending_comment[0] = '\0';
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DOCUMENT HELPERS
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    const char *root;
    size_t root_len;
    const char *out_dir;
    FILE *index;
    size_t file_count;
} BulkContext;

static DOCUNATION *parse_document(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open '%s'\n", filename);
        return NULL;
    }

    DOCUNATION *doc = calloc(1, sizeof(DOCUNATION));
    if (!doc) {
        fprintf(stderr, "Error: Cannot allocate memory\n");
        fclose(fp);
        return NULL;
    }

    safe_strcpy(doc->filepath, filename, MAX_LINE);
    extract_module_name(filename, doc->module_name, MAX_NAME);

    time_t now = time(NULL);
    struct tm *tm_info = NULL;
#ifdef _WIN32
    tm_info = localtime(&now);
#else
    struct tm tm_buf;
    tm_info = localtime_r(&now, &tm_buf);
#endif
    if (!tm_info) tm_info = localtime(&now);
    if (tm_info) {
        strftime(doc->timestamp, sizeof(doc->timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    } else {
        safe_strcpy(doc->timestamp, "unknown", sizeof(doc->timestamp));
    }

    Parser *parser = calloc(1, sizeof(Parser));
    if (!parser) {
        fprintf(stderr, "Error: Cannot allocate memory\n");
        free(doc);
        fclose(fp);
        return NULL;
    }
    parser->fp = fp;
    parser->doc = doc;
    parse_file(parser);
    fclose(fp);
    free(parser);
    return doc;
}

static int write_outputs(DOCUNATION *doc, const char *txt_path,
                         const char *json_path, const char *html_path) {
    int prev_color = use_color;

    FILE *out = fopen(txt_path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot write '%s'\n", txt_path);
        return -1;
    }
    use_color = 0;
    output_text(doc, out);
    fclose(out);
    use_color = prev_color;

    out = fopen(json_path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot write '%s'\n", json_path);
        return -1;
    }
    output_json(doc, out);
    fclose(out);

    out = fopen(html_path, "w");
    if (!out) {
        fprintf(stderr, "Error: Cannot write '%s'\n", html_path);
        return -1;
    }
    output_html(doc, out);
    fclose(out);
    return 0;
}

static int bulk_process_file(BulkContext *ctx, const char *filepath) {
    const char *rel = filepath;
    if (strncmp(filepath, ctx->root, ctx->root_len) == 0) {
        rel += ctx->root_len;
        if (*rel == '/' || *rel == '\\') rel++;
    }
    if (!*rel) rel = filepath;

    char safe[MAX_PATH_LEN];
    sanitize_rel_path(rel, safe, sizeof(safe));
    if (!safe[0]) safe_strcpy(safe, "file", sizeof(safe));

    char base[MAX_PATH_LEN];
    safe_strcpy(base, safe, sizeof(base));
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';

    char txt_path[MAX_PATH_LEN];
    char json_path[MAX_PATH_LEN];
    char html_path[MAX_PATH_LEN];
    snprintf(txt_path, sizeof(txt_path), "%s/txt/%s.txt", ctx->out_dir, base);
    snprintf(json_path, sizeof(json_path), "%s/json/%s.json", ctx->out_dir, base);
    snprintf(html_path, sizeof(html_path), "%s/html/%s.html", ctx->out_dir, base);

    DOCUNATION *doc = parse_document(filepath);
    if (!doc) return -1;
    int rc = write_outputs(doc, txt_path, json_path, html_path);
    free(doc);
    if (rc != 0) {
        fprintf(stderr, "Error: Failed documenting %s\n", filepath);
        return -1;
    }

    fprintf(ctx->index,
            "<tr><td>%s</td><td><a href=\"html/%s.html\">HTML</a></td><td><a href=\"txt/%s.txt\">Text</a></td><td><a href=\"json/%s.json\">JSON</a></td></tr>\n",
            rel, base, base, base);
    ctx->file_count++;
    return 0;
}

static void walk_directory(BulkContext *ctx, const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "Error: Cannot open directory '%s'\n", dir_path);
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char path[MAX_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            walk_directory(ctx, path);
        } else if (S_ISREG(st.st_mode) && ends_with(path, ".c")) {
            bulk_process_file(ctx, path);
        }
    }
    closedir(dir);
}

static int process_directory(const char *root, const char *out_dir) {
    struct stat st;
    if (stat(root, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: '%s' is not a directory\n", root);
        return -1;
    }

    if (ensure_dir(out_dir) != 0) return -1;

    char txt_dir[MAX_PATH_LEN];
    char json_dir[MAX_PATH_LEN];
    char html_dir[MAX_PATH_LEN];
    snprintf(txt_dir, sizeof(txt_dir), "%s/txt", out_dir);
    snprintf(json_dir, sizeof(json_dir), "%s/json", out_dir);
    snprintf(html_dir, sizeof(html_dir), "%s/html", out_dir);
    if (ensure_dir(txt_dir) != 0) return -1;
    if (ensure_dir(json_dir) != 0) return -1;
    if (ensure_dir(html_dir) != 0) return -1;

    char index_path[MAX_PATH_LEN];
    snprintf(index_path, sizeof(index_path), "%s/index.html", out_dir);
    FILE *index = fopen(index_path, "w");
    if (!index) {
        fprintf(stderr, "Error: Cannot write '%s'\n", index_path);
        return -1;
    }
    fprintf(index, "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>DOCUNATION Index</title></head><body>\n");
    fprintf(index, "<h1>DOCUNATION Output</h1><p>Root: %s</p>\n", root);
    fprintf(index, "<table border=1 cellspacing=0 cellpadding=4>\n");
    fprintf(index, "<tr><th>Source</th><th>HTML</th><th>Text</th><th>JSON</th></tr>\n");

    BulkContext ctx = { root, strlen(root), out_dir, index, 0 };
    walk_directory(&ctx, root);

    fprintf(index, "</table>\n<p>Total files: %zu</p>\n</body></html>\n", ctx.file_count);
    fclose(index);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * OUTPUT FORMATTERS
 * ═══════════════════════════════════════════════════════════════════════════ */

static void output_text(DOCUNATION *doc, FILE *out) {
#define PTF(...) fprintf(out, __VA_ARGS__)
#define PTC(ch) fputc((ch), out)
    PTF("%s%s", C(COL_BOLD), C(COL_MAGENTA));
    for (int i = 0; i < 70; i++) PTC('=');
    PTF("%s\n", C(COL_RESET));
    PTF("%sModule: %s%s\n", C(COL_BOLD), doc->module_name, C(COL_RESET));
    PTF("File: %s\n", doc->filepath);
    PTF("Generated: %s\n", doc->timestamp);
    PTF("%s%s", C(COL_MAGENTA), C(COL_BOLD));
    for (int i = 0; i < 70; i++) PTC('=');
    PTF("%s\n", C(COL_RESET));

    if (doc->docstring[0]) {
        PTF("\n%sDESCRIPTION%s\n", C(COL_CYAN), C(COL_RESET));
        PTF("    %s\n", doc->docstring);
    }

    PTF("\n%sINCLUDES%s\n", C(COL_BLUE), C(COL_RESET));
    for (int i = 0; i < doc->node_count; i++) {
        if (doc->nodes[i].type == NODE_INCLUDE) {
            PTF("    %s%s%s\n", C(COL_GREEN), doc->nodes[i].name, C(COL_RESET));
        }
    }

    int has_macros = 0;
    for (int i = 0; i < doc->node_count; i++) {
        if (doc->nodes[i].type == NODE_MACRO) {
            if (!has_macros) {
                PTF("\n%sMACROS%s\n", C(COL_BLUE), C(COL_RESET));
                has_macros = 1;
            }
            PTF("    %s%s%s\n", C(COL_GREEN), doc->nodes[i].name, C(COL_RESET));
            if (doc->nodes[i].docstring[0]) {
                PTF("        %s%s%s\n", C(COL_CYAN), doc->nodes[i].docstring, C(COL_RESET));
            }
        }
    }

    int has_vars = 0;
    for (int i = 0; i < doc->node_count; i++) {
        if (doc->nodes[i].type == NODE_VARIABLE) {
            if (!has_vars) {
                PTF("\n%sDATA%s\n", C(COL_BLUE), C(COL_RESET));
                has_vars = 1;
            }
            DocNode *n = &doc->nodes[i];
            PTF("    %s%s%s", C(COL_GREEN), n->name, C(COL_RESET));
            if (n->is_static) PTF(" [static]");
            PTF("\n");
            PTF("        %s\n", n->signature);
            if (n->docstring[0]) {
                PTF("        %s%s%s\n", C(COL_CYAN), n->docstring, C(COL_RESET));
            }
        }
    }

    int has_types = 0;
    for (int i = 0; i < doc->node_count; i++) {
        if (doc->nodes[i].type == NODE_TYPEDEF ||
            doc->nodes[i].type == NODE_STRUCT ||
            doc->nodes[i].type == NODE_UNION ||
            doc->nodes[i].type == NODE_ENUM) {
            if (!has_types) {
                PTF("\n%sTYPES%s\n", C(COL_BLUE), C(COL_RESET));
                has_types = 1;
            }
            PTF("    %s%s%s (%s)\n", C(COL_GREEN), doc->nodes[i].name, C(COL_RESET),
                node_type_names[doc->nodes[i].type]);
            if (doc->nodes[i].docstring[0]) {
                PTF("        %s%s%s\n", C(COL_CYAN), doc->nodes[i].docstring, C(COL_RESET));
            }
        }
    }

    int has_funcs = 0;
    for (int i = 0; i < doc->node_count; i++) {
        if (doc->nodes[i].type == NODE_FUNCTION) {
            if (!has_funcs) {
                PTF("\n%sFUNCTIONS%s\n", C(COL_BLUE), C(COL_RESET));
                has_funcs = 1;
            }
            DocNode *n = &doc->nodes[i];
            PTF("    %s%s%s", C(COL_GREEN), n->name, C(COL_RESET));
            if (n->is_static) PTF(" [static]");
            if (n->is_inline) PTF(" [inline]");
            if (n->is_extern) PTF(" [extern]");
            PTF("\n");
            PTF("        %s\n", n->signature);
            if (n->docstring[0]) {
                PTF("        %s%s%s\n", C(COL_CYAN), n->docstring, C(COL_RESET));
            }
        }
    }

    PTF("\n%s%s", C(COL_MAGENTA), C(COL_BOLD));
    for (int i = 0; i < 70; i++) PTC('=');
    PTF("%s\n", C(COL_RESET));
#undef PTF
#undef PTC
}

static void output_json(DOCUNATION *doc, FILE *out) {
    fprintf(out, "{\n");
    fprintf(out, "  \"filepath\": \"%s\",\n", doc->filepath);
    fprintf(out, "  \"module_name\": \"%s\",\n", doc->module_name);
    fprintf(out, "  \"timestamp\": \"%s\",\n", doc->timestamp);

    fprintf(out, "  \"docstring\": \"");
    for (char *p = doc->docstring; *p; p++) {
        if (*p == '"') fprintf(out, "\\\"");
        else if (*p == '\n') fprintf(out, "\\n");
        else if (*p == '\\') fprintf(out, "\\\\");
        else fputc(*p, out);
    }
    fprintf(out, "\",\n");

    fprintf(out, "  \"nodes\": [\n");
    for (int i = 0; i < doc->node_count; i++) {
        DocNode *n = &doc->nodes[i];
        fprintf(out, "    {\n");
        fprintf(out, "      \"name\": \"%s\",\n", n->name);
        fprintf(out, "      \"type\": \"%s\",\n", node_type_names[n->type]);
        fprintf(out, "      \"line\": %d,\n", n->line);
        fprintf(out, "      \"signature\": \"");
        for (char *p = n->signature; *p; p++) {
            if (*p == '"') fprintf(out, "\\\"");
            else if (*p == '\n') fprintf(out, "\\n");
            else if (*p == '\\') fprintf(out, "\\\\");
            else fputc(*p, out);
        }
        fprintf(out, "\",\n");
        fprintf(out, "      \"docstring\": \"");
        for (char *p = n->docstring; *p; p++) {
            if (*p == '"') fprintf(out, "\\\"");
            else if (*p == '\n') fprintf(out, "\\n");
            else if (*p == '\\') fprintf(out, "\\\\");
            else fputc(*p, out);
        }
        fprintf(out, "    }%s\n", i < doc->node_count-1 ? "," : "");
    }
    fprintf(out, "  ]\n");
    fprintf(out, "}\n");
}

static void output_html(DOCUNATION *doc, FILE *out) {
    fprintf(out, "<!DOCTYPE html>\n<html>\n<head>\n");
    fprintf(out, "<meta charset=\"UTF-8\">\n<title>%s</title>\n", doc->module_name);
    fprintf(out, "</head>\n<body bgcolor=\"#f0f0f0\">\n");

    fprintf(out, "<table width=\"100%%\" cellspacing=0 cellpadding=2 border=0>\n");
    fprintf(out, "<tr bgcolor=\"#7799ee\"><td>&nbsp;</td>\n");
    fprintf(out, "<td><font face=\"helvetica, arial\" size=\"+1\"><strong>%s</strong></font></td></tr></table>\n",
            doc->module_name);
    fprintf(out, "<p><tt>%s</tt></p>\n", doc->filepath);

    if (doc->docstring[0]) {
        fprintf(out, "<p><table width=\"100%%\" cellspacing=0 cellpadding=2 border=0>\n");
        fprintf(out, "<tr bgcolor=\"#eeaa77\"><td>&nbsp;</td>\n");
        fprintf(out, "<td><strong>Description</strong></td></tr></table>\n");
        fprintf(out, "<pre>%s</pre>\n", doc->docstring);
    }

    int has_includes = 0;
    for (int i = 0; i < doc->node_count; i++) {
        if (doc->nodes[i].type == NODE_INCLUDE) {
            if (!has_includes) {
                fprintf(out, "<p><table width=\"100%%\" cellspacing=0 cellpadding=2 border=0>\n");
                fprintf(out, "<tr bgcolor=\"#aa55cc\"><td>&nbsp;</td>\n");
                fprintf(out, "<td><strong>Includes</strong></td></tr></table>\n");
                fprintf(out, "<dl>\n");
                has_includes = 1;
            }
            fprintf(out, "<dt><tt>%s</tt></dt>\n", doc->nodes[i].signature);
        }
    }
    if (has_includes) fprintf(out, "</dl>\n");

    int has_macros = 0;
    for (int i = 0; i < doc->node_count; i++) {
        DocNode *n = &doc->nodes[i];
        if (n->type == NODE_MACRO) {
            if (!has_macros) {
                fprintf(out, "<p><table width=\"100%%\" cellspacing=0 cellpadding=2 border=0>\n");
                fprintf(out, "<tr bgcolor=\"#aa55cc\"><td>&nbsp;</td>\n");
                fprintf(out, "<td><strong>Macros</strong></td></tr></table>\n");
                fprintf(out, "<dl>\n");
                has_macros = 1;
            }
            fprintf(out, "<dt><a name=\"%s\"><strong>%s</strong></a></dt>\n", n->name, n->name);
            fprintf(out, "<dd><tt>%s</tt></dd>\n", n->signature);
            if (n->docstring[0]) fprintf(out, "<dd>%s</dd>\n", n->docstring);
        }
    }
    if (has_macros) fprintf(out, "</dl>\n");

    int has_vars = 0;
    for (int i = 0; i < doc->node_count; i++) {
        DocNode *n = &doc->nodes[i];
        if (n->type == NODE_VARIABLE) {
            if (!has_vars) {
                fprintf(out, "<p><table width=\"100%%\" cellspacing=0 cellpadding=2 border=0>\n");
                fprintf(out, "<tr bgcolor=\"#aa55cc\"><td>&nbsp;</td>\n");
                fprintf(out, "<td><strong>Data</strong></td></tr></table>\n");
                fprintf(out, "<dl>\n");
                has_vars = 1;
            }
            fprintf(out, "<dt><a name=\"%s\"><strong>%s</strong></a></dt>\n", n->name, n->name);
            fprintf(out, "<dd><tt>%s</tt></dd>\n", n->signature);
            if (n->docstring[0]) fprintf(out, "<dd>%s</dd>\n", n->docstring);
        }
    }
    if (has_vars) fprintf(out, "</dl>\n");

    int has_types = 0;
    for (int i = 0; i < doc->node_count; i++) {
        DocNode *n = &doc->nodes[i];
        if (n->type == NODE_TYPEDEF || n->type == NODE_STRUCT ||
            n->type == NODE_UNION || n->type == NODE_ENUM) {
            if (!has_types) {
                fprintf(out, "<p><table width=\"100%%\" cellspacing=0 cellpadding=2 border=0>\n");
                fprintf(out, "<tr bgcolor=\"#aa55cc\"><td>&nbsp;</td>\n");
                fprintf(out, "<td><strong>Types</strong></td></tr></table>\n");
                fprintf(out, "<dl>\n");
                has_types = 1;
            }
            fprintf(out, "<dt><a name=\"%s\"><strong>%s</strong></a> (%s)</dt>\n",
                    n->name, n->name, node_type_names[n->type]);
            fprintf(out, "<dd><tt>%s</tt></dd>\n", n->signature);
            if (n->docstring[0]) fprintf(out, "<dd>%s</dd>\n", n->docstring);
        }
    }
    if (has_types) fprintf(out, "</dl>\n");

    int has_funcs = 0;
    for (int i = 0; i < doc->node_count; i++) {
        DocNode *n = &doc->nodes[i];
        if (n->type == NODE_FUNCTION) {
            if (!has_funcs) {
                fprintf(out, "<p><table width=\"100%%\" cellspacing=0 cellpadding=2 border=0>\n");
                fprintf(out, "<tr bgcolor=\"#aa55cc\"><td>&nbsp;</td>\n");
                fprintf(out, "<td><strong>Functions</strong></td></tr></table>\n");
                fprintf(out, "<dl>\n");
                has_funcs = 1;
            }
            fprintf(out, "<dt><a name=\"%s\"><strong>%s</strong></a>(", n->name, n->name);
            char *paren = strchr(n->signature, '(');
            if (paren) {
                char *end = strrchr(n->signature, ')');
                if (end) {
                    int len = end - paren - 1;
                    if (len > 0) fprintf(out, "%.*s", len, paren + 1);
                }
            }
            fprintf(out, ")</dt>\n");
            fprintf(out, "<dd><tt>%s</tt></dd>\n", n->signature);
            if (n->docstring[0]) fprintf(out, "<dd>%s</dd>\n", n->docstring);
        }
    }
    if (has_funcs) fprintf(out, "</dl>\n");

    fprintf(out, "<hr>\n<p><small>Generated by DOCUNATION.C - Ring 1</small></p>\n");
    fprintf(out, "</body>\n</html>\n");
}


/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

static void print_usage(const char *prog) {
    printf("DOCUNATION %s - Documentation Generator for C\n\n", DOCUNATION_VERSION);
    printf("Usage: %s [options] <file.c>\n", prog);
    printf("       %s -R <root> -O <output>\n\n", prog);
    printf("Options:\n");
    printf("  -j          Output JSON format\n");
    printf("  -h          Output HTML format\n");
    printf("  -n          No color output\n");
    printf("  -R <dir>    Recursively document .c files under <dir>\n");
    printf("  -O <dir>    Output directory for bulk mode\n");
    printf("  -v          Show version\n");
    printf("  --help      Show this help\n\n");
    printf("Examples:\n");
    printf("  %s myfile.c           # Document a C file\n", prog);
    printf("  %s -j myfile.c        # Output JSON\n", prog);
    printf("  %s -h myfile.c > doc.html  # Output HTML\n", prog);
    printf("  %s -R src -O docs     # Document an entire tree\n", prog);
}

int main(int argc, char **argv) {
    char *filename = NULL;
    int format = 0;  /* 0=text, 1=json, 2=html */
    const char *bulk_root = NULL;
    const char *bulk_out = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-j") == 0) {
            format = 1;
        } else if (strcmp(argv[i], "-h") == 0) {
            format = 2;
        } else if (strcmp(argv[i], "-n") == 0) {
            use_color = 0;
        } else if (strcmp(argv[i], "-v") == 0) {
            printf("DOCUNATION %s (Ring 1)\n", DOCUNATION_VERSION);
            return 0;
        } else if (strcmp(argv[i], "-R") == 0) {
            if (i + 1 < argc) bulk_root = argv[++i];
        } else if (strcmp(argv[i], "-O") == 0) {
            if (i + 1 < argc) bulk_out = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            filename = argv[i];
        }
    }

    if (bulk_root) {
        if (!bulk_out) {
            fprintf(stderr, "Error: -O <output_dir> required with -R\n");
            return 1;
        }
        return process_directory(bulk_root, bulk_out) == 0 ? 0 : 1;
    }

    if (!filename) {
        print_usage(argv[0]);
        return 1;
    }

    DOCUNATION *doc = parse_document(filename);
    if (!doc) {
        return 1;
    }

    switch (format) {
        case 1: output_json(doc, stdout); break;
        case 2: output_html(doc, stdout); break;
        default: output_text(doc, stdout); break;
    }

    free(doc);
    return 0;
}
