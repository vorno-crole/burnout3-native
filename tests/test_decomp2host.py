# tests/test_decomp2host.py -- transform cases of tools/decomp2host/decomp2host.py reduced from the decomp (port8).
# Run by `make test`: python3 -I tests/test_decomp2host.py (exit 1 on a failure).
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "decomp2host"))
import decomp2host as d  # noqa: E402

CASES = [
    # T9, port8: the inner cast of a nested cast (CSparkBank::Construct 0x264170 stored truncated host pointers)
    ("x = (b3host::ps2_ulong)(unsigned int)p << 32;",
     "x = b3host::icast<b3host::ps2_ulong>(b3host::icast<unsigned int>(p)) << 32;"),
    ("x = (b3host::ps2_ulong)((unsigned int)p) << 32;",
     "x = b3host::icast<b3host::ps2_ulong>((b3host::icast<unsigned int>(p))) << 32;"),
    # T9, port8: a two-level pointer cast reads a pointer stored in guest memory: gp<T> (UpdateTransition 0x19F4B0)
    ("(*(E1 * const *)&t1->field_2C)[i].field_0 = v;",
     "(*b3host::pcast<b3host::gp<E1> const>(&t1->field_2C))[i].field_0 = v;"),
    ("x = *(T **)p;", "x = *b3host::pcast<b3host::gp<T>>(p);"),
    # T9, port8: a cast to a function-pointer type calls a guest address (OpenViewport 0x227360)
    ("((void (*)(int, int))*fp)(16, 1);",
     "(b3host::gfn<void(int, int)>::from_raw(b3host::icast<unsigned int>(*fp)))(16, 1);"),
    # T9, port8: macro bodies (func_00285C00's PLAY, viBufReset's HW)
    ("#define HW(a) (*(volatile unsigned int *)(a))\n",
     "#define HW(a) (*b3host::pcast<volatile unsigned int>((a)))\n"),
    ("#define P(id, b) f(self, (const Bank*)self->field_78, \\\n   (int)b)\n",
     "#define P(id, b) f(self, b3host::pcast<const Bank>(self->field_78), \\\n   b3host::icast<int>(b))\n"),
    # unchanged: object-like macros without casts, calls, and port6/port7 forms
    ("#define D_1 (b3host::guest_global<int>(0x1u))\n", "#define D_1 (b3host::guest_global<int>(0x1u))\n"),
    ("y = (int)(char *)q;", "y = b3host::icast<int>(b3host::pcast<char>(q));"),
    ("g(x)(int);", "g(x)(int);"),
]

fails = 0
for src, want in CASES:
    got = d.cast_transforms(src, [])
    if got != want:
        fails += 1
        print("FAIL\n  in:   %r\n  want: %r\n  got:  %r" % (src, want, got))
# T17, port13: call arguments that contain guest calls are evaluated first, left to right, as MWCC does
# (func_00310200: jal func_0030CEC0 at 0x3102C4 before jal func_0030CE80 at 0x3102D4)
SEQ = "{ /* decomp2host T17: call arguments in MWCC's order */ "
FWD = "static_cast<decltype(__b3_seq%d_%d)&&>(__b3_seq%d_%d)"
SEQ_CASES = [
    ("    f(&D, q[i], a(&D), b(&D), 1);\n",
     "    " + SEQ + "auto&& __b3_seq0_2 = (a(&D)); auto&& __b3_seq0_3 = (b(&D)); f(&D, q[i], %s, %s, 1); }\n"
     % (FWD % (0, 2, 0, 2), FWD % (0, 3, 0, 3))),
    ("case 3: f(a(1), b(2)); break;",
     "case 3: " + SEQ + "auto&& __b3_seq0_0 = (a(1)); auto&& __b3_seq0_1 = (b(2)); f(%s, %s); } break;"
     % (FWD % (0, 0, 0, 0), FWD % (0, 1, 0, 1))),
    ("{ return f(a(1), b(2)); }",
     "{ " + SEQ + "auto&& __b3_seq0_0 = (a(1)); auto&& __b3_seq0_1 = (b(2)); return f(%s, %s); } }"
     % (FWD % (0, 0, 0, 0), FWD % (0, 1, 0, 1))),
    ("f(a(1), x); g(b(2), y);", "f(a(1), x); g(b(2), y);"),   # one guest call per call: unchanged
]
for src, want in SEQ_CASES:
    got = d.guest_arg_order_transforms(src, {"a", "b"}, [], "test")
    if got != want:
        fails += 1
        print("FAIL\n  in:   %r\n  want: %r\n  got:  %r" % (src, want, got))
for src in ("int z = f(a(1), b(2));", "if (f(a(1), b(2))) z = 1;"):   # not at statement level: refused
    try:
        d.guest_arg_order_transforms(src, {"a", "b"}, [], "test")
        fails += 1
        print("FAIL (not refused): %r" % src)
    except (SystemExit, d.GenError):
        pass
CASES += SEQ_CASES + [None, None]
# T18, port13: bound-1 data members -> open_array (CAsyncLoadManager's queue, 0x13D250); functions, statics untouched
OPEN_CASES = [
    ("class C {\npublic:\n    void Update();\n    E field_0[1];\n    unsigned char pad_50[0x730];\n    static int s[1];\n};",
     "class C {\npublic:\n    void Update();\n    b3host::open_array<E> field_0;\n    unsigned char pad_50[0x730];\n    static int s[1];\n};"),
    ("struct S { b3host::gp<T> p[1]; int f(int x[1]); unsigned long long q[0x1]; };",
     "struct S { b3host::open_array<b3host::gp<T>> p; int f(int x[1]); b3host::open_array<unsigned long long> q; };"),
    ("void f() { int a[1]; a[0] = 1; }", "void f() { int a[1]; a[0] = 1; }"),   # not a member
]
for src, want in OPEN_CASES:
    got = d.open_array_transforms(src, [])
    if got != want:
        fails += 1
        print("FAIL\n  in:   %r\n  want: %r\n  got:  %r" % (src, want, got))
CASES += OPEN_CASES
# T19, port14: `(T *)&x[i]` is an element address, not a dereference (Fixup__16CB3TriggerStructFUi 0x269D20: `lw; addu;
# sw`; through gp<T>::operator[] a relocated offset outside RAM was fatal)
ELEM_CASES = [
    ("field_0 = (E0 *)&field_0[p0];", "field_0 = b3host::pcast_elem<E0>(field_0, p0);"),
    ("this->a.b = (T *)&this->a.b[i * 2];", "this->a.b = b3host::pcast_elem<T>(this->a.b, i * 2);"),
    ("p = (T *)&q[f(x)[1]];", "p = b3host::pcast_elem<T>(q, f(x)[1]);"),
    ("p = (T *)&q[1].r;", "p = b3host::pcast<T>(&q[1].r);"),   # not an element address: unchanged form
    ("p = (T *)&q[1][2];", "p = b3host::pcast<T>(&q[1][2]);"),
]
for src, want in ELEM_CASES:
    got = d.cast_transforms(src, [])
    if got != want:
        fails += 1
        print("FAIL\n  in:   %r\n  want: %r\n  got:  %r" % (src, want, got))
CASES += ELEM_CASES


# boot1 cases
class _Decomp:
    symbols = {"kNamed": 0x004C5000}


def _check(what, ok, got):
    global fails
    if not ok:
        fails += 1
        print("FAIL %s\n  got: %r" % (what, got))


# pic1 (docs/native-renderer.md §32.3): the watch prologue (config/native_watch.txt) -- the definition reports its entry
# with its arguments as the EE holds them (this, references by address, pointers, scalars); nothing for unlisted ones
_src = ("struct S { void M(const T &space, const void *data, int n); };\n"
        "void S::M(const T &space, const void *data, int n)\n{\n    g(n);\n}\n"
        "void F(int a)\n{\n}\n")
_tf = [x[:3] + ({"void S::M(const T &space, const void *data, int n)": 0x1D5FB0, "void F(int a)": 0x100000}[x[4]],) + x[4:]
       for x in d.top_level_functions(_src)]
_fns = [(0x1D5FB0, "matched", "M__1SFRC1TPCvi", 16), (0x100000, "matched", "F__Fi", 8)]
_t, _done = d.watch_prologues(_src, _fns, _tf, {0x1D5FB0}, [])
_check("watch prologue", _done == [0x1D5FB0] and _t.startswith('#include "b3host/watch.h"') and
       "{\n    auto b3_watch_scope = b3host::watch_scope<0x001D5FB0u>(this, &space, data, n);" in _t and _t.count("watch_scope<") == 1, _t)
_t2, _done2 = d.watch_prologues(_src, _fns, _tf, set(), [])
_check("watch prologue: empty list", _t2 == _src and _done2 == [], _t2)
CASES += [None, None]
# mod kit 2a (config/mod_hooks.txt): the route check (b3host/hook_route.h) ahead of the watch scope; nothing for an
# empty list; a member passes this, every parameter through stub_arg
_t3, _done3 = d.watch_prologues(_src, _fns, _tf, {0x1D5FB0}, [], {0x1D5FB0, 0x100000})
_check("route prologue", _done3 == [0x1D5FB0] and _t3.startswith('#include "b3host/watch.h"') and
       '#include "b3host/hook_route.h"' in _t3 and
       "{\n    b3host::RouteScope b3_route_scope;   // decomp2host: config/mod_hooks.txt (mod kit 2a)\n"
       "    if (b3host::route_call<0x001D5FB0u>(b3_route_scope)) return b3host::call_guest<void>(0x001D5FB0u, "
       "b3host::ptr_arg(this), b3host::stub_arg<decltype(space)>(space), b3host::stub_arg<decltype(data)>(data), "
       "b3host::stub_arg<decltype(n)>(n));\n    auto b3_watch_scope = b3host::watch_scope<0x001D5FB0u>(this, &space, data, n);" in _t3 and
       "return b3host::call_guest<void>(0x00100000u, b3host::stub_arg<decltype(a)>(a));" in _t3 and _t3.count("route_call<") == 2, _t3)
_t4, _done4 = d.watch_prologues(_src, _fns, _tf, set(), [], set())
_check("route prologue: empty lists", _t4 == _src and _done4 == [], _t4)
CASES += [None, None]


# T4: splat's short D_ names (main 0x12EB30: D_10000, D_1FF0000)
g = d.global_transforms("extern unsigned char D_10000[];\nextern int D_1FF0000;\n", _Decomp, [])
_check("T4 short D_", "guest_global<unsigned char>(0x00010000u)" in g and "guest_global<int>(0x01FF0000u)" in g, g)
# T4: an array assigned to itself is dropped (func_004D8D50 0x4D8D50)
g = d.global_transforms("extern unsigned char D_004C7184[];\nvoid f() { D_004C7184 = D_004C7184; }\n", _Decomp, [])
_check("T4 array self-assignment", "(an array assigned to itself: no effect) */;" in g and "D_004C7184 = D_004C7184;" not in g.split("*/")[-1], g)
# the C-name #define follows declarations, not calls in a class's inline member (Update__22CB3MessageBoxComponentFf)
src = 'namespace n {\nextern "C" {\nint F(int);\n}\nclass C { void g() { F(1); } };\n}\n'
blocks = d.code_blocks(d.preproc_mask(d.mask(src)))
_check("code_blocks", len(blocks) == 1 and src[blocks[0][0] - 8:blocks[0][0]] == "class C " and src.index("F(1)") > blocks[0][0], blocks)
# T25: a static initialiser's unit of global definitions (func_004D7E20, B3HeadingComponent_004D7E20.cpp)
src = ("#include \"b3/x.h\"\nusing GtMathPs2::CGtV2d;\nextern const CGtV2d D_004C51F8;\n"
       "const CGtV2d D_004C51F8(0.0f, 0.5f);\nCGtV4d D_01EA3190 = Calc(0, 1.0f);\nconst CGtV2d kNamed(1.0f / 608.0f, 2.0f);\n"
       "const float D_004C52E0[2] = {0.0f, 1.0f};\n")
t = d.sinit_unit(src, [(0x004D7E20, "matched", "func_004D7E20", 72)], _Decomp, [])
_check("T25 body", "::new ((void *)&D_004C51F8) CGtV2d(0.0f, 0.5f);" in t and "::new ((void *)&D_01EA3190) CGtV4d(Calc(0, 1.0f));" in t
       and "::new ((void *)&kNamed) CGtV2d(1.0f / 608.0f, 2.0f);" in t and "// 0x004D7E20" in t
       and 'extern "C" void func_004D7E20()' in t and "extern CGtV4d D_01EA3190;" in t
       and "constant data in the image: D_004C52E0" in t, t)
_check("T25 not a sinit", d.sinit_unit(src, [(0x00100000, "matched", "f", 4)], _Decomp, []) == src, None)
CASES += [None] * 6
# T27 (audio1): guest-stack locals at retail's frame offsets (_RwaStreamContainerInstantiateRenderChains' list heads)
t = d.stack_frame_transforms("{\n    B3_RETAIL_FRAME(0x1C0);\n    RwaLLNode *head = B3_FRAME_LOCAL(RwaLLNode, 0x198);\n}\n")
_check("T27", t == "{\n    b3host::GuestStackFrame b3_frame(0x1C0);\n"
       "    RwaLLNode *head = b3host::gp<RwaLLNode>::from_raw(b3_frame.addr() + (0x198)).get();\n}\n", t)
CASES += [None]
# T26: a constructor of a class with a T13 vptr stores the class's vtable (__ct__12CUnk0028B700Fv 0x28B700)
class _DecompVt:
    symbols = {"__vt__12CUnk0028B700": 0x004DE1A0}


src = "class CUnk0028B700 {\npublic:\n    CUnk0028B700();\n    virtual void vfunc_8();\n};\nCUnk0028B700::CUnk0028B700()\n{\n}\n"
t = d.ctor_vptr_transforms(d.virtual_transforms(src, []), _DecompVt, [])
_check("T26 store", "CUnk0028B700::CUnk0028B700()\n{ b3_vptr = 0x004DE1A0u;" in t and t.count("b3_vptr = ") == 1, t)
src2 = "class CNoVt {\n    virtual void vfunc_8();\n    CNoVt() {}\n};\n"
try:
    d.RAISE = True
    d.ctor_vptr_transforms(d.virtual_transforms(src2, []), _DecompVt, [])
    _check("T26 missing __vt__ stops", False, "no error")
except d.GenError:
    pass
finally:
    d.RAISE = False
CASES += [None] * 2
# boot1: guest data named by its address is extern (the image holds it); a literal constant scalar stays a host constant
src = ("static float D_004E26BC;\nstatic const signed char D_004C3FE8[3] = {1, -2, 3};\nstatic const float kOne = 1.0f;\n"
       "void f() { D_004E26BC = 1; }\n")
t = d.static_data_transforms(src, [])
_check("static data", "extern float D_004E26BC;" in t and "extern const signed char D_004C3FE8[3];" in t
       and "static const float kOne = 1.0f;" in t, t)
d.RAISE = True
try:
    d.check_no_data(t, "test")
    ok_ = True
