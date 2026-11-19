/* Native feature implementation boundary. SPDX-License-Identifier: GPL-3.0-only */
#ifndef WTOP_FEATURES_NT_H
#define WTOP_FEATURES_NT_H
#include "providers_nt.h"

/* These observations run inside an owned provider child. A caller-supplied
   process handle must remain open and its lifetime must be revalidated after
   the composed observation. No function retains a borrowed handle. */
void wtop_feature_image_query(HANDLE process, WCHAR *path, DWORD capacity,
    wtop_provider_status *status);
void wtop_feature_affinity_query(HANDLE process, wtop_provider_affinity *affinity);
void wtop_feature_threads_collect(const wtop_provider_request *request,
    wtop_provider_result *result);
/* Pure check shared by the action and independent boundary-value tests. */
int wtop_feature_affinity_mask_valid(ULONGLONG requested,
    const wtop_provider_affinity *affinity);
#endif
