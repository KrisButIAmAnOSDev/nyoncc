#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* nyoncc - C to AArch64 Linux Cross-Compiler */

/* ============================================================
 * Token types
 * ============================================================ */
typedef enum {
    TK_INT=256, TK_CHAR, TK_VOID, TK_RETURN, TK_IF, TK_ELSE,
    TK_WHILE, TK_FOR, TK_CONST, TK_IDENT, TK_NUM, TK_STR, TK_CHARLIT,
    TK_PLUS, TK_MINUS, TK_MUL, TK_DIV, TK_MOD,
    TK_ASSIGN, TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_LEQ, TK_GEQ,
    TK_AND, TK_OR, TK_NOT, TK_QUESTION, TK_BNOT,
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET, TK_SEMICOLON, TK_COMMA,
    TK_COLON, TK_PERIOD, TK_ARROW, TK_AMP, TK_STAR,
    TK_BREAK, TK_CONTINUE, TK_EOF
} TokenType;

typedef struct { TokenType type; char *str; int val; int ln; } Token;

/* ============================================================
 * Types, symbols, expressions, statements, functions
 * ============================================================ */
typedef struct { int kind; int ptr; } Type;
typedef struct Sym Sym;
typedef struct Expr Expr;
typedef struct Stmt Stmt;
typedef struct FuncDef FuncDef;

struct Sym { char *name; Type ty; int off; int loc; int arr_size; Sym *nx; };

struct Expr {
    int kind; int op; Type ty;
    Expr *l, *r;
    Sym *var;
    int val; char *sval;
    int nargs;
};

#define EX_LIT 1
#define EX_VAR 2
#define EX_BINOP 3
#define EX_UNOP 4
#define EX_CALL 5
#define EX_ASSIGN 6

struct Stmt {
    int kind; Stmt *nx; Sym *decl; Expr *ex;
    Stmt *f_init; Expr *f_cond, *f_inc;
    Stmt *then, *els, *body;
};

#define ST_DECL 10
#define ST_EXPR 11
#define ST_IF 12
#define ST_WHILE 13
#define ST_FOR 14
#define ST_RETURN 15
#define ST_BLOCK 16
#define ST_BREAK 17
#define ST_CONTINUE 18

struct FuncDef {
    char *name; Type rty; int pc;
    char **pns; Type *pts; Stmt *body; FuncDef *nx;
};

/* ============================================================
 * Global state
 * ============================================================ */
static char *src;
static int sp, slen, cl;
static Token *toks;
static int tc, tc2;
static int cp;

static Sym *gsym, *lsym;
static int gvc;
static int temp_off = 8;
static char *gvn[512]; static Type gvt[512]; static int garr[512]; static Expr *ginit[512];

/* ============================================================
 * Lexer
 * ============================================================ */