except d.GenError:
    ok_ = False
_check("literal constant accepted", ok_, t)
try:
    d.check_no_data(d.static_data_transforms("static int D_00500000 = 5 + x;\n", []), "test")
    _check("dynamic initializer refused", False, "accepted")
except d.GenError:
    pass
d.RAISE = False
CASES += [None] * 3
# boot2: MWCC's nested-class mangling, Class::Name for marker-less nonmatching drafts
_check("cxx_name Q2", d.cxx_name("Construct__Q216CB3ParticleArray15CB3ParticleBankFUi") ==
       "CB3ParticleArray::CB3ParticleBank::Construct", d.cxx_name("Construct__Q216CB3ParticleArray15CB3ParticleBankFUi"))
_check("cxx_name plain", d.cxx_name("Update__18CB3MenuPageManagerFv") == "CB3MenuPageManager::Update", "")
# boot2: `&D_x` of an array is its address (func_003999E0)


class _DecompArr:
    symbols = {}


t = d.global_transforms("extern unsigned char D_01E9FC00[];\nvoid f() { g(&D_01E9FC00); h(D_01E9FC00[1]); }\n", _DecompArr, [])
_check("&array", "g(D_01E9FC00)" in t and "h(D_01E9FC00[1])" in t, t)
# boot2: a static const array member without an initialiser is guest data (CB3SkyDomeRenderer::maDefinitions)


class _DecompStatic:
    symbols = {"maDefs__6CThing": 0x004B9CA0}


d.STATIC_MEMBERS.clear()
t = d.static_member_transforms("class CThing {\npublic:\n    static const Def maDefs[];\n    static const int kN = 4;\n};\n"
                               "void f(int i) { use(&CThing::maDefs[i]); }\n", _DecompStatic, [])
_check("static const member array", "guest_global<Def>(0x004B9CA0u)" in t and "static const int kN = 4;" in t, t)
d.STATIC_MEMBERS.clear()
# boot2: function_address of a D_ alternate entry inside a function's code


class _DecompFn:
    by_name = {}
    symbols = {}
    funcs = {0x0022F800: ("func_0022F800", 0xB0)}


_check("D_ alternate entry", d.function_address(_DecompFn, "D_0022F890") == 0x0022F890 and
       d.function_address(_DecompFn, "D_00483F20") is None, "")
CASES += [None] * 5
# T8 (native3 2026-11-06): goto-crossed split locals are exempt from -ftrivial-auto-var-init
t = d.goto_transforms("void f(int a)\n{\n    if (a) goto out;\n    int *slot = g(a);\nout:\n    h(slot);\n}\n")
_check("T8 uninitialized", "int *slot __attribute__((uninitialized)); slot = g(a); /* decomp2host T8 */" in t, t)
CASES += [None]
# T9 (boot2): RenderWare / unit integer typedefs are integer casts (a gp<T> to RwUInt32 is its guest address)
r = []
t = d.cast_transforms("typedef unsigned int Word;\nvoid f(void) { cur = (RwUInt32)(p + 1); w = (Word)q; }\n", r)
_check("T9 RwUInt32", "b3host::icast<RwUInt32>((p + 1))" in t or "b3host::icast<RwUInt32>(p + 1)" in t, t)
_check("T9 unit typedef", "b3host::icast<Word>(q)" in t, t)
CASES += [None, None]
# T9 (boot2): `(T *&)x` re-types the pointer object
r = []
t = d.cast_transforms("void f(void) { *(((int *&)field_1D04)++) = 32768; }\n", r)
_check("T9 ref cast", "b3host::ref_cast<int>(field_1D04)" in t, t)
CASES += [None]
# T29 (boot2): float literal ternary arms are ps2f
t = d.ternary_float_transforms("void f(void) { v = (x < 0.0f) ? 0.0f : ((x > 1.0f) ? 1.0f : x); w = c ? y : 2.5f; s = A::b; }\n")
_check("T29 arms", "? ps2f(0.0f) : ps2f(((x > 1.0f) ? ps2f(1.0f) : ps2f(x)));" in t and "c ? ps2f(y) : ps2f(2.5f);" in t and "(x < 0.0f)" in t and "A::b" in t, t)
t = d.ternary_float_transforms("void f(void) { wn = next == &u ? 2.0f : ((T *)next)->W / 255.0f; g(a ? b : c, 1); }\n")
_check("T29 plain float arm", "? ps2f(2.0f) : ps2f(((T *)next)->W / 255.0f);" in t and "a ? b : c" in t, t)
CASES += [None]
CASES += [None]
import os
os.environ["DECOMP2HOST_T31C"] = "1"
# E1 (boot2): host locals escaping into guest memory
h = d.escape_lint("int f(int *p)\n{\n    unsigned int info[2];\n    int x;\n    int *q;\n    S blk;\n    g(b3host::icast<unsigned int>(info));\n    g(b3host::icast<unsigned int>(&x));\n    g(b3host::icast<unsigned int>(q));\n    p->field_4 = &blk;\n    blk.f = &x;\n    h(&x);\n    p->field_8 = info[1];\n    g(b3host::icast<unsigned int>(info[0]));\n    return 0;\n}\n")
ex = [x[1] for x in h]
_check("E1 hits", len(h) == 4 and any("blk.f = &x" in e for e in ex) and any("info" in e for e in ex) and any("&x" in e for e in ex) and any("p->field_4" in e for e in ex), h)
CASES += [None]
# T31 (boot2): a decomp unit's escaping local becomes a guest-stack local
t = d.escape_fix("int f(S *p)\n{\n    T tbl;\n    unsigned char arr[4];\n    p->field_C = &tbl;\n    p->field_10 = arr;\n    return 0;\n}\n", ["tbl", "arr"])
_check("T31 rewrite", "b3host::GuestLocal<T> b3_esc_tbl; T &tbl = *b3_esc_tbl;" in t and "    b3host::GuestLocalArray<unsigned char, (4)> b3_esc_arr; unsigned char (&arr)[4] = *b3_esc_arr;" in t and not d.escape_lint(t), t)   # gen1: the GuestLocal form
CASES += [None]
# gen1: definitions inside an `extern "C" { ... }` block are top level (func_0012F460, the ezNetCnf family)
fns = d.top_level_functions('extern "C" {\nstruct S { int a; };\n// 0x0012F460\nint f(S *s)\n{\n    return s->a;\n}\n}\n')
_check("extern C block", [f[3] for f in fns] == [0x0012F460], fns)
CASES += [None]
# gen1: T3b leaves a product with a dereference alone (`lScalingMat * *m`, UpdateSuspension 0x1CC900)
t = d.pointer_to_pointer_transforms("void f(CGtMatrix3x3 *m, ps2f k, ps2f *p)\n{\n    CGtMatrix3x3 lScalingMat;\n"
                                    "    *m = lScalingMat * *m;\n    ps2f s = k * *p + self->field_C * *(p + 1);\n    T **pp = 0;\n}\n")
_check("T3b product", "lScalingMat * *m;" in t and "k * *p" in t and "field_C * *(p" in t and "b3host::gp<T> *pp" in t, t)
CASES += [None]
# gen1: a wrapper's `unsigned n` parameter is an unsigned int (RpcAlloc 0x33C880)
d.HEADER_TEXT.clear()
w = d.wrapper_for('extern "C" void *RpcAlloc(unsigned n)', "", 0x0033C880)[1]
_check("wrapper unsigned n", w.startswith("b3host::gp<void> b3gen_0033C880(unsigned int a0) { return b3host::gp<void>(RpcAlloc(a0)); }"), w)
CASES += [None]
# gen1: nested class parameter types are qualified in the wrapper (CB3EffectsManager::CLighting, 0x25FB60), also a base
# class's (CGt2dObject::CGt2dObjectSpace in CGt2dObjectTable::_Render), never a name a parameter only elaborates
unit = ("class CB3EffectsManager {\npublic:\n    struct CLighting { int a; };\n    void DoVehicleFire(CB3VehiclePhysics *p, float t, const CLighting &l);\n"
        "    void Hit(struct CB3CollidingPair *pair);\n};\n"
        "class CBase { public: struct CSpace { int b; }; };\nclass CDerived : public CBase { public: static void R(const CSpace &s, const void *d); };\n")
d.HEADER_TEXT.clear()
w = d.wrapper_for("void CB3EffectsManager::DoVehicleFire(CB3VehiclePhysics *p, ps2f t, const CLighting &l)", unit, 0x0025FB60)[1]
_check("wrapper nested type", "*a2.cast<const CB3EffectsManager::CLighting>()" in w, w)
w = d.wrapper_for("void CB3EffectsManager::Hit(struct CB3CollidingPair *pair)", unit, 0x001ADC70)[1]
_check("wrapper elaborated type", "a0.cast<struct CB3CollidingPair>()" in w and "CB3EffectsManager::CB3CollidingPair" not in w, w)
w = d.wrapper_for("void CDerived::R(const CSpace &s, const void *d)", unit, 0x0033ED30)[1]
_check("wrapper base nested type", "*a0.cast<const CBase::CSpace>()" in w and "a1.cast<const void>()" in w
       and "auto (*)(const CBase::CSpace &, const void *)" in w, w)
unit2 = "class Gt2dRenderer {\npublic:\n    enum EGt2dRendererFilter { UnkEnum_Filter_0 };\n    static void SetTextureFilterMode(EGt2dRendererFilter p0);\n};\n"
w = d.wrapper_for("void Gt2dRenderer::SetTextureFilterMode(EGt2dRendererFilter p0)", unit2, 0x00217D40)[1]
_check("registration nested enum", "auto (*)(Gt2dRenderer::EGt2dRendererFilter) -> decltype(Gt2dRenderer::SetTextureFilterMode("
       "std::declval<Gt2dRenderer::EGt2dRendererFilter>()))" in w, w)
CASES += [None] * 4
# gen1: an empty C varargs definition gets a wrapper over its fixed parameters (func_00447170); va_start stops it
d.HEADER_TEXT.clear()
w = d.wrapper_for('extern "C" void func_00447170(int, ...)', 'extern "C" void func_00447170(int, ...)\n{\n}\n', 0x00447170)[1]
_check("varargs wrapper", w.startswith("void b3gen_00447170(int a0) { func_00447170(a0); }") and
       "auto (*)(int, ...) -> decltype(func_00447170(std::declval<int>()))" in w, w)
d.RAISE = True
try:
    d.wrapper_for('extern "C" void f(int, ...)', "void f(int n, ...) { va_list a; va_start(a, n); }", 0x00100000)
    _check("varargs va_start refused", False, "accepted")
except d.GenError:
    pass
d.RAISE = False
CASES += [None] * 2
# gen1: T7 takes a forward declaration above the last #include into the namespace with its definition (func_004617B0)
src = "struct UnkStruct_004617B0;\nstruct Other;\n#include \"b3/GtMathPs2.h\"\nstruct UnkStruct_004617B0 { int a; };\n"
at = src.index("struct UnkStruct_004617B0 {")
t, at2, fwd = d.t7_forward_declarations(src, at)
_check("T7 forward declaration", fwd == "struct UnkStruct_004617B0;\n" and t.startswith("struct Other;\n#include") and
       t[at2:].startswith("struct UnkStruct_004617B0 {"), (t, at2, fwd))
CASES += [None]
# gen1 T32: a local pointer whose address is taken, or a pointer array passed whole, is a 4-byte guest pointer
src = ("int f(CB3AILane *start)\n{\n    CB3AILane *lane = start;\n    unsigned short seg;\n    Find(&lane, &seg);\n"
       "    CB3ControllerMapping *maps[2];\n    Get(maps, 0);\n    char *p = q;\n    g(p);\n"
       "    if (x) { CGtV4d c(0.0f); h(&c); }\n    { T *c = r; c->x = 1; }\n    return 0;\n}\n")
t = d.address_taken_pointer_locals(src)
_check("T32", "b3host::gp<CB3AILane> lane = start;" in t and "b3host::gp<CB3ControllerMapping> maps[2];" in t
       and "char *p = q;" in t and "T *c = r;" in t, t)
CASES += [None]
# gen1 T33: MWCC's __declspec(sdata) is dropped before T4 (func_001A3B30)
class _DecompNone:
    symbols = {}
    by_name = {}
    funcs = {}


t = d.transform_text("extern __declspec(sdata) const float D_004E19C0;\n", _DecompNone, [])
_check("T33 sdata", "guest_global<const ps2f>(0x004E19C0u)" in t and "sdata" not in t, t)
CASES += [None]
# gen1: a helper used only by a class body's inline member is kept (CB3Im3dVertex() { func_0015E340_inl1(...); })
src = ("static inline void func_0015E340_inl1(float *v, float x) { v[0] = x; }\n"
       "struct CB3Im3dVertex { float field_0[4]; CB3Im3dVertex() { func_0015E340_inl1(field_0, 0.0f); } };\n"
       "// 0x0015E340\nvoid f(void)\n{\n    CB3Im3dVertex v;\n}\n")
t, _k = d.reduce_unit(src, {0x0015E340}, [], {0x0015E340: "f"})
_check("helper from a class body", "static inline void func_0015E340_inl1(float *v" in t, t)
CASES += [None]
# gen1: a dropped game function that kept code calls keeps its declaration (Gt2dFrameHierarchy.cpp's func_001D4E70)
src = ('// 0x001D4E70\nextern "C" L * func_001D4E70(F * f) { return f->field_4; }\n'
       '// 0x001D4EB0\nextern "C" L * func_001D4EB0(F * f) { return 0; }\n'
       '// 0x001D51C0\nvoid Clean(F *self)\n{\n    for (L *l = func_001D4E70(self); l; l = 0) {}\n}\n')
t, _k = d.reduce_unit(src, {0x001D51C0}, [], {0x001D51C0: "Clean"})
_check("declaration kept", 'extern "C" L * func_001D4E70(F * f);   // decomp2host T5: declaration kept' in t
       and "func_001D4EB0" not in t.replace("/* decomp2host T5: 0x001D4EB0 dropped */", ""), t)
CASES += [None]
# gen1: a definition whose header declares it extern "C" gets that linkage (func_00319330, B3MenuComponentBase.h)
import tempfile
_tmp = tempfile.mkdtemp(prefix="d2h_test_")
os.makedirs(os.path.join(_tmp, "include", "b3"))
open(os.path.join(_tmp, "include", "b3", "M.h"), "w").write('struct S;\nextern "C" bool func_00319330(S *self);\n')


class _DecompRoot:
    root = _tmp


src = '#include "b3/M.h"\n// 0x00319330\nbool func_00319330(S *self)\n{\n    return 0;\n}\n// 0x00319360\nbool g(S *self)\n{\n    return 1;\n}\n'
t = d.header_c_linkage(src, _DecompRoot, {0x00319330: "func_00319330", 0x00319360: "g"}, [])
_check("header C linkage", 'extern "C" bool func_00319330(S *self)' in t and "\nbool g(S *self)" in t, t)
CASES += [None]
# gen1: the decomp's linker aliases (config/undefined_syms_manual.txt) name functions and data
ms = d.manual_symbols("/* N66: C calls CB3Strings::GetString by name */\nGetString__10CB3StringsFi = func_00271A00;\n"
                      "PROVIDE(func_00319C80 = 0x00319C80);\nkan8HeadingAwardIconImages = 0x004E1D20;\nPROVIDE(D_004C52DC = 0x004C52DC);\n")
