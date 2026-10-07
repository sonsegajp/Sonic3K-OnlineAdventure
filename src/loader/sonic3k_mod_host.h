#pragma once
struct RecompLauncherCModProvider;
int s3k_online_enabled(void);
void s3k_online_load_settings(const char *);
const struct RecompLauncherCModProvider *s3k_online_mods(const struct RecompLauncherCModProvider *);
