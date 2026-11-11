#include "media_workspace.h"

#include <stddef.h>

static _Alignas(256) media_workspace_t g_workspace;
static const void *g_owner;

media_workspace_t *media_workspace_claim(const void *owner)
{
    if(!owner || g_owner)
        return NULL;
    g_owner = owner;
    return &g_workspace;
}

void media_workspace_release(const void *owner)
{
    if(owner && owner == g_owner)
        g_owner = NULL;
}