_check("manual symbols", ms == {"GetString__10CB3StringsFi": 0x00271A00, "kan8HeadingAwardIconImages": 0x004E1D20}, ms)
CASES += [None]
# gen1 T35: `T *&` (a reference to a pointer stored in guest memory) -> gp<T> &, compiler-guided (ref_pointer_transforms,
# only for a unit that fails to bind one); a cast `(T *&)x` stays T9's. Always: `T *const &` in an extern "C" prototype
# (a guest callee: func_00310180's `unsigned short *const &arg`)
t = d.ref_pointer_transforms("void f(void)\n{\n    CGt2dObject *&p = field_48[i];\n    const T *const &q = r;\n"
                             "    *(((int *&)field_1D04)++) = 1;\n}\nvoid g(U *&u);\n")
_check("T35", "b3host::gp<CGt2dObject> &p = field_48[i];" in t and "b3host::gp<const T> const &q" in t
       and "((int *&)field_1D04)" in t and "void g(b3host::gp<U> &u);" in t, t)
t = d.pointer_to_pointer_transforms('extern "C" void func_00310180(unsigned short *dst, const unsigned short *fmt, unsigned short *const &arg);\n'
                                    "static void inl2(RwUInt8 *id, RwUInt8 *&cur);\nvoid f(void)\n{\n    CGt2dObject *&p = field_48[i];\n}\n")
_check("T35 always: extern C const reference only", "b3host::gp<unsigned short> const &arg" in t and "RwUInt8 *&cur" in t
       and "CGt2dObject *&p" in t, t)
CASES += [None] * 2
# gen1 T11: a volatile quadword pointer cast reads plainly (func_001CB780)
t = d.transform_text("void f(CGtV3d v) { u_long128 a = *(volatile const u_long128 *)&v.mData; u_long128 b = *(volatile u_long128*)&v.mData; }\n",
                     _DecompNone, [])
_check("T11 volatile quadword", "*b3host::pcast<const u_long128>(&v.mData)" in t and "*b3host::pcast<u_long128>(&v.mData)" in t
       and "volatile u" not in t, t)
CASES += [None]
# gen1 T8b: declarations GCC names as crossed by a jump are split (const locals T8 does not see)
src = "void f(int j)\n{\n    if (j) goto out;\n    const int t24 = j + 1;\n    const T *const t13 = d->field_10;\n    CGtV2d v = g();\nout:\n    h();\n}\n"
err = ("u.cpp:7:1: error: jump to label 'out'\nu.cpp:3:15: note:   from here\nu.cpp:4:15: note:   crosses initialization of 'const int t24'\n"
       "u.cpp:5:20: note:   crosses initialization of 'const T* const t13'\nu.cpp:6:12: note:   crosses initialization of 'CGtV2d v'\n")
t = d.crossed_declaration_fix(src, "/x/u.cpp", err)
_check("T8b", "int t24 __attribute__((uninitialized)); t24 = j + 1; /* decomp2host T8b */" in t
       and "const T *t13 __attribute__((uninitialized)); t13 = d->field_10; /* decomp2host T8b */" in t
       and "CGtV2d v = g();" in t, t)   # CGtV2d: no trivial default constructor (gen1)
CASES += [None]
# gen1 T4: arrays of rows and arrays of const pointers are guest arrays (D_004B71B0, kapcB3HUDIconTextureNames)
class _DecompT4:
    symbols = {"kapcNames": 0x004B7600}


g = d.global_transforms("extern float D_004B71B0[][5];\nextern char D_01E7C8C0[4][64];\nextern const char * const kapcNames[];\n", _DecompT4, [])
_check("T4 rows / const pointers", "(&b3host::guest_global<ps2f[5]>(0x004B71B0u))" in g and "(&b3host::guest_global<char[64]>(0x01E7C8C0u))" in g
       and "(&b3host::guest_global<const b3host::gp<const char>>(0x004B7600u))" in g, g)
CASES += [None]
# gen1: the size of a scalar-only struct (a 4-byte one a stub passes by value needs its lone_float decision: RwRGBA)
_check("small struct size", d.small_struct_size("RwRGBA", ["struct RwRGBA { unsigned char red, green, blue, alpha; };"]) == 4
       and d.small_struct_size("P", ["struct P { float x; float y; };"]) == 8
       and d.small_struct_size("Q", ["struct Q { CGtV3d v; };"]) is None, "")
CASES += [None]
# gen1 T10b: MWCC's __int128 is the prelude's u_long128 (func_00217C40)
t = d.transform_text("void f(int p0) { const __int128 *t1 = &q[p0]; unsigned __int128 v = *t1; }\n", _DecompNone, [])
_check("T10b __int128", "const u_long128 *t1" in t and "u_long128 v = *t1;" in t and "__int128" not in t, t)
CASES += [None]
# gen1: T3b's product guard on native3's func_0025E410 lines 87 / 91 (`k * *(float *)(...)`, `k * e->field_30 * *(...)`)
t = d.pointer_to_pointer_transforms("void f(char *obj, E *e, float frac)\n{\n    float k = g();\n"
                                    "    float s = k * *(float *)(obj + 0x108) + frac;\n    float u = k * e->field_30 * *(float *)(obj + 0x10C);\n}\n")
_check("T3b product func_0025E410", "k * *(float *)(obj + 0x108)" in t and "e->field_30 * *(float *)(obj + 0x10C)" in t and "gp<" not in t, t)
CASES += [None]
# gen1 T34: a GtMathPs2 float element's address converts to ps2f* (the shadow headers' float members)
t = d.transform_text("void f(CGtV3d v) { ps2f *py = &v.marValues[1]; g(&self->field_10.marValues[0]); }\n", _DecompNone, [])
_check("T34", "ps2f *py = b3host::f32_addr(&v.marValues[1]);" in t and "g(b3host::f32_addr(&self->field_10.marValues[0]));" in t, t)
CASES += [None]
# gen1 T36: a kept definition's function-pointer parameter is a gfn, in its class declaration too; the wrapper takes the
# guest code address (CGtLobbyPS2DirtySock::ForAllLobbys 0x338440)
src = ("class CLobby {\npublic:\n    void ForAll(int (*cb)(int, void *), int n);\n};\n"
       "// 0x00338440\nvoid CLobby::ForAll(int (*cb)(int, void *), int n)\n{\n    cb(n, 0);\n}\n")
t = d.fnptr_param_transforms(src, {0x00338440: "ForAll__6CLobbyFPFiPv_ii"}, [])
_check("T36 definition and declaration", t.count("b3host::gfn<int(int, void *)> cb, int n") == 2, t)
d.HEADER_TEXT.clear()
w = d.wrapper_for("void CLobby::ForAll(b3host::gfn<int(int, void *)> cb, int n)", t, 0x00338440)[1]
_check("T36 wrapper", "b3gen_00338440(b3host::gp<void> self, unsigned int a0, int a1)" in w
       and "b3host::gfn<int(int, void *)>::from_raw(a0)" in w, w)
CASES += [None] * 2
# gen1: a namespace function the unit declares stays one when another unit's header has a class of that name declaring
# it as a member (CB3Globe2dObject::_Render's B3Im3d::SetBackfaceCull vs classes/B3Im3d.h; full build only)
d.HEADER_TEXT.clear()
d.HEADER_TEXT.append("class B3Im3d {\npublic:\n    void SetBackfaceCull(bool p0);\n};\n")
_check("namespace from the unit", d.is_namespace("B3Im3d", "namespace B3Im3d {\nvoid SetBackfaceCull(bool);\n}\n", "SetBackfaceCull")
       and not d.is_namespace("B3Im3d", "void g();\n", "SetBackfaceCull"), "")
d.HEADER_TEXT.clear()
CASES += [None]
# gen1: a by-value parameter of a function-pointer typedef nested in its class (B3_FNPTR -> T14 gfn) is the caller's
# guest code address, qualified (CGt2dObject::Prepare 0x1D5E70's UnkFn_RenderCallback)
d.HEADER_TEXT.clear()
u = ("class CGt2dObject {\npublic:\n    typedef b3host::gfn<void(const CGt2dObjectSpace&, const void*)> UnkFn_RenderCallback /* decomp2host T14 */;\n"
     "    void Prepare(int a, UnkFn_RenderCallback cb, unsigned int n);\n};\n")
w = d.wrapper_for("void CGt2dObject::Prepare(int a, UnkFn_RenderCallback cb, unsigned int n)", u, 0x001D5E70)[1]
_check("gfn typedef parameter", "b3gen_001D5E70(b3host::gp<void> self, int a0, unsigned int a1, unsigned int a2)" in w
       and "CGt2dObject::UnkFn_RenderCallback::from_raw(a1)" in w, w)
CASES += [None]
# T31 (boot2): a rewrite stays in the escaping local's own function (two functions with a local `b`)
src = "int g(int p)\n{\n    bool b;\n    b = p != 0;\n    return b;\n}\nint f(S *p)\n{\n    T b;\n    p->field_0 = &b;\n    return 0;\n}\n"
hits = d.escape_lint(src, guest_names=set())
t = d.escape_fix(src, [(x[2], x[3]) for x in hits])
_check("T31 own function", "    bool b;\n" in t and "b3host::GuestLocal<T> b3_esc_b; T &b = *b3_esc_b;" in t, t)   # gen1: the GuestLocal form
# gen1: an unmarked helper that is a game function (func_00173D30 in the 0x186850 draft; B3TrafficVehicle.cpp defines it
# too) is dropped like a marked one, keeping a declaration when none exists: the call is a stub, one definition links
src = ('extern "C" void func_00173D30(V * self)\n{\n    self->a = 1;\n}\nstatic inline int inl1(V *v) { return v->b; }\n'
       '// 0x00186850\nvoid Update(V *self)\n{\n    if (inl1(self)) func_00173D30(self);\n}\n')
t, _k = d.reduce_unit(src, {0x00186850}, [], {0x00186850: "Update"})
_check("game-function helper", 'extern "C" void func_00173D30(V * self);   // decomp2host T5: declaration kept' in t
       and "self->a = 1" not in t and "static inline int inl1" in t, t)
CASES += [None]
# gen1 T9: a cast to a pointer to a function pointer reads a guest function pointer (a vtable slot, 0x1BB6E0)
t = d.cast_transforms("void f(void *self) { (*(void (**)(void *, int))(*(char **)self + 0x28))(self, 0); }\n", [])
_check("T9 pointer to function pointer", "(*b3host::pcast<b3host::gfn<void(void *, int)>>((*b3host::pcast<b3host::gp<char>>(self) + 0x28)))(self, 0);" in t, t)
CASES += [None]
# gen1 T7 (compiler-guided): a header-declared class forward-declared inside the unit's namespace moves out; a namespace
# the header also declares gets `using namespace ::X;` (func_003D62A0 / func_00381030 and Gt2d's SetTexture)
_tmp2 = tempfile.mkdtemp(prefix="d2h_test_")
os.makedirs(os.path.join(_tmp2, "include", "b3"))
open(os.path.join(_tmp2, "include", "b3", "G.h"), "w").write("class CGtTexture;\nnamespace Gt2dRenderer { void SetTexture(CGtTexture *); }\n")
src = ('#include "b3gen/prelude.h"\n#include "b3/G.h"\nnamespace b3gen_u {\nclass CGtTexture;\nclass COwn;\nstruct COwn { int a; };\n'
       "namespace Gt2dRenderer {\nvoid SetZWrite(bool);\n}\nvoid f(CGtTexture *t) { Gt2dRenderer::SetTexture(t); }\n\n}  // namespace b3gen_u\nusing namespace b3gen_u;\n")
t = d.t7_header_names(src, _tmp2)
_check("T7 header names", t.index("class CGtTexture;") < t.index("namespace b3gen_u {") and "class COwn;" in t.split("namespace b3gen_u {")[1]
       and "namespace Gt2dRenderer { using namespace ::Gt2dRenderer; }" in t.split("namespace b3gen_u {")[1], t)
CASES += [None]
# gen1 T36: in such a unit the extern "C" prototypes' function-pointer parameters are gfn too (a received gfn is handed on)
src = ('extern "C" int IntersectLine(void *t, const L &l, bool (*cb)(const R &, void *), void *ud);\n'
       '// 0x00301BC0\nextern "C" int Unit(void *self, bool (*cb)(const R &, void *), void *ud)\n{\n    return IntersectLine(self, l, cb, ud);\n}\n')
t = d.fnptr_param_transforms(src, {0x00301BC0: "Unit"}, [])
_check("T36 prototypes", t.count("b3host::gfn<bool(const R &, void *)> cb") == 2, t)
CASES += [None]
# gen1: a GtMathPs2 value type the unit declares itself (no GtMath include) is named as the unit has it in the wrapper
d.HEADER_TEXT.clear()
w = d.wrapper_for('extern "C" int f(void *self, const CGtSphere s)', "struct CGtSphere { u_long128 mData; };\n", 0x00301B00)[1]
_check("wrapper local GtMath type", "*a1.cast<CGtSphere>()" in w and "::GtMathPs2::" not in w, w)
w = d.wrapper_for('extern "C" int f(void *self, const CGtSphere s)', '#include "b3/GtMathPs2.h"\n', 0x00301B00)[1]
_check("wrapper shadow GtMath type", "*a1.cast<::GtMathPs2::CGtSphere>()" in w, w)
CASES += [None] * 2
# gen1 T9 (compiler-guided): a game function cast to a gfn, passed where the callee takes a host function pointer
src = "void f()\n{\n    Prep(this, b3host::gfn<void(const S &, const void *)>::from_raw(b3host::icast<unsigned int>(_Render__X)));\n}\n"
err = "/x/u.cpp:3:30: error: cannot convert \u2018b3host::gfn<void(const S&, const void*)>\u2019 to \u2018void (*)(const S&, const void*)\u2019\n"
t = d.host_fnptr_fix(src, "/y/u.cpp", err)
_check("T9 host function pointer", "Prep(this, reinterpret_cast<void (*)(const S&, const void*)>(&_Render__X));" in t, t)
CASES += [None]
# gen1 T8b: a crossed GtMath value (copy- or direct-initialised, a call with `->` arguments) and a struct with only
# empty constructor / destructor (func_003A9250, func_00257C70)
src = ("struct UnkStruct_pad { int x[4]; UnkStruct_pad() {} ~UnkStruct_pad() {} };\nvoid f(S *self, int n)\n{\n    if (n) goto out;\n"
       "    CGtV3d pos = g(self->a, self->b);\n    CGtV4d r(1.0f, 2.0f, 3.0f, 4.0f);\n    UnkStruct_pad q0;\nout:\n    h();\n}\n")
