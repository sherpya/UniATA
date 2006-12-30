
#ifdef CROSSNT_DECL
#undef CROSSNT_DECL
#endif //CROSSNT_DECL

/***************************/
#ifdef CROSSNT_DECL_API

#define CROSSNT_DECL(type, name, args, callargs) \
extern "C" \
type \
CrNt##name \
    args; \
extern "C" type CrNt##name##_impl args;

#endif //CROSSNT_DECL_API

/***************************/
#ifdef CROSSNT_DECL_STUB

/*
#define CROSSNT_DECL(type, name, args, callargs) \
typedef type (*ptr##name) args;        \
ptr##name g_ptr##name = NULL;          \
extern "C" \
type                                   \
CrNt##name                             \
    args                               \
{                                      \
    if(!g_ptr##name) {                 \
        g_ptr##name = (ptr##name)CrNtGetProcAddress(g_hNtosKrnl, #name); \
        if(!g_ptr##name) {             \
            g_ptr##name = (ptr##name)(-1); \
        }                              \
    }                                  \
    if(g_ptr##name != (ptr##name)(-1)) { \
        return g_ptr##name callargs;       \
    }                                  \
    return CrNt##name##_impl callargs;     \
}
*/

#define CROSSNT_DECL(type, name, args, callargs) \
typedef type (*ptr##name) args;        \
ptr##name g_ptr##name = NULL;          \
extern "C" \
type                                   \
CrNt##name                             \
    args                               \
{                                      \
    return g_ptr##name callargs;       \
}

#endif //CROSSNT_DECL_STUB

/***************************/
#ifdef CROSSNT_INIT_STUB

#define CROSSNT_DECL(type, name, args, callargs) \
    if(!g_ptr##name) {                 \
        g_ptr##name = (ptr##name)CrNtGetProcAddress(g_hNtosKrnl, #name); \
        if(!g_ptr##name) {             \
            g_ptr##name = CrNt##name##_impl; \
        }                              \
    }

#endif //CROSSNT_INIT_STUB