static void lex(void) {
    tc = 0; cl = 1; sp = 0; tc2 = 256;
    toks = malloc(tc2 * sizeof(Token));
    while (src[sp]) {
        while (src[sp]==' '||src[sp]=='\t'||src[sp]=='\r') sp++;
        if (src[sp]=='\n') { cl++; sp++; continue; }
        if (src[sp]=='/' && src[sp+1]=='/') { while(src[sp] && src[sp]!='\n') sp++; continue; }
        if (src[sp]=='/' && src[sp+1]=='*') {
            sp += 2;
            while (src[sp] && !(src[sp]=='*' && src[sp+1]=='/')) {
                if (src[sp]=='\n') cl++;
                sp++;
            }
            sp += 2; continue;
        }
        if (isalpha(src[sp]) || src[sp]=='_') {
            char b[256]; int i = 0;
            while (isalnum(src[sp]) || src[sp]=='_') {
                if (i < 255) b[i++] = src[sp];
                sp++;
            }
            b[i] = 0;
            TokenType tt = TK_IDENT;
            if (!strcmp(b,"int")) tt=TK_INT;
            else if (!strcmp(b,"char")) tt=TK_CHAR;
            else if (!strcmp(b,"void")) tt=TK_VOID;
            else if (!strcmp(b,"return")) tt=TK_RETURN;
            else if (!strcmp(b,"if")) tt=TK_IF;
            else if (!strcmp(b,"else")) tt=TK_ELSE;
            else if (!strcmp(b,"while")) tt=TK_WHILE;
            else if (!strcmp(b,"for")) tt=TK_FOR;
            else if (!strcmp(b,"const")) tt=TK_CONST;
            else if (!strcmp(b,"break")) tt=TK_BREAK;
            else if (!strcmp(b,"continue")) tt=TK_CONTINUE;
            if (tc >= tc2) { tc2 *= 2; toks = realloc(toks, tc2 * sizeof(Token)); }
            toks[tc].type = tt; toks[tc].str = strdup(b);
            toks[tc].val = 0; toks[tc].ln = cl; tc++; continue;
        }
        if (isdigit(src[sp])) {
            int v = 0;
            while (isdigit(src[sp])) { v = v*10 + (src[sp]-'0'); sp++; }
            if (tc >= tc2) { tc2 *= 2; toks = realloc(toks, tc2 * sizeof(Token)); }
            toks[tc].type = TK_NUM; toks[tc].str = NULL;
            toks[tc].val = v; toks[tc].ln = cl; tc++; continue;
        }
        if (src[sp]=='"') {
            sp++; char b[4096]; int i = 0;
            while (src[sp] && src[sp]!='"') {
                if (src[sp]=='\\' && src[sp+1]) {
                    sp++;
                    switch(src[sp]) {
                        case 'n': b[i++]='\n'; break;
                        case 't': b[i++]='\t'; break;
                        case '\\': b[i++]='\\'; break;
                        case '"': b[i++]='"'; break;
                        default: b[i++]=src[sp]; break;
                    }
                } else b[i++] = src[sp++];
            }
            sp++; b[i] = 0;
            if (tc >= tc2) { tc2 *= 2; toks = realloc(toks, tc2 * sizeof(Token)); }
            toks[tc].type = TK_STR; toks[tc].str = strdup(b);
            toks[tc].val = i; toks[tc].ln = cl; tc++; continue;
        }
        if (src[sp]=='\'') {
            sp++; int ch = src[sp];
            if (src[sp]=='\\') { sp++; switch(src[sp]) {
                case 'n': ch='\n'; break; case 't': ch='\t'; break;
                case '\\': ch='\\'; break; case '\'': ch='\''; break;
            }}
            sp++;
            if (tc >= tc2) { tc2 *= 2; toks = realloc(toks, tc2 * sizeof(Token)); }
            toks[tc].type = TK_CHARLIT; toks[tc].str = NULL;
            toks[tc].val = ch; toks[tc].ln = cl; tc++; continue;
        }
        TokenType tt;
        switch (src[sp]) {
            case '+': tt=TK_PLUS; break; case '-': tt=TK_MINUS; break;
            case '*': tt=TK_MUL; break; case '/': tt=TK_DIV; break;
            case '%': tt=TK_MOD; break;
            case '<': tt=(src[sp+1]=='=')?TK_LEQ:TK_LT; if(src[sp+1]=='=')sp++; break;
            case '>': tt=(src[sp+1]=='=')?TK_GEQ:TK_GT; if(src[sp+1]=='=')sp++; break;
            case '=': tt=(src[sp+1]=='=')?TK_EQ:TK_ASSIGN; if(src[sp+1]=='=')sp++; break;
            case '!': tt=(src[sp+1]=='=')?TK_NEQ:TK_NOT; if(src[sp+1]=='=')sp++; break;
            case '&': tt=(src[sp+1]=='&')?TK_AND:TK_AMP; if(src[sp+1]=='&')sp++; break;
            case '|': tt=(src[sp+1]=='|')?TK_OR:TK_AMP; if(src[sp+1]=='|')sp++; break;
            case '(':tt=TK_LPAREN;break;case ')':tt=TK_RPAREN;break;
            case '{':tt=TK_LBRACE;break;case '}':tt=TK_RBRACE;break;
            case '[':tt=TK_LBRACKET;break;case ']':tt=TK_RBRACKET;break;
            case ';':tt=TK_SEMICOLON;break;case ',':tt=TK_COMMA;break;
            case ':':tt=TK_COLON;break;case '.':tt=TK_PERIOD;break;
            case '~':tt=TK_BNOT;break;
            default: fprintf(stderr,"Unknown '%c' line %d\n",src[sp],cl); sp++; continue;
        }
        if (tc >= tc2) { tc2 *= 2; toks = realloc(toks, tc2 * sizeof(Token)); }
        toks[tc].type = tt; toks[tc].str = NULL;
        toks[tc].val = 0; toks[tc].ln = cl; tc++; sp++;
    }
    if (tc >= tc2) { tc2 *= 2; toks = realloc(toks, tc2 * sizeof(Token)); }
    toks[tc].type = TK_EOF; toks[tc].str = NULL; toks[tc].val = 0; toks[tc].ln = cl; tc++;
}

/* ============================================================
 * Parser helpers
 * ============================================================ */
#define CUR() toks[cp]
static void adv(void) { cp++; }
static void expect(TokenType t) {
    if (CUR().type != t) { fprintf(stderr,"L%d:expect %d got %d\n",CUR().ln,t,CUR().type); exit(1); }
    adv();
}

/* ============================================================
 * Forward declarations for parser
 * ============================================================ */
