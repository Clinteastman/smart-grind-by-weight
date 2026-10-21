#pragma once

#include <cstdio>

#ifdef SMART_GRIND_SIM
#define BUILD_NUMBER 0
#define GIT_COMMIT_ID "desktop-simulator"
#define GIT_BRANCH "desktop-simulator"
#define GIT_REPO "Cuxx/smart-grind-by-weight-singledose"
#define GIT_REPO_OWNER "Cuxx"
#define GIT_REPO_OWNER_LOWER "cuxx"
#define GIT_REPO_NAME "smart-grind-by-weight-singledose"
#define GITHUB_PAGES_BASE_URL "https://cuxx.github.io/smart-grind-by-weight-singledose/"
#else
#include "git_info.h"
#endif

#ifndef GIT_REPO
#define GIT_REPO "Cuxx/smart-grind-by-weight-singledose"
#endif
#ifndef GIT_REPO_OWNER
#define GIT_REPO_OWNER "Cuxx"
#endif
#ifndef GIT_REPO_OWNER_LOWER
#define GIT_REPO_OWNER_LOWER "cuxx"
#endif
#ifndef GIT_REPO_NAME
#define GIT_REPO_NAME "smart-grind-by-weight-singledose"
#endif
#ifndef GITHUB_PAGES_BASE_URL
#define GITHUB_PAGES_BASE_URL "https://" GIT_REPO_OWNER_LOWER ".github.io/" GIT_REPO_NAME "/"
#endif

// Build information - BUILD_FIRMWARE_VERSION is automatically updated by release scripts
#define BUILD_FIRMWARE_VERSION "1.6.6-beta.2"                                          // Firmware version string (updated by release automation)
// BUILD_NUMBER is now defined in git_info.h as an auto-incrementing integer
#define BUILD_DATE __DATE__                                                // Compiler-provided build date
#define BUILD_TIME __TIME__                                                // Compiler-provided build time

inline const char* get_git_commit_id() {
    return GIT_COMMIT_ID;
}

inline const char* get_git_branch() {
    return GIT_BRANCH;
}

inline const char* get_git_repo() {
    return GIT_REPO;
}

inline const char* get_build_datetime() {
    static char datetime[32];
    snprintf(datetime, sizeof(datetime), "%s %s", __DATE__, __TIME__);
    return datetime;
}
