#pragma once

// The product version, in one place.
//
// It is stamped into every binary's VERSIONINFO resource and into the
// installer's filename, so which build someone is running can be read from
// Explorer's properties or from the name of the file they downloaded -- without
// starting anything and without a support question.
//
// Bump it for every build that leaves this machine. A release asset is never
// replaced in place: a file that has been downloaded once and then quietly
// changed is worse than two files with different names.

#define IVX_VERSION_MAJOR 1
#define IVX_VERSION_MINOR 0
#define IVX_VERSION_PATCH 6

#define IVX_VERSION_STRINGIFY_(x) #x
#define IVX_VERSION_STRINGIFY(x) IVX_VERSION_STRINGIFY_(x)

#define IVX_VERSION_STRING          \
    IVX_VERSION_STRINGIFY(IVX_VERSION_MAJOR) \
    "." IVX_VERSION_STRINGIFY(IVX_VERSION_MINOR) "." IVX_VERSION_STRINGIFY(IVX_VERSION_PATCH)

// The engine this wraps, which is not the same number and never changes.
#define IVX_ENGINE_VERSION "Infovox 230 v1.12"
