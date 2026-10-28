/* Device settings from TreeFrogUI.
 *
 * TreeFrogUI writes /tmp/tfdevice.env at boot describing the device it runs on
 * (TF_DEVICE, TF_PANEL_W/H, TF_ROTATE, TF_PRESENT, and TF_DRIVER, the driver
 * that matches the hardware, e.g. /mnt/sdcard/cubegm/driver_sf3000.so). Look
 * keys up there so the same binary works on every supported device. A variable
 * of the same name in the environment wins. Header-only, C and C++. */
#ifndef TFENV_H
#define TFENV_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TFENV_FILE           "/tmp/tfdevice.env"
#define TFENV_DEFAULT_DRIVER "/mnt/sdcard/cubegm/driver_sf3500.so"

#if defined(__GNUC__)
#define TFENV_UNUSED __attribute__((unused))
#else
#define TFENV_UNUSED
#endif

/* Value of KEY from the environment or tfdevice.env, copied into OUT
 * (OUTSZ bytes). Returns OUT, or NULL if the key is not set. */
static TFENV_UNUSED const char *tfenv_get(const char *key, char *out, size_t outsz)
{
	const char *e = getenv(key);
	size_t n = strlen(key);
	char line[320];
	FILE *f;

	if (e && *e) {
		snprintf(out, outsz, "%s", e);
		return out;
	}
	f = fopen(TFENV_FILE, "r");
	if (!f)
		return NULL;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, key, n) != 0 || line[n] != '=')
			continue;
		line[strcspn(line, "\r\n")] = '\0';
		fclose(f);
		if (!line[n + 1])
			return NULL;
		snprintf(out, outsz, "%s", line + n + 1);
		return out;
	}
	fclose(f);
	return NULL;
}

/* Driver to dlopen: TF_DRIVER, else the SF3500 driver these ports were
 * developed on. */
static TFENV_UNUSED const char *tfdriver_path(void)
{
	static char path[256];
	static int resolved = 0;

	if (!resolved) {
		resolved = 1;
		if (!tfenv_get("TF_DRIVER", path, sizeof(path)))
			snprintf(path, sizeof(path), "%s", TFENV_DEFAULT_DRIVER);
	}
	return path;
}

/* One log line with the device, present mode and driver picked, plus a
 * warning when the device does not present through disp_frame (R36SX and
 * R36HD use fbwrite), which these builds do not support. */
static TFENV_UNUSED void tfenv_log_device(const char *who)
{
	char dev[64], pres[64];

	if (!tfenv_get("TF_DEVICE", dev, sizeof(dev)))
		snprintf(dev, sizeof(dev), "unknown");
	if (!tfenv_get("TF_PRESENT", pres, sizeof(pres)))
		snprintf(pres, sizeof(pres), "unknown");
	fprintf(stderr, "%s: device %s, present %s, driver %s\n", who, dev, pres, tfdriver_path());
	if (strcmp(pres, "dispframe") != 0 && strcmp(pres, "unknown") != 0)
		fprintf(stderr, "%s: warning: this build presents frames with the driver's disp_frame call only\n", who);
}

#endif