static Type pty(void);
static Expr *pe(void);
static Expr *ppi(void);
static Expr *ppo(void);
static Expr *pun(void);
static Expr *pmu(void);
static Expr *pad(void);
static Expr *pre(void);
static Expr *peq(void);
static Expr *pan(void);
static Expr *po(void);
static Expr *pa(void);
static Stmt *ps(void);
static Stmt *pb(void);
static Stmt *pif(void);
static Stmt *pw(void);
static Stmt *pf(void);
static Stmt *pr(void);
static Stmt *pd(void);
static Stmt *pexp(void);
static FuncDef *pf2(void);
static FuncDef *ppr(void);

/* ============================================================
 * Parser: type
 * ============================================================ */
static Type pty(void) {
    Type t = {0, 0};
    if (CUR().type == TK_CONST) adv();
    if (CUR().type == TK_INT) { t.kind = 0; adv(); }
    else if (CUR().type == TK_CHAR) { t.kind = 1; adv(); }
    else if (CUR().type == TK_VOID) { t.kind = 2; adv(); }
    if (CUR().type == TK_MUL) { adv(); t.ptr = 1; }
    return t;
}

/* ============================================================
 * Parser: expressions (precedence climbing)
 * ============================================================ */
static Type mt(int k,int p){Type t={k,p};return t;}
static Expr *nx(int k) { Expr *e = calloc(1,sizeof(Expr)); e->kind=k; e->ty=mt(0,0); return e; }
static Sym *lookup(const char *n);

static Expr *ppi(void) {
    Token t = CUR();
    switch (t.type) {
        case TK_NUM: adv(); { Expr *e=nx(EX_LIT); e->val=t.val; e->ty=mt(0,0); return e; }
        case TK_CHARLIT: adv(); { Expr *e=nx(EX_LIT); e->val=t.val; e->ty=mt(1,0); return e; }
        case TK_STR: adv(); { Expr *e=nx(EX_LIT); e->sval=strdup(t.str); e->ty=mt(0,1); e->val=t.val; return e; }
        case TK_IDENT: {
            adv(); Sym *s = lookup(t.str);
            if (CUR().type == TK_LPAREN) {
                Expr *e = nx(EX_CALL); e->var = s; e->ty = mt(0,0);
                adv(); int nc = 0; Expr **tp = &e->l;
                if (CUR().type != TK_RPAREN) { *tp = pe(); nc++; tp = &(*tp)->r; }
                while (CUR().type == TK_COMMA) { adv(); *tp = pe(); nc++; tp = &(*tp)->r; }
                expect(TK_RPAREN); e->nargs = nc; return e;
            } else { Expr *e = nx(EX_VAR); e->var = s; e->ty = s ? s->ty : mt(0,0); return e; }
        }
        case TK_LPAREN: adv(); { Expr *e = pe(); expect(TK_RPAREN); return e; }
        default: fprintf(stderr,"L%d:bad prim tok=%d\n",t.ln,t.type); exit(1);
    }
}

static Expr *ppo(void) {
    Expr *e = ppi();
    while (CUR().type == TK_LBRACKET) {
        adv(); Expr *i = pe(); expect(TK_RBRACKET);
        Expr *b = nx(EX_BINOP); b->op='+'; b->l=e; b->r=i;
        b->ty = e->ty.ptr ? mt(e->ty.kind,0) : mt(0,0); e = b;
    }
    return e;
}

static Expr *pun(void) {
    if (CUR().type==TK_MINUS||CUR().type==TK_NOT||CUR().type==TK_AMP||CUR().type==TK_MUL||CUR().type==TK_BNOT) {
        Token t = CUR(); adv(); Expr *e = pun(); Expr *ue = nx(EX_UNOP);
        ue->op = t.type; ue->l = e;
        if (t.type==TK_MUL) ue->ty = e->ty.ptr ? mt(e->ty.kind,0) : e->ty;
        else if (t.type==TK_AMP) ue->ty = mt(e->ty.kind,1);
        else ue->ty = e->ty;
        return ue;
    }
    return ppo();
}

static Expr *pmu(void) {
    Expr *e = pun();
    while (CUR().type==TK_MUL||CUR().type==TK_DIV||CUR().type==TK_MOD) {
        Token t = CUR(); adv(); Expr *r = pun();
        Expr *b = nx(EX_BINOP); b->op=t.type; b->l=e; b->r=r; b->ty=mt(0,0); e=b;
    }
    return e;
}

static Expr *pad(void) {
    Expr *e = pmu();
    while (CUR().type==TK_PLUS||CUR().type==TK_MINUS) {
        Token t = CUR(); adv(); Expr *r = pmu();
        Expr *b = nx(EX_BINOP); b->op=t.type; b->l=e; b->r=r; b->ty=mt(0,0); e=b;
    }
    return e;
}

