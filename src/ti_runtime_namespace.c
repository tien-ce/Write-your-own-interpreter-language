#include "include/ti_runtime_namespace.h"
#include "include/ti_runtime.h"
#include "include/ti_runtime_context.h"
#include "include/ti_runtime_visitor.h"
#include "include/ti_build_program.h"
#include "include/debug.h"
#include "TienInterpreter.h"
#include <string.h>

/* -------------------- Static Storage -------------------- */

/* Host callback that provides script module sources (NULL: the host offers no script modules) */
static ti_module_loader_t s_module_loader = NULL;

/* -------------------- Loader Registration -------------------- */

/* Register the callback that loads script modules */
void ti_register_module_loader(ti_module_loader_t loader)
{
    s_module_loader = loader;
}

/* -------------------- Namespace Functions -------------------- */

/* Initialize a caller-provided namespace: globals scope on the runtime alloc list, empty function table */
ti_status_t ti_namespace_init(ti_runtime_t *rt, ti_namespace_t *ns)
{
    /* Fields are set even on failure, so the struct is always in a known state */
    ns->functions = NULL;
    ns->function_count = 0;
    ns->prog = NULL;
    ns->globals = context_init_global(&rt->alloc_list);

    return (ns->globals != NULL) ? TI_OK : TI_ERR_NO_MEMORY;
}

/* Free what a namespace owns (variables, globals scope, function table); AST parts are borrowed and stay */
void ti_namespace_free(ti_runtime_t *rt, ti_namespace_t *ns)
{
    if (ns == NULL) {
        return;
    }

    /* Step 1: variables own heap values outside the alloc list (handles get destroyed), then the scope itself */
    context_free(&rt->alloc_list, ns->globals);

    /* Step 2: each function owns only its parameter array; function_free keeps name, body and param names */
    for (int i = 0; i < ns->function_count; i++) {
        function_free(&rt->alloc_list, &ns->functions[i]);
    }

    /* Step 3: the entries live in one shared array, freed once here (never per entry) */
    tracked_free(&rt->alloc_list, ns->functions);

    /* Step 4: leave the struct empty; its storage belongs to whoever allocated it */
    ns->globals = NULL;
    ns->functions = NULL;
    ns->function_count = 0;
}

/* Run the top-level statements of an AST inside a namespace, then restore the running namespace */
bool ti_namespace_run(ti_runtime_t *rt, ti_namespace_t *ns, ast_t *root)
{
    /* Step 1: make ns the running namespace, so top-level function definitions are stored in it */
    ti_namespace_t *saved_ns = rt->ns;
    rt->ns = ns;

    /* Step 2: walk the AST with the namespace globals as scope, so top-level variables are stored in it */
    visitor_visit(rt, ns->globals, root);

    /* Step 3: validate top-level control flow (only if nothing failed earlier) */
    if (rt->status == TI_RT_OK) {
        if (ns->globals->flow_state == FLOW_BREAK) {
            ti_raise(rt, TI_ERR_RUNTIME, 0, "'break' statement not within a loop");
        } else if (ns->globals->flow_state == FLOW_CONTINUE) {
            ti_raise(rt, TI_ERR_RUNTIME, 0, "'continue' statement not within a loop");
        } else if (ns->globals->flow_state == FLOW_RETURN) {
            /* A top-level return only ends the run: consume it so later runs start clean */
            ns->globals->flow_state = FLOW_NORMAL;
        }
    }

    /* Step 4: back to the namespace that was running before (the caller's) */
    rt->ns = saved_ns;

    /* Ran to the end only if nothing was raised and nobody cancelled the runtime */
    return rt->status == TI_RT_OK && !ti_runtime_is_interrupted(rt);
}

/* Find a FUNC_TI function defined at the top level of a namespace */
function_t *ti_namespace_find_function(ti_namespace_t *ns, const char *name)
{
    /* Few functions per namespace: a linear scan needs no extra memory */
    for (int i = 0; i < ns->function_count; i++) {
        if (strcmp(ns->functions[i].name, name) == 0) {
            return &ns->functions[i];
        }
    }
    return NULL;
}

/* Find a variable defined at the top level of a namespace */
variable_t *ti_namespace_find_variable(ti_namespace_t *ns, const char *name)
{
    /* The globals scope has no parent, so the lookup never leaves this namespace */
    return context_find_variable(ns->globals, name);
}

/* Ask the host's loader for the source and the path of a script module */
bool ti_namespace_read_source(ti_runtime_t *rt, const char *name, int line, char **text_out, char **path_out)
{
    char *text = NULL;
    char *path = NULL;

    /* Step 1: without a loader the host offers no script modules */
    if (s_module_loader == NULL) {
        ti_raise(rt, TI_ERR_UNDEFINED, line, "Unknown module '%s'", name);
        return false;
    }

    /* Step 2: ask the host, passing the path of the file that runs this import (the running namespace's
     * program: the main script or a module); what the name means is the host's business */
    const char *importer_path = (rt->ns->prog != NULL) ? rt->ns->prog->path : NULL;
    ti_status_t status = s_module_loader(importer_path, name, &text, &path);
    if (status != TI_OK) {
        /* The out parameters are not read on failure: the host leaves nothing in them */
        if (status == TI_ERR_UNDEFINED) {
            ti_raise(rt, TI_ERR_UNDEFINED, line, "Unknown module '%s'", name);
        } else {
            ti_raise(rt, status, line, "Cannot load module '%s': %s", name, ti_err_to_str(status));
        }
        return false;
    }

    /* Step 3: both are required: the source to compile, the path to know which file it is */
    if (text == NULL || path == NULL) {
        ti_raw_free(text);
        ti_raw_free(path);
        ti_raise(rt, TI_ERR_INVALID_VALUE, line, "Cannot load module '%s': the loader returned no %s",
                 name, text == NULL ? "source" : "path");
        return false;
    }

    *text_out = text;
    *path_out = path;
    return true;
}

/* Compile a script module's source and give it a new empty namespace (not run); text and path are freed */
ti_namespace_t *ti_namespace_load(ti_runtime_t *rt, const char *name, int line, char *text, char *path)
{
    /* Step 1: compile; the program keeps its own copy of the path, so both host buffers are released now */
    ti_status_t status = TI_OK;
    ti_program_t *prog = ti_program_compile(text, path, &status);
    ti_raw_free(text);
    ti_raw_free(path);
    if (prog == NULL) {
        /* A syntax error is already logged with its file and line */
        ti_raise(rt, status, line, "Cannot import module '%s': %s", name, ti_err_to_str(status));
        return NULL;
    }

    /* Step 2: the script's own namespace; allocated once on the runtime alloc list, so it never moves */
    ti_namespace_t *ns = tracked_calloc(&rt->alloc_list, 1, sizeof(ti_namespace_t));
    if (ns == NULL || ti_namespace_init(rt, ns) != TI_OK) {
        tracked_free(&rt->alloc_list, ns);
        ti_program_free(prog);
        ti_raise(rt, TI_ERR_NO_MEMORY, line, "Cannot import '%s': out of memory", name);
        return NULL;
    }
    ns->prog = prog;
    return ns;
}
