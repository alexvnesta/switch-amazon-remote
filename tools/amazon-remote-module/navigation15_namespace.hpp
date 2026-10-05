// SPDX-License-Identifier: GPL-2.0-only
#pragma once
// Same temporary program ID; separate evidence/intent namespace per trial.
#ifdef ARMODULE_NAV16
#define ARMODULE_NAV15_CONFIG "sdmc:/config/amazon-remote-nav16"
#define ARMODULE_NAV15_CONFIG_FS "/config/amazon-remote-nav16"
#elif defined(ARMODULE_NAV15_SYSTEM)
#define ARMODULE_NAV15_CONFIG "sdmc:/config/amazon-remote-nav15s"
#define ARMODULE_NAV15_CONFIG_FS "/config/amazon-remote-nav15s"
#else
#define ARMODULE_NAV15_CONFIG "sdmc:/config/amazon-remote-nav15"
#define ARMODULE_NAV15_CONFIG_FS "/config/amazon-remote-nav15"
#endif