static Expr *pre(void) {
    Expr *e = pad();
    while (CUR().type==TK_LT||CUR().type==TK_GT||CUR().type==TK_LEQ||CUR().type==TK_GEQ) {
        Token t = CUR(); adv(); Expr *r = pad();
        Expr *b = nx(EX_BINOP); b->op=t.type; b->l=e; b->r=r; b->ty=mt(0,0); e=b;
    }
    return e;
}

static Expr *peq(void) {
    Expr *e = pre();
    while (CUR().type==TK_EQ||CUR().type==TK_NEQ) {
        Token t = CUR(); adv(); Expr *r = pre();
        Expr *b = nx(EX_BINOP); b->op=t.type; b->l=e; b->r=r; b->ty=mt(0,0); e=b;
    }
    return e;
}

static Expr *pan(void) {
    Expr *e = peq();
    while (CUR().type==TK_AND) { adv(); Expr *r = peq();
        Expr *b = nx(EX_BINOP); b->op=TK_AND; b->l=e; b->r=r; b->ty=mt(0,0); e=b; }
    return e;
}

static Expr *po(void) {
    Expr *e = pan();
    while (CUR().type==TK_OR) { adv(); Expr *r = pan();
        Expr *b = nx(EX_BINOP); b->op=TK_OR; b->l=e; b->r=r; b->ty=mt(0,0); e=b; }
    return e;
}

static Expr *pa(void) {
    Expr *e = po();
    if (CUR().type==TK_ASSIGN) { adv(); Expr *r = pa();
        Expr *a = nx(EX_ASSIGN); a->l=e; a->r=r; a->ty=e->ty; return a; }
    return e;
}

static Expr *pe(void) { return pa(); }

/* ============================================================
 * Parser: statements and functions
 * ============================================================ */
static Stmt *ns(int k) { Stmt *s = calloc(1,sizeof(Stmt)); s->kind=k; return s; }

static Sym *lookup(const char *n) {
    for (Sym *s=lsym; s; s=s->nx) if (!strcmp(s->name,n)) return s;
    for (Sym *s=gsym; s; s=s->nx) if (!strcmp(s->name,n)) return s;
    return NULL;
}
static Sym *ig(const char *n, Type t) {
    if (lookup(n)) return lookup(n);
    Sym *s = malloc(sizeof(Sym)); s->name=strdup(n); s->ty=t; s->loc=0; s->nx=gsym; gsym=s; return s;
}
static Sym *il(const char *n, Type t, int arr) {
    for (Sym *s=lsym; s; s=s->nx) if (!strcmp(s->name,n)) return s;
    Sym *s = malloc(sizeof(Sym)); s->name=strdup(n); s->ty=t; s->loc=1; s->off=0; s->arr_size=arr; s->nx=lsym; lsym=s; return s;
}

static Stmt *pd(void) {
    Type t = pty(); Stmt *s = ns(ST_DECL);
    s->decl = malloc(sizeof(Sym)); s->decl->name = strdup(CUR().str);
    s->decl->ty = t; s->decl->off = 0;
    Sym *sym = il(s->decl->name, t, 0); adv();
    if (CUR().type==TK_LBRACKET) { adv(); s->decl->arr_size=CUR().val; sym->arr_size=CUR().val; expect(TK_NUM); expect(TK_RBRACKET); }
    if (CUR().type==TK_ASSIGN) { adv(); s->ex = pe(); }
    expect(TK_SEMICOLON); return s;
}
static Stmt *pexp(void) { Stmt *s = ns(ST_EXPR); if (CUR().type!=TK_SEMICOLON) s->ex=pe(); expect(TK_SEMICOLON); return s; }
static Stmt *pb(void) {
    Stmt *s = ns(ST_BLOCK); Stmt **tp = &s->nx;
    expect(TK_LBRACE);
    while (CUR().type!=TK_RBRACE && CUR().type!=TK_EOF) {
        if (CUR().type==TK_INT||CUR().type==TK_CHAR||(CUR().type==TK_VOID&&CUR().type!=TK_RPAREN)||CUR().type==TK_CONST)
            *tp = pd();
        else if (CUR().type==TK_RETURN) *tp = pr();
        else if (CUR().type==TK_IF) *tp = pif();
        else if (CUR().type==TK_WHILE) *tp = pw();
        else if (CUR().type==TK_FOR) *tp = pf();
        else if (CUR().type==TK_BREAK) { *tp=ns(ST_BREAK); adv(); expect(TK_SEMICOLON); }
        else if (CUR().type==TK_CONTINUE) { *tp=ns(ST_CONTINUE); adv(); expect(TK_SEMICOLON); }
        else *tp = pexp();
        tp = &(*tp)->nx;
    }
    expect(TK_RBRACE); return s;
}
static Stmt *pif(void) { Stmt *s=ns(ST_IF); adv(); expect(TK_LPAREN); s->f_cond=pe(); expect(TK_RPAREN);
    s->then=ps(); if (CUR().type==TK_ELSE){adv(); s->els=ps();} return s; }