err = ("u.cpp:9:1: error: jump to label 'out'\nu.cpp:5:12: note:   crosses initialization of \u2018GtMathPs2::CGtV3d pos\u2019\n"
       "u.cpp:6:12: note:   crosses initialization of \u2018GtMathPs2::CGtV4d r\u2019\n"
       "u.cpp:7:19: note:   crosses initialization of \u2018b3gen_u::UnkStruct_pad q0\u2019\n")
t = d.crossed_declaration_fix(src, "/x/u.cpp", err)
_check("T8b GtMath / empty ctor", "CGtV3d pos __attribute__((uninitialized)); pos = g(self->a, self->b); /* decomp2host T8b */" in t
       and "CGtV4d r __attribute__((uninitialized)); r = CGtV4d(1.0f, 2.0f, 3.0f, 4.0f); /* decomp2host T8b */" in t
       and "UnkStruct_pad() {}" not in t and "int x[4];" in t, t)
CASES += [None]
# gen1: GtMathPs2 header inlines emitted out of line become definitions over the shadow header (0x1D4E80, 0x31A590, 0x2ED2C0)
g1 = d.gtmath_instance("__as__Q29GtMathPs26CGtV4dFRCQ29GtMathPs26CGtV4d", 0x001D4E80)
g2 = d.gtmath_instance("__ct__Q29GtMathPs212CGtMatrix2x3FRCQ29GtMathPs212CGtMatrix2x3", 0x0031A590)
g3 = d.gtmath_instance("GetSquaredLength__Q29GtMathPs26CGtV3dCFv", 0x002ED2C0)
_check("GtMath instances", "// 0x001D4E80" in g1 and "*self = o;" in g1 and "::new (static_cast<void *>(self)) GtMathPs2::CGtMatrix2x3(o);" in g2
       and "*ret = self->GetSquaredLength();" in g3 and d.gtmath_instance("Update__10CB3RaceCarFv", 0x00152770) is None
       and d.gtmath_instance("__as__Q29GtMathPs26CGtV4dFRCQ29GtMathPs26CGtV3d", 0x1) is None, (g1, g2, g3))
CASES += [None]
# gen1: a guest call before the C name's last declaration (an inline member, an earlier function: rw_texcache.cpp's
# _rwDMAOpenVIFPkt) gets the macro after the first declaration; later declarations are wrapped in #undef / #define
src = ('extern "C" int F(int);\nstruct S { int g() { return F(1); } };\nvoid h() { F(2); }\nextern "C" int F(int);\n'
       'void k() { F(3); }\nextern "C" int G(int);\nvoid j() { G(4); }\n')
t, nm = d.place_guest_macros(src, {"F": 0x001F3080, "G": 0x00100000}, [])
fdef = "#define F (b3host::guest_fn<decltype(F), 0x001F3080u>::call)"
_check("guest macro before the first call", nm == 2 and t.index(fdef) < t.index("return F(1)") and "#undef F" in t
       and t.index("#undef F") < t.rindex('extern "C" int F(int);') < t.rindex(fdef)
       and t.count("#define G (") == 1 and "#undef G" not in t, t)
CASES += [None]
# gen1 T20: a goto label before a store in a case is no declaration (func_00452ED0: `c10_join: *out = g(x);` had become
# `c10_join:* out; out = g(x);`, a store to the pointer itself)
src = ("void f(int x, signed char *out)\n{\n    switch (x) {\n    case 1:\n        if (x) goto c10_join;\n    c10_join:\n        *out = g(x);\n"
       "        break;\n    case 2:\n        int t0 = x + 1;\n        h(t0);\n    }\n}\n")
t = d.case_decl_transforms(src, [])
_check("T20 label", "    c10_join:\n        *out = g(x);" in t and "int t0; /* decomp2host T20 */ t0 = x + 1;" in t, t)
CASES += [None]
# gen1: pointer <-> integer conversions -fpermissive accepted, found in the host compile's diagnostics and made explicit
src = ("void a() {\n    (b3host::guest_global<int>(0x004E2D50u)) = this;\n}\n"
       "void b(void *p2) {\n    s->Handle((EB3MenuFlowFSMEvents)(int)p2);\n}\n"
       "void c(void *p, unsigned int t, int off, int n) {\n    memcpy(b3host::icast<unsigned int>(p), t + off, n);\n}\n")
err = ("/x/u.cpp:2:48: warning: invalid conversion from ‘CB3CoronaRenderer*’ to ‘int’ [-fpermissive]\n"
       "/x/u.cpp:5:37: warning: cast from ‘void*’ to ‘int’ loses precision [-fpermissive]\n"
       "/x/u.cpp:8:39: warning: invalid conversion from ‘unsigned int’ to ‘void*’ [-fpermissive]\n"
       "/x/u.cpp:8:46: warning: invalid conversion from ‘unsigned int’ to ‘const void*’ [-fpermissive]\n"
       "/x/u.cpp:9:1: warning: invalid conversion from ‘const char*’ to ‘char*’ [-fpermissive]\n")
cv = d.permissive_conversions(err, "u.cpp")
t = d.permissive_fix(src, cv)
_check("permissive conversions", [c[4] for c in cv] == ["p2i", "cast", "i2p", "i2p"]
       and "= b3host::icast<int>(this);" in t and "s->Handle((EB3MenuFlowFSMEvents)b3host::icast<int>(p2));" in t
       and "memcpy((p), b3host::pcast<const void>(t + off), n);" in t, (cv, t))
CASES += [None]
# T12 (boot2): a static member's use inside a #define body is rewritten too
m = d.preproc_mask_keep_defines("#include <x>\n#define RB (T *)C::mg, 1\nint y;\n")
_check("T12 define body", m.split("\n")[0].strip() == "" and "C::mg, 1" in m.split("\n")[1] and "RB" not in m.split("\n")[1], m)
# gen1 (T31 audit): escaping locals become b3host::GuestLocal holders with the declaration's own initialisation
src = ("int f(S *p)\n{\n    T tbl;\n    unsigned char arr[4];\n    Q q = {1, 2};\n    R r = g(3);\n    W w(1, 2);\n    const C c = k();\n"
       "    int v[2] = {5};\n    p->a = &tbl; p->b = arr; p->c = &q; p->d = &r; p->e = &w; p->f = &c; p->g = v;\n    return 0;\n}\n")
h = d.escape_lint(src)
t = d.escape_fix(src, [(x[2], x[3], x[4]) for x in h])
_check("T31 forms", "b3host::GuestLocal<T> b3_esc_tbl; T &tbl = *b3_esc_tbl;" in t
       and "b3host::GuestLocalArray<unsigned char, (4)> b3_esc_arr; unsigned char (&arr)[4] = *b3_esc_arr;" in t
       and "b3host::GuestLocal<Q> b3_esc_q(b3host::brace_init, 1, 2); Q &q = *b3_esc_q;" in t
       and "b3host::GuestLocal<R> b3_esc_r(b3host::copy_init, g(3)); R &r = *b3_esc_r;" in t
       and "b3host::GuestLocal<W> b3_esc_w(1, 2); W &w = *b3_esc_w;" in t
       and "b3host::GuestLocal<const C> b3_esc_c(b3host::copy_init, k()); const C &c = *b3_esc_c;" in t
       and "{ int b3_ini_v[2] = {5}; for (std::size_t b3_i = 0; b3_i < (2); b3_i++) v[b3_i] = b3_ini_v[b3_i]; }" in t
       and not d.escape_lint(t), t)
CASES += [None]
# gen1 (T31 audit): the declaration in scope at the escape is the one rewritten (a shadowing inner block's, not an
# outer or sibling one of the same name; boot2's GtSoundStream case was another function's)
src = ("int f(S *p, int k)\n{\n    T b;\n    use(b);\n    if (k) {\n        T b;\n        p->a = &b;\n    }\n    { T b; use(b); }\n"
       "    return 0;\n}\nint g(S *p)\n{\n    bool b = 0;\n    return b;\n}\n")
h = d.escape_lint(src)
t = d.escape_fix(src, [(x[2], x[3], x[4]) for x in h])
_check("T31 shadowing", len(h) == 1 and t.count("GuestLocal") == 1 and "    T b;\n    use(b);" in t
       and "        b3host::GuestLocal<T> b3_esc_b; T &b = *b3_esc_b;" in t and "{ T b; use(b); }" in t and "bool b = 0;" in t, t)
CASES += [None]
# gen1 (T31 audit): a T8-split local (a goto crosses it) is seen and its holder goes to the function's start; an
# escaping by-value parameter gets a guest copy under its name
src = ("int f(S *p, CGtV3d v, int k)\n{\n    if (k) goto out;\n    T x __attribute__((uninitialized)); x = g(); /* decomp2host T8 */\n"
       "    p->d = &x; p->e = &v;\nout:\n    return 0;\n}\n")
h = d.escape_lint(src)
t = d.escape_fix(src, [(x[2], x[3], x[4]) for x in h])
_check("T31 hoist / parameter", sorted(x[2] for x in h) == ["v", "x"] and "int f(S *p, CGtV3d b3_arg_v, int k)" in t
       and "b3host::GuestLocal<CGtV3d> b3_esc_v(b3host::copy_init, b3_arg_v); CGtV3d &v = *b3_esc_v;" in t
       and "b3host::GuestLocal<T> b3_esc_x; T &x = *b3_esc_x;" in t.split("if (k)")[0]
       and "/* decomp2host T31: x moved to the function's start (crossed by a jump) */ x = g();" in t, t)
CASES += [None]
# gen1 (T31 audit): a comma list's escaping declarator is split out; a static local is no stack local
src = "int f(S *p)\n{\n    T a, b = g(), c;\n    static T s;\n    p->x = &b; p->y = &s;\n    return 0;\n}\n"
h = d.escape_lint(src)
t = d.escape_fix(src, [(x[2], x[3], x[4]) for x in h])
_check("T31 comma list / static", [x[2] for x in h] == ["b"] and "T a; b3host::GuestLocal<T> b3_esc_b(b3host::copy_init, g()); T &b = *b3_esc_b;" in t
       and " T c;" in t and "static T s;" in t, t)
CASES += [None]
# gen1 (T31 audit): a case label is no type (`default: a = 1;` read as a declaration of a, so the real one was skipped,
# func_00131B30); a comment above the declaration stays out of the holder's type (func_004DB400's `// sp+0x20`)
src = ("int f(S *p, int k)\n{\n    int a;\n    switch (k) {\n    default: a = 1;\n    }\n    // sp+0x20\n    V b = { 1.0f, 2.0f };\n"
       "    p->x = &a; p->y = &b;\n    return 0;\n}\n")
h = d.escape_lint(src)
t = d.escape_fix(src, [(x[2], x[3], x[4]) for x in h])
_check("T31 label / comment", sorted(x[2] for x in h) == ["a", "b"] and "b3host::GuestLocal<int> b3_esc_a; int &a = *b3_esc_a;" in t
       and "    // sp+0x20\n    b3host::GuestLocal<V> b3_esc_b(b3host::brace_init, 1.0f, 2.0f); V &b = *b3_esc_b;" in t
       and "default: a = 1;" in t and not d.escape_lint(t), t)
CASES += [None]
# gen1 (T31 audit): several escaping declarators of one comma list all become holders (B3SoundESM_00287970's pos, at)
src = "int f(S *p)\n{\n    CGtV3d pos, at, vel;\n    p->a = &pos; p->b = &at;\n    return 0;\n}\n"
h = d.escape_lint(src)
t = d.escape_fix(src, [(x[2], x[3], x[4]) for x in h])
_check("T31 comma list, two escaping", t.count("GuestLocal<CGtV3d>") == 2 and " CGtV3d vel;" in t and not d.escape_lint(t), t)
CASES += [None]
# T37 (boot2): direct hardware access through a volatile pointer cast
r = []
t = d.mmio_transforms("#define HW(a) (*(volatile unsigned int *)(a))\nvoid f(int *p) { *(volatile unsigned int *)0x10008000 = 0x145; x = *(volatile int *)&p[1]; *(volatile u_long128 *)(q) = v; }\n", r)
_check("T37 macro", "#define HW(a) (b3host::mmio<unsigned int>(a))" in t, t)
_check("T37 literal", "b3host::mmio<unsigned int>(0x10008000) = 0x145" in t, t)
_check("T37 &object kept", "*(volatile int *)&p[1]" in t and "*(volatile u_long128 *)(q)" in t, t)
# gen1 T8 / T8b: several declarators counted by brackets (`->` is none); a crossed one split in order (func_003D1F00)
src = "void f(N *na, N *nb, int k)\n{\n    if (k) goto out;\n    int fb = *(int *)&nb->field_18, fa = *(int *)&na->field_18;\nout:\n    h(fa, fb);\n}\n"
t = d.goto_transforms(src)
_check("T8 several declarators", "int fb = *(int *)&nb->field_18, fa = *(int *)&na->field_18;" in t, t)
err = "u.cpp:5:1: error: jump to label 'out'\nu.cpp:4:9: note:   crosses initialization of ‘int fb’\n"
t = d.crossed_declaration_fix(src, "/x/u.cpp", err)
_check("T8b several declarators", "int fb __attribute__((uninitialized)); fb = *(int *)&nb->field_18; int fa __attribute__((uninitialized)); fa = *(int *)&na->field_18;" in t, t)
CASES += [None] * 2
# gen1 (speed): T12's header statics found by word (a set lookup, not one regex per member): the same members, in
# the same order; a longer identifier containing the name is no use of it
class _DecompSt2:
    symbols = {}


d.STATIC_MEMBERS[:] = [("A", "mgX", "int", 0x100, False), ("B", "mgY", "int", 0x200, True), ("C", "mgZ", "int", 0x300, False)]
t = d.static_member_transforms("void f() { g(mgY); h(A::mgX); k(mgZq); }\n", _DecompSt2, [], apply_header_statics=True)
_check("T12 header statics by word", t == "void f() { g((&b3host::guest_global<int>(0x00000200u))); h((b3host::guest_global<int>(0x00000100u))); k(mgZq); }\n", t)
d.STATIC_MEMBERS.clear()
CASES += [None]
# gen1 (speed): mask() jumps between comments and literals with a regex; the old per-character loop is the reference
_mt = ['int a; // c "q"\nb = "x\\"y" + \'/\'; /* m\n n */ c', '"open', "'\\", "/* open\n", "a //", "x/*/ y */z", "'\\''",
       '"\\', "//", "s = \"//\"; t = '/*'; /**/", ""]
_check("mask = mask_slow", all(d.mask.__wrapped__(x) == d.mask_slow(x) for x in _mt), [x for x in _mt if d.mask.__wrapped__(x) != d.mask_slow(x)])
CASES += [None]
# gen1 (speed): T17 pairs brackets in one pass and counts guest calls by position: a call nested in brackets of
# another kind, and a guest name inside a longer identifier, as before
t = d.guest_arg_order_transforms("void f()\n{\n    h(a[g1(1)], {g2(2)}, xg1(3));\n    k(g1(1), m(xg2(2)));\n}\n",
                                 {"g1", "g2"}, [], "test")
