#include "sonic3k_mod_host.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int online_enabled;
static char s_path[1024],s_error[256];
int s3k_online_enabled(void) {return online_enabled;}
void s3k_online_load_settings(const char *path) {
    snprintf(s_path,sizeof s_path,"%s",path?path:"settings.ini");
    char *a=strrchr(s_path,'/'),*b=strrchr(s_path,'\\');
    char *name=a && (!b || a>b)?a+1:b?b+1:s_path;
    snprintf(name,sizeof s_path-(size_t)(name-s_path),"online-adventure.ini");
    online_enabled=0;FILE *f=fopen(s_path,"rb");
    if(f) {char line[128];while(fgets(line,sizeof line,f)) {int value;if(sscanf(line,"enabled=%d",&value)==1)online_enabled=value==1;}fclose(f);}
}
static int online_save(void) {
    FILE *f=fopen(s_path,"wb");if(!f){snprintf(s_error,sizeof s_error,"Cannot save Online Adventure settings");return 0;}
    fprintf(f,"[online-adventure]\nenabled=%d\n",online_enabled);int ok=!ferror(f);if(fclose(f))ok=0;return ok;
}
#if RECOMP_LAUNCHER
#include "recomp_launcher.h"
#define COPY(dst, value) snprintf(dst, sizeof(dst), "%s", value)
static const RecompLauncherCModProvider *base;
static const char PACKAGE[] = "sonic3k.online-adventure", FEATURE[] = "online-adventure", GROUP[] = "Characters";
static const char DESCRIPTION[] = "Shared-world development preview for up to eight players: shared objects, pickups, terrain, bosses and emeralds, with independent cameras and coordinated exits. Connect on Data Select. Restart the game after changing this toggle.";

