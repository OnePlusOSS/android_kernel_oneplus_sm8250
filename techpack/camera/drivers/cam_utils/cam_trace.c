// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
 */

/* Instantiate tracepoints */
#define CREATE_TRACE_POINTS
#include "cam_trace.h"

pid_t camera_provider_pid;
const char* GetFileName(const char* pFilePath)
{
    const char* pFileName = strrchr(pFilePath, '/');

    if (NULL != pFileName)
    {
        // StrRChr will return a pointer to the /, advance one to the filename
        pFileName += 1;
    }
    else
    {
        pFileName = pFilePath;
    }

    return pFileName;
}