_check("T17 brackets / identifiers", "auto&& __b3_seq0_0 = (a[g1(1)]);" in t and "auto&& __b3_seq0_1 = ({g2(2)});" in t
       and "    k(g1(1), m(xg2(2)));" in t, t)
CASES += [None]
# gen1 (speed): call_stubs' T31 (c) compile outcome cached by the unit's text (both outcomes)
import tempfile
with tempfile.TemporaryDirectory() as _td:
    os.makedirs(os.path.join(_td, "units")); os.makedirs(os.path.join(_td, "include"))
    _ok, _bad = os.path.join(_td, "units", "ok.cpp"), os.path.join(_td, "units", "bad.cpp")
    open(_ok, "w").write("int b3_test_ok() { return 1; }\n"); open(_bad, "w").write("int b3_test_bad() { return }\n")
    _old = os.environ.get("DECOMP2HOST_CACHE"); os.environ["DECOMP2HOST_CACHE"] = os.path.join(_td, "cache"); os.makedirs(os.environ["DECOMP2HOST_CACHE"])
    r1 = (d.compiles_cached(_ok), d.compiles_cached(_bad))
    n1 = len(os.listdir(os.environ["DECOMP2HOST_CACHE"]))
    r2 = (d.compiles_cached(_ok), d.compiles_cached(_bad))
    if _old is None: del os.environ["DECOMP2HOST_CACHE"]
    else: os.environ["DECOMP2HOST_CACHE"] = _old
_check("T31c compile cache", r1 == (True, False) and r2 == r1 and n1 == 2, (r1, r2, n1))
CASES += [None]
# gen1 (speed): call_stubs' pool worker takes its context from _STUBS_CTX (set before the fork); a unit that did not
# compile passes its error through
_e = d.GenError("x")
d._STUBS_CTX[:] = [["u.cpp"], [_e], {}, None, True]
_check("call_stubs pool worker", d._stubs_or_fail(0) is _e, None)
d._STUBS_CTX[:] = []
CASES += [None]
# gen1 (speed): preproc_mask as one regex substitution, the per-line form as reference
_pt = ["x\n  #define A\n\t# x\n#\n \r#y\nz", "\x0b#a\n\xa0#b", "#", "a#b\n #", ""]
_check("preproc_mask = preproc_mask_slow", all(d.preproc_mask(x) == d.preproc_mask_slow(x) for x in _pt), None)
CASES += [None]
# gen1 (speed): scoped()'s base-class lookups from per-text indexes: the first match per name, as re.search found it
# (overlapping candidates: a base list that runs into a later class head; an identifier captured whole)
_src = "class A : x class C : y {}; struct CX : z { struct In; }; class C : w {}; struct D { };\nclass AB : q {"
_ok = True
for _c in ["A", "C", "CX", "D", "AB", "In", "Q"]:
    _bm = d.re.search(r"\b(?:class|struct)\s+%s\s*:\s*([^{;]*)\{" % d.re.escape(_c), _src)
    _um = d.re.search(r"\b(class|struct)\s+%s\b[^;{]*\{" % d.re.escape(_c), _src)
    _ok = _ok and (_bm.group(1) if _bm else None) == d._class_base_index(_src).get(_c) and (_um.end() if _um else None) == d._class_open_index(_src).get(_c)
_check("class indexes = searches", _ok, (d._class_base_index(_src), d._class_open_index(_src)))
CASES += [None]
# gen1 (speed): top_level_functions memoised: equal results, and a caller editing its list does not edit the cache's
_s = "namespace N {\nint f(int a)\n{\n    return a;\n}\n}\nextern \"C\" {\nvoid g() { }\n}\nstruct S { int x; };\n"
_r1 = d.top_level_functions(_s); _r1.append(None); _r2 = d.top_level_functions(_s)
_check("top_level_functions memo", len(_r2) == 2 and _r2[0][4] == "int f(int a)" and _r2[1][4].endswith("void g()"), _r2)
# gen1 T3: member statements: an aggregate defined in the member's declaration (pointer declarators -> gp of it, an
# anonymous one named), an inline method definition, a method declaration with a function-pointer parameter, a pointer
# to a function pointer (0x152AF0, 0x1B5AA0, 0x301340, 0x3078F0, 0x4670A0)
src = ("struct O {\n    struct { unsigned char pad_0[0x1B4]; L * field_1B4; } * field_0;\n"
       "    struct V { unsigned char pad_0[0xA0]; signed char field_A0; } *field_60;\n    struct { void * field_0; } field_30[2];\n"
       "    D * f_inl1(int u) { return u ? (D *)0 : (*g)(u); }\n    void ForEvery(void (*)(D *, int, void *), void *);\n"
       "    int (**field_4C)(int, void *, void *);\n};\n")
t = d.member_transforms(src)
_check("member statements", "b3host::gp<L> field_1B4" in t and "b3host::gp<b3anon_" in t and "} ; b3host::gp<V> field_60;" in t.replace("}; b3host", "} ; b3host")
       and "b3host::gp<void> field_0; } field_30[2];" in t and "D * f_inl1(int u)" in t and "void ForEvery(void (*)(D *, int, void *), void *);" in t
       and "b3host::gp<b3host::gfn<int(int, void *, void *)>> field_4C;" in t, t)
CASES += [None]
# gen1: a multi-line #define's continued lines are no part of the next function's head (func_003A9B70's ZOOM macro)
src = ("#define ZOOM(O) \\\n    a = 1; \\\n    Prepare__X(O, 0)\n\nstatic inline float func_003A9B70_inl1(float x)\n{\n    return x;\n}\n")
fns = d.top_level_functions(src)
_check("continued #define", len(fns) == 1 and fns[0][4].strip().startswith("static inline float func_003A9B70_inl1"), fns)
CASES += [None]
# gen1 T4: `T const X[]` (const elements) and `&X` of a guest array inside a #define body (0x1D6AD0, 0x1D2210)
class _DecompT4b:
    symbols = {"mgapfRenderFunctions__15CGt2dObjectText": 0x004B8120}


g = d.global_transforms("extern \"C\" CGt2dObject::UnkFn_RenderCallback const mgapfRenderFunctions__15CGt2dObjectText[];\n"
                        "extern UnkStruct_spike D_0066DEB0[];\n#define SPF(off) (((float *)((char *)&D_0066DEB0 + (off)))[0])\n", _DecompT4b, [])
_check("T4 const elements / macro &array", "guest_global<const CGt2dObject::UnkFn_RenderCallback>(0x004B8120u)" in g
       and "((char *)D_0066DEB0 + (off))" in g, g)
CASES += [None]
# gen1 T34: a ps2f reference bound to a GtMathPs2 float element (func_0033D890)
t = d.f32_address_transforms("void f(CGtV2d total) { ps2f &th = total.marValues[1]; }\n")
_check("T34 reference", "ps2f &th = *static_cast<ps2f *>(b3host::f32_addr(&total.marValues[1]));" in t, t)
CASES += [None]
# gen1: static or not by the declaration with the definition's parameter count (CB3AILane::LaneIsDriveable, 0x148D60)
d.HEADER_TEXT.clear()
unit = ("class CB3AILane {\npublic:\n    static bool LaneIsDriveable(unsigned char a, unsigned short b, unsigned short c);\n"
        "    bool LaneIsDriveable(unsigned short a, unsigned short b);\n};\n")
w = d.wrapper_for("bool CB3AILane::LaneIsDriveable(unsigned short a, unsigned short b)", unit, 0x00148D60)[1]
w2 = d.wrapper_for("bool CB3AILane::LaneIsDriveable(unsigned char a, unsigned short b, unsigned short c)", unit, 0x00148CA0)[1]
_check("static overload", "self.cast<CB3AILane>()->LaneIsDriveable(a0, a1)" in w and "b3gen_00148CA0(unsigned char a0" in w2, (w, w2))
CASES += [None]
# gen1 T3b (compiler-guided): `T *const *` where GCC finds a gp<T> pointer (func_0028E2E0)
src = "void f(int i)\n{\n    const CB3Wheel *const *wp = &wheels[i];\n}\n"
err = "/x/u.cpp:3:39: error: cannot convert ‘b3host::gp<const CB3Wheel>*’ to ‘const CB3Wheel* const*’ in initialization\n"
t = d.const_ptr_ptr_fix(src, "/y/u.cpp", err)
_check("T3b const pointer", "b3host::gp<const CB3Wheel> const *wp = &wheels[i];" in t, t)
CASES += [None]
# gen1: no data definition: a typedef of / an extern of an anonymous struct, a multi-line #define (0x1AB610, 0x2E7D20,
# 0x166F50); T4 names the anonymous extern's type
d.RAISE = True
try:
    d.check_no_data("typedef struct { float x, y; } UnkStruct_q;\nextern struct { unsigned char field_0; } D_004E2D3C;\n"
                    "#define ADD(n) \\\n    for (i = 0; i < n; i++) { \\\n        f(i); \\\n    }\n", "test")
    ok_ = True
except d.GenError as e_:
    ok_ = str(e_)
d.RAISE = False
_check("no data definition", ok_ is True, ok_)


class _DecompNone2:
    symbols = {}


g = d.global_transforms("extern struct { unsigned char field_0; } D_004E2D3C;\nvoid f() { D_004E2D3C.field_0 = 0; }\n", _DecompNone2, [])
_check("T4 anonymous extern struct", "struct b3anon_D_004E2D3C { unsigned char field_0; };" in g
       and "guest_global<b3anon_D_004E2D3C>(0x004E2D3Cu)" in g, g)
CASES += [None] * 2
# gen1 T12: a static data member used in a #define body is guest data there too (func_00371BE0's RB macro)


class _DecompSt:
    symbols = {"mgu8RenderBatchData__11CGt2dObject": 0x01E8A000}


d.STATIC_MEMBERS.clear()
t = d.static_member_transforms("class CGt2dObject {\npublic:\n    static unsigned char mgu8RenderBatchData[0x1000];\n};\n"
                               "#define RB ((CGtV2d *)CGt2dObject::mgu8RenderBatchData)\nvoid f() { g(RB); }\n", _DecompSt, [])
_check("T12 in a macro body", "#define RB ((CGtV2d *)(&b3host::guest_global<unsigned char[0x1000]>(0x01E8A000u)))" in t
       or "#define RB ((CGtV2d *)(&b3host::guest_global<unsigned char>(0x01E8A000u)))" in t, t)
d.STATIC_MEMBERS.clear()
CASES += [None]
# gen1: a marker with text after the address names the definition below it (B3HeadingComponent_00319C80.cpp)
src = ("// 0x00319C80 (Target #32): Prepare(EGlobalStrings, EHeadingType), identity from the E-build maps (N66).\n"
       "// In the E builds this overload is a wrapper.\nbool CB3HeadingComponent::Prepare(int id, int type)\n{\n    return id;\n}\n")
t, kept = d.reduce_unit(src, {0x00319C80}, [], {0x00319C80: "func_00319C80"})
_check("marker with text", len(kept) == 1 and "// 0x00319C80\nbool CB3HeadingComponent::Prepare" in t, t)
CASES += [None]
# gen1 (compiler-guided): GCC's "did you mean b3gen_U::X" for a helper called from a header class's member (0x19D4D0)
src = "void C::f()\n{\n    if (a != func_0019D4D0_inl2(me)) {}\n}\n"
err = "/x/u.cpp:3:14: error: ‘func_0019D4D0_inl2’ was not declared in this scope; did you mean ‘b3gen_U::func_0019D4D0_inl2’?\n"
t = d.ns_qualify_fix(src, "/y/u.cpp", err)
_check("namespace qualification", "if (a != b3gen_U::func_0019D4D0_inl2(me))" in t, t)
CASES += [None]
# gen1: DECOMP2HOST_NATIVE_ROWS=1 ends each unit with its native table rows and lists the arrays in
# b3port/native_rows.h, in game_overrides' order; without it no file and the units unchanged
import tempfile
with tempfile.TemporaryDirectory() as _td:
    os.makedirs(os.path.join(_td, "units")); os.makedirs(os.path.join(_td, "include/b3port"))
    for _u in ("a", "b"):
        open(os.path.join(_td, "units", _u + ".cpp"), "w").write("// " + _u + "\n")
    _ents = [("f1", 0x100, 8, 0, "b3gen_00000100", "matched", "src/a.cpp"), ("f2", 0x200, 12, 0, "b3gen_00000200", "matched", "src/a.cpp"),
             ("g", 0x50, 4, 0, "b3gen_00000050", "nonmatching", "src/b.cpp")]
    _units = {"src/a.cpp": [], "src/b.cpp": []}
    _old = os.environ.pop("DECOMP2HOST_NATIVE_ROWS", None)
    d.native_rows(_td, _units, _ents)
    _off = (not os.path.exists(os.path.join(_td, "include/b3port/native_rows.h")), open(os.path.join(_td, "units/a.cpp")).read())
    os.environ["DECOMP2HOST_NATIVE_ROWS"] = "1"
    d.native_rows(_td, _units, _ents)
    if _old is None: del os.environ["DECOMP2HOST_NATIVE_ROWS"]
    else: os.environ["DECOMP2HOST_NATIVE_ROWS"] = _old
    _lst = open(os.path.join(_td, "include/b3port/native_rows.h")).read()
    _ua = open(os.path.join(_td, "units/a.cpp")).read()
_check("native rows", _off == (True, "// a\n") and "X(b3rows_a, 2) \\\n    X(b3rows_b, 1)" in _lst
       and 'B3_NATIVE_ROW("f1", 0x00000100u, 8u, b3gen_00000100, matched),\n    B3_NATIVE_ROW("f2"' in _ua
       and "extern const b3host::NativeRow b3rows_a[] = {" in _ua, (_lst, _ua))
# gen1 T38: what only -fpermissive accepts, rewritten after a successful compile from GCC's own diagnostics: a jump
# past a for-init / a split class-type local (moved to the function's start), past `T *const p = e` (split in place,
# GCC 15's "skips" wording), a store into a volatile local aggregate, the address of a temporary's member
src = ("void f(S *s, int n)\n{\n    if (n) goto join;\n    for (int c = 0; c < n; c++) {\n    join:\n        g(c);\n    }\n"
       "    CGtV2d v __attribute__((uninitialized)); v = h(); /* decomp2host T8 */\n    T * const p = s->q;\n"
       "    volatile V t;\n    t.field_0 = 0.0f;\n    const CGtV3d *pp = &k(s, n).mPos;\n    use(v, p, pp);\n}\n"
       "void g2(int c)\n{\n    if (c) goto out;\n    for (int c2 = 0; c2 < 2; c2++) {\n    out:\n        h(c2);\n    }\n}\n")
err = ("/x/u.cpp:4:14: note:   skips initialization of ‘int c’\n/x/u.cpp:8:12: note:   skips initialization of ‘CGtV2d v’\n"
       "/x/u.cpp:9:15: note:   skips initialization of ‘T* const p’\n"
       "/x/u.cpp:11:17: warning: passing ‘volatile b3host::ps2f’ as ‘this’ argument discards qualifiers [-fpermissive]\n"
       "/x/u.cpp:12:44: warning: taking address of rvalue [-fpermissive]\n")