static int mine(const char *p, const char *f) { return p && !strcmp(p, PACKAGE) && (!f || !strcmp(f, FEATURE)); }
static int base_count(int (*fn)(void *)) { return base && fn ? fn(base->ctx) : 0; }
static const char *status(void)
{
    return online_enabled ? "Enabled: connect on Data Select" : "Disabled";
}
static int package_count(void *ctx) { (void)ctx; return 1 + base_count(base ? base->package_count : NULL); }
static int package_get(void *ctx, int i, RecompLauncherCModPackage *out)
{
    (void)ctx;
    if (i >= 1) return base && base->package_get && base->package_get(base->ctx, i - 1, out);
    if (i < 0 || !out) return 0;
    memset(out, 0, sizeof *out);
    COPY(out->id, PACKAGE); COPY(out->name, "Online Adventure"); COPY(out->version, "1.1.0-preview.15");
    COPY(out->author, "Sonic3AndKnucklesRecomp contributors");
    COPY(out->description, DESCRIPTION); COPY(out->license, "Project license; uses only your ROM");
    COPY(out->status, status());
    out->enabled = online_enabled;
    return 1;
}
static int feature_count(void *ctx) { (void)ctx; return 1 + base_count(base ? base->feature_count : NULL); }
static int feature_get(void *ctx, int i, RecompLauncherCModFeature *out)
{
    if (i >= 1) return base && base->feature_get && base->feature_get(base->ctx, i - 1, out);
    RecompLauncherCModPackage p;
    if (!out || i < 0 || !package_get(ctx, 0, &p)) return 0;
    memset(out, 0, sizeof *out);
    COPY(out->id, FEATURE); COPY(out->package_id, p.id); COPY(out->package_name, p.name);
    COPY(out->package_version, p.version); COPY(out->name, "Online Adventure");
    COPY(out->author, p.author); COPY(out->description, DESCRIPTION); COPY(out->group, GROUP);
    COPY(out->status, status());
    out->enabled = online_enabled; out->option_count = 0;
    return 1;
}
/* Another enabled character mod, in this provider or the composed base. */
static const char *character_conflict(void)
{
    static RecompLauncherCModFeature f;
    int n = base_count(base ? base->feature_count : NULL);
    for (int i = 0; i < n; ++i)
        if (base->feature_get(base->ctx, i, &f) && f.enabled && !strcmp(f.group, GROUP)) return f.name;
    return NULL;
}
static int feature_enable(void *ctx, const char *p, const char *f, int on)
{
    (void)ctx; s_error[0] = 0;
    if (!mine(p,f)) {
        if(on && online_enabled && p && !strcmp(p,"sonic3.knuckles-army")) {COPY(s_error,"Disable Online Adventure before Knuckles & Knuckles");return 0;}
        return !mine(p,NULL) && base && base->feature_enable && base->feature_enable(base->ctx,p,f,on);
    }
    const char *other = on ? character_conflict() : NULL;
    if (other) { snprintf(s_error, sizeof s_error, "Disable %s first: character mods are exclusive", other); return 0; }
    online_enabled = on != 0;
    return 1;
}
static int set_enabled(void *ctx, const char *p, int on)
{
    if (mine(p, NULL)) return feature_enable(ctx, p, FEATURE, on);
    if(on && online_enabled && p && !strcmp(p,"sonic3.knuckles-army")) {COPY(s_error,"Disable Online Adventure first");return 0;}
    return base && base->set_enabled && base->set_enabled(base->ctx,p,on);
}
static int option_get(void *ctx, const char *p, const char *f, int n, RecompLauncherCModOption *out)
{
    (void)ctx;
    if (!mine(p, f)) return !mine(p, NULL) && base && base->feature_option_get && base->feature_option_get(base->ctx, p, f, n, out);
    return 0;
}
static int choice_get(void *ctx, const char *p, const char *f, const char *o, int n, RecompLauncherCModChoice *out)
{
    (void)ctx;
    if (!mine(p, f)) return !mine(p, NULL) && base && base->feature_choice_get && base->feature_choice_get(base->ctx, p, f, o, n, out);
    return 0;
}
static int set_option(void *ctx, const char *p, const char *f, const char *o, const char *v)
{
    (void)ctx; s_error[0] = 0;
    if (!mine(p, f)) return !mine(p, NULL) && base && base->feature_set_option && base->feature_set_option(base->ctx, p, f, o, v);
    return 0;
}
static int commit(void *ctx, const char *image)
{
    (void)ctx; s_error[0] = 0;
    const char *other = online_enabled ? character_conflict() : NULL;
    if (other) { snprintf(s_error, sizeof s_error, "Online Adventure cannot combine with %s", other); return 0; }
    if (base && base->commit && !base->commit(base->ctx, image)) {
        COPY(s_error, base->last_error ? base->last_error(base->ctx) : "Unable to save common mods");
        return 0;
    }
    return online_save();
}
static int commit_netplay(void *ctx, const char *image)
{
    (void)ctx; s_error[0] = 0;
    if (online_enabled) { COPY(s_error, "Online Adventure uses Data Select to connect. Disable it to use the launcher netplay mode"); return 0; }
    return !base || !base->commit_netplay || base->commit_netplay(base->ctx, image);
}
static const char *last_error(void *ctx)
{
    (void)ctx;
    return *s_error ? s_error : base && base->last_error ? base->last_error(base->ctx) : "";
}
static int resource_count(void *ctx, const char *p, const char *f)
{
    (void)ctx;
    return !mine(p, NULL) && base && base->feature_resource_count ? base->feature_resource_count(base->ctx, p, f) : 0;
}
static int resource_get(void *ctx, const char *p, const char *f, int n, RecompLauncherCModResource *out)
{
    (void)ctx;
    return !mine(p, NULL) && base && base->feature_resource_get && base->feature_resource_get(base->ctx, p, f, n, out);
}
static int resource_set(void *ctx, const char *p, const char *f, const char *r, const char *path)
{
    (void)ctx;
    return !mine(p, NULL) && base && base->feature_resource_set_path && base->feature_resource_set_path(base->ctx, p, f, r, path);
}
const RecompLauncherCModProvider *s3k_online_mods(const RecompLauncherCModProvider *common)
{
    static RecompLauncherCModProvider p;
    base = common; memset(&p, 0, sizeof p);
    p.package_count = package_count; p.package_get = package_get; p.set_enabled = set_enabled;
    p.feature_count = feature_count; p.feature_get = feature_get; p.feature_enable = feature_enable;
    p.feature_option_get = option_get; p.feature_choice_get = choice_get; p.feature_set_option = set_option;
    p.feature_resource_count = resource_count; p.feature_resource_get = resource_get;
    p.feature_resource_set_path = resource_set;
    p.commit = commit; p.commit_netplay = commit_netplay; p.last_error = last_error;
    p.archive_extension = common ? common->archive_extension : ".genmod";
    p.archive_description = common ? common->archive_description : "GenesisRecomp mod package";
    return &p;
}

#else
const struct RecompLauncherCModProvider *s3k_online_mods(const struct RecompLauncherCModProvider *p){return p;}
#endif