static Stmt *pw(void) { Stmt *s=ns(ST_WHILE); adv(); expect(TK_LPAREN); s->f_cond=pe(); expect(TK_RPAREN); s->body=ps(); return s; }
static Stmt *pf(void) { Stmt *s=ns(ST_FOR); adv(); expect(TK_LPAREN);
    if (CUR().type!=TK_SEMICOLON) {
        if (CUR().type==TK_INT||CUR().type==TK_CHAR||CUR().type==TK_CONST) s->f_init=pd();
        else s->f_init=pexp();
    }
    if (CUR().type!=TK_SEMICOLON) s->f_cond=pe();
    expect(TK_SEMICOLON);
    if (CUR().type!=TK_SEMICOLON) s->f_inc=pe();
    expect(TK_RPAREN); s->body=ps(); return s; }
static Stmt *pr(void) { Stmt *s=ns(ST_RETURN); adv(); if (CUR().type!=TK_SEMICOLON) s->ex=pe(); expect(TK_SEMICOLON); return s; }
static Stmt *ps(void) {
    Token t = CUR();
    if (t.type==TK_INT||t.type==TK_CHAR||(t.type==TK_VOID&&CUR().type!=TK_RPAREN)||t.type==TK_CONST) return pd();
    if (t.type==TK_RETURN) return pr();
    if (t.type==TK_IF) return pif();
    if (t.type==TK_WHILE) return pw();
    if (t.type==TK_FOR) return pf();
    if (t.type==TK_BREAK){Stmt *s=ns(ST_BREAK);adv();expect(TK_SEMICOLON);return s;}
    if (t.type==TK_CONTINUE){Stmt *s=ns(ST_CONTINUE);adv();expect(TK_SEMICOLON);return s;}
    if (t.type==TK_LBRACE) return pb();
    return pexp();
}
static FuncDef *pf2(void) {
    Type ret = pty();
    if (CUR().type == TK_LPAREN || CUR().type == TK_EOF) return NULL;
    char *name = strdup(CUR().str);
    ig(name, mt(0,0));
    { lsym=NULL; }
    adv();
    if (CUR().type != TK_LPAREN) {
        int arr = 0;
        if (CUR().type == TK_LBRACKET) {
            adv(); arr = CUR().val; expect(TK_NUM); expect(TK_RBRACKET);
        }
        if (CUR().type == TK_ASSIGN) { adv(); ginit[gvc] = pe(); }
        else ginit[gvc] = NULL;
        expect(TK_SEMICOLON);
        gvn[gvc]=name; gvt[gvc]=ret; garr[gvc]=arr; gvc++;
        return NULL;
    }
    expect(TK_LPAREN);
    FuncDef *f = malloc(sizeof(FuncDef)); f->name=name; f->rty=ret;
    f->pc=0; f->pns=NULL; f->pts=NULL; f->nx=NULL;
    int pc=0; char **pn=NULL; Type *pt=NULL;
    if (!(CUR().type==TK_VOID&&CUR().type!=TK_RPAREN)) {
        while (CUR().type!=TK_RPAREN && CUR().type!=TK_EOF && CUR().type!=TK_LPAREN) {
            Type pt2 = pty();
            pn=realloc(pn,(pc+1)*sizeof(char*)); pn[pc]=strdup(CUR().str);
            pt=realloc(pt,(pc+1)*sizeof(Type)); pt[pc]=pt2;
            il(pn[pc],pt2,0); adv(); pc++;
            if (CUR().type==TK_COMMA) adv();
        }
    }
    f->pc=pc; f->pns=pn; f->pts=pt;
    expect(TK_RPAREN); f->body=pb(); return f;
}
static FuncDef *ppr(void) {
    FuncDef *h=NULL, **tp=&h;
    while (CUR().type!=TK_EOF) {
        FuncDef *f=pf2();
        if (!f) {
            if (CUR().type==TK_SEMICOLON) { adv(); continue; }
            if (CUR().type==TK_LPAREN) break;
            adv(); continue;
        }
        *tp=f; tp=&f->nx;
    }
    return h;
}

/* ============================================================
 * Code generator
 * ============================================================ */
static FILE *out;
static int lblc, lvc, strc, locsz;
static char *lvn[512]; static Type lvt[512]; static int lvo[512];
static char *strd[512];