t = d.strict_fix(src, "/y/u.cpp", err)
_check("T38 strict C++", "{ CGtV2d v __attribute__((uninitialized)); /* decomp2host T38: crossed by a jump */ int c __attribute__((uninitialized));" in t
       and "    for (c = 0; c < n; c++) {" in t and "/* decomp2host T38: v at the function's start */ v = h();" in t
       and "T *p __attribute__((uninitialized)); p = s->q; /* decomp2host T8b */" in t
       and "/* decomp2host T38: volatile */ V t;" in t
       and "auto &&b3_tmp_pp = k(s, n); const CGtV3d *pp = &b3_tmp_pp.mPos;" in t
       and "for (int c2 = 0; c2 < 2; c2++)" in t and len(t.split("\n")) == len(src.split("\n")), t)
CASES += [None]
# T38: not moved when the function names it before its declaration (a hoist would capture that use)
src2 = "int c;\nvoid f(int n)\n{\n    c = n;\n    if (n) goto join;\n    for (int c = 0; c < n; c++) {\n    join:\n        g(c);\n    }\n}\n"
t = d.strict_fix(src2, "/y/u.cpp", "/x/u.cpp:6:14: note:   skips initialization of ‘int c’\n")
_check("T38 no capture", t == src2, t)
CASES += [None]
# gen1 T31: a char buffer that a sprintf-family call fills gets slack before the canary (retail overflows func_003B78D0's
# char[4] with "%d/" harmlessly); another array, none
src = 'void f(S *s)\n{\n    char b[4];\n    char c[8];\n    b3guest_sprintf(b, "%d/", s->n);\n    g(c);\n    s->p = b; s->q = c;\n}\n'
h = d.escape_lint(src)
t = d.escape_fix(src, [(x[2], x[3], x[4]) for x in h])
_check("T31 sprintf slack", "b3host::GuestLocalArray<char, (4), %d> b3_esc_b;" % d.SPRINTF_SLACK in t
       and "b3host::GuestLocalArray<char, (8)> b3_esc_c;" in t, t)
CASES += [None]
# T31: a 16-bit string a CGtUnicode Print* call fills gets the same slack (func_00458AE0's PrintUInt into ushort[4])
src = 'void func_00458AE0(S *s)\n{\n    unsigned short b[4];\n    unsigned short c[4];\n    PrintUInt__10CGtUnicodeFPUsUii(b, s->n, 1);\n    g(c);\n    s->p = b; s->q = c;\n}\n'
h = d.escape_lint(src)
t = d.escape_fix(src, [(x[2], x[3], x[4]) for x in h])
_check("T31 unicode print slack", "b3host::GuestLocalArray<unsigned short, (4), %d> b3_esc_b;" % d.SPRINTF_SLACK in t
       and "b3host::GuestLocalArray<unsigned short, (4)> b3_esc_c;" in t, t)
# (another unit's ushort buffer: unchanged, its frame keeps its layout)
src2 = src.replace("func_00458AE0", "func_0012345")
h = d.escape_lint(src2)
t = d.escape_fix(src2, [(x[2], x[3], x[4]) for x in h])
_check("T31 unicode print slack: other units", "b3host::GuestLocalArray<unsigned short, (4)> b3_esc_b;" in t, t)
CASES += [None]
# gen1: an asm macro's string argument keeps its commas and stays a bare literal (func_00429BE0's OPN); an identifier
# argument stays bare too
src = ('#define OPN(name, text) static inline u_long128 name(u_long128 a, u_long128 b) { u_long128 r; asm(text : "=j"(r) : "j"(a), "j"(b)); return r; }\n'
       'OPN(f_subyx, "vsuby.x %0, %1, %2y")\n')
t = d.expand_asm_macros(src, [])
_check("asm macro string argument", 'static inline u_long128 f_subyx(u_long128 a, u_long128 b) { u_long128 r; asm("vsuby.x %0, %1, %2y" : "=j"(r)' in t
       and d.split_macro_args('a, "x, y", (b, c)') == ["a", '"x, y"', "(b, c)"], t)
CASES += [None]
# gen1 T39: integer division / remainder with a non-literal divisor -> b3host::ee_div / ee_mod (the EE's results where
# x86 traps): operands delimited as C++ parses them; non-zero literal divisors stay; both operands calling stays
src = ("int f(S *s, int n, float x)\n{\n    int a = s->total % n;\n    int b = (int)s->k / s->c + 1;\n    int c = x * 2 / n;\n"
       "    int e = s->v[3] / 4;\n    float g = x / 2.0f;\n    a /= n + 1;\n    unsigned u = static_cast<unsigned>(a) % s->m[n].q;\n"
       "    bool w = a < b && (c) / n > 2;\n    int k = f(1) / g(2);\n    int z = a / 0;\n    return u / (n - 1);\n}\n")
t = d.ee_division_transforms(src, [])
_check("T39 EE division", "int a = b3host::ee_mod(s->total, n);" in t and "int b = b3host::ee_div((int)s->k, s->c) + 1;" in t
       and "int c = b3host::ee_div(x * 2, n);" in t and "int e = s->v[3] / 4;" in t and "float g = x / 2.0f;" in t
       and "b3host::ee_div_assign(a, n + 1);" in t and "b3host::ee_mod(static_cast<unsigned>(a), s->m[n].q)" in t
       and "a < b && b3host::ee_div((c), n) > 2" in t and "int k = f(1) / g(2);" in t and "int z = b3host::ee_div(a, 0);" in t
       and "return b3host::ee_div(u, (n - 1));" in t, t)
CASES += [None]
# gen1 T40: an annotated function-local static is the guest object (guard byte: constructed once in place, as retail);
# without a guard and with a constant initialiser, the guest global; an unannotated one stays a host static
src = ("void f(S *s)\n{\n    static CGtV2d sScale(1.0f, 0.9f);       // D_004B7300 (guard D_004E26C0)\n"
       "    static CGtV2d sLine1(320.0f, 160.0f);   // D_004B7308 (D_004E26C4)\n    static int lnRotate;\n"
       "    static int k = 3;   // D_004E0000\n    g(sScale, sLine1, lnRotate, k);\n}\n")
r_ = []
t = d.local_static_transforms(src, r_)
_check("T40 guest statics", "CGtV2d &sScale = b3host::guest_static<CGtV2d>(0x004B7300u, 0x004E26C0u, [&]() { return CGtV2d(1.0f, 0.9f); });" in t
       and "CGtV2d &sLine1 = b3host::guest_static<CGtV2d>(0x004B7308u, 0x004E26C4u," in t and "    static int lnRotate;" in t
       and "int &k = b3host::guest_global<int>(0x004E0000u);" in t and "// D_004B7300 (guard D_004E26C0)" in t, t)
CASES += [None]
# T39: casts on the divisor and the dividend are part of their operand (`x / (float)n` broke 40 units in a first cut)
src = ("void f(S *s, int n)\n{\n    float a = k / (float)s->n;\n    float b = 0.9f / (float)(int)s->p->q;\n"
       "    unsigned c = (unsigned)n % (unsigned int)s->m;\n    int d = (s->x) / n;\n    float e = x / (UnkStruct_1 *)p - 1;\n}\n")
t = d.ee_division_transforms(src, [])
_check("T39 casts", "float a = b3host::ee_div(k, (float)s->n);" in t and "float b = b3host::ee_div(0.9f, (float)(int)s->p->q);" in t
       and "unsigned c = b3host::ee_mod((unsigned)n, (unsigned int)s->m);" in t and "int d = b3host::ee_div((s->x), n);" in t, t)
CASES += [None]
# gen1 T31 / E1 (b): a store of a local's address into a member of a local array's element, or into an element of a
# local array, is an escape (CGtCollisionKDTree::IntersectLineNearest 0x3BF720: `stack[0].field_0 = &root;`, a gp field)
src = ("struct E { b3host::gp<N> field_0; ps2f field_4; };\nint f(S *s, int i)\n{\n    E stack[64];\n    N root;\n    N other;\n"
       "    b3host::gp<N> ptrs[4];\n    stack[0].field_0 = &root;\n    ptrs[i] = &other;\n    return 0;\n}\n")
h = d.escape_lint(src)
_check("E1 store into an array element", sorted(x[2] for x in h) == ["other", "root"], [(x[1], x[2]) for x in h])
CASES += [None]
# gen1 T37: a volatile typedef in a register macro is hardware access too (func_002F34C0 / RenderBeforeVehicles
# 0x21C780: `typedef volatile unsigned int vu32; #define R32(a) (*(vu32 *)(a))`, the DMAC / toIPU registers)
src = ("typedef volatile unsigned int vu32;\n#define R32(a) (*(vu32 *)(a))\ntypedef unsigned short volatile vu16;\n"
       "void f(int src)\n{\n    R32(0x1000E010) = 0x200;\n    *(vu16 *)0x12000010 = 1;\n    *(volatile int *)0x10003000 = src;\n}\n")
t = d.mmio_transforms(src, [])
_check("T37 volatile typedef", "#define R32(a) (b3host::mmio<unsigned int>(a))" in t and "b3host::mmio<unsigned short>(0x12000010) = 1;" in t
       and "b3host::mmio<int>(0x10003000) = src;" in t and "    R32(0x1000E010) = 0x200;" in t, t)
CASES += [None]
# gen1 T40: an unannotated function-local static is the guest object when retail's symbol table names exactly one
# local static so (`// no symbol form @ 0x004E294C: lnRotate$36523 [medium]`, CB3GraphicsManager::PresentDrawBuffer)
import tempfile
with tempfile.TemporaryDirectory() as _td:
    os.makedirs(os.path.join(_td, "config"))
    open(os.path.join(_td, "config/symbol_addrs_b3names.txt"), "w").write(
        "// no symbol form @ 0x004E294C: lnRotate$36523 [medium]\n// no symbol form @ 0x004C69A0: __FUNCTION__$107 [medium]\n"
        "// no symbol form @ 0x004C6B38: __FUNCTION__$46 [high]\n")

    class _DLS:
        root = _td
    t = d.local_static_transforms("void f()\n{\n    static int lnRotate;\n    static int other;\n    g(lnRotate, other);\n}\n", [], _DLS)
_check("T40 retail's local-static name", "int &lnRotate = b3host::guest_global<int>(0x004E294Cu);" in t and "    static int other;" in t, t)
CASES += [None]
# gen1 T7: a reopened namespace no header declares is declared at global scope first (func_003CD920); operator= gets
# MWCC's __as__ name for its stub
r_, g_ = d.t7_reopen("namespace GtMathPs2 {\nstruct CGtV2d { float a[2]; };\n}\nusing namespace GtMathPs2;\n")
_r2, _g2 = d.t7_reopen("namespace GtMathPs2 {\nint h();\n}\nusing namespace GtMathPs2;\n", lambda n: True)
_check("T7 global namespace / operator= name", "using namespace ::GtMathPs2;" in r_ and g_.startswith("namespace GtMathPs2 {}") and _g2 == ""
       and d.mw_mangle("GtMathPs2::CGtV2d", "operator=", ["GtMathPs2::CGtV2d const&"], False) == "__as__Q29GtMathPs26CGtV2dFRCQ29GtMathPs26CGtV2d",
       (r_, g_, d.mw_mangle("GtMathPs2::CGtV2d", "operator=", ["GtMathPs2::CGtV2d const&"], False)))
CASES += [None]
# gen1: a marked but unselected `static` definition is a helper of this file (FindTarget 0x32FF80's
# `// 0x0032F550` above `static inline bool func_0032F550_inl1(...)`); an unselected non-static marked one is not
src = ("// 0x0032F550\nstatic inline bool func_0032F550_inl1(S *s)\n{\n    return s != 0;\n}\n"
       "// 0x00330000\nvoid Other__1SFv(S *s)\n{\n    g(s);\n}\n"
       "// 0x0032FF80\nvoid FindTarget(S *s)\n{\n    s->f = func_0032F550_inl1(s);\n}\n")
t, kept = d.reduce_unit(src, {0x0032FF80}, [], {0x0032FF80: "FindTarget"})
_check("marked static helper kept", "static inline bool func_0032F550_inl1(S *s)" in t and "Other__1SFv" not in t and len(kept) == 1, t)
CASES += [None]
# gen1: a selected CodeWarrior asm function becomes a definition with the declared signature whose body is
# eeasm.translate_asm_function's (here a stand-in recording its arguments); register images by type; T8 leaves `auto`
import types
_calls = []
_fake = types.ModuleType("eeasm")
_fake.AsmError = Exception
_fake.translate_asm_function = lambda body, params, sym, addr, where="": (_calls.append((params, sym("farFogPlane"), addr)) or "BODY(%d);" % len(params))
_real = sys.modules.get("eeasm")
sys.modules["eeasm"] = _fake


class _DAF:
    symbols = {"farFogPlane": 0x004E1234}
    by_name = {}
    funcs = {}


try:
    src = ("class B3Im3d { public: static void RenderTristrip(const V *, int); void Draw(const M &, unsigned n); };\n"
           "// 0x00365040\nasm void B3Im3d::RenderTristrip(const V*, int)\n{\n    .set noreorder\n    jr $ra\n    nop\n}\n"
           "// 0x00365100\nasm void B3Im3d::Draw(const M& m, unsigned n)\n{\n    jr $ra\n    nop\n}\n")
    t = d.asm_function_transforms(src, [(0x00365040, "m", "RenderTristrip", 4), (0x00365100, "m", "Draw", 4)], _DAF, [])
finally:
    if _real is not None:
        sys.modules["eeasm"] = _real
    else:
        del sys.modules["eeasm"]
_check("asm function translated", "void B3Im3d::RenderTristrip(const V* p0, int p1)\n{" in t and "BODY(2);" in t and "BODY(3);" in t
       and _calls[0][0] == ["b3host::guest_address(p0)", "b3host::sx32(static_cast<std::uint32_t>(p1))".replace("b3host::sx32", "b3asm::sx32")]
       and _calls[1][0] == ["b3host::guest_address(this)", "b3host::guest_address(&m)", "b3asm::sx32(static_cast<std::uint32_t>(n))"]
       and _calls[0][1] == 0x004E1234 and t.startswith("#include <cstring>"), (t, _calls))
t = d.goto_transforms("void f(int k)\n{\n    auto M = [](int x) { return x; };\n    if (k) goto L;\n    int y = M(k);\nL:\n    g(y);\n}\n")
_check("T8 leaves auto", "    auto M = [](int x) { return x; };" in t and "int y __attribute__((uninitialized)); y =" in t, t)
CASES += [None] * 2
# gen1 T41: in a VU-bound function, calls of asm-holding static inline helpers are expanded in place as lambdas,
# arguments first (an argument's own helper call runs before the body), so the asm occurrences follow source order
os.environ["DECOMP2HOST_T41_ALL"] = "1"
try:
    src = ("static inline u_long128 h1(u_long128 a) { u_long128 r; asm(\"vaddx.x %0, $vf0, %1x\" : \"=j\"(r) : \"j\"(a)); return r; }\n"
           "static inline u_long128 h2(u_long128 a, u_long128 b) { return h1(a); }\nstatic inline int plain(int x) { return x; }\n"
           "// 0x00100000\nvoid f(u_long128 *p)\n{\n    p[0] = h2(h1(p[1]), p[2]);\n    p[3] = plain(1);\n}\n")
    t = d.inline_asm_helpers(src, [(0x00100000, "m", "f", 4)], None, [])
