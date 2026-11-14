/* Windows x64 native C ABI smoke test for WowType core.
 * This loads the real DLL at runtime rather than relying on an import .lib.
 * It is NOT a Windows input method (TSF).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "wowtype_core.h"

#ifdef _WIN32
#include <windows.h>
typedef HMODULE Library;
static Library open_library(const char *path) { return LoadLibraryA(path); }
static void *resolve(Library library, const char *name) { return (void *)GetProcAddress(library, name); }
static void close_library(Library library) { FreeLibrary(library); }
#else
#include <dlfcn.h>
typedef void *Library;
static Library open_library(const char *path) { return dlopen(path, RTLD_NOW); }
static void *resolve(Library library, const char *name) { return dlsym(library, name); }
static void close_library(Library library) { dlclose(library); }
#endif

#define NIHAO "\xE4\xBD\xA0\xE5\xA5\xBD"
#define ZHONGGUO "\xE4\xB8\xAD\xE5\x9B\xBD"

typedef int (*AbiVersion)(void);
typedef const char *(*LastError)(void);
typedef WowtypeEngineHandle *(*EngineNew)(const char *, const char *);
typedef void (*EngineFree)(WowtypeEngineHandle *);
typedef WowtypeCandidates *(*EngineInput)(WowtypeEngineHandle *, const char *);
typedef size_t (*CandidatesLen)(const WowtypeCandidates *);
typedef const char *(*CandidatesText)(const WowtypeCandidates *, size_t);
typedef void (*CandidatesFree)(WowtypeCandidates *);
typedef WowtypeSession *(*SessionNew)(WowtypeEngineHandle *, const WowtypeSessionConfig *);
typedef void (*SessionFree)(WowtypeSession *);
typedef const WowtypeFrame *(*SessionKey)(WowtypeSession *, const WowtypeKey *);
typedef const char *(*SessionCandidate)(WowtypeSession *, size_t);
typedef int (*SessionView)(WowtypeSession *, WowtypeSessionView *);
typedef size_t (*CandidateTotal)(WowtypeSession *);
typedef const WowtypeFrame *(*SessionReset)(WowtypeSession *, int);
typedef const WowtypeFrame *(*SessionSelect)(WowtypeSession *, size_t);

#define LOAD(name, typedef_name) \
    typedef_name name = (typedef_name)resolve(lib, #name); \
    if (!name) { fprintf(stderr, "missing export: %s\n", #name); close_library(lib); return 1; }

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: wowtype-abi-smoke <wowtype_core.dll> <lexicon.tsv> [words.lm]\n");
        return 2;
    }
    Library lib = open_library(argv[1]);
    if (!lib) {
        fputs("Unable to load native core library.\n", stderr);
        return 1;
    }
    LOAD(wowtype_abi_version, AbiVersion);
    LOAD(wowtype_last_error, LastError);
    LOAD(wowtype_engine_new, EngineNew);
    LOAD(wowtype_engine_free, EngineFree);
    LOAD(wowtype_engine_input, EngineInput);
    LOAD(wowtype_candidates_len, CandidatesLen);
    LOAD(wowtype_candidates_text, CandidatesText);
    LOAD(wowtype_candidates_free, CandidatesFree);
    LOAD(wowtype_session_new, SessionNew);
    LOAD(wowtype_session_free, SessionFree);
    LOAD(wowtype_session_key, SessionKey);
    LOAD(wowtype_session_candidate, SessionCandidate);
    LOAD(wowtype_session_select, SessionSelect);
    LOAD(wowtype_session_view, SessionView);
    LOAD(wowtype_session_candidate_total, CandidateTotal);
    LOAD(wowtype_session_reset, SessionReset);

    int result = 1;
    WowtypeEngineHandle *engine = NULL;
    WowtypeCandidates *candidates = NULL;
    WowtypeSession *session = NULL;
    const WowtypeFrame *frame = NULL;
    const char *inputs[] = {"nihao", "zhongguo"};
    const char *expected[] = {NIHAO, ZHONGGUO};
    if (wowtype_abi_version() / 1000 != WOWTYPE_ABI_VERSION_MAJOR) {
        fputs("C header and DLL ABI major versions differ.\n", stderr);
        goto finish;
    }
    engine = wowtype_engine_new(argv[2], argc == 4 ? argv[3] : NULL);
    if (!engine) {
        fprintf(stderr, "Engine initialization failed: %s\n", wowtype_last_error());
        goto finish;
    }
    for (size_t i = 0; i < 2; ++i) {
        int found = 0;
        candidates = wowtype_engine_input(engine, inputs[i]);
        if (!candidates) {
            fprintf(stderr, "Engine input failed: %s\n", wowtype_last_error());
            goto finish;
        }
        for (size_t j = 0; j < wowtype_candidates_len(candidates); ++j) {
            const char *s = wowtype_candidates_text(candidates, j);
            if (s && strcmp(s, expected[i]) == 0) found = 1;
        }
        wowtype_candidates_free(candidates);
        candidates = NULL;
        if (!found) {
            fprintf(stderr, "Expected UTF-8 candidate not found for %s.\n", inputs[i]);
            goto finish;
        }
        printf("PASS: %s conversion\n", inputs[i]);
    }
    session = wowtype_session_new(engine, NULL);
    if (!session) {
        fputs("Session initialization failed.\n", stderr);
        goto finish;
    }
    for (const char *p = "nihao"; *p; ++p) {
        WowtypeKey key = {0};
        key.size = (uint32_t)sizeof(key);
        key.keysym = (uint32_t)(unsigned char)*p;
        frame = wowtype_session_key(session, &key);
        if (!frame || frame->size < offsetof(WowtypeFrame, cand_count) + sizeof(size_t)
                   || !frame->handled) {
            fputs("Invalid session key response / frame ABI.\n", stderr);
            goto finish;
        }
    }
    if (!frame || !frame->preedit || !*frame->preedit || !frame->cand_count) {
        fputs("Composition preedit or candidates missing.\n", stderr);
        goto finish;
    }
    size_t chosen = frame->cand_count;
    for (size_t i = 0; i < frame->cand_count; ++i) {
        const char *candidate = wowtype_session_candidate(session, i);
        if (candidate && !strcmp(candidate, NIHAO)) {
            chosen = i;
            break;
        }
    }
    if (chosen >= frame->cand_count) {
        fputs("Composition candidate nihao missing.\n", stderr);
        goto finish;
    }
    frame = wowtype_session_select(session, chosen);
    if (!frame || !frame->commit || strcmp(frame->commit, NIHAO) != 0) {
        fputs("Composition did not commit the expected Unicode text.\n", stderr);
        goto finish;
    }
    puts("PASS: session composition and commit");
    // Full lexicon has many candidates for "shi"; the demo fixture does not.
    // Drive the real Rust page state, not a UI-side 9-row truncation.
    if (argc == 4) {
        wowtype_session_reset(session, 0);
        for (const char *p = "shi"; *p; ++p) {
            WowtypeKey typed = {0};
            typed.size = (uint32_t)sizeof(typed);
            typed.keysym = (unsigned char)*p;
            frame = wowtype_session_key(session, &typed);
            if (!frame || !frame->handled) {
                fputs("Could not enter paging test input.\n", stderr);
                goto finish;
            }
        }
        if (wowtype_session_candidate_total(session) <= 9 || !frame->has_next) {
            fputs("Full lexicon did not provide more than one candidate page.\n", stderr);
            goto finish;
        }
        WowtypeSessionView initial = {0};
        initial.size = (uint32_t)sizeof(initial);
        if (wowtype_session_view(session, &initial) != 0 || initial.page != 0) {
            fputs("Expected first page for paging smoke.\n", stderr);
            goto finish;
        }
        WowtypeKey next = {0};
        next.size = (uint32_t)sizeof(next);
        next.keysym = 0xff56; // XKB PageDown
        frame = wowtype_session_key(session, &next);
        WowtypeSessionView paged = {0};
        paged.size = (uint32_t)sizeof(paged);
        if (!frame || !frame->handled || !frame->has_prev ||
            wowtype_session_view(session, &paged) != 0 || paged.page != 1) {
            fputs("PageDown did not advance Rust session page.\n", stderr);
            goto finish;
        }
        WowtypeKey pick = {0};
        pick.size = (uint32_t)sizeof(pick);
        pick.keysym = '1';
        frame = wowtype_session_key(session, &pick);
        if (!frame || !frame->handled || !frame->commit || !*frame->commit) {
            fputs("Selecting a candidate on the second page failed.\n", stderr);
            goto finish;
        }
        puts("PASS: 9-per-page navigation, second-page candidate selection");
    }
    result = 0;

finish:
    if (candidates) wowtype_candidates_free(candidates);
    if (session) wowtype_session_free(session);
    if (engine) wowtype_engine_free(engine);
    close_library(lib);
    if (!result) puts("WOWTYPE WINDOWS CORE ABI SMOKE OK");
    return result;
}