static char *nl(void) { static char b[64]; snprintf(b,sizeof(b),".L%d",lblc++); return strdup(b); }
static void alv(const char *n, Type t, int o) { lvn[lvc]=strdup(n); lvt[lvc]=t; lvo[lvc]=o; lvc++; }
static int flv(const char *n) { for (int i=lvc-1;i>=0;i--) if (!strcmp(lvn[i],n)) return i; return -1; }
static int astr(const char *s) { for (int i=0;i<strc;i++) if (!strcmp(strd[i],s)) return i; strd[strc]=strdup(s); return strc++; }

static void gi(int v) {
    if (!v) { fprintf(out,"\tmov\tx0,xzr\n"); return; }
    if (v>=-128 && v<=127) { fprintf(out,"\tmov\tx0,#%d\n",v); return; }
    unsigned long long uv = (unsigned long long)v;
    fprintf(out,"\tmov\tx0,#%llu\n",uv&0xFFFF);
    if ((uv>>16)&0xFFFF) fprintf(out,"\tmovk\tx0,#%llu,lsl#16\n",(uv>>16)&0xFFFF);
    if ((uv>>32)&0xFFFF) fprintf(out,"\tmovk\tx0,#%llu,lsl#32\n",(uv>>32)&0xFFFF);
    if ((uv>>48)&0xFFFF) fprintf(out,"\tmovk\tx0,#%llu,lsl#48\n",(uv>>48)&0xFFFF);
}

static void gexpr(Expr *e);
static void gstmt(Stmt *s, const char *rl, const char *bl, const char *cl);

