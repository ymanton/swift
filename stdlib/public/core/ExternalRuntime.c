#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include "swift/Runtime/Config.h"

#define SWIFT_RUNTIME_LIBRARY_NAME	"swiftRuntime.so"
#define SWIFT_RUNTIME_START_HOOK_NAME	"swiftRuntimeStart"
#define SWIFT_RUNTIME_STOP_HOOK_NAME	"swiftRuntimeStop"

typedef int (*swiftRuntimeStartFunction)(int argc, char **argv);
typedef int (*swiftRuntimeStopFunction)(void);

static void *swiftRuntimeHandle = NULL;
static swiftRuntimeStopFunction swiftRuntimeStop = NULL;

static
void stopRuntime(void) {
	if (swiftRuntimeStop)
		swiftRuntimeStop();
	if (swiftRuntimeHandle)
		dlclose(swiftRuntimeHandle);
}

struct RuntimeOptions {
	int debugOutput;
};

static
void parseRuntimeArgsIfAny(int argc, char **argv, struct RuntimeOptions *options) {
	options->debugOutput = 0;

	if (!options)
		return;

	for (int i = 1; i < argc; ++i) {
		if (!strncmp(argv[i], "-runtime:", 9)) {
			char *runtimeOptions = strdup(argv[i] + 9);
			if (runtimeOptions) {
				char *state;
				char *token = strtok_r(runtimeOptions, ",", &state);
				while (token) {
					if (!strcmp(token, "debug")) {
						options->debugOutput = 1;
					}
					token = strtok_r(NULL, ",", &state);
				}
				free(runtimeOptions);
			}
		}
	}
}

static
int filterRuntimeArgsIfAny(int argc, char **argv) {
	int filteredArgc = argc;
	for (int i = 1; i < argc; ++i) {
		if (!strncmp(argv[i], "-runtime:", 9)) {
			if (i < (argc - 1))
				memmove(&argv[i], &argv[i + 1], sizeof(char *) * (argc - i - 1));
			--filteredArgc;
		}
	}
	return filteredArgc;
}

SWIFT_CC(swift)
int _swift_Process_initializeExternalRuntimeIfAny(int argc, char **argv) {
	struct RuntimeOptions     options;
	swiftRuntimeStartFunction swiftRuntimeStart = NULL;
	int                       runtimeStartStatus = 0;

	parseRuntimeArgsIfAny(argc, argv, &options);

	swiftRuntimeHandle = dlopen(SWIFT_RUNTIME_LIBRARY_NAME, RTLD_NOW);
	if (!swiftRuntimeHandle) {
		if (options.debugOutput)
			fprintf(stderr, "%s: %s\n", __func__, dlerror());
		goto err;
	}

	swiftRuntimeStart = (swiftRuntimeStartFunction)dlsym(swiftRuntimeHandle, SWIFT_RUNTIME_START_HOOK_NAME);
	if (!swiftRuntimeStart) {
		if (options.debugOutput)
			fprintf(stderr, "%s: %s\n", __func__, dlerror());
		goto err;
	}

	swiftRuntimeStop = (swiftRuntimeStopFunction)dlsym(swiftRuntimeHandle, SWIFT_RUNTIME_STOP_HOOK_NAME);
	if (!swiftRuntimeStop) {
		if (options.debugOutput)
			fprintf(stderr, "%s: %s\n", __func__, dlerror());
		goto err;
	}

	runtimeStartStatus = swiftRuntimeStart(argc, argv);
	if (runtimeStartStatus != 0) {
		if (options.debugOutput)
			fprintf(stderr, "%s: %s: %d\n", __func__, SWIFT_RUNTIME_START_HOOK_NAME, runtimeStartStatus);
		goto err;
	}

	if (atexit(stopRuntime)) {
		if (options.debugOutput)
			perror(__func__);
		goto err;
	}

	return filterRuntimeArgsIfAny(argc, argv);

err:
	if (runtimeStartStatus == 0 && swiftRuntimeStop)
		swiftRuntimeStop();

	if (swiftRuntimeHandle)
		dlclose(swiftRuntimeHandle);

	return filterRuntimeArgsIfAny(argc, argv);
}