finally:
    del os.environ["DECOMP2HOST_T41_ALL"]
_body = t[t.index("void f("):]
_check("T41 asm helpers inlined", _body.count("decomp2host T41: h1 inlined") == 2 and "decomp2host T41: h2 inlined" in _body
       and _body.index("asm(") < _body.index("return [](u_long128 a, u_long128 b)") < _body.rindex("asm(")
       and "p[3] = plain(1);" in _body and t.startswith("static inline u_long128 h1(u_long128 a)"), t)
CASES += [None]
# gen1: `(T *)int + K` with K beyond main RAM (the scratchpad 0x70000000, the uncached mirrors) is guest-address
# arithmetic (b3host::pcast_add): RenderBeforeVehicles 0x21C780's `(char *)s + 0x70002000`; a small offset, a
# dereferenced pcast and a non-literal offset stay as they were
src = ("void f(int s, char *p, int n)\n{\n    int m = (b3host::pcast<S>((b3host::pcast<char>(s) + 0x70002000)))->f;\n"
       "    char *q = b3host::pcast<char>(s) - 0x20000000;\n    char *r = b3host::pcast<char>(s) + 16;\n    int v = *b3host::pcast<int>(s) + 0x70000000;\n"
       "    char *z = b3host::pcast<char>(s) + 4 * n;\n}\n")
t = d.pcast_offset_transforms(src, [])
_check("pcast offset across windows", "b3host::pcast_add<char>(s, 0x70002000)" in t and "b3host::pcast_add<char>(s, -0x20000000)" in t
       and "b3host::pcast<char>(s) + 16;" in t and "*b3host::pcast<int>(s) + 0x70000000;" in t and "b3host::pcast<char>(s) + 4 * n;" in t, t)
CASES += [None]
# gen1 T3: a pointer-array member whose size holds parentheses becomes guest pointers too (CheckCrashingTraffic 0x13E920:
# `T *field_122A10[(0x122B1C - 0x122A10) / 4];` had stayed 8-byte host pointers, the next field at the wrong offset)
t = d.member_transforms("struct S {\n    unsigned char pad_0[0x122A10];\n    B *field_122A10[(0x122B1C - 0x122A10) / 4];\n    int field_122B1C;\n};\n")
_check("T3 pointer array, computed size", "b3host::gp<B> field_122A10[(0x122B1C - 0x122A10) / 4];" in t, t)
# gen1 (boot2's rescan): `extern "C" asm void f(...)` is translated too (keeps its linkage); a destructor stub passes
# MWCC's flag -1; an out-of-line GtMath copy over its own GtMathPs2 view goes to the shadow instance; T7 (c) moves a
# header-declared struct's definition out of the unit namespace; crossed GtMath values (`T v = e;`, `T r(a, b);`) go
# to the function's start; the T7 retry also follows an ambiguity / template-argument parse error
_fake2 = types.ModuleType("eeasm")
_fake2.AsmError = Exception
_fake2.translate_asm_function = lambda body, params, sym, addr, where="": "BODY(%d);" % len(params)
_real2 = sys.modules.get("eeasm"); sys.modules["eeasm"] = _fake2
try:
    t = d.asm_function_transforms('// 0x002FC530\nextern "C" asm void func_002FC530(void*, void*)\n{\n    jr $ra\n    nop\n}\n',
                                  [(0x002FC530, "m", "func_002FC530", 4)], _DAF, [])
finally:
    if _real2 is not None: sys.modules["eeasm"] = _real2
    else: del sys.modules["eeasm"]
_ok1 = 'extern "C" void func_002FC530(void* p0, void* p1)\n{' in t and "BODY(2);" in t
_ok2 = d.destructor_stub("N::CGtLinearMalloc::~CGtLinearMalloc", 0x002F9DD0, "__dt__15CGtLinearMallocFv") == \
    "inline N::CGtLinearMalloc::~CGtLinearMalloc() { b3host::call_guest<void>(0x002F9DD0u, b3host::ptr_arg(this), -1); }   // __dt__15CGtLinearMallocFv"
import tempfile
with tempfile.TemporaryDirectory() as _td:
    os.makedirs(os.path.join(_td, "src")); os.makedirs(os.path.join(_td, "include/b3/classes"))
    open(os.path.join(_td, "src/v.cpp"), "w").write("namespace GtMathPs2 { struct CGtV3d { float m[4]; }; }\n// 0x002ED2C0\nfloat f() { return 0; }\n")
    open(os.path.join(_td, "src/h.cpp"), "w").write('#include "b3/GtMathPs2.h"\n// 0x002ED2C0\nfloat f() { return 0; }\n')

    class _DG:
        def __init__(self, f): self._f = f
        def matched_file(self, a): return self._f
        def nonmatching_file(self, n): return None

    class _A:
        decomp = _td
    _ok3 = d.gtmath_defined(_DG("src/v.cpp"), _A, 0x002ED2C0, "x") is False and d.gtmath_defined(_DG("src/h.cpp"), _A, 0x002ED2C0, "x") is True
    open(os.path.join(_td, "include/b3/classes/H.h"), "w").write("struct UnkStruct_a1;\nstruct H { virtual void v(UnkStruct_a1 *); };\n")
    unit = ('#include "b3/classes/H.h"\nnamespace b3gen_U {\nstruct UnkStruct_a1;\nstruct UnkStruct_a1 {\n    int field_0;\n};\n'
            'void g(UnkStruct_a1 *p) { }\n}  // namespace b3gen_U\nusing namespace b3gen_U;\n')
    t7 = d.t7_header_names(unit, _td)
_ok4 = t7.index("struct UnkStruct_a1 {") < t7.index("namespace b3gen_U {") and t7.count("struct UnkStruct_a1;") == 0
src = ("void f(int n)\n{\n    if (n) goto L;\n    CGtV2d v = g(n);\n    CGtV2d r(1.0f, 2.0f);\nL:\n    h(v, r);\n}\n")
err = "/x/u.cpp:4:12: note:   crosses initialization of ‘GtMathPs2::CGtV2d v’\n/x/u.cpp:5:12: note:   crosses initialization of ‘GtMathPs2::CGtV2d r’\n"
t = d.strict_fix(src, "/y/u.cpp", err)
_ok5 = "    v = g(n);" in t and "    r = CGtV2d(1.0f, 2.0f);" in t and "CGtV2d v __attribute__((uninitialized));" in t.split("if (n)")[0] \
    and "CGtV2d r __attribute__((uninitialized));" in t.split("if (n)")[0]
_check("rescan fixes", _ok1 and _ok2 and _ok3 and _ok4 and _ok5, (_ok1, _ok2, _ok3, _ok4, _ok5, t7, t))
CASES += [None]
# gen1: a helper called on a macro's continued line is kept (func_00424C40's STEP -> pabsw / pextlw0); several
# `extern ...;` on one line are split (B3Im3DPS2_00365040's farFogPlane / D_004E0E50)
src = ("static inline int pabsw(int x)\n{\n    return x;\n}\n"
       "#define STEP(F) \\\n    { \\\n        int step = pabsw(F); \\\n    }\n"
       "// 0x00424C40\nvoid f(int a)\n{\n    STEP(a);\n}\n")
t, kept = d.reduce_unit(src, {0x00424C40}, [], {0x00424C40: "f"})
_okm = "static inline int pabsw(int x)" in t and len(kept) == 1
t2 = d.split_multi_externs('extern "C" unsigned char a, b; extern "C" int farFogPlane; extern "C" float D_004E0E50;\n')
_check("macro-continued helper / several externs on a line", _okm and t2 == 'extern "C" unsigned char a;\nextern "C" unsigned char b;\n'
       'extern "C" int farFogPlane;\nextern "C" float D_004E0E50;\n', (t, t2))
CASES += [None]
# gen1 (boot2's rescan): `extern "C" asm void f(...)` is translated too (keeps its linkage); a destructor stub passes
# MWCC's flag -1; an out-of-line GtMath copy over its own GtMathPs2 view goes to the shadow instance; T7 (c) moves a
# header-declared struct's definition out of the unit namespace; crossed GtMath values (`T v = e;`, `T r(a, b);`) go
# to the function's start; the T7 retry also follows an ambiguity / template-argument parse error
_fake2 = types.ModuleType("eeasm")
_fake2.AsmError = Exception
_fake2.translate_asm_function = lambda body, params, sym, addr, where="": "BODY(%d);" % len(params)
_real2 = sys.modules.get("eeasm"); sys.modules["eeasm"] = _fake2
try:
    t = d.asm_function_transforms('// 0x002FC530\nextern "C" asm void func_002FC530(void*, void*)\n{\n    jr $ra\n    nop\n}\n',
                                  [(0x002FC530, "m", "func_002FC530", 4)], _DAF, [])
finally:
    if _real2 is not None: sys.modules["eeasm"] = _real2
    else: del sys.modules["eeasm"]
_ok1 = 'extern "C" void func_002FC530(void* p0, void* p1)\n{' in t and "BODY(2);" in t
_ok2 = d.destructor_stub("N::CGtLinearMalloc::~CGtLinearMalloc", 0x002F9DD0, "__dt__15CGtLinearMallocFv") == \
    "inline N::CGtLinearMalloc::~CGtLinearMalloc() { b3host::call_guest<void>(0x002F9DD0u, b3host::ptr_arg(this), -1); }   // __dt__15CGtLinearMallocFv"
import tempfile
with tempfile.TemporaryDirectory() as _td:
    os.makedirs(os.path.join(_td, "src")); os.makedirs(os.path.join(_td, "include/b3/classes"))
    open(os.path.join(_td, "src/v.cpp"), "w").write("namespace GtMathPs2 { struct CGtV3d { float m[4]; }; }\n// 0x002ED2C0\nfloat f() { return 0; }\n")
    open(os.path.join(_td, "src/h.cpp"), "w").write('#include "b3/GtMathPs2.h"\n// 0x002ED2C0\nfloat f() { return 0; }\n')

    class _DG:
        def __init__(self, f): self._f = f
        def matched_file(self, a): return self._f
        def nonmatching_file(self, n): return None

    class _A:
        decomp = _td
    _ok3 = d.gtmath_defined(_DG("src/v.cpp"), _A, 0x002ED2C0, "x") is False and d.gtmath_defined(_DG("src/h.cpp"), _A, 0x002ED2C0, "x") is True
    open(os.path.join(_td, "include/b3/classes/H.h"), "w").write("struct UnkStruct_a1;\nstruct H { virtual void v(UnkStruct_a1 *); };\n")
    unit = ('#include "b3/classes/H.h"\nnamespace b3gen_U {\nstruct UnkStruct_a1;\nstruct UnkStruct_a1 {\n    int field_0;\n};\n'
            'void g(UnkStruct_a1 *p) { }\n}  // namespace b3gen_U\nusing namespace b3gen_U;\n')
    t7 = d.t7_header_names(unit, _td)
_ok4 = t7.index("struct UnkStruct_a1 {") < t7.index("namespace b3gen_U {") and t7.count("struct UnkStruct_a1;") == 0
src = ("void f(int n)\n{\n    if (n) goto L;\n    CGtV2d v = g(n);\n    CGtV2d r(1.0f, 2.0f);\nL:\n    h(v, r);\n}\n")
err = "/x/u.cpp:4:12: note:   crosses initialization of ‘GtMathPs2::CGtV2d v’\n/x/u.cpp:5:12: note:   crosses initialization of ‘GtMathPs2::CGtV2d r’\n"
t = d.strict_fix(src, "/y/u.cpp", err)
_ok5 = "    v = g(n);" in t and "    r = CGtV2d(1.0f, 2.0f);" in t and "CGtV2d v __attribute__((uninitialized));" in t.split("if (n)")[0] \
    and "CGtV2d r __attribute__((uninitialized));" in t.split("if (n)")[0]
_check("rescan fixes", _ok1 and _ok2 and _ok3 and _ok4 and _ok5, (_ok1, _ok2, _ok3, _ok4, _ok5, t7, t))
CASES += [None]
# gen1: a helper called on a macro's continued line is kept (func_00424C40's STEP -> pabsw / pextlw0); several
# `extern ...;` on one line are split (B3Im3DPS2_00365040's farFogPlane / D_004E0E50)
src = ("static inline int pabsw(int x)\n{\n    return x;\n}\n"
       "#define STEP(F) \\\n    { \\\n        int step = pabsw(F); \\\n    }\n"
       "// 0x00424C40\nvoid f(int a)\n{\n    STEP(a);\n}\n")
t, kept = d.reduce_unit(src, {0x00424C40}, [], {0x00424C40: "f"})
_okm = "static inline int pabsw(int x)" in t and len(kept) == 1
t2 = d.split_multi_externs('extern "C" unsigned char a, b; extern "C" int farFogPlane; extern "C" float D_004E0E50;\n')
_check("macro-continued helper / several externs on a line", _okm and t2 == 'extern "C" unsigned char a;\nextern "C" unsigned char b;\n'
       'extern "C" int farFogPlane;\nextern "C" float D_004E0E50;\n', (t, t2))
CASES += [None]
# gen1: MWCC's constant folds (config/fold_sites.tsv): an entry replaces its text once, with retail's constant; not
# found or found twice is fatal; each committed entry's text occurs exactly once in its function's decomp source
_site = [(0x001D1130, 0x001D1388, 0x3F7851EC, "inl7(0.03f, 1.0f, x)", "(ps2f(0.03f) + x * ps2f(b3fp::flt(0x3F7851ECu)))")]
t = d.fold_site_transforms("void f() {\n    y = inl7(0.03f, 1.0f, x);\n}\n", {0x001D1130}, "u", [], _site)
_okf = "y = (ps2f(0.03f) + x * ps2f(b3fp::flt(0x3F7851ECu))) /* gen1 [C] MWCC fold: retail 0x3F7851EC at 0x001D1388 */;" in t
_okn = d.fold_site_transforms("y = inl7(0.03f, 1.0f, x);", {0x00100000}, "u", [], _site) == "y = inl7(0.03f, 1.0f, x);"
_raise = d.RAISE
d.RAISE = True
_bad = 0
for _txt in ("y = 1;", "inl7(0.03f, 1.0f, x); inl7(0.03f, 1.0f, x);"):
    try:
        d.fold_site_transforms(_txt, {0x001D1130}, "u", [], _site)
    except Exception:
        _bad += 1