static void gexpr(Expr *e) {
    if (!e) return;
    switch (e->kind) {
        case EX_LIT:
            if (e->sval) { int id = astr(e->sval); fprintf(out,"\tadr\tx0,.Lstr%d\n",id); }
            else gi(e->val);
            break;
        case EX_VAR:
            if (!e->var) break;
            if (!e->var->loc) {
                char lb[64]; snprintf(lb,sizeof(lb),".Ld_%s",e->var->name);
                fprintf(out,"\tadr\tx0,%s\n",lb); fprintf(out,"\tldr\tx0,[x0]\n");
            } else {
                int i = flv(e->var->name);
                if (i >= 0) {
                    if (e->var->arr_size > 0) fprintf(out,"\tadd\tx0,x29,#%d\n",lvo[i]);
                    else fprintf(out,"\tldr\tx0,[x29,#%d]\n",lvo[i]);
                }
            }
            break;
        case EX_BINOP: {
            if (e->op==TK_AND) {
                char *end = nl();
                gexpr(e->l); fprintf(out,"\tcmp\tx0,#0\n\tbeq\t%s\n",end);
                gexpr(e->r); fprintf(out,"\tcmp\tx0,#0\n\tcset\tx0,ne\n");
                fprintf(out,"%s:\n",end); break;
            }
            if (e->op==TK_OR) {
                char *end = nl();
                gexpr(e->l); fprintf(out,"\tcmp\tx0,#0\n\tbne\t%s\n",end);
                gexpr(e->r); fprintf(out,"\tcmp\tx0,#0\n\tcset\tx0,ne\n");
                fprintf(out,"%s:\n",end); break;
            }
            gexpr(e->l);
            fprintf(out,"\tstr\tx0,[x29,#-%d]\n",temp_off);
            temp_off += 8;
            gexpr(e->r);
            temp_off -= 8;
            fprintf(out,"\tldr\tx1,[x29,#-%d]\n",temp_off);
            if(e->l->kind==EX_VAR && e->l->var && e->l->var->arr_size > 0) {
                fprintf(out,"\tadd\tx0,x1,x0,lsl#3\n");
                fprintf(out,"\tldr\tx0,[x0]\n");
            } else {
                switch (e->op) {
                    case TK_PLUS: fprintf(out,"\tadd\tx0,x1,x0\n"); break;
                    case TK_MINUS: fprintf(out,"\tsub\tx0,x1,x0\n"); break;
                    case TK_MUL: fprintf(out,"\tmul\tx0,x1,x0\n"); break;
                    case TK_DIV: fprintf(out,"\tsdiv\tx0,x1,x0\n"); break;
                    case TK_MOD: fprintf(out,"\tsdiv\tx3,x1,x0\n\tmsub\tx0,x3,x0,x1\n"); break;
                    case TK_EQ: fprintf(out,"\tcmp\tx1,x0\n\tcset\tx0,eq\n"); break;
                    case TK_NEQ: fprintf(out,"\tcmp\tx1,x0\n\tcset\tx0,ne\n"); break;
                    case TK_LT: fprintf(out,"\tcmp\tx1,x0\n\tcset\tx0,lt\n"); break;
                    case TK_GT: fprintf(out,"\tcmp\tx1,x0\n\tcset\tx0,gt\n"); break;
                    case TK_LEQ: fprintf(out,"\tcmp\tx1,x0\n\tcset\tx0,le\n"); break;
                    case TK_GEQ: fprintf(out,"\tcmp\tx1,x0\n\tcset\tx0,ge\n"); break;
                    default: break;
                }
            }
            break;
        }
        case EX_UNOP: {
            gexpr(e->l);
            switch (e->op) {
                case TK_MINUS: fprintf(out,"\tneg\tx0,x0\n"); break;
                case TK_BNOT: fprintf(out,"\tmvn\tx0,x0\n"); break;
                case TK_NOT: fprintf(out,"\tcmp\tx0,#0\n\tcset\tx0,eq\n"); break;
                case TK_MUL: fprintf(out,"\tldr\tx0,[x0]\n"); break;
                case TK_AMP:{
                if(e->l->kind==EX_VAR&&e->l->var){
                    if(!e->l->var->loc){
                        char lb[64]; snprintf(lb,sizeof(lb),".Ld_%s",e->l->var->name);
                        fprintf(out,"\tadr\tx0,%s\n",lb);
                    }else{
                        int i=flv(e->l->var->name);
                        if(i>=0) fprintf(out,"\tadd\tx0,x29,#%d\n",lvo[i]);
                    }
                }
                break;
            }
                default: break;
            }
            break;
        }
        case EX_CALL: {
            int na = e->nargs; Expr *a[16]; int n = 0;
            Expr *x = e->l; while (x && n < 16) { a[n++] = x; x = x->r; }
            for (int i = na-1; i >= 0; i--) { gexpr(a[i]); fprintf(out,"\tstr\tx0,[x29,#%d]\n",48+i*8); }
            int nr = na < 8 ? na : 8;
            for (int i = 0; i < nr; i++) fprintf(out,"\tldr\tx%d,[x29,#%d]\n",i,48+i*8);
            fprintf(out,"\tbl\t%s\n", e->var ? e->var->name : "unknown");
            if (na > 8) fprintf(out,"\tadd\tsp,sp,#%d\n",(na-8)*8);
            break;
        }
        case EX_ASSIGN: {
            gexpr(e->r);
            if (e->l->kind==EX_VAR && e->l->var) {
                if (!e->l->var->loc) {
                    char lb[64]; snprintf(lb,sizeof(lb),".Ld_%s",e->l->var->name);
                    fprintf(out,"\tadr\tx1,%s\n",lb); fprintf(out,"\tstr\tx0,[x1]\n");
                } else {
                    int i = flv(e->l->var->name);
                    if (i >= 0) fprintf(out,"\tstr\tx0,[x29,#%d]\n",lvo[i]);
                }
            } else if (e->l->kind==EX_BINOP && e->l->op=='+') {
                Expr *arr = e->l->l;
                if (arr->kind==EX_VAR && arr->var && arr->var->arr_size > 0) {
                    int off = lvo[flv(arr->var->name)];
                    fprintf(out,"\tmov\tx20,x0\n");
                    gexpr(e->l->r);
                    fprintf(out,"\tadd\tx1,x29,#%d\n",off);
                    fprintf(out,"\tadd\tx0,x1,x0,lsl#3\n");
                    fprintf(out,"\tmov\tx1,x20\n");
                    fprintf(out,"\tstr\tx1,[x0]\n");
                }
            }
            break;
        }
        default: break;
    }
}

static void gstmt(Stmt *s, const char *rl, const char *bl, const char *cl) {
    if (!s) return;
    switch (s->kind) {
        case ST_DECL:
            if (s->decl) {
                Type t = s->decl->ty; int off = locsz + 16;
                locsz += 8;
                if (s->decl->arr_size > 0) {
                    locsz += s->decl->arr_size * 8 - 8;
                }
                if (flv(s->decl->name) < 0) alv(s->decl->name, t, off);
                if (s->ex) { gexpr(s->ex); fprintf(out,"\tstr\tx0,[x29,#%d]\n",off); }
            }
            break;
        case ST_EXPR: gexpr(s->ex); break;
        case ST_RETURN: if (s->ex) gexpr(s->ex); fprintf(out,"\tb\t%s\n",rl); break;
        case ST_BLOCK:
            s = s->nx;
            while (s) { gstmt(s, rl, bl, cl); s = s->nx; } break;
        case ST_IF: {
            char *el = nl(), *en = nl();
            gexpr(s->f_cond); fprintf(out,"\tcmp\tx0,#0\n\tbeq\t%s\n",el);
            gstmt(s->then, rl, bl, cl); fprintf(out,"\tb\t%s\n",en); fprintf(out,"%s:\n",el);
            gstmt(s->els, rl, bl, cl); fprintf(out,"%s:\n",en); break;
        }
        case ST_WHILE: {
            char *cd = nl(), *bd = nl(), *ed = nl();
            fprintf(out,"%s:\n",cd); gexpr(s->f_cond); fprintf(out,"\tcmp\tx0,#0\n\tbeq\t%s\n",ed);
            fprintf(out,"%s:\n",bd); gstmt(s->body, rl, ed, cd); fprintf(out,"\tb\t%s\n",cd); fprintf(out,"%s:\n",ed); break;
        }
        case ST_FOR: {
            char *c2 = nl(), *bd = nl(), *in = nl(), *en = nl();
            gstmt(s->f_init, rl, en, c2); fprintf(out,"%s:\n",c2); gexpr(s->f_cond);
            fprintf(out,"\tcmp\tx0,#0\n\tbeq\t%s\n",en); fprintf(out,"%s:\n",bd);
            gstmt(s->body, rl, en, c2); fprintf(out,"%s:\n",in); gexpr(s->f_inc);
            fprintf(out,"\tb\t%s\n",c2); fprintf(out,"%s:\n",en); break;
        }
        case ST_BREAK: fprintf(out,"\tb\t%s\n",bl?bl:".Lbreak"); break;
        case ST_CONTINUE: fprintf(out,"\tb\t%s\n",cl?cl:".Lcontinue"); break;
        default: break;
    }
}

static void gfunc(FuncDef *f) {
    fprintf(out,"\n\t.global %s\n", f->name);
    fprintf(out,"%s:\n", f->name);
    locsz = 0; lvc = 0; temp_off = 8;
    for (int i = 0; i < f->pc; i++) {
        int off = 32 + i*8;
        alv(f->pns[i], f->pts[i], off);
    }
    fprintf(out,"\tstp\tx29,x30,[sp,#-64]!\n");
    fprintf(out,"\tmov\tx29,sp\n");
    for (int i = 0; i < f->pc; i++) {
        int off = 32 + i*8;
        if (i < 8) fprintf(out,"\tstr\tx%d,[x29,#%d]\n", i, off);
    }
    char *rl = nl(); gstmt(f->body, rl, NULL, NULL); fprintf(out,"%s:\n", rl);
    fprintf(out,"\tldp\tx29,x30,[sp],#64\n"); fprintf(out,"\tret\n");
}

static void compile(FuncDef *p) {
    fprintf(out,"\t.arch armv8-a\n\t.text\n\t.balign 4\n");
    fprintf(out,"\t.global _start\n");
    fprintf(out,"_start:\n");
    fprintf(out,"\tbl\tmain\n");
    fprintf(out,"\tmov\tx8,#93\n");
    fprintf(out,"\tsvc\t#0\n");
    fprintf(out,"\t.section .rodata\n\t.balign 8\n");
    for (int i = 0; i < strc; i++) {
        fprintf(out,".Lstr%d:\t.asciz\t\"", i); char *s = strd[i];
        for (int j = 0; s[j]; j++) {
            if (s[j]=='"') fprintf(out,"\\\"");
            else if (s[j]=='\n') fprintf(out,"\\n");
            else fprintf(out,"%c", s[j]);
        }
        fprintf(out,"\"\n");
    }
    fprintf(out,"\t.text\n");
    for (FuncDef *f = p; f; f = f->nx) gfunc(f);
    fprintf(out,"\n\t.section .data\n\t.balign 8\n");
    for (int i = 0; i < gvc; i++) {
        int esz = gvt[i].ptr ? 8 : (gvt[i].kind==0?4:(gvt[i].kind==1?1:0));
        int total = garr[i] > 0 ? esz * garr[i] : esz;
        fprintf(out,".Ld_%s:\n", gvn[i]);
        if (ginit[i] && ginit[i]->kind == EX_LIT && garr[i] == 0) {
            fprintf(out,"\t.word\t%d\n", ginit[i]->val);
        } else {
            fprintf(out,"\t.zero\t%d\n", total);
        }
    }
}

/* ============================================================
 * Main
 * ============================================================ */
int main(int argc, char**argv) {
    if (argc < 2) { fprintf(stderr,"Usage: %s <file.c> [output.s]\n", argv[0]); return 1; }
    FILE *in = fopen(argv[1],"rb");
    if (!in) { perror("fopen"); return 1; }
    fseek(in,0,SEEK_END); slen = ftell(in); fseek(in,0,SEEK_SET);
    src = malloc(slen+1); fread(src,1,slen,in); src[slen]=0; fclose(in);
    lex();
    FuncDef *prog = ppr();
    const char *on = argc>=3 ? argv[2] : "output.s";
    out = fopen(on,"w"); if (!out) { perror("fopen"); return 1; }
    compile(prog);
    fclose(out);
    printf("Compilation successful: %s\n", on);
    return 0;
}