d.RAISE = _raise
_okt = True
_snap = os.environ.get("B3_DECOMP_SNAP_GEN1L") or os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(subprocess.run(["git", "-C", os.path.dirname(os.path.abspath(__file__)), "rev-parse", "--path-format=absolute", "--git-common-dir"], capture_output=True, text=True).stdout.strip()))), "decomp-snap", "gen1l")
if os.path.isdir(os.path.join(_snap, "src")):
    for _a, _pc, _c, _find, _rep in d.fold_sites(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../config/fold_sites.tsv")):
        _hits = [os.path.join(r_, f_) for r_, _, fs_ in os.walk(os.path.join(_snap, "src")) for f_ in fs_
                 if f_.endswith(".cpp") and ("// 0x%08X" % _a) in open(os.path.join(r_, f_), errors="replace").read()]
        _okt = _okt and len(_hits) >= 1 and sum(open(h_, errors="replace").read().count(_find) for h_ in _hits) == 1
_check("fold sites", _okf and _okn and _bad == 2 and _okt, (t, _okn, _bad, _okt))
CASES += [None]
# gen1 T12: a static the unit does not declare takes the header's declaration over another unit's private class view;
# and another class's static of the same name never rewrites `Cls::name` (CB3PopcornRenderer::mnNumRenderableArrays)
class _DS:
    symbols = {"mgpBuf__6CB3Cam": 0x004E2D30, "mnN__5CB3Sp": 0x004E2E00, "mnN__5CB3Po": 0x004E2E3C}
d.STATIC_MEMBERS.clear()
d.static_member_transforms("class CB3Cam { static UnkPriv *mgpBuf; };", _DS, [], apply_header_statics=True)   # a unit's view
d.static_member_transforms("class CB3Cam { static int mgpBuf; };\nclass CB3Po { static int mnN; };", _DS, [])  # the header
d.static_member_transforms("class CB3Sp { static int mnN; };", _DS, [], apply_header_statics=True)            # another unit
t = d.static_member_transforms("void f() { int a = CB3Cam::mgpBuf; int b = CB3Po::mnN; }", _DS, [], apply_header_statics=True)
d.STATIC_MEMBERS.clear()
_check("T12 header statics first, qualified names", "(b3host::guest_global<int>(0x004E2D30u))" in t and "UnkPriv" not in t
       and "int b = (b3host::guest_global<int>(0x004E2E3Cu))" in t and "CB3Po::(" not in t, t)
CASES += [None]
# gen1 (mac1): the check compiles' GCC-only flag follows the compiler (clang: -Wno-address-of-temporary)
import tempfile as _tf
with _tf.TemporaryDirectory() as _td:
    _fc = os.path.join(_td, "fakeclang")
    open(_fc, "w").write("#!/bin/sh\necho 'Ubuntu clang version 18.1.3'\n")
    os.chmod(_fc, 0o755)
    _old = os.environ.get("DECOMP2HOST_CXX")
    os.environ["DECOMP2HOST_CXX"] = _fc
    _okc = d._host_cxx_is_clang()
    # macintel1: a fake GCC too, as the clang above: on macOS `g++` is Apple clang ("Apple clang version 17...")
    _fg = os.path.join(_td, "fakegcc")
    open(_fg, "w").write("#!/bin/sh\necho 'g++ (Ubuntu 13.3.0-6ubuntu2~24.04) 13.3.0'\n")
    os.chmod(_fg, 0o755)
    os.environ["DECOMP2HOST_CXX"] = _fg
    _okg = not d._host_cxx_is_clang()
    if _old is None: del os.environ["DECOMP2HOST_CXX"]
    else: os.environ["DECOMP2HOST_CXX"] = _old
_check("clang check-compile flags", _okc and _okg, (_okc, _okg))
CASES += [None]
# gen1 (mac1, clang): an #include after a class the unit defines and whose members it defines below moves up before
# the class, so T7's namespace (after the last include) encloses both; without such a member the text stays
src = ("// two drafts\nclass CB3A {\npublic:\n    void F();\n};\n#include \"b3/GtMathPs2.h\"\n"
       "void CB3A::F()\n{\n}\n")
t = d.hoist_late_includes(src, [])
src2 = "class S {\n    int a;\n};\n#include \"b3/GtMathPs2.h\"\nvoid g()\n{\n}\n"
_check("late includes hoisted", t.index('#include "b3/GtMathPs2.h"') < t.index("class CB3A {") and t.count("#include") == 1
       and d.hoist_late_includes(src2, []) == src2, t)
CASES += [None]
# mac5 T42: a derived class's members start at sizeof(Base) as under MWCC, not in a non-POD base's tail padding
# (CB3RaceCarPhysics : CB3CollidingBody, 0x208 data / 0x210 size): compiled with the host compiler, offsets printed
import subprocess as _sp
src = ("struct alignas(16) V { float x[4]; V() {} };\n"
       "struct Base { unsigned char pad_0[0x10]; V v; int p; Base() {} };\n"          # data 0x24, 16-aligned: sizeof 0x30
       "struct Der : public Base {\n    void f();\n    unsigned char pad_30[4];\n    int field_34;\n};\n")
t = d.derived_layout_transforms(src, [])
prog = ("#include <cstdio>\n#include <cstddef>\n%s\nint main() { Der d; std::printf(\"%%d %%d\\n\", int(sizeof(Base)), "
        "int(reinterpret_cast<char*>(&d.field_34) - reinterpret_cast<char*>(&d))); }\n")
_res = []
with _tf.TemporaryDirectory() as _td:
    for _name, _text in (("plain", src), ("t42", t)):
        _c, _x = os.path.join(_td, _name + ".cpp"), os.path.join(_td, _name)
        open(_c, "w").write(prog % _text)
        _r = _sp.run([os.environ.get("CXX", "g++"), "-std=c++20", "-w", _c, "-o", _x], capture_output=True, text=True)
        _res.append(_sp.run([_x], capture_output=True, text=True).stdout.split() if _r.returncode == 0 else ["build", _r.stderr[:200]])
# without T42 the host reuses the tail padding (field_34 at 0x28 = 40); with it, at sizeof(Base) + 4 = 0x34 = 52
_check("T42 derived layout at sizeof(Base)", _res[0] == ["48", "40"] and _res[1] == ["48", "52"]
       and "b3_t42_base_end[0]" in t and d.derived_layout_transforms("struct S { int a; };\n", []) == "struct S { int a; };\n", _res)
CASES += [None]
# mac5 (the Windows garage static): T1 / T2 reach a #define's body (func_0042A4E0's CARID read a 32-bit long on
# Windows); the directive and macro head stay as written, an #include line too
_t = d.token_transforms('#include "long.h"\n#define CARID(i) (*(unsigned long *)(p + (i)))\n#define K (1UL << 32)\n'
                        '#define F(o) (*(float *)(o))\nlong x;\n')
_check("T1/T2 in a #define body",
       '#define CARID(i) (*(b3host::ps2_ulong *)(p + (i)))' in _t and '#define K (1ULL << 32)' in _t and
       '#define F(o) (*(ps2f *)(o))' in _t and '#include "long.h"' in _t and 'b3host::ps2_long x;' in _t, _t)
CASES += [None]
# 1.2.0 T46: UpdateSuspension's laImpulse[4] -> [6] inside the marked function only; a changed declaration stops
_src = ("void g() {\n    CGtV3d laImpulse[4];\n}\n// 0x001CC900\nvoid CB3VehiclePhysics::UpdateSuspension(ps2f t)\n{\n"
        "    CGtV3d laImpulse[4];\n    laImpulse[0] = laImpulse[1];\n}\n")
_t, _n = d.t46_local_arrays(_src)
_stopped = False
try:
    d.t46_local_arrays("// 0x001CC900\nvoid f()\n{\n    CGtV3d laImpulse[5];\n}\n")
except BaseException:
    _stopped = True
_check("T46 local array sized as retail's frame",
       _n == 1 and _t.count("CGtV3d laImpulse[4];") == 1 and _t.index("CGtV3d laImpulse[4];") < _t.index("0x001CC900")
       and "CGtV3d laImpulse[6];   /* decomp2host T46" in _t and d.t46_local_arrays("void f() {}\n") == ("void f() {}\n", 0)
       and _stopped, (_n, _t))
_t2, _n2 = d.t46_local_arrays("// 0x002B7DB0\nps2f f()\n{\n    ps2f d[20];\n}\n")
_check("T46 func_002B7DB0 d[44] (ps2f)", _n2 == 1 and "ps2f d[44];   /* decomp2host T46" in _t2, _t2)
# 1.2.1 T47: the RECORDS car icons (SetRecordsContentPanelContents) at retail's y offset and anchor, inside the marked
# function only; a draft that no longer has the statement stops
_body = ("{\n    ps2f base = 168.0f;\n    if (names2 == 0) {\n        base = base + 100.0f / 2.0f;\n    }\n"
         "    func_001D6C40(o, CGtV2d(cols[2], 0.0f + base + (ps2f)(i - 1) * 32.0f), CGtV2d(0.0f, 0.0f), mgr->field_10, f, t, 20.0f);\n"
         "    Prepare(o, CGtV2d(cols[2] - 10.0f, 0.0f + base + (ps2f)(i - 1) * 32.0f), CGtV2d(0.0f, 0.0f), D_004C9E88.v, col, tex, "
         "CGtV2d(0.0f, 0.0f), CGtV2d(1.0f, 1.0f), v);\n}\n")
_src = "void g()\n" + _body + "// 0x003A4A80\nextern \"C\" void func_003A4A80(int n)\n" + _body
_t, _n = d.t47_draft_fixes(_src)
_stopped = False
try:
    d.t47_draft_fixes("// 0x003A4A80\nvoid f()\n{\n    base = base + 16.0f;\n}\n")
except BaseException:
    _stopped = True
_mine = _t[_t.index("0x003A4A80"):]
_check("T47 the RECORDS car icons at retail's offset and anchor",
       _n == 2 and _t.count("base = base + 100.0f / 2.0f;") == 1 and _t.index("100.0f / 2.0f") < _t.index("0x003A4A80")
       and "base = base + D_004C9E88.v.marValues[1] / 2.0f;" in _mine
       and "32.0f), CGtV2d(0.0f, 0.5f) /* decomp2host T47" in _mine and "D_004C9E88.v, col, tex, CGtV2d(0.0f, 0.0f), CGtV2d(1.0f, 1.0f)" in _mine
       and "mgr->field_10" in _mine and "32.0f), CGtV2d(0.0f, 0.0f), mgr->field_10" in _mine
       and d.t47_draft_fixes("void f() {}\n") == ("void f() {}\n", 0) and _stopped, (_n, _t))
# genret T47: ForAllGames returns slot 0x14's v0 (the unit's declaration before the marker made int, the call returned);
# a reward switch's fall-off path gets a defined value and the one-time note
_src = ("class CGtLobbyPS2DirtySock {\npublic:\n    b3host::gaddr b3_vptr; void vfunc_10();\n    void vfunc_14();\n};\n"
        "// 0x00339830\nint CGtLobbyPS2DirtySock::ForAllGames(int p0)\n{\n    for (;;) {\n        if (p0)\n            return 2;\n"
        "    }\n    vfunc_14();\n}\n")
_t, _n = d.t47_draft_fixes(_src)
_check("T47 ForAllGames returns vfunc_14 (GetLobbyStatus)",
       _n == 2 and "    int vfunc_14();   /* decomp2host T47" in _t and "\n    return vfunc_14();   /* decomp2host T47" in _t
       and "void vfunc_10();" in _t and _t.index("int vfunc_14") < _t.index("// 0x00339830"), _t)
# genret T47: the declaration fix is scoped to the class (another placeholder class's `void vfunc_14();` stays) and a unit
# holding both lobby functions fixes the declaration once
_src = ("struct Other {\n    b3host::gaddr b3_vptr; void vfunc_14();\n};\n"
        "class CGtLobbyPS2DirtySock {\npublic:\n    b3host::gaddr b3_vptr; void vfunc_10();\n    void vfunc_14();\n};\n"
        "// 0x00339480\nint CGtLobbyPS2DirtySock::ForAllGamesFound(int p0)\n{\n    if (p0)\n        return 1;\n    vfunc_14();\n}\n"
        "// 0x00339830\nint CGtLobbyPS2DirtySock::ForAllGames(int p0)\n{\n    if (p0)\n        return 2;\n    vfunc_14();\n}\n")
_t, _n = d.t47_draft_fixes(_src)
_check("T47 vfunc_14's declaration: scoped to its class, fixed once for both functions",
       _n == 3 and _t.count("int vfunc_14();") == 1 and "struct Other {\n    b3host::gaddr b3_vptr; void vfunc_14();" in _t
       and _t.count("return vfunc_14();") == 2, _t)
# genret T47: a marked function dropped from the unit (only its marker comment left, T5) is skipped, not fixed in the
# next function
_src = ("// 0x003F64D0\n// [C] cases 0-3\n/* decomp2host T5: helper dropped */\n\n// 0x003F6590\nvoid C::Construct()\n{\n"
        "    return func_00271A00(&D_0051BA44, r0);\n    }\n}\n")
_check("T47 a dropped marked function is skipped", d.t47_draft_fixes(_src) == (_src, 0), d.t47_draft_fixes(_src))
# genret T47: every replacement carries `decomp2host T47` (what t47_check_applied looks for); an entry whose function
# was generated but no unit carries its fix stops the generation; an entry outside the selection is not looked for
_marked = all("decomp2host T47" in fx[1] for fxs in d.T47_DRAFT_FIXES.values() for fx in fxs)
import tempfile
with tempfile.TemporaryDirectory() as _td:
    with open(os.path.join(_td, "a.cpp"), "w") as _f:
        _f.write("// 0x003A4A80\nvoid f()\n{\n    x = 1;   /* decomp2host T47: y */\n}\n")
    with open(os.path.join(_td, "b.cpp"), "w") as _f:
        _f.write("// 0x00339830\nint g()\n{\n    vfunc_14();\n}\n")
    _ok_sel = _ok_out = True
    try:
        d.t47_check_applied(_td, ["a", "b"], {0x003A4A80, 0x00100000})
    except BaseException:
        _ok_sel = False
    _stopped = False
    try:
        d.t47_check_applied(_td, ["a", "b"], {0x003A4A80, 0x00339830})
    except BaseException:
        _stopped = True
_check("T47 an entry applied in no unit stops the generation", _marked and _ok_sel and _stopped,
       (_marked, _ok_sel, _stopped))
_src = ("// 0x003F66F0\nstatic inline b3host::ps2_ulong h(S * self)\n{\n    switch (self->field_10) {\n    case 0:\n"
        "        return 0;\n    case 1:\n        return 0x8000000001ULL;\n    }\n}\n")
_t, _n = d.t47_draft_fixes(_src)
_check("T47 func_003F66F0's helper: a defined return on the fall-off path, noted",
       _n == 1 and _t.endswith("    }\n    b3host::draft_fall_off(\"0x003F66F0 status: field_10 neither 0 nor 1\"); "
                                "/* decomp2host T47: retail leaves the caller's v0 here (b .L003F6754, 0x3F6718) */ return 0;\n}\n"), _t)

print("%d cases, %d failures" % (len(CASES), fails))
sys.exit(1 if fails else 0)